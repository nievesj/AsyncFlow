# Core Module

**Header:** `#include "AsyncFlow.h"`
**Build.cs:** `PublicDependencyModuleNames.Add("AsyncFlow");`

## TTask\<T\>

The core type. A lazily-started, copyable, discardable coroutine handle.

- `TTask<void>` for coroutines that return nothing.
- `TTask<T>` for coroutines that `co_return` a value.
- Lazily started: calling the coroutine function only creates the handle. Nothing executes until you call `Start()`.
- **Copyable:** Uses a shared control block (`FCoroutineControlBlock<T>`). Multiple copies share the same coroutine.
- **Discardable:** After `Start()`, the coroutine is self-sustaining via an internal self-reference. All copies become
  passive observers — letting every copy go out of scope is safe.
- **No tick dependency:** `TTask` itself has zero tick dependency. Only timing awaiters (delays, conditions, tick
  counts) require `UAsyncFlowTickSubsystem`. Thread awaiters, sync primitives, and delegate awaiters work without any
  subsystem.

### Creating a Coroutine

Any function that returns `AsyncFlow::TTask<T>` and uses `co_await` or `co_return` is a coroutine.

```cpp
AsyncFlow::TTask<void> UMyComponent::DoWork()
{
    co_await AsyncFlow::Delay(1.0f);
    UE_LOG(LogTemp, Log, TEXT("Done"));
}

AsyncFlow::TTask<int32> UMyComponent::ComputeValue()
{
    co_await AsyncFlow::NextTick();
    co_return 42;
}
```

### Lifecycle

```cpp
// 1. Create
AsyncFlow::TTask<void> Task = DoWork();

// 2. Start (resumes the coroutine for the first time)
Task.Start();
// After Start(), the coroutine is self-sustaining (fire-and-forget).
// All copies are passive observers from this point.

// 3. Query state
Task.IsValid();      // Has a coroutine handle
Task.IsStarted();    // Start() has been called
Task.IsCompleted();  // Finished executing
Task.IsCancelled();  // Was cancelled
Task.WasSuccessful();// Completed and not cancelled

// 4. Cancel (stops at next co_await)
Task.Cancel();
```

### Copying and Sharing

Multiple copies of a `TTask` share the same underlying coroutine via `FCoroutineControlBlock<T>`:

```cpp
AsyncFlow::TTask<void> Task = DoWork();

// Copy is allowed — both share the same coroutine
AsyncFlow::TTask<void> Copy = Task;

Task.Start();

// Both observe the same state
check(Copy.IsValid());
check(!Copy.IsCompleted()); // same coroutine, same state

// Fire-and-forget: even if both Task and Copy go out of scope,
// the coroutine runs to completion (self-sustaining after Start()).
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

## Dual Execution Mode

`TTask` supports two execution modes, selected automatically at compile time:

### Async Mode (Default)

Same as before. The caller calls `Start()` or `co_await` to begin execution.

```cpp
AsyncFlow::TTask<void> MyAsyncCoro()
{
    co_await AsyncFlow::Delay(1.0f);
}

// Usage:
auto Task = MyAsyncCoro();
Task.Start();
```

### Latent Mode

Detected at compile time when `FLatentActionInfo` is present in the parameter list. In latent mode:

- The coroutine auto-registers with `FLatentActionManager`.
- A `CO_CONTRACT` for `UObject` lifetime is added automatically.
- Blueprint-friendly via the standard latent UFUNCTION pattern.

```cpp
// Latent mode — FLatentActionInfo triggers auto-detection
AsyncFlow::TTask<void> MyLatentCoro(UObject* Ctx, FLatentActionInfo Info)
{
    co_await AsyncFlow::Delay(1.0f);
}
```

#### Blueprint Latent Node

```cpp
UFUNCTION(BlueprintCallable, meta=(Latent, LatentInfo="LatentInfo"))
void MyLatentFunc(UObject* WorldContextObject, FLatentActionInfo LatentInfo)
{
    // In latent mode, the coroutine auto-registers — no manual StartLatentCoroutine needed.
    MyLatentCoro(WorldContextObject, LatentInfo).Start();
}
```

> **Migration note:** The explicit `AsyncFlow::StartLatentCoroutine()` helper still works but is no longer required for
> coroutines that accept `FLatentActionInfo`.

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

Registers a predicate checked before every `co_await`. If the predicate returns false, the coroutine is cancelled at the
next suspension point.

Capture `TWeakObjectPtr` for UObject references — raw pointers will dangle.

```cpp
AsyncFlow::TTask<void> UMyComponent::DoWork()
{
    TWeakObjectPtr<AActor> WeakOwner = GetOwner();
    CO_CONTRACT([WeakOwner]() { return WeakOwner.IsValid(); });

    co_await AsyncFlow::Delay(5.0f);
    // ^ if GetOwner() was destroyed during the delay, the coroutine stops here
}
```

> **Note:** In latent mode, a `CO_CONTRACT` for the `UObject*` world context lifetime is added automatically. You do not
> need to add one yourself unless you want additional invariants.

---

## Cancellation

### Manual Cancellation

```cpp
Task.Cancel();
```

The coroutine stops at the next `co_await`. Any awaiters in flight (timers, delegates, etc.) are cleaned up by their
destructors.

### Expedited Cancellation

When `TTask::Cancel()` is called, the current awaiter's `CancelAwaiter()` method is invoked if the awaiter supports the
`CancelableAwaiter` concept. This provides immediate cancellation rather than waiting until the next natural `co_await`
resume.

All timing awaiters (`Delay`, `NextTick`, `Ticks`, `WaitForCondition`, etc.) and all sync primitive awaiters (
`FAwaitableEvent`, `FAwaitableSemaphore`, `FAutoResetEvent`) support expedited cancellation.

### FinishNowIfCanceled

Lightweight check inside a coroutine body. Does not suspend — returns immediately if cancellation has not been
requested.

```cpp
co_await AsyncFlow::FFinishNowIfCanceled{};
// If Cancel() was called, the coroutine stops here without suspension.
// Otherwise, execution continues immediately.
```

Use this for cheap cancellation checks between non-awaiting work:

```cpp
AsyncFlow::TTask<void> UMyComponent::ProcessBatch()
{
    for (auto& Item : Items)
    {
        ProcessItem(Item);
        co_await AsyncFlow::FFinishNowIfCanceled{};
    }
}
```

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

RAII guard that defers cancellation within a scope. While active, contract checks and `Cancel()` calls are held until
the guard is destroyed.

```cpp
AsyncFlow::TTask<void> UMyComponent::CriticalSection()
{
    {
        AsyncFlow::FCancellationGuard Guard;
        // Cancellation is deferred here — the coroutine won't stop mid-operation
        co_await AsyncFlow::Delay(0.1f);
        SaveState();
    }
    // Guard destroyed — cancellation can take effect again
    co_await AsyncFlow::Delay(1.0f);
}
```

---

## Core Awaiters

These are included automatically via `#include "AsyncFlow.h"`.

> **v2 change:** World context is now **optional** on all timing awaiters. The world is resolved automatically in this
> order: explicit context → latent mode world context → `GEngine->GetCurrentPlayWorld()`. The old signatures that require
> a `UObject*` context still compile but are **deprecated**.

### Delay

Suspend for N seconds using game-dilated time.

```cpp
co_await AsyncFlow::Delay(2.0f);
```

<details><summary>Deprecated signature (still compiles)</summary>

```cpp
co_await AsyncFlow::Delay(this, 2.0f);  // deprecated — world context is auto-resolved
```

</details>

### RealDelay

Suspend for N seconds using wall-clock time. Ignores pause and time dilation.

```cpp
co_await AsyncFlow::RealDelay(2.0f);
```

<details><summary>Deprecated signature</summary>

```cpp
co_await AsyncFlow::RealDelay(this, 2.0f);
```

</details>

### UnpausedDelay

Suspend using unpaused time (ticks continue during pause).

```cpp
co_await AsyncFlow::UnpausedDelay(1.0f);
```

<details><summary>Deprecated signature</summary>

```cpp
co_await AsyncFlow::UnpausedDelay(this, 1.0f);
```

</details>

### AudioDelay

Suspend using audio time.

```cpp
co_await AsyncFlow::AudioDelay(0.5f);
```

<details><summary>Deprecated signature</summary>

```cpp
co_await AsyncFlow::AudioDelay(this, 0.5f);
```

</details>

### SecondsForActor

Suspend factoring in an actor's `CustomTimeDilation`. Still requires the actor reference.

```cpp
co_await AsyncFlow::SecondsForActor(MyActor, 1.0f);
```

### NextTick

Suspend until the next frame.

```cpp
co_await AsyncFlow::NextTick();
```

<details><summary>Deprecated signature</summary>

```cpp
co_await AsyncFlow::NextTick(this);
```

</details>

### Ticks

Suspend for N frames.

```cpp
co_await AsyncFlow::Ticks(5);
```

<details><summary>Deprecated signature</summary>

```cpp
co_await AsyncFlow::Ticks(this, 5);
```

</details>

### WaitForCondition

Poll a predicate each tick. Resume when it returns true.

```cpp
co_await AsyncFlow::WaitForCondition([this]()
{
    return bDoorOpen;
});
```

<details><summary>Deprecated signature</summary>

```cpp
co_await AsyncFlow::WaitForCondition(this, [this]()
{
    return bDoorOpen;
});
```

</details>

### FTickTimeBudget

Time-sliced processing within a per-tick budget. Yields to the next frame when the budget runs out, then picks up where
it left off. World context is optional (auto-resolved in latent mode).

```cpp
auto Budget = AsyncFlow::FTickTimeBudget::Milliseconds(5.0); // 5ms per frame
for (FItem& Item : BigArray)
{
    ProcessItem(Item);
    co_await Budget; // yields if budget exceeded, resumes next frame
}
```

`await_ready()` returns `true` if the budget has not been exceeded, so no suspension occurs until the time limit is hit.

<details><summary>Deprecated signature</summary>

```cpp
auto Budget = AsyncFlow::FTickTimeBudget::Milliseconds(this, 2.0);  // deprecated — world context moved to second arg
```

</details>

### UntilTime / UntilRealTime / UntilUnpausedTime / UntilAudioTime

Wait until a clock reaches an absolute target time. If the target has already passed, the coroutine continues without suspending. `UntilRealTime` uses `FPlatformTime::Seconds()` (wall-clock); the others use their respective `UWorld` time domains.

```cpp
co_await AsyncFlow::UntilTime(World->GetTimeSeconds() + 10.0);
co_await AsyncFlow::UntilRealTime(FPlatformTime::Seconds() + 5.0);
co_await AsyncFlow::UntilUnpausedTime(World->GetUnpausedTimeSeconds() + 3.0);
co_await AsyncFlow::UntilAudioTime(World->GetAudioTimeSeconds() + 2.0);
```

Each variant uses its corresponding time source. Optional world context parameter:

```cpp
co_await AsyncFlow::UntilTime(TargetTime, this);  // explicit context
co_await AsyncFlow::UntilTime(TargetTime);         // inferred from coroutine
```

---

## Flow Control

### WhenAll

Wait for all tasks to complete. Tasks are started automatically. Returns a `[[nodiscard]]` awaiter — calling
`WhenAll(...)` without `co_await` is a compile warning.

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

Wait for the first task to complete. Returns the 0-based index of the winner. Returns a `[[nodiscard]]` awaiter —
calling `WhenAny(...)` without `co_await` is a compile warning.

```cpp
int32 Winner = co_await AsyncFlow::WhenAny(TaskA, TaskB);
```

### Race

Like `WhenAny`, but cancels all other tasks when the first completes. Returns a `[[nodiscard]]` awaiter — calling
`Race(...)` without `co_await` is a compile warning.

```cpp
int32 Winner = co_await AsyncFlow::Race(TaskA, TaskB);
// Loser tasks are cancelled automatically
```

Also works with `TArray<TTask<void>*>`:

```cpp
TArray<AsyncFlow::TTask<void>*> Tasks = { &TaskA, &TaskB };
int32 Winner = co_await AsyncFlow::Race(Tasks);
```

All three aggregates (`WhenAll`, `WhenAny`, `Race`) support **expedited cancellation** — if the parent coroutine is cancelled while waiting, all inner tasks are cancelled immediately via `CancelAwaiter()`.

### Latent::WhenAll / Latent::WhenAny

UObject-lifetime-tracked variants. If the context object is destroyed, all inner tasks are cancelled automatically via a contract check.

```cpp
co_await AsyncFlow::Latent::WhenAll(this, TaskA, TaskB);
co_await AsyncFlow::Latent::WhenAny(this, TaskA, TaskB);
```

TArray overloads:

```cpp
TArray<AsyncFlow::TTask<void>*> Tasks = { &TaskA, &TaskB };
co_await AsyncFlow::Latent::WhenAll(this, Tasks);
int32 Winner = co_await AsyncFlow::Latent::WhenAny(this, Tasks);
```

Use these in latent coroutines (spawned from Blueprint) where actor/component lifetime matters.

---

## Delegates

### WaitForDelegate (Multicast)

Bind to any UE multicast delegate and suspend until it fires. Returns delegate arguments as a `TTuple`.

```cpp
auto Args = co_await AsyncFlow::WaitForDelegate(OnTakeDamageDelegate);
float Damage = Args.Get<0>();
AActor* Instigator = Args.Get<1>();
```

Void delegates:

```cpp
co_await AsyncFlow::WaitForDelegate(OnFireDelegate);
```

### Implicit Delegate Awaiting

Multicast and unicast delegates are directly awaitable without the `WaitForDelegate` wrapper:

```cpp
// Implicit — delegates are directly co_awaitable
auto Args = co_await OnTakeDamageDelegate;
co_await OnFireDelegate;
co_await MyUnicastDelegate;
```

The explicit `WaitForDelegate` wrapper is still available for readability or when you need to disambiguate.

### WaitForDynamicDelegate

Wait for a dynamic multicast delegate (`DECLARE_DYNAMIC_MULTICAST_DELEGATE`) to fire. Works with any zero-arg dynamic delegate type.

```cpp
co_await AsyncFlow::WaitForDynamicDelegate(MyActor->OnSomeEvent);
```

Dynamic delegates can also be awaited implicitly (same as multicast/unicast):

```cpp
co_await MyActor->OnSomeEvent;
```

> **Note:** Only the "fired" event is captured — delegate parameters are not forwarded. For typed dynamic delegates with parameters, use `AsyncFlow::Chain()` with manual binding.

### WaitForDelegate (Unicast)

Works with `TDelegate<void(Args...)>`:

```cpp
auto Args = co_await AsyncFlow::WaitForDelegate(MyUnicastDelegate);
```

### Chain

Universal wrapper for callback-based async functions. Wraps any function that takes a completion callback as its last
argument.

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

### Automatic (Latent Mode)

When a coroutine's parameter list includes `FLatentActionInfo`, the coroutine enters **latent mode** automatically:

```cpp
UFUNCTION(BlueprintCallable, meta=(Latent, LatentInfo="LatentInfo"))
void MyLatentFunc(UObject* WorldContextObject, FLatentActionInfo LatentInfo)
{
    MyLatentCoro(WorldContextObject, LatentInfo).Start();
}

AsyncFlow::TTask<void> MyLatentCoro(UObject* Ctx, FLatentActionInfo Info)
{
    // Auto-registered with FLatentActionManager
    // Auto CO_CONTRACT for Ctx lifetime
    co_await AsyncFlow::Delay(1.0f);
}
```

> **Latent fast-path (v3):** In latent mode, timing awaiters (Delay, RealDelay, Ticks, NextTick, WaitForCondition,
> UntilTime, etc.) register their condition directly with the latent action instead of routing through the tick
> subsystem. This eliminates one level of indirection and reduces per-frame overhead. The optimization is automatic —
> no API change required.

### Manual (Legacy)

The explicit `StartLatentCoroutine` helper is still supported:

```cpp
UFUNCTION(BlueprintCallable, meta=(Latent, LatentInfo="LatentInfo"))
void MyLatentFunc(UObject* WorldContextObject, FLatentActionInfo LatentInfo)
{
    AsyncFlow::StartLatentCoroutine(WorldContextObject, LatentInfo, MyCoroutine());
}
```

The latent action manages the coroutine lifetime. If the owning UObject is destroyed, the coroutine is cancelled.

---

## Coroutine Parameter Safety

Coroutine functions copy or move their parameters into the coroutine frame before the first suspension point. Parameters
that are references or raw pointers bind to the caller's locals — those locals may be destroyed long before the
coroutine resumes.

**Never pass `const T&`, `T&`, or `T*` parameters to a coroutine function.**

```cpp
// WRONG — Name is a reference to a caller local.
// After the first co_await, the caller's stack frame may be gone.
AsyncFlow::TTask<void> ProcessName(const FString& Name)
{
    co_await AsyncFlow::Delay(1.0f);
    UE_LOG(LogTemp, Log, TEXT("%s"), *Name);  // Name dangles — undefined behavior
}

// CORRECT — Name is copied into the coroutine frame at call time.
AsyncFlow::TTask<void> ProcessName(FString Name)
{
    co_await AsyncFlow::Delay(1.0f);
    UE_LOG(LogTemp, Log, TEXT("%s"), *Name);  // Safe
}
```

For large objects where copying is expensive, pass by `TSharedPtr<T>` or move ownership in:

```cpp
// TSharedPtr — shared ownership, no dangling risk
AsyncFlow::TTask<void> ProcessData(TSharedPtr<FLargeData> Data)
{
    co_await AsyncFlow::Delay(1.0f);
    Data->Process();
}

// Move — transfers ownership into the frame
AsyncFlow::TTask<void> ConsumeData(FLargeData Data)
{
    co_await AsyncFlow::Delay(1.0f);
    Data.Process();
}

// Calling site
ConsumeData(MoveTemp(LocalData));
```

This applies to all `TTask<T>` coroutines regardless of whether they are immediately `Start()`ed or stored for later.

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

`UAsyncFlowTickSubsystem` is a `UTickableWorldSubsystem` that drives all tick-based awaiters (delays, conditions, tick
counts, timelines). It is created automatically per-world. You do not need to interact with it directly.

> **Note:** Only timing awaiters depend on this subsystem. `TTask` itself, thread awaiters, sync primitives, and
> delegate awaiters have **zero tick dependency** and work without any subsystem.

Scheduling methods (used internally by awaiters):

| Method                      | Time Source                                 |
|-----------------------------|---------------------------------------------|
| `ScheduleDelay`             | Game-dilated time                           |
| `ScheduleRealDelay`         | Wall-clock time                             |
| `ScheduleUnpausedDelay`     | Unpaused time                               |
| `ScheduleAudioDelay`        | Audio time                                  |
| `ScheduleActorDilatedDelay` | Per-actor CustomTimeDilation                |
| `ScheduleTicks`             | Frame count                                 |
| `ScheduleCondition`         | Predicate polling                           |
| `ScheduleTickUpdate`        | Per-frame callback (returns true when done) |
| `ScheduleUntilTime`         | Absolute game-time target                   |
| `ScheduleUntilRealTime`     | Absolute real-time target                   |
| `ScheduleUntilUnpausedTime` | Absolute unpaused-time target               |
| `ScheduleUntilAudioTime`    | Absolute audio-time target                  |
