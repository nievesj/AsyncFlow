// MIT License
//
// Copyright (c) 2026 José M. Nieves
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// AsyncFlowSyncPrimitives.h — Coroutine-compatible synchronization primitives
//
// These are NOT OS-level primitives. They work by suspending coroutines (co_await)
// rather than blocking threads. Multiple coroutines can wait on the same primitive
// without blocking any threads.
//
// Thread-safe: Signal(), Reset(), and Release() can be called from any thread.
// Waiters always resume on the game thread via AsyncTask(ENamedThreads::GameThread).
#pragma once

#include "Async/Async.h"
#include "AsyncFlowTask.h"
#include "Containers/Array.h"
#include "HAL/CriticalSection.h"
#include "Templates/SharedPointer.h"

#include <atomic>
#include <coroutine>

namespace AsyncFlow
{

	// ============================================================================
	// FAwaitableEvent — manual-reset event for coroutines
	// ============================================================================

	/**
	 * Coroutine-compatible manual-reset event. Works like FEvent, but suspends
	 * coroutines instead of blocking threads.
	 *
	 * When not signaled, co_await suspends the caller. When Signal() is called,
	 * all waiting coroutines resume. Subsequent co_awaits resume immediately
	 * until Reset() is called.
	 *
	 * Non-copyable. Typically stored as a member of a component or system.
	 *
	 * Usage:
	 *   FAwaitableEvent DoorOpened;
	 *
	 *   // In one coroutine:
	 *   co_await DoorOpened; // suspends until Signal() is called
	 *
	 *   // In game logic (any thread):
	 *   DoorOpened.Signal(); // resumes all waiters on the game thread
	 */
	struct FAwaitableEvent
	{
		FAwaitableEvent() = default;
		FAwaitableEvent(const FAwaitableEvent&) = delete;
		FAwaitableEvent& operator=(const FAwaitableEvent&) = delete;

		/**
		 * @return true if the event is currently in the signaled state.
		 * This is a relaxed-atomic read and may return a slightly stale value
		 * when called concurrently with Signal()/Reset().
		 */
		bool IsSignaled() const
		{
			return bSignaled.load(std::memory_order_relaxed);
		}

		/**
		 * Signal the event. All currently suspended coroutines resume on the
		 * game thread. Subsequent co_awaits skip suspension until Reset().
		 * Thread-safe — may be called from any thread.
		 */
		void Signal()
		{
			TArray<FWaiterEntry> LocalWaiters;
			{
				FScopeLock Lock(&CriticalSection);
				bSignaled.store(true, std::memory_order_relaxed);
				LocalWaiters = MoveTemp(Waiters);
			}

			for (const FWaiterEntry& Entry : LocalWaiters)
			{
				const std::coroutine_handle<> Handle = Entry.Handle;

				if (Entry.bHasAliveFlag)
				{
					// Protected path: only resume if the coroutine frame is still alive.
					const TWeakPtr<bool> WeakAlive = Entry.AliveFlag;
					AsyncTask(ENamedThreads::GameThread, [Handle, WeakAlive]() {
						if (const TSharedPtr<bool> Alive = WeakAlive.Pin(); Alive && *Alive)
						{
							Handle.resume();
						}
					});
				}
				else
				{
					// Legacy path: no alive flag; check done() as a best-effort guard.
					AsyncTask(ENamedThreads::GameThread, [Handle]() {
						if (Handle && !Handle.done())
						{
							Handle.resume();
						}
					});
				}
			}
		}

		/**
		 * Reset the event to the non-signaled state.
		 * Future co_awaits will suspend again.
		 * Thread-safe — may be called from any thread.
		 */
		void Reset()
		{
			bSignaled.store(false, std::memory_order_relaxed);
		}

		// co_await interface ---------------------------------------------------------

		/**
		 * @return true if the event is already signaled. Called by the coroutine
		 * machinery before await_suspend to fast-path already-signaled events.
		 */
		bool await_ready() const
		{
			return bSignaled.load(std::memory_order_relaxed);
		}

		/**
		 * Suspend the coroutine. Called by TContractCheckAwaiter (via await_suspend_alive)
		 * or directly by non-TTask coroutines (legacy path). Returns false if the event
		 * was signaled in the window between await_ready and now.
		 */
		bool await_suspend(std::coroutine_handle<> Handle)
		{
			FScopeLock Lock(&CriticalSection);
			if (bSignaled.load(std::memory_order_relaxed))
			{
				return false; // Signaled between await_ready and here — don't suspend
			}
			Waiters.Add({ Handle, TWeakPtr<bool>{}, false });
			return true;
		}

		/**
		 * Alive-flag-aware suspend. Called by TContractCheckAwaiter which holds the
		 * alive flag for this co_await site. Signal() will not resume the handle once
		 * the coroutine frame has been destroyed (alive flag set to false).
		 */
		bool await_suspend_alive(std::coroutine_handle<> Handle, TWeakPtr<bool> InAliveFlag)
		{
			FScopeLock Lock(&CriticalSection);
			if (bSignaled.load(std::memory_order_relaxed))
			{
				return false;
			}
			Waiters.Add({ Handle, MoveTemp(InAliveFlag), true });
			return true;
		}

		void await_resume() const
		{
		}

	private:
		struct FWaiterEntry
		{
			std::coroutine_handle<> Handle;
			TWeakPtr<bool> AliveFlag;
			bool bHasAliveFlag;
		};

		std::atomic<bool> bSignaled{ false };
		mutable FCriticalSection CriticalSection;
		TArray<FWaiterEntry> Waiters;
	};

	// ============================================================================
	// FAwaitableSemaphore — counting semaphore for coroutines
	// ============================================================================

	// Forward-declared so FAcquireGuardedAwaiter can be a friend.
	struct FAcquireGuardedAwaiter;

	/**
	 * Coroutine-compatible counting semaphore. Controls concurrent access
	 * to a limited resource without blocking threads.
	 *
	 * Construct with a maximum count. co_await acquires one permit (suspends
	 * if none available). Release() returns a permit and resumes one waiter.
	 *
	 * Non-copyable. Typically stored as a member to rate-limit operations.
	 *
	 * @warning Manual Release() calls are unsafe if the coroutine can be cancelled.
	 *          Use FSemaphoreGuard for exception-safe / cancellation-safe acquire+release.
	 *
	 * Usage:
	 *   FAwaitableSemaphore LoadSlots{3}; // allow 3 concurrent loads
	 *
	 *   co_await LoadSlots; // acquires a slot (suspends if all 3 are in use)
	 *   auto* Obj = co_await AsyncFlow::AsyncLoadObject(SoftPtr);
	 *   LoadSlots.Release(); // frees the slot for other waiters
	 */
	struct FAwaitableSemaphore
	{
		friend struct FAcquireGuardedAwaiter;

		/**
		 * @param InMaxCount  Maximum number of concurrent permits. Must be >= 1.
		 */
		explicit FAwaitableSemaphore(int32 InMaxCount = 1)
			: MaxCount(InMaxCount)
		{
			check(InMaxCount >= 1);
		}

		FAwaitableSemaphore(const FAwaitableSemaphore&) = delete;
		FAwaitableSemaphore& operator=(const FAwaitableSemaphore&) = delete;

		/**
		 * @return the number of currently available permits.
		 * This is a relaxed-atomic read and may reflect a slightly stale count
		 * when called concurrently with acquire/release operations.
		 */
		int32 GetAvailable() const
		{
			return MaxCount - CurrentCount.load(std::memory_order_relaxed);
		}

		/**
		 * Return one permit. Resumes the oldest live waiting coroutine, if any.
		 * Dead waiters (frames destroyed while queued) are skipped automatically.
		 * If no live waiters remain, the permit count is decremented.
		 *
		 * @warning Calling Release() more times than permits were acquired triggers
		 *          an ensure.
		 * @warning The semaphore must outlive any AsyncTask callbacks posted by
		 *          Release(). Destroying the semaphore while callbacks are in flight
		 *          is a programming error.
		 */
		void Release()
		{
			std::coroutine_handle<> HandleToResume;
			TWeakPtr<bool> LiveAlive;
			bool bFoundWaiter = false;
			bool bWaiterIsProtected = false;

			{
				FScopeLock Lock(&CriticalSection);

				// Walk the queue from the head, skipping dead protected waiters.
				while (WaiterHead < Waiters.Num())
				{
					FWaiterEntry& Entry = Waiters[WaiterHead++];

					if (Entry.bHasAliveFlag)
					{
						// Protected entry: only hand permit if the frame is still alive.
						if (Entry.AliveFlag.Pin())
						{
							HandleToResume = Entry.Handle;
							LiveAlive = Entry.AliveFlag;
							bFoundWaiter = true;
							bWaiterIsProtected = true;
							break;
						}
						// Dead protected entry: no permit was counted for it — skip.
					}
					else
					{
						// Legacy unprotected entry: always hand the permit.
						HandleToResume = Entry.Handle;
						bFoundWaiter = true;
						bWaiterIsProtected = false;
						break;
					}
				}

				// Compact once the entire queue has been drained.
				if (WaiterHead >= Waiters.Num())
				{
					Waiters.Reset();
					WaiterHead = 0;
				}

				if (!bFoundWaiter)
				{
					// No waiter to hand the permit to — truly free it.
					CurrentCount.fetch_sub(1, std::memory_order_relaxed);
					ensureMsgf(
						CurrentCount.load(std::memory_order_relaxed) >= 0,
						TEXT("FAwaitableSemaphore::Release() called more times than acquired"));
				}
				// If bFoundWaiter: permit passes to waiter; CurrentCount unchanged.
			}

			if (bFoundWaiter)
			{
				if (bWaiterIsProtected)
				{
					// Protected path: check alive flag on the game thread.
					// NOTE: 'this' must remain valid until the callback runs.
					AsyncTask(ENamedThreads::GameThread, [HandleToResume, LiveAlive, this]() {
						if (const TSharedPtr<bool> Alive = LiveAlive.Pin(); Alive && *Alive)
						{
							HandleToResume.resume();
						}
						else
						{
							// Waiter died after Release() already "gave" it the permit.
							// Return the permit by releasing again.
							this->Release();
						}
					});
				}
				else
				{
					// Legacy path: resume if not already done.
					AsyncTask(ENamedThreads::GameThread, [HandleToResume]() {
						if (HandleToResume && !HandleToResume.done())
						{
							HandleToResume.resume();
						}
					});
				}
			}
		}

		// co_await interface ---------------------------------------------------------

		/** Always returns false — acquisition is done atomically inside await_suspend. */
		bool await_ready() const
		{
			return false;
		}

		/**
		 * Acquire one permit under the lock. If permits are available, acquires
		 * immediately (returns false = don't suspend). Otherwise queues the handle.
		 */
		bool await_suspend(std::coroutine_handle<> Handle)
		{
			FScopeLock Lock(&CriticalSection);
			if (CurrentCount.load(std::memory_order_relaxed) < MaxCount)
			{
				CurrentCount.fetch_add(1, std::memory_order_relaxed);
				return false; // Acquired — don't suspend
			}
			Waiters.Add({ Handle, TWeakPtr<bool>{}, false });
			return true;
		}

		/**
		 * Alive-flag-aware acquire. Called by TContractCheckAwaiter.
		 * Release() will not resume the handle once the frame is destroyed.
		 */
		bool await_suspend_alive(std::coroutine_handle<> Handle, TWeakPtr<bool> InAliveFlag)
		{
			FScopeLock Lock(&CriticalSection);
			if (CurrentCount.load(std::memory_order_relaxed) < MaxCount)
			{
				CurrentCount.fetch_add(1, std::memory_order_relaxed);
				return false;
			}
			Waiters.Add({ Handle, MoveTemp(InAliveFlag), true });
			return true;
		}

		void await_resume()
		{
		}

	private:
		struct FWaiterEntry
		{
			std::coroutine_handle<> Handle;
			TWeakPtr<bool> AliveFlag;
			bool bHasAliveFlag;
		};

		int32 MaxCount = 1;
		std::atomic<int32> CurrentCount{ 0 };
		mutable FCriticalSection CriticalSection;

		/** FIFO queue of coroutines waiting for a permit. */
		TArray<FWaiterEntry> Waiters;

		/**
		 * Index of the next entry to examine in Waiters.
		 * Avoids O(n) memmove from RemoveAt(0) on every dequeue.
		 * Reset to 0 when WaiterHead reaches Waiters.Num().
		 */
		int32 WaiterHead = 0;
	};

	// ============================================================================
	// FSemaphoreGuard — RAII permit holder (no permit leaks on cancel/throw)
	// ============================================================================

	/**
	 * RAII guard that releases a semaphore permit on destruction. Prevents
	 * permit leaks when a coroutine is cancelled or an exception occurs
	 * between acquire and release.
	 *
	 * Usage:
	 *   co_await Semaphore;
	 *   FSemaphoreGuard Guard(Semaphore);
	 *   // ... do work ...
	 *   // Guard releases the permit automatically, even if cancelled
	 *
	 * Or use the helper:
	 *   auto Guard = co_await AcquireGuarded(Semaphore);
	 */
	struct FSemaphoreGuard
	{
		explicit FSemaphoreGuard(FAwaitableSemaphore& InSemaphore)
			: Semaphore(&InSemaphore)
		{
		}

		~FSemaphoreGuard()
		{
			if (Semaphore)
			{
				Semaphore->Release();
			}
		}

		FSemaphoreGuard(FSemaphoreGuard&& Other) noexcept
			: Semaphore(Other.Semaphore)
		{
			Other.Semaphore = nullptr;
		}

		FSemaphoreGuard& operator=(FSemaphoreGuard&& Other) noexcept
		{
			if (this != &Other)
			{
				if (Semaphore)
				{
					Semaphore->Release();
				}
				Semaphore = Other.Semaphore;
				Other.Semaphore = nullptr;
			}
			return *this;
		}

		FSemaphoreGuard(const FSemaphoreGuard&) = delete;
		FSemaphoreGuard& operator=(const FSemaphoreGuard&) = delete;

		/** Explicitly release the permit early. No-op after the first call. */
		void Release()
		{
			if (Semaphore)
			{
				Semaphore->Release();
				Semaphore = nullptr;
			}
		}

	private:
		FAwaitableSemaphore* Semaphore;
	};

	/**
	 * Awaiter that acquires a semaphore permit and returns an FSemaphoreGuard.
	 * Combines co_await + RAII in one step. Uses the alive-flag-aware path so
	 * Release() does not touch a destroyed coroutine frame.
	 *
	 * Usage:
	 *   auto Guard = co_await AsyncFlow::AcquireGuarded(Semaphore);
	 *   // permit held; released automatically when Guard goes out of scope
	 */
	struct FAcquireGuardedAwaiter
	{
		FAwaitableSemaphore& Semaphore;

		bool await_ready() const
		{
			return false;
		}

		// Delegates to the semaphore's own await_suspend/await_suspend_alive.
		// TContractCheckAwaiter wraps this and provides the alive flag.
		bool await_suspend(std::coroutine_handle<> Handle)
		{
			return Semaphore.await_suspend(Handle);
		}

		bool await_suspend_alive(std::coroutine_handle<> Handle, TWeakPtr<bool> InAliveFlag)
		{
			return Semaphore.await_suspend_alive(Handle, MoveTemp(InAliveFlag));
		}

		FSemaphoreGuard await_resume()
		{
			return FSemaphoreGuard(Semaphore);
		}
	};

	/**
	 * Acquire a semaphore permit and return an RAII guard that releases it
	 * automatically on destruction. Cancellation-safe.
	 *
	 * @param Semaphore  The semaphore to acquire from.
	 * @return           An awaiter — co_await yields FSemaphoreGuard.
	 */
	[[nodiscard]] inline FAcquireGuardedAwaiter AcquireGuarded(FAwaitableSemaphore& Semaphore)
	{
		return FAcquireGuardedAwaiter{ Semaphore };
	}

} // namespace AsyncFlow
