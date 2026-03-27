# Synchronization Primitives

**Header:** `#include "AsyncFlowSyncPrimitives.h"` (included by `AsyncFlow.h`)

Thread-safe synchronization types that coroutines can `co_await`.

---

## FAwaitableEvent

Manual-reset event. Coroutines that `co_await` a signaled event resume immediately. Coroutines that `co_await` before `Signal()` is called are suspended until `Signal()`.

All waiters resume on the game thread.

### API

| Method | Description |
|--------|-------------|
| `Signal()` | Signal the event. All waiting coroutines resume on the game thread. |
| `Reset()` | Reset the event so future `co_await`s suspend again. |
| `IsSignaled()` | Returns true if the event has been signaled. |

### Usage

```cpp
// Member
AsyncFlow::FAwaitableEvent DataReadyEvent;

// Producer (any thread)
void UMySubsystem::OnDataLoaded()
{
    DataReadyEvent.Signal();
}

// Consumer (coroutine)
AsyncFlow::TTask<void> UMyComponent::WaitForData()
{
    co_await DataReadyEvent;
    // Data is ready — process it
}
```

### Multiple Waiters

Multiple coroutines can wait on the same event. All are resumed when `Signal()` is called.

```cpp
AsyncFlow::FAwaitableEvent LevelLoadedEvent;

// Coroutine A
co_await LevelLoadedEvent;
SpawnEnemies();

// Coroutine B
co_await LevelLoadedEvent;
StartMusic();

// When LevelLoadedEvent.Signal() fires, both A and B resume
```

### Re-use with Reset

```cpp
DataReadyEvent.Reset();
// Future co_await calls will suspend again until the next Signal()
```

### Thread Safety

`FAwaitableEvent` is thread-safe. `Signal()` and `Reset()` can be called from any thread. Waiters always resume on the game thread via `AsyncTask(ENamedThreads::GameThread)`.

---

## FAwaitableSemaphore

Counting semaphore for coroutines. `co_await` on the semaphore suspends if no permits are available. `Release()` returns a permit and resumes one waiter.

All waiters resume on the game thread.

### API

| Method | Description |
|--------|-------------|
| `FAwaitableSemaphore(int32 MaxCount)` | Construct with a maximum permit count (>= 1). |
| `co_await Semaphore` | Acquires one permit. Suspends if all permits are in use. |
| `Release()` | Returns one permit or resumes one waiting coroutine. |
| `GetAvailable()` | Returns the number of currently available permits. |

### Usage — Concurrency Limiting

Limit the number of concurrent operations:

```cpp
// 3 permits — at most 3 coroutines run the body concurrently
AsyncFlow::FAwaitableSemaphore Semaphore(3);

AsyncFlow::TTask<void> UMyComponent::DoWork()
{
    co_await Semaphore;
    // At most 3 coroutines are here at once
    co_await AsyncFlow::Delay(this, 1.0f);
    DoExpensiveOperation();
    Semaphore.Release();
}
```

### Thread Safety

`FAwaitableSemaphore` is thread-safe. `Release()` can be called from any thread. Waiters always resume on the game thread.

---

## Combining Primitives

Events and semaphores compose with all other AsyncFlow awaiters:

```cpp
AsyncFlow::TTask<void> UMyComponent::GatedSequence()
{
    co_await DataReadyEvent;                  // Wait for external signal
    co_await Semaphore;                       // Wait for a permit
    co_await AsyncFlow::Delay(this, 1.0f);    // Game-time delay
    DoWork();
    Semaphore.Release();
}
```

