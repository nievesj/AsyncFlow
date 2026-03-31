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

`await_suspend` returns `bool`. If `Signal()` fires in the window between `await_ready` and `await_suspend`, the coroutine is not suspended — it continues immediately rather than hanging indefinitely waiting for a signal that already fired.

---

## FAwaitableSemaphore

Counting semaphore for coroutines. Every `co_await` enters `await_suspend` and acquires a permit atomically under a lock — there is no fast-path bypass. If a permit is available, the count is incremented and the coroutine continues without suspending. If all permits are in use, the coroutine is enqueued and suspended.

All waiters resume on the game thread.

### API

| Method | Description |
|--------|-------------|
| `FAwaitableSemaphore(int32 MaxCount)` | Construct with a maximum permit count (>= 1). |
| `co_await Semaphore` | Acquires one permit under the lock. Suspends if all permits are in use. |
| `Release()` | If there are waiters, resumes the oldest (FIFO) and hands it the permit directly (count unchanged). If no waiters, decrements the count. |
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

> **Warning:** Calling `Release()` manually is unsafe if the coroutine can be cancelled between `co_await Semaphore` and `Semaphore.Release()`. A cancellation in that window leaks the permit permanently. Prefer `FSemaphoreGuard` or `AcquireGuarded()` (see below).

### Thread Safety

`FAwaitableSemaphore` is thread-safe. `Release()` can be called from any thread. Waiters always resume on the game thread. All permit acquisition and waiter enqueuing is serialized through an internal lock.

---

## FSemaphoreGuard

RAII permit holder for `FAwaitableSemaphore`. Calls `Release()` on destruction. Move-only — cannot be copied.

Prevents permit leaks when a coroutine is cancelled or an early return occurs after a permit is acquired.

### API

| Method | Description |
|--------|-------------|
| `FSemaphoreGuard(FAwaitableSemaphore&)` | Constructs a guard that owns one permit on the given semaphore. |
| `Release()` | Releases the permit immediately. The destructor becomes a no-op after this call. |
| Destructor | Calls `Release()` if the permit has not already been released. |

### Usage

```cpp
AsyncFlow::TTask<void> UMyComponent::DoWork()
{
    // Prefer constructing via AcquireGuarded — see the section below
    AsyncFlow::FSemaphoreGuard Guard = co_await AsyncFlow::AcquireGuarded(Semaphore);
    co_await AsyncFlow::Delay(this, 1.0f);
    DoExpensiveOperation();
    // Guard releases the permit here, even if the coroutine is cancelled mid-delay
}
```

---

## AcquireGuarded

`co_await AcquireGuarded(Semaphore)` acquires one permit and returns an `FSemaphoreGuard` in a single atomic step. This is the recommended pattern for semaphore acquisition in coroutines.

```cpp
AsyncFlow::TTask<void> UMyComponent::DoWork()
{
    AsyncFlow::FSemaphoreGuard Guard = co_await AsyncFlow::AcquireGuarded(Semaphore);
    // Permit is held — at most MaxCount coroutines are in this section
    co_await AsyncFlow::Delay(this, 1.0f);
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
    co_await AsyncFlow::Delay(this, 5.0f);
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
    co_await AsyncFlow::Delay(this, 1.0f);                                            // Game-time delay
    DoWork();
    // Guard releases the permit when it goes out of scope
}
```
