# Synchronization Primitives

**Header:** `#include "AsyncFlowSyncPrimitives.h"` (included by `AsyncFlow.h`)

Thread-safe synchronization types that coroutines can `co_await`. All primitives are truly thread-safe, implemented with
atomics and critical sections. They work from any thread — there is no game-thread requirement for signaling or
releasing.

> **v2 changes:**
> - All primitives are fully thread-safe (no game-thread-only restriction).
> - New type: `FAutoResetEvent` — signal-and-reset in one operation.
> - `FAwaitableSemaphore::Release(int32 Count)` — batch release of multiple permits.
> - **Inline resume:** If `Signal()` or `Release()` is called on the game thread, waiting coroutines resume inline (no
    dispatch overhead).
> - **Cancelable:** All sync primitive awaiters support `CancelAwaiter()` for expedited cancellation when
    `TTask::Cancel()` is called.

---

## FAwaitableEvent

Manual-reset event. Coroutines that `co_await` a signaled event resume immediately. Coroutines that `co_await` before
`Signal()` is called are suspended until `Signal()`.

### API

| Method         | Description                                          |
|----------------|------------------------------------------------------|
| `Signal()`     | Signal the event. All waiting coroutines resume.     |
| `Reset()`      | Reset the event so future `co_await`s suspend again. |
| `IsSignaled()` | Returns true if the event has been signaled.         |

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

`FAwaitableEvent` is fully thread-safe. `Signal()` and `Reset()` can be called from any thread. Internal state is
protected by atomics and a critical section.

If `Signal()` is called on the game thread, waiting coroutines resume **inline** — no `AsyncTask(GameThread)` dispatch
is needed, eliminating a frame of latency.

`await_suspend` returns `bool`. If `Signal()` fires in the window between `await_ready` and `await_suspend`, the
coroutine is not suspended — it continues immediately rather than hanging indefinitely waiting for a signal that already
fired.

### Cancelable

The `FAwaitableEvent` awaiter supports expedited cancellation. When `TTask::Cancel()` is called on a coroutine that is
suspended on an event, `CancelAwaiter()` is invoked to remove the waiter from the event's queue immediately rather than
waiting for a signal.

---

## FAutoResetEvent

Auto-reset event. `Signal()` wakes exactly **one** waiter and resets automatically. If no waiter is suspended, the
signal is latched so the next `co_await` returns immediately.

Use `FAutoResetEvent` for producer/consumer patterns where each signal should wake exactly one consumer.

### API

| Method         | Description                                                                                               |
|----------------|-----------------------------------------------------------------------------------------------------------|
| `Signal()`     | Wake one waiter (FIFO) and auto-reset. If no waiter is present, latch the signal for the next `co_await`. |
| `IsSignaled()` | Returns true if a latched signal is pending (no waiter consumed it yet).                                  |

### Usage

```cpp
AsyncFlow::FAutoResetEvent WorkAvailable;

// Producer (any thread)
void UMySubsystem::EnqueueWork()
{
    WorkQueue.Enqueue(NewItem);
    WorkAvailable.Signal(); // wakes exactly one worker
}

// Worker coroutines
AsyncFlow::TTask<void> UMyComponent::WorkerLoop()
{
    while (true)
    {
        co_await WorkAvailable;
        FWorkItem Item;
        if (WorkQueue.Dequeue(Item))
        {
            ProcessItem(Item);
        }
    }
}
```

### Thread Safety

`FAutoResetEvent` is fully thread-safe. `Signal()` can be called from any thread. If called on the game thread, the
woken coroutine resumes inline.

### Cancelable

Supports `CancelAwaiter()` for expedited cancellation.

---

## FAwaitableSemaphore

Counting semaphore for coroutines. Every `co_await` enters `await_suspend` and acquires a permit atomically under a
lock — there is no fast-path bypass. If a permit is available, the count is incremented and the coroutine continues
without suspending. If all permits are in use, the coroutine is enqueued and suspended.

### API

| Method                                | Description                                                                                                                                                  |
|---------------------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `FAwaitableSemaphore(int32 MaxCount)` | Construct with a maximum permit count (>= 1).                                                                                                                |
| `co_await Semaphore`                  | Acquires one permit under the lock. Suspends if all permits are in use.                                                                                      |
| `Release()`                           | Release one permit. If there are waiters, resumes the oldest (FIFO) and hands it the permit directly (count unchanged). If no waiters, decrements the count. |
| `Release(int32 Count)`                | **Batch release.** Release N permits at once. Wakes up to N waiters in FIFO order.                                                                           |
| `GetAvailable()`                      | Returns the number of currently available permits.                                                                                                           |

### Usage — Concurrency Limiting

Limit the number of concurrent operations:

```cpp
// 3 permits — at most 3 coroutines run the body concurrently
AsyncFlow::FAwaitableSemaphore Semaphore(3);

AsyncFlow::TTask<void> UMyComponent::DoWork()
{
    co_await Semaphore;
    // At most 3 coroutines are here at once
    co_await AsyncFlow::Delay(1.0f);
    DoExpensiveOperation();
    Semaphore.Release();
}
```

### Batch Release

Release multiple permits in a single call. Useful when a batch of work items complete simultaneously:

```cpp
// Release 5 permits at once — wakes up to 5 waiting coroutines
Semaphore.Release(5);
```

> **Warning:** Calling `Release()` manually is unsafe if the coroutine can be cancelled between `co_await Semaphore` and
`Semaphore.Release()`. A cancellation in that window leaks the permit permanently. Prefer `FSemaphoreGuard` or
`AcquireGuarded()` (see below).

### Thread Safety

`FAwaitableSemaphore` is fully thread-safe. `Release()` and `Release(int32)` can be called from any thread. All permit
acquisition and waiter enqueuing is serialized through an internal critical section.

If `Release()` is called on the game thread, the woken coroutine resumes **inline** (no dispatch overhead).

### Cancelable

The semaphore awaiter supports `CancelAwaiter()`. When a coroutine waiting on a semaphore is cancelled via
`TTask::Cancel()`, the waiter is removed from the queue immediately without consuming a permit.

---

## FSemaphoreGuard

RAII permit holder for `FAwaitableSemaphore`. Calls `Release()` on destruction. Move-only — cannot be copied.

Prevents permit leaks when a coroutine is cancelled or an early return occurs after a permit is acquired.

### API

| Method                                  | Description                                                                      |
|-----------------------------------------|----------------------------------------------------------------------------------|
| `FSemaphoreGuard(FAwaitableSemaphore&)` | Constructs a guard that owns one permit on the given semaphore.                  |
| `Release()`                             | Releases the permit immediately. The destructor becomes a no-op after this call. |
| Destructor                              | Calls `Release()` if the permit has not already been released.                   |

### Usage

```cpp
AsyncFlow::TTask<void> UMyComponent::DoWork()
{
    // Prefer constructing via AcquireGuarded — see the section below
    AsyncFlow::FSemaphoreGuard Guard = co_await AsyncFlow::AcquireGuarded(Semaphore);
    co_await AsyncFlow::Delay(1.0f);
    DoExpensiveOperation();
    // Guard releases the permit here, even if the coroutine is cancelled mid-delay
}
```

---

## AcquireGuarded

`co_await AcquireGuarded(Semaphore)` acquires one permit and returns an `FSemaphoreGuard` in a single atomic step. This
is the recommended pattern for semaphore acquisition in coroutines.

```cpp
AsyncFlow::TTask<void> UMyComponent::DoWork()
{
    AsyncFlow::FSemaphoreGuard Guard = co_await AsyncFlow::AcquireGuarded(Semaphore);
    // Permit is held — at most MaxCount coroutines are in this section
    co_await AsyncFlow::Delay(1.0f);
    DoExpensiveOperation();
    // Guard destructor releases the permit automatically
}
```

For early release before the function returns:

```cpp
AsyncFlow::TTask<void> UMyComponent::DoWork()
{
    AsyncFlow::FSemaphoreGuard Guard = co_await AsyncFlow::AcquireGuarded(Semaphore);
    DoExpensiveOperation();
    Guard.Release();    // Release early — remaining work does not need the permit
    co_await AsyncFlow::Delay(5.0f);
}
```

---

## Combining Primitives

Events and semaphores compose with all other AsyncFlow awaiters:

```cpp
AsyncFlow::TTask<void> UMyComponent::GatedSequence()
{
    co_await DataReadyEvent;                                                          // Wait for external signal
    AsyncFlow::FSemaphoreGuard Guard = co_await AsyncFlow::AcquireGuarded(Semaphore); // Acquire permit (RAII)
    co_await AsyncFlow::Delay(1.0f);                                                  // Game-time delay
    DoWork();
    // Guard releases the permit when it goes out of scope
}
```
