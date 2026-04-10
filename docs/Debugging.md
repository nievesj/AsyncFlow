# Debugging

**Header:** `#include "AsyncFlowDebug.h"` (included by `AsyncFlow.h`)

---

## FAsyncFlowDebugger

Global singleton that tracks active coroutines. Opt-in — you register tasks explicitly.

### Registering a Task

```cpp
AsyncFlow::TTask<void> Task = MyCoroutine();
Task.SetDebugName(TEXT("LevelTransition"));
AsyncFlow::DebugRegisterTask(Task);
Task.Start();
```

Since `TTask` is copyable, multiple copies of the same task share the same underlying coroutine. Registering any copy
registers the shared coroutine — you only need to register once.

### Unregistering

```cpp
AsyncFlow::DebugUnregisterTask(Task);
```

Typically called in the completion callback or when the task is no longer needed for tracking. Since tasks are
discardable (self-sustaining after `Start()`), unregistering is purely for debugger bookkeeping — it does not affect the
coroutine's lifecycle.

### Low-Level Register / Unregister

The template helpers above call through to public methods on `FAsyncFlowDebugger`. You can use these directly when
tracking non-`TTask` coroutines or when you already have a coroutine handle address:

```cpp
AsyncFlow::FAsyncFlowDebugger& Debugger = AsyncFlow::FAsyncFlowDebugger::Get();

// Register with an arbitrary uint64 ID (typically the coroutine handle address)
Debugger.Register(Id, TEXT("MyCoroutine"));

// Unregister when the coroutine completes or is no longer tracked
Debugger.Unregister(Id);
```

| Method                                             | Description                                          |
|----------------------------------------------------|------------------------------------------------------|
| `void Register(uint64 Id, const FString& DebugName)` | Start tracking a coroutine. Duplicate IDs are ignored. |
| `void Unregister(uint64 Id)`                       | Remove a coroutine from tracking.                    |

### Querying Active Coroutines

```cpp
FAsyncFlowDebugger& Debugger = AsyncFlow::FAsyncFlowDebugger::Get();

int32 Count = Debugger.GetActiveCount();

TMap<uint64, AsyncFlow::FCoroutineDebugInfo> Active = Debugger.GetActiveCoroutines();
for (const auto& Pair : Active)
{
    UE_LOG(LogTemp, Log, TEXT("Coroutine [%s] age: %.2fs"),
        *Pair.Value.DebugName,
        FPlatformTime::Seconds() - Pair.Value.CreationTime);
}
```

`GetActiveCount()` is **lock-free** — it reads a `std::atomic<int32>` without acquiring the internal lock. It is safe to
call frequently (e.g., every tick) without lock contention.

`GetActiveCoroutines()` acquires the lock and returns a copy of the full tracking map. Use it for diagnostics and
logging rather than hot-path checks.

### Dump to Log

```cpp
AsyncFlow::FAsyncFlowDebugger::Get().DumpToLog();
```

Console command: `AsyncFlow.List` — dumps all active tracked coroutines to the output log.

---

## FCoroutineDebugInfo

Per-coroutine tracking data:

| Field          | Type      | Description                                                                              |
|----------------|-----------|------------------------------------------------------------------------------------------|
| `DebugName`    | `FString` | Name set via `SetDebugName()`                                                            |
| `CreationTime` | `double`  | `FPlatformTime::Seconds()` at registration                                               |
| `bCompleted`   | `bool`    | Whether the coroutine has finished — not auto-updated by the debugger; available for user code |
| `bCancelled`   | `bool`    | Whether the coroutine was cancelled — not auto-updated by the debugger; available for user code |

---

## Debug Names on TTask

Every `TTask` supports a debug name independent of the debugger:

```cpp
Task.SetDebugName(TEXT("SpawnWave_3"));
FString Name = Task.GetDebugName();
```

Debug names are stored in the shared control block, so all copies of a `TTask` see the same name.

Debug names appear in:

- `FAsyncFlowDebugger` tracking
- `ensure` messages from `OnComplete` double-registration checks
- `FAsyncFlowLatentAction::GetDescription()` in the editor latent action list (`WITH_EDITOR` only)

---

## Alive-Flag Pattern

All tick-subsystem-driven awaiters use an `FAwaiterAliveFlag` shared between the awaiter and the subsystem entry. If the
coroutine frame is destroyed while an awaiter is suspended (e.g., the owning `TTask` goes out of scope), the alive flag
is set to false and the subsystem skips the pending resume.

This prevents use-after-free crashes without requiring the subsystem to hold strong references to coroutine frames.

> **Note:** After `Start()`, the coroutine is self-sustaining via an internal self-reference. The alive-flag pattern is
> still relevant for edge cases where the coroutine is cancelled or destroyed before completion.

---

## Debugging Expedited Cancellation

When `TTask::Cancel()` is called, the current awaiter's `CancelAwaiter()` method is invoked (if the awaiter supports the
`CancelableAwaiter` concept). This causes the coroutine to resume immediately at the cancellation point rather than
waiting for the next natural wake-up.

To debug cancellation behavior:

- Set a breakpoint in `FAsyncFlowState::Cancel()` with the condition `DebugName == TEXT("YourTaskName")`.
- All timing awaiters, sync primitives (`FAwaitableEvent`, `FAwaitableSemaphore`, `FAutoResetEvent`), and delegate
  awaiters support expedited cancellation via `CancelAwaiter()`.
- Use `co_await AsyncFlow::FFinishNowIfCanceled{}` in your coroutine to add explicit cancellation check points that show
  up clearly in a debugger.

---

## Common Debugging Patterns

### Tracking All Abilities

```cpp
AsyncFlow::TTask<EAbilitySuccessType> UGA_MyAbility::ExecuteAbility(FAbilityParams Params)
{
    // The task is 'this' coroutine — register via the active task in the base class
    // Or track externally when the ability activates
}
```

### Leak Detection

Dump active coroutines periodically to catch tasks that never complete:

```cpp
AsyncFlow::TTask<void> UMySubsystem::LeakWatchdog()
{
    while (true)
    {
        co_await AsyncFlow::Delay(30.0f);
        AsyncFlow::FAsyncFlowDebugger& Debugger = AsyncFlow::FAsyncFlowDebugger::Get();
        if (Debugger.GetActiveCount() > 100)
        {
            UE_LOG(LogAsyncFlow, Warning, TEXT("Over 100 active coroutines — possible leak"));
            Debugger.DumpToLog();
        }
    }
}
```

`GetActiveCount()` is lock-free, so calling it every tick or on a short interval has negligible overhead.

> **Note:** Since tasks are fire-and-forget after `Start()`, a common source of "leaks" is long-lived coroutines that
> are still running as intended. Use debug names to distinguish between expected long-lived tasks and genuine leaks.

### Conditional Breakpoints

`FAsyncFlowState` exposes `DebugName` as a public `FString`. Set a conditional breakpoint in `FAsyncFlowState::Cancel()`
on `DebugName == TEXT("YourTaskName")` to catch specific cancellations.
