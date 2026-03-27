# Synchronization Primitives

**Header:** `#include "AsyncFlowSyncPrimitives.h"` (included by `AsyncFlow.h`)

Thread-safe synchronization types that coroutines can `co_await`.

---

## FAwaitableEvent

Manual-reset event. Coroutines that `co_await` a triggered event resume immediately. Coroutines that `co_await` before `Trigger()` is called are suspended until `Trigger()`.

All waiters resume on the game thread.

### API

| Method | Description |
|--------|-------------|
| `Trigger()` | Signal the event. All waiting coroutines resume on the game thread. |
| `Reset()` | Reset the event so future `co_await`s suspend again. |
| `IsTriggered()` | Returns true if the event has been triggered. |

### Usage

```cpp
// Member
AsyncFlow::FAwaitableEvent DataReadyEvent;

// Producer (any thread)
void UMySubsystem::OnDataLoaded()
{
    DataReadyEvent.Trigger();
}

// Consumer (coroutine)
AsyncFlow::TTask<void> UMyComponent::WaitForData()
{
    co_await DataReadyEvent;
    // Data is ready — process it
}
```

### Multiple Waiters

Multiple coroutines can wait on the same event. All are resumed when `Trigger()` is called.

```cpp
AsyncFlow::FAwaitableEvent LevelLoadedEvent;

// Coroutine A
co_await LevelLoadedEvent;
SpawnEnemies();

// Coroutine B
co_await LevelLoadedEvent;
StartMusic();

// When LevelLoadedEvent.Trigger() fires, both A and B resume
```

### Re-use with Reset

```cpp
DataReadyEvent.Reset();
// Future co_await calls will suspend again until the next Trigger()
```

### Thread Safety

`FAwaitableEvent` is thread-safe. `Trigger()` and `Reset()` can be called from any thread. Waiters always resume on the game thread via `AsyncTask(ENamedThreads::GameThread)`.

---

## FAwaitableSemaphore

Counting semaphore for coroutines. `Acquire()` suspends if the count is zero. `Release()` increments the count and resumes one waiter.

All waiters resume on the game thread.

### API

| Method | Description |
|--------|-------------|
| `FAwaitableSemaphore(int32 InitialCount)` | Construct with a starting count. |
| `Acquire()` | Returns an awaiter. Suspends if count == 0, otherwise decrements and continues. |
| `Release()` | Increments count or resumes one waiter. |

### Usage — Concurrency Limiting

Limit the number of concurrent operations:

```cpp
// 3 slots — at most 3 coroutines run the body concurrently
AsyncFlow::FAwaitableSemaphore Semaphore(3);

AsyncFlow::TTask<void> UMyComponent::DoWork()
{
    co_await Semaphore.Acquire();
    // At most 3 coroutines are here at once
    co_await AsyncFlow::Delay(this, 1.0f);
    DoExpensiveOperation();
    Semaphore.Release();
}
```

### Usage — Producer/Consumer

```cpp
AsyncFlow::FAwaitableSemaphore ItemsReady(0);

// Producer
void ProduceItem()
{
    Queue.Enqueue(NewItem);
    ItemsReady.Release();
}

// Consumer coroutine
AsyncFlow::TTask<void> ConsumeLoop()
{
    while (true)
    {
        co_await ItemsReady.Acquire();
        FItem Item;
        Queue.Dequeue(Item);
        ProcessItem(Item);
    }
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
    co_await Semaphore.Acquire();             // Wait for a slot
    co_await AsyncFlow::Delay(this, 1.0f);    // Game-time delay
    DoWork();
    Semaphore.Release();
}
```

