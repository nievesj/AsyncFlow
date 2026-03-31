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

### Unregistering

```cpp
AsyncFlow::DebugUnregisterTask(Task);
```

Typically called in the completion callback or when the task is destroyed.

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

`GetActiveCount()` is **lock-free** — it reads a `std::atomic<int32>` without acquiring the internal lock. It is safe to call frequently (e.g., every tick) without lock contention.

`GetActiveCoroutines()` acquires the lock and returns a copy of the full tracking map. Use it for diagnostics and logging rather than hot-path checks.

### Dump to Log

```cpp
AsyncFlow::FAsyncFlowDebugger::Get().DumpToLog();
```

Console command: `AsyncFlow.List` — dumps all active tracked coroutines to the output log.

---

## FCoroutineDebugInfo

Per-coroutine tracking data:

| Field | Type | Description |
|-------|------|-------------|
| `DebugName` | `FString` | Name set via `SetDebugName()` |
| `CreationTime` | `double` | `FPlatformTime::Seconds()` at registration |
| `bCompleted` | `bool` | Whether the coroutine has finished |
| `bCancelled` | `bool` | Whether the coroutine was cancelled |

---

## Debug Names on TTask

Every `TTask` supports a debug name independent of the debugger:

```cpp
Task.SetDebugName(TEXT("SpawnWave_3"));
FString Name = Task.GetDebugName();
```

Debug names appear in:
- `FAsyncFlowDebugger` tracking
- `ensure` messages from `OnComplete` double-registration checks
- `FAsyncFlowLatentAction::GetDescription()` in the editor latent action list

---

## Alive-Flag Pattern

All tick-subsystem-driven awaiters use an `FAwaiterAliveFlag` shared between the awaiter and the subsystem entry. If the coroutine frame is destroyed while an awaiter is suspended (e.g., the owning `TTask` goes out of scope), the alive flag is set to false and the subsystem skips the pending resume.

This prevents use-after-free crashes without requiring the subsystem to hold strong references to coroutine frames.

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
        co_await AsyncFlow::Delay(this, 30.0f);
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

### Conditional Breakpoints

`FAsyncFlowState` exposes `DebugName` as a public `FString`. Set a conditional breakpoint in `FAsyncFlowState::Cancel()` on `DebugName == TEXT("YourTaskName")` to catch specific cancellations.
