# Core Module

**Header:** `#include "AsyncFlow.h"`
**Build.cs:** `PublicDependencyModuleNames.Add("AsyncFlow");`

## TTask\<T\>

The core type. A lazily-started, move-only coroutine handle that runs on the game thread.

- `TTask<void>` for coroutines that return nothing.
- `TTask<T>` for coroutines that `co_return` a value.
- Lazily started: calling the coroutine function only creates the handle. Nothing executes until you call `Start()`.
- Move-only: cannot be copied.
- Destructor calls `Handle.destroy()` automatically.

### Creating a Coroutine

Any function that returns `AsyncFlow::TTask<T>` and uses `co_await` or `co_return` is a coroutine.

```cpp
AsyncFlow::TTask<void> UMyComponent::DoWork()
{
    co_await AsyncFlow::Delay(this, 1.0f);
    UE_LOG(LogTemp, Log, TEXT("Done"));
}

AsyncFlow::TTask<int32> UMyComponent::ComputeValue()
{
    co_await AsyncFlow::NextTick(this);
    co_return 42;
}
```

### Lifecycle

```cpp
// 1. Create
AsyncFlow::TTask<void> Task = DoWork();

// 2. Start (resumes the coroutine for the first time)
Task.Start();

// 3. Query state
Task.IsValid();      // Has a coroutine handle
Task.IsCompleted();  // Finished executing
Task.IsCancelled();  // Was cancelled
Task.WasSuccessful();// Completed and not cancelled

// 4. Cancel (stops at next co_await)
Task.Cancel();
```

### Getting Results

```cpp
AsyncFlow::TTask<int32> Task = ComputeValue();
Task.Start();
// ... later, after completion:
int32 Value = Task.GetResult();    // const reference
int32 Value = Task.MoveResult();   // move the value out
```

### Completion Callbacks

```cpp
Task.OnComplete([]()
{
    UE_LOG(LogTemp, Log, TEXT("Task finished"));
});

Task.OnCancelled([]()
{
    UE_LOG(LogTemp, Log, TEXT("Task was cancelled"));
});

// Weak-ref version — only fires if the UObject is still alive
Task.ContinueWithWeak(this, [this]()
{
    OnTaskFinished();
});
```

### Composing Tasks

`co_await` another `TTask` from inside a coroutine. The inner task is started automatically.

```cpp
AsyncFlow::TTask<void> UMyComponent::OuterTask()
{
    int32 Result = co_await ComputeValue();
    UE_LOG(LogTemp, Log, TEXT("Got %d"), Result);
}
```

### Factory Methods

```cpp
// Already-completed task with a value
AsyncFlow::TTask<int32> Task = AsyncFlow::TTask<int32>::FromResult(42);

// Already-completed void task
AsyncFlow::TTask<void> Task = AsyncFlow::TTask<void>::CompletedTask();
```

### Debug Names

Attach a name for tracking and diagnostics:

```cpp
Task.SetDebugName(TEXT("AttackSequence"));
FString Name = Task.GetDebugName();
```

---

## Macros

### co_verifyf

For `TTask<void>` coroutines. If the expression is false, logs a warning and `co_return`s immediately.

```cpp
co_verifyf(Ptr != nullptr, TEXT("Ptr was null in %s"), *GetName());
co_verifyf(Health > 0.0f, TEXT("Dead actor tried to attack"));
```

### co_verifyf_r

For `TTask<T>` coroutines where T is default-constructible. Returns `T{}` on failure.

```cpp
// In a TTask<bool> coroutine:
co_verifyf_r(Count > 0, TEXT("Count was zero"));
// returns false
```

### CO_CONTRACT

Registers a predicate checked before every `co_await`. If the predicate returns false, the coroutine is cancelled at the next suspension point.

Capture `TWeakObjectPtr` for UObject references — raw pointers will dangle.

```cpp
AsyncFlow::TTask<void> UMyComponent::DoWork()
{
    TWeakObjectPtr<AActor> WeakOwner = GetOwner();
    CO_CONTRACT([WeakOwner]() { return WeakOwner.IsValid(); });

    co_await AsyncFlow::Delay(this, 5.0f);
    // ^ if GetOwner() was destroyed during the delay, the coroutine stops here
}
```

---

## Cancellation

### Manual Cancellation

```cpp
Task.Cancel();
```

The coroutine stops at the next `co_await`. Any awaiters in flight (timers, delegates, etc.) are cleaned up by their destructors.

### Self-Cancellation

From inside a coroutine, cancel yourself immediately:

```cpp
co_await AsyncFlow::FSelfCancellation{};
// Nothing after this line executes
```

### Querying Cancellation

From inside a coroutine body:

```cpp
if (AsyncFlow::IsCurrentCoroutineCanceled())
{
    // Clean up and bail
    co_return;
}
```

### FCancellationGuard

RAII guard that defers cancellation within a scope. While active, contract checks and `Cancel()` calls are held until the guard is destroyed.

```cpp
AsyncFlow::TTask<void> UMyComponent::CriticalSection()
{
    {
        AsyncFlow::FCancellationGuard Guard;
        // Cancellation is deferred here — the coroutine won't stop mid-operation
        co_await AsyncFlow::Delay(this, 0.1f);
        SaveState();
    }
    // Guard destroyed — cancellation can take effect again
    co_await AsyncFlow::Delay(this, 1.0f);
}
```

---

## Core Awaiters

These are included automatically via `#include "AsyncFlow.h"`.

### Delay

Suspend for N seconds using game-dilated time.

```cpp
co_await AsyncFlow::Delay(this, 2.0f);
```

### RealDelay

Suspend for N seconds using wall-clock time. Ignores pause and time dilation.

```cpp
co_await AsyncFlow::RealDelay(this, 2.0f);
```

### UnpausedDelay

Suspend using unpaused time (ticks continue during pause).

```cpp
co_await AsyncFlow::UnpausedDelay(this, 1.0f);
```

### AudioDelay

Suspend using audio time.

```cpp
co_await AsyncFlow::AudioDelay(this, 0.5f);
```

### SecondsForActor

Suspend factoring in an actor's `CustomTimeDilation`.

```cpp
co_await AsyncFlow::SecondsForActor(MyActor, 1.0f);
```

### NextTick

Suspend until the next frame.

```cpp
co_await AsyncFlow::NextTick(this);
```

### Ticks

Suspend for N frames.

```cpp
co_await AsyncFlow::Ticks(this, 5);
```

### WaitForCondition

Poll a predicate each tick. Resume when it returns true.

```cpp
co_await AsyncFlow::WaitForCondition(this, [this]()
{
    return bDoorOpen;
});
```

### FTickTimeBudget

Time-sliced processing within a per-tick budget. Yields to the next frame when the budget runs out, then picks up where it left off.

```cpp
auto Budget = AsyncFlow::FTickTimeBudget::Milliseconds(this, 2.0);
for (FItem& Item : BigArray)
{
    ProcessItem(Item);
    co_await Budget;
}
```

---

## Flow Control

### WhenAll

Wait for all tasks to complete. Tasks are started automatically.

```cpp
AsyncFlow::TTask<void> TaskA = DoThingA();
AsyncFlow::TTask<void> TaskB = DoThingB();
co_await AsyncFlow::WhenAll(TaskA, TaskB);
```

Also works with `TArray<TTask<void>*>`:

```cpp
TArray<AsyncFlow::TTask<void>*> Tasks = { &TaskA, &TaskB };
co_await AsyncFlow::WhenAll(Tasks);
```

### WhenAny

Wait for the first task to complete. Returns the 0-based index of the winner.

```cpp
int32 Winner = co_await AsyncFlow::WhenAny(TaskA, TaskB);
```

### Race

Like `WhenAny`, but cancels all other tasks when the first completes.

```cpp
int32 Winner = co_await AsyncFlow::Race(TaskA, TaskB);
// Loser tasks are cancelled automatically
```

---

## Delegates

### WaitForDelegate (Multicast)

Bind to any UE multicast delegate and suspend until it fires. Returns delegate arguments as a `TTuple`.

```cpp
auto [Damage, Instigator] = co_await AsyncFlow::WaitForDelegate(OnTakeDamageDelegate);
```

Void delegates:

```cpp
co_await AsyncFlow::WaitForDelegate(OnFireDelegate);
```

### WaitForDelegate (Unicast)

Works with `TDelegate<void(Args...)>`:

```cpp
auto Args = co_await AsyncFlow::WaitForDelegate(MyUnicastDelegate);
```

### Chain

Universal wrapper for callback-based async functions. Wraps any function that takes a completion callback as its last argument.

```cpp
int32 Result = co_await AsyncFlow::Chain<int32>([](TFunction<void(int32)> Callback)
{
    SomeAsyncAPI(MoveTemp(Callback));
});
```

### TCallbackAwaiter

For manual callback patterns where you control the resume point:

```cpp
AsyncFlow::TCallbackAwaiter<int32> Awaiter;
// Give the awaiter's SetResult to some external system
ExternalSystem.OnComplete([&Awaiter](int32 Val) { Awaiter.SetResult(Val); });
int32 Result = co_await Awaiter;
```

---

## Latent UFUNCTION Support

Bridge coroutines into Blueprint latent nodes:

```cpp
UFUNCTION(BlueprintCallable, meta=(Latent, LatentInfo="LatentInfo"))
void MyLatentFunc(UObject* WorldContextObject, FLatentActionInfo LatentInfo)
{
    AsyncFlow::StartLatentCoroutine(WorldContextObject, LatentInfo, MyCoroutine());
}
```

The latent action manages the coroutine lifetime. If the owning UObject is destroyed, the coroutine is cancelled.

---

## TGenerator\<T\>

Synchronous pull-based generator driven by `co_yield`. O(1) memory. Supports range-based for loops.

```cpp
AsyncFlow::TGenerator<int32> CountTo(int32 N)
{
    for (int32 I = 0; I < N; ++I)
    {
        co_yield I;
    }
}

for (int32 Val : CountTo(10))
{
    UE_LOG(LogTemp, Log, TEXT("%d"), Val);
}
```

Manual iteration:

```cpp
AsyncFlow::TGenerator<int32> Gen = CountTo(5);
while (Gen.MoveNext())
{
    int32 Val = Gen.Current();
}
```

`co_await` is explicitly deleted inside generators — they are synchronous by design.

---

## Tick Subsystem

`UAsyncFlowTickSubsystem` is a `UTickableWorldSubsystem` that drives all tick-based awaiters (delays, conditions, tick counts, timelines). It is created automatically per-world. You do not need to interact with it directly.

Scheduling methods (used internally by awaiters):

| Method | Time Source |
|--------|-------------|
| `ScheduleDelay` | Game-dilated time |
| `ScheduleRealDelay` | Wall-clock time |
| `ScheduleUnpausedDelay` | Unpaused time |
| `ScheduleAudioDelay` | Audio time |
| `ScheduleActorDilatedDelay` | Per-actor CustomTimeDilation |
| `ScheduleTicks` | Frame count |
| `ScheduleCondition` | Predicate polling |
| `ScheduleTickUpdate` | Per-frame callback (returns true when done) |

