# Threading

**Header:** `#include "AsyncFlowThreadAwaiters.h"`

All threading awaiters guarantee that the coroutine resumes on the game thread unless you explicitly migrate with
`MoveToThread` / `MoveToTask`.

**Rule:** UObject access, GC interaction, and world queries are forbidden inside background lambdas. Only pure
computation should run off the game thread.

> **v2 note:** `TTask` itself has **zero tick dependency**. Thread awaiters, sync primitives, and delegate awaiters all
> work without `UAsyncFlowTickSubsystem` or any other subsystem. Only timing awaiters (delays, conditions, tick counts)
> require the tick subsystem.

---

## RunOnBackgroundThread

Offload a lambda to the thread pool. The coroutine suspends, the lambda runs on a worker, and the coroutine resumes on
the game thread with the result.

```cpp
int32 Result = co_await AsyncFlow::RunOnBackgroundThread([]()
{
    return ExpensivePathfindingCalc();
});
// Back on game thread with the result

co_await AsyncFlow::RunOnBackgroundThread([]()
{
    CompressData(Buffer);
});
// Back on game thread
```

`RunOnBackgroundThread` is `[[nodiscard]]` — calling it without `co_await` is a compile warning.

### Exception Handling

Exceptions thrown inside the `Work` lambda are caught on the background thread and marshaled back to the game thread.
They are re-thrown at the `co_await` site in the coroutine.

```cpp
AsyncFlow::TTask<void> UMyComponent::ProcessData()
{
    try
    {
        FResult Result = co_await AsyncFlow::RunOnBackgroundThread([]()
        {
            if (!LoadData())
            {
                throw std::runtime_error("Failed to load data");
            }
            return ComputeResult();
        });
        ApplyResult(Result);
    }
    catch (const std::exception& Ex)
    {
        UE_LOG(LogTemp, Error, TEXT("Background work failed: %s"), UTF8_TO_TCHAR(Ex.what()));
    }
}
```

This guarantee applies only to `RunOnBackgroundThread`. `AwaitFuture` and the thread migration awaiters (`MoveToThread`,
`MoveToTask`, `MoveToNewThread`) do not catch exceptions — an unhandled exception from those contexts will terminate the
process.

---

## Implicit Awaiting of Engine Types

Several UE engine types are directly `co_await`-able without explicit wrapper functions:

### TFuture\<T\>

```cpp
TFuture<FString> Future = SomeAsyncAPI();
FString Result = co_await Future;  // directly awaitable — no wrapper needed
```

Void futures:

```cpp
TFuture<void> Future = SomeAsyncVoidAPI();
co_await Future;
```

### UE::Tasks::TTask\<T\> (UE 5.4+)

```cpp
UE::Tasks::TTask<int32> UETask = UE::Tasks::Launch(
    TEXT("Compute"),
    []() { return 42; }
);
int32 Result = co_await UETask;  // directly awaitable
```

Void tasks (`UE::Tasks::FTask`):

```cpp
UE::Tasks::FTask VoidTask = UE::Tasks::Launch(
    TEXT("Work"),
    []() { DoSomething(); }
);
co_await VoidTask;
```

### Multicast & Unicast Delegates

```cpp
co_await OnTakeDamageDelegate;   // multicast — directly awaitable
co_await MyUnicastDelegate;      // unicast — directly awaitable
```

> The explicit wrappers (`AwaitFuture`, `AwaitUETask`, `WaitForDelegate`) are still available for backward compatibility
> or when you need to pass additional options.

---

## AwaitFuture

Wrap an existing `TFuture<T>` as a co_awaitable. Blocks a thread-pool worker until the future resolves, then resumes on
the game thread.

> **Note:** `TFuture<T>` is now directly `co_await`-able (see above). This explicit wrapper is still available but no
> longer required.

```cpp
TFuture<FString> Future = SomeAsyncAPI();
FString Result = co_await AsyncFlow::AwaitFuture(MoveTemp(Future));
```

Void futures:

```cpp
TFuture<void> Future = SomeAsyncVoidAPI();
co_await AsyncFlow::AwaitFuture(MoveTemp(Future));
```

---

## ParallelForAsync

Run a `ParallelFor` on a background thread. Each iteration calls `Body(Index)`. The coroutine resumes on the game thread
when all iterations finish.

```cpp
TArray<FResult> Results;
Results.SetNum(1000);

co_await AsyncFlow::ParallelForAsync(1000, [&Results](int32 Index)
{
    Results[Index] = HeavyCompute(Index);
});
// All 1000 items processed, back on game thread
```

---

## AwaitUETask (UE 5.4+)

Wrap a `UE::Tasks::TTask<T>` as a co_awaitable. Available when `Tasks/Task.h` is present.

> **Note:** `UE::Tasks::TTask<T>` is now directly `co_await`-able (see above). This explicit wrapper is still available
> but no longer required.

```cpp
UE::Tasks::TTask<int32> UETask = UE::Tasks::Launch(
    TEXT("Compute"),
    []() { return 42; }
);
int32 Result = co_await AsyncFlow::AwaitUETask(MoveTemp(UETask));
```

Void tasks (`UE::Tasks::FTask`):

```cpp
UE::Tasks::FTask VoidTask = UE::Tasks::Launch(
    TEXT("Work"),
    []() { DoSomething(); }
);
co_await AsyncFlow::AwaitUETask(MoveTemp(VoidTask));
```

---

## Thread Migration

Move the coroutine body between threads. Use these for sustained background computation where multiple sequential
operations need to run off the game thread.

### MoveToGameThread

Resume the coroutine on the game thread. Use after any off-thread section to safely access UObjects again.

```cpp
co_await AsyncFlow::MoveToGameThread();
```

No-op if already on the game thread.

### MoveToThread

Resume on a specific named UE thread.

```cpp
co_await AsyncFlow::MoveToThread(ENamedThreads::AnyBackgroundThreadNormalTask);
// Now running on a background thread — no UObject access
DoHeavyWork();
co_await AsyncFlow::MoveToGameThread();
// Safe again
```

### MoveToTask

Resume on a `UE::Tasks` worker thread.

```cpp
co_await AsyncFlow::MoveToTask();
// Running on task thread pool
ComputePathGrid();
co_await AsyncFlow::MoveToGameThread();
```

### MoveToNewThread

Resume on a brand-new dedicated thread. Good for blocking I/O.

```cpp
co_await AsyncFlow::MoveToNewThread();
FString Data = FFileHelper::LoadFileToString(...);
co_await AsyncFlow::MoveToGameThread();
ProcessData(Data);
```

---

## Yield

Yield the coroutine to the game thread scheduler. No world context required. Schedules resumption via
`AsyncTask(GameThread)`.

```cpp
co_await AsyncFlow::Yield();
```

---

## PlatformSeconds

Free-threaded delay using `FPlatformProcess::Sleep` on a worker thread. Resumes on the game thread. No world context or
tick subsystem required.

```cpp
co_await AsyncFlow::PlatformSeconds(0.5);
```

---

## Migration Pattern

A typical pattern for background work with intermediate game-thread access:

```cpp
AsyncFlow::TTask<void> UMyComponent::ProcessLargeDataSet()
{
    // Fetch data on game thread
    TArray<FRawData> Data = GatherData();

    // Move to background for processing
    co_await AsyncFlow::MoveToTask();
    TArray<FProcessedData> Processed = CrunchNumbers(Data);

    // Back to game thread to apply results
    co_await AsyncFlow::MoveToGameThread();
    ApplyResults(Processed);

    // Another background pass
    co_await AsyncFlow::MoveToTask();
    TArray<FCompressedData> Compressed = CompressResults(Processed);

    // Final game-thread write
    co_await AsyncFlow::MoveToGameThread();
    SaveToSlot(Compressed);
}
```

---

## Safety Notes

- All `RunOnBackgroundThread`, `MoveToThread`, `MoveToTask`, `MoveToNewThread` lambdas/sections run off the game thread.
- UObject pointers, `GetWorld()`, GC-managed memory, and any engine API that requires the game thread are **forbidden**
  in off-thread contexts.
- The alive-flag pattern (`FAwaiterAliveFlag`) prevents stale resumes if the coroutine frame is destroyed while the
  background work is in flight.
- `ParallelFor` distributes work across cores. Wrapping it in `RunOnBackgroundThread` prevents it from blocking the game
  thread.
