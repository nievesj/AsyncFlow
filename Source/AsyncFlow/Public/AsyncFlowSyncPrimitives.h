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
// Thread-safe: Signal(), Reset(), Release(), and all co_await operations can be
// called from any thread. Waiters resume on the caller's thread — the thread
// that calls Signal()/Release(). Use co_await MoveToGameThread() after resume
// if game-thread safety is needed.
#pragma once

#include "AsyncFlowTask.h"
#include "Containers/Array.h"
#include "HAL/CriticalSection.h"
#include "Templates/SharedPointer.h"

#include <atomic>
#include <coroutine>

namespace AsyncFlow
{

	// ============================================================================
	// FAwaitableEvent — manual-reset event for coroutines (thread-safe)
	// ============================================================================

	/**
	 * Thread-safe coroutine-compatible manual-reset event. Works like FEvent, but
	 * suspends coroutines instead of blocking threads.
	 *
	 * When not signaled, co_await suspends the caller. When Signal() is called,
	 * all waiting coroutines resume. Subsequent co_awaits resume immediately
	 * until Reset() is called.
	 *
	 * All methods are thread-safe. Signal(), Reset(), and co_await may be called
	 * from any thread. Waiters resume on the thread that calls Signal().
	 *
	 * The co_await FAwaiter satisfies the CancelableAwaiter concept, enabling
	 * expedited cancellation when used inside a TTask.
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
		 * when called concurrently with Signal()/Reset(). Thread-safe.
		 */
		bool IsSignaled() const
		{
			return bSignaled.load(std::memory_order_relaxed);
		}

		/**
		 * Signal the event. All currently suspended coroutines resume inline
		 * on the calling thread. Subsequent co_awaits skip suspension until
		 * Reset(). Thread-safe — may be called from any thread.
		 *
		 * @warning If called from a non-game thread, resumed coroutines run
		 *          on that thread. UObject access requires co_await MoveToGameThread().
		 */
		void Signal()
		{
			TArray<FWaiterEntry> LocalWaiters;
			{
				FScopeLock Lock(&CriticalSection);
				bSignaled.store(true, std::memory_order_release);
				LocalWaiters = MoveTemp(Waiters);
			}

			for (const FWaiterEntry& Entry : LocalWaiters)
			{
				if (Entry.bHasAliveFlag)
				{
					if (const TSharedPtr<bool> Alive = Entry.AliveFlag.Pin(); Alive && *Alive)
					{
						Entry.Handle.resume();
					}
				}
				else
				{
					if (Entry.Handle && !Entry.Handle.done())
					{
						Entry.Handle.resume();
					}
				}
			}
		}

		/**
		 * Reset the event to the non-signaled state.
		 * Future co_awaits will suspend again. Thread-safe.
		 */
		void Reset()
		{
			bSignaled.store(false, std::memory_order_relaxed);
		}

		// Backward-compatible co_await interface (used by non-TTask coroutines
		// and direct callers). TTask coroutines go through operator co_await()
		// which returns an FAwaiter with CancelableAwaiter support.

		bool await_ready() const
		{
			return bSignaled.load(std::memory_order_relaxed);
		}

		bool await_suspend(std::coroutine_handle<> Handle)
		{
			FScopeLock Lock(&CriticalSection);
			if (bSignaled.load(std::memory_order_relaxed))
			{
				return false;
			}
			Waiters.Add({ Handle, TWeakPtr<bool>{}, false });
			return true;
		}

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

		// FAwaiter — per-co_await awaiter with CancelableAwaiter support --------

		/**
		 * Awaiter object returned by operator co_await(). Each co_await gets its
		 * own FAwaiter instance which tracks the coroutine handle, enabling
		 * CancelAwaiter() to remove a specific waiter from the queue.
		 *
		 * Satisfies the CancelableAwaiter concept for expedited cancellation.
		 */
		struct FAwaiter
		{
			FAwaitableEvent* Event = nullptr;
			std::coroutine_handle<> Handle;
			TWeakPtr<bool> Alive;

			bool await_ready() const
			{
				return Event->bSignaled.load(std::memory_order_relaxed);
			}

			bool await_suspend(std::coroutine_handle<> H)
			{
				Handle = H;
				return Event->await_suspend(H);
			}

			bool await_suspend_alive(std::coroutine_handle<> H, TWeakPtr<bool> InAliveFlag)
			{
				Handle = H;
				Alive = InAliveFlag;
				return Event->await_suspend_alive(H, MoveTemp(InAliveFlag));
			}

			void await_resume() const
			{
			}

			/**
			 * Cancel this awaiter: remove from the event's waiter queue and resume
			 * the coroutine so it can process cancellation. Called by the TTask
			 * cancel machinery. Thread-safe.
			 */
			void CancelAwaiter()
			{
				{
					FScopeLock Lock(&Event->CriticalSection);
					for (int32 i = 0; i < Event->Waiters.Num(); ++i)
					{
						if (Event->Waiters[i].Handle == Handle)
						{
							Event->Waiters.RemoveAt(i);
							break;
						}
					}
				}
				Handle.resume();
			}

			/**
			 * Remove this awaiter from the event's waiter queue WITHOUT resuming the
			 * coroutine. Used by TContractCheckAwaiter's cancel path so that the frame
			 * can be permanently terminated via TerminateFunc without executing code
			 * after co_await as if the event was successfully signaled. Thread-safe.
			 */
			void CleanupAwaiter()
			{
				FScopeLock Lock(&Event->CriticalSection);
				for (int32 i = 0; i < Event->Waiters.Num(); ++i)
				{
					if (Event->Waiters[i].Handle == Handle)
					{
						Event->Waiters.RemoveAt(i);
						break;
					}
				}
			}
		};

		/** Returns a per-co_await FAwaiter with CancelableAwaiter support. */
		FAwaiter operator co_await()
		{
			return FAwaiter{ this };
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
	// FAutoResetEvent — auto-reset event for coroutines (thread-safe)
	// ============================================================================

	/**
	 * Thread-safe coroutine-compatible auto-reset event. Like a Windows
	 * auto-reset event: Signal() wakes exactly one waiter. If no waiter is
	 * queued, the next co_await passes through immediately and auto-resets.
	 *
	 * All methods are thread-safe. Signal(), Reset(), and co_await may be called
	 * from any thread. The woken waiter resumes on the thread that calls Signal().
	 *
	 * The co_await FAwaiter satisfies the CancelableAwaiter concept, enabling
	 * expedited cancellation when used inside a TTask.
	 *
	 * Non-copyable. Typically stored as a member.
	 *
	 * Usage:
	 *   FAutoResetEvent NextStepReady;
	 *
	 *   // Producer (any thread):
	 *   NextStepReady.Signal(); // wakes exactly one waiter
	 *
	 *   // Consumer coroutine:
	 *   co_await NextStepReady; // suspends until Signal(), then auto-resets
	 */
	class FAutoResetEvent
	{
	public:
		FAutoResetEvent() = default;
		FAutoResetEvent(const FAutoResetEvent&) = delete;
		FAutoResetEvent& operator=(const FAutoResetEvent&) = delete;

		/**
		 * Signal: wake exactly one waiting coroutine. If no waiter is queued,
		 * set the event so the next co_await passes through immediately.
		 * Thread-safe — may be called from any thread.
		 */
		void Signal()
		{
			std::coroutine_handle<> HandleToResume;
			TWeakPtr<bool> LiveAlive;
			bool bFoundWaiter = false;
			bool bWaiterIsProtected = false;

			{
				FScopeLock Lock(&CriticalSection);

				while (WaitingHandles.Num() > 0)
				{
					FWaiter& Front = WaitingHandles[0];

					if (Front.bHasAliveFlag)
					{
						if (Front.AliveFlag.Pin())
						{
							HandleToResume = Front.Handle;
							LiveAlive = Front.AliveFlag;
							bFoundWaiter = true;
							bWaiterIsProtected = true;
							WaitingHandles.RemoveAt(0);
							break;
						}
						// Dead protected entry: discard and try next
						WaitingHandles.RemoveAt(0);
					}
					else
					{
						HandleToResume = Front.Handle;
						bFoundWaiter = true;
						bWaiterIsProtected = false;
						WaitingHandles.RemoveAt(0);
						break;
					}
				}

				if (!bFoundWaiter)
				{
					// No live waiters — latch the signal for the next co_await
					bSignaled.store(true, std::memory_order_relaxed);
				}
			}

			if (bFoundWaiter)
			{
				if (bWaiterIsProtected)
				{
					if (const TSharedPtr<bool> Alive = LiveAlive.Pin(); Alive && *Alive)
					{
						HandleToResume.resume();
					}
					else
					{
						Signal(); // Waiter died after dequeue — retry next
					}
				}
				else
				{
					if (HandleToResume && !HandleToResume.done())
					{
						HandleToResume.resume();
					}
				}
			}
		}

		/**
		 * Reset: clear the signaled state (manual use; normally auto-resets).
		 * Thread-safe.
		 */
		void Reset()
		{
			bSignaled.store(false, std::memory_order_relaxed);
		}

		/**
		 * @return true if the event is currently in the signaled state.
		 * Diagnostic use. Thread-safe (relaxed-atomic read).
		 */
		bool IsSignaled() const
		{
			return bSignaled.load(std::memory_order_relaxed);
		}

		/**
		 * Awaiter object returned by operator co_await(). Implements auto-reset
		 * semantics: if the event is signaled, await_ready atomically consumes
		 * the signal and returns true. Satisfies the CancelableAwaiter concept.
		 */
		struct FAwaiter
		{
			FAutoResetEvent* Event = nullptr;
			std::coroutine_handle<> Handle;
			TWeakPtr<bool> Alive;

			bool await_ready()
			{
				// Atomically consume the signal if set (auto-reset)
				bool Expected = true;
				return Event->bSignaled.compare_exchange_strong(
					Expected, false, std::memory_order_acquire, std::memory_order_relaxed);
			}

			bool await_suspend(std::coroutine_handle<> H)
			{
				Handle = H;
				FScopeLock Lock(&Event->CriticalSection);
				// Double-check: signal may have arrived between await_ready and here
				bool Expected = true;
				if (Event->bSignaled.compare_exchange_strong(
						Expected, false, std::memory_order_acquire, std::memory_order_relaxed))
				{
					return false; // Consumed signal — don't suspend
				}
				Event->WaitingHandles.Add({ H, TWeakPtr<bool>{}, false });
				return true;
			}

			bool await_suspend_alive(std::coroutine_handle<> H, TWeakPtr<bool> InAlive)
			{
				Handle = H;
				Alive = InAlive;
				FScopeLock Lock(&Event->CriticalSection);
				bool Expected = true;
				if (Event->bSignaled.compare_exchange_strong(
						Expected, false, std::memory_order_acquire, std::memory_order_relaxed))
				{
					return false;
				}
				Event->WaitingHandles.Add({ H, MoveTemp(InAlive), true });
				return true;
			}

			void await_resume() const
			{
			}

			/**
			 * Cancel this awaiter: remove from the queue and resume the coroutine
			 * so it can process cancellation. Thread-safe.
			 */
			void CancelAwaiter()
			{
				{
					FScopeLock Lock(&Event->CriticalSection);
					for (int32 i = 0; i < Event->WaitingHandles.Num(); ++i)
					{
						if (Event->WaitingHandles[i].Handle == Handle)
						{
							Event->WaitingHandles.RemoveAt(i);
							break;
						}
					}
				}
				Handle.resume();
			}

			/**
			 * Remove this awaiter from the auto-reset event's queue WITHOUT resuming
			 * the coroutine. Used by TContractCheckAwaiter's cancel path so the frame
			 * is permanently terminated without executing code after co_await as if
			 * the event was successfully signaled. Thread-safe.
			 */
			void CleanupAwaiter()
			{
				FScopeLock Lock(&Event->CriticalSection);
				for (int32 i = 0; i < Event->WaitingHandles.Num(); ++i)
				{
					if (Event->WaitingHandles[i].Handle == Handle)
					{
						Event->WaitingHandles.RemoveAt(i);
						break;
					}
				}
			}
		};

		FAwaiter operator co_await()
		{
			return FAwaiter{ this };
		}

	private:
		mutable FCriticalSection CriticalSection;
		std::atomic<bool> bSignaled{ false };

		struct FWaiter
		{
			std::coroutine_handle<> Handle;
			TWeakPtr<bool> AliveFlag;
			bool bHasAliveFlag;
		};
		TArray<FWaiter> WaitingHandles;
	};

	// ============================================================================
	// FAwaitableSemaphore — counting semaphore for coroutines (thread-safe)
	// ============================================================================

	// Forward-declared so FAcquireGuardedAwaiter can be a friend.
	struct FAcquireGuardedAwaiter;

	/**
	 * Thread-safe coroutine-compatible counting semaphore. Controls concurrent
	 * access to a limited resource without blocking threads.
	 *
	 * Construct with a maximum count. co_await acquires one permit (suspends
	 * if none available). Release() returns one or more permits and resumes
	 * waiting coroutines.
	 *
	 * All methods are thread-safe. Release(), co_await, and GetAvailable() may
	 * be called from any thread. Waiters resume on the thread that calls Release().
	 *
	 * The co_await FAwaiter satisfies the CancelableAwaiter concept, enabling
	 * expedited cancellation when used inside a TTask.
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
	 *
	 *   LoadSlots.Release(2); // batch-release 2 slots at once
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
		 * when called concurrently with acquire/release operations. Thread-safe.
		 */
		int32 GetAvailable() const
		{
			return MaxCount - CurrentCount.load(std::memory_order_relaxed);
		}

		/**
		 * Return one or more permits. For each permit, resumes the oldest live
		 * waiting coroutine. Dead waiters (frames destroyed while queued) are
		 * skipped automatically. If no live waiters remain for a permit, the
		 * permit count is decremented.
		 *
		 * Waiters resume inline on the calling thread. If you need game-thread
		 * safety after resume, use co_await MoveToGameThread() in the waiter.
		 *
		 * @param Count  Number of permits to release. Must be >= 1.
		 *
		 * @warning Calling Release() more times than permits were acquired triggers
		 *          an ensure.
		 */
		void Release(int32 Count = 1)
		{
			check(Count >= 1);

			struct FResumeInfo
			{
				std::coroutine_handle<> Handle;
				TWeakPtr<bool> AliveFlag;
				bool bIsProtected;
			};
			TArray<FResumeInfo> ToResume;

			{
				FScopeLock Lock(&CriticalSection);

				int32 Remaining = Count;

				// Walk the queue from the head, handing permits to live waiters.
				while (Remaining > 0 && WaiterHead < Waiters.Num())
				{
					FWaiterEntry& Entry = Waiters[WaiterHead++];

					if (Entry.bHasAliveFlag)
					{
						if (Entry.AliveFlag.Pin())
						{
							ToResume.Add({ Entry.Handle, Entry.AliveFlag, true });
							--Remaining;
						}
						// Dead protected entry: no permit was counted — skip.
					}
					else
					{
						ToResume.Add({ Entry.Handle, TWeakPtr<bool>{}, false });
						--Remaining;
					}
				}

				// Compact once the entire queue has been drained.
				if (WaiterHead >= Waiters.Num())
				{
					Waiters.Reset();
					WaiterHead = 0;
				}

				// Free permits that weren't handed to waiters.
				if (Remaining > 0)
				{
					CurrentCount.fetch_sub(Remaining, std::memory_order_relaxed);
					ensureMsgf(
						CurrentCount.load(std::memory_order_relaxed) >= 0,
						TEXT("FAwaitableSemaphore::Release() called more times than acquired"));
				}
			}

			for (const FResumeInfo& Info : ToResume)
			{
				if (Info.bIsProtected)
				{
					if (const TSharedPtr<bool> Alive = Info.AliveFlag.Pin(); Alive && *Alive)
					{
						Info.Handle.resume();
					}
					else
					{
						// Waiter died after we "gave" it the permit — bounce back.
						Release();
					}
				}
				else
				{
					if (Info.Handle && !Info.Handle.done())
					{
						Info.Handle.resume();
					}
				}
			}
		}

		// Backward-compatible co_await interface (used by non-TTask coroutines
		// and direct callers). TTask coroutines go through operator co_await()
		// which returns an FAwaiter with CancelableAwaiter support.

		/** Always returns false — acquisition is done atomically inside await_suspend. */
		bool await_ready() const
		{
			return false;
		}

		bool await_suspend(std::coroutine_handle<> Handle)
		{
			FScopeLock Lock(&CriticalSection);
			if (CurrentCount.load(std::memory_order_relaxed) < MaxCount)
			{
				CurrentCount.fetch_add(1, std::memory_order_relaxed);
				return false;
			}
			Waiters.Add({ Handle, TWeakPtr<bool>{}, false });
			return true;
		}

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

		// FAwaiter — per-co_await awaiter with CancelableAwaiter support --------

		/**
		 * Awaiter object returned by operator co_await(). Tracks the coroutine
		 * handle so CancelAwaiter() can remove a specific waiter from the queue.
		 * Satisfies the CancelableAwaiter concept.
		 */
		struct FAwaiter
		{
			FAwaitableSemaphore* Semaphore = nullptr;
			std::coroutine_handle<> Handle;
			TWeakPtr<bool> Alive;

			bool await_ready() const
			{
				return false;
			}

			bool await_suspend(std::coroutine_handle<> H)
			{
				Handle = H;
				return Semaphore->await_suspend(H);
			}

			bool await_suspend_alive(std::coroutine_handle<> H, TWeakPtr<bool> InAliveFlag)
			{
				Handle = H;
				Alive = InAliveFlag;
				return Semaphore->await_suspend_alive(H, MoveTemp(InAliveFlag));
			}

			void await_resume()
			{
			}

			/**
			 * Cancel this awaiter: remove from the semaphore's waiter queue and
			 * resume the coroutine so it can process cancellation. The waiter
			 * never acquired a permit, so no permit accounting changes. Thread-safe.
			 */
			void CancelAwaiter()
			{
				{
					FScopeLock Lock(&Semaphore->CriticalSection);
					for (int32 i = Semaphore->WaiterHead; i < Semaphore->Waiters.Num(); ++i)
					{
						if (Semaphore->Waiters[i].Handle == Handle)
						{
							Semaphore->Waiters.RemoveAt(i);
							break;
						}
					}
				}
				Handle.resume();
			}

			/**
			 * Remove this awaiter from the semaphore's waiter queue WITHOUT resuming
			 * the coroutine. Used by TContractCheckAwaiter's cancel path so the frame
			 * is permanently terminated without executing code after co_await as if
			 * the permit was successfully acquired. Thread-safe.
			 */
			void CleanupAwaiter()
			{
				FScopeLock Lock(&Semaphore->CriticalSection);
				for (int32 i = Semaphore->WaiterHead; i < Semaphore->Waiters.Num(); ++i)
				{
					if (Semaphore->Waiters[i].Handle == Handle)
					{
						Semaphore->Waiters.RemoveAt(i);
						break;
					}
				}
			}
		};

		/** Returns a per-co_await FAwaiter with CancelableAwaiter support. */
		FAwaiter operator co_await()
		{
			return FAwaiter{ this };
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
	 * Satisfies the CancelableAwaiter concept for expedited cancellation.
	 *
	 * Usage:
	 *   auto Guard = co_await AsyncFlow::AcquireGuarded(Semaphore);
	 *   // permit held; released automatically when Guard goes out of scope
	 */
	struct FAcquireGuardedAwaiter
	{
		FAwaitableSemaphore& Semaphore;
		std::coroutine_handle<> Handle;

		bool await_ready() const
		{
			return false;
		}

		bool await_suspend(std::coroutine_handle<> H)
		{
			Handle = H;
			return Semaphore.await_suspend(H);
		}

		bool await_suspend_alive(std::coroutine_handle<> H, TWeakPtr<bool> InAliveFlag)
		{
			Handle = H;
			return Semaphore.await_suspend_alive(H, MoveTemp(InAliveFlag));
		}

		FSemaphoreGuard await_resume()
		{
			return FSemaphoreGuard(Semaphore);
		}

		/**
		 * Cancel this awaiter: remove from the semaphore's waiter queue and
		 * resume the coroutine so it can process cancellation. The waiter
		 * never acquired a permit, so no permit accounting changes. Thread-safe.
		 */
		void CancelAwaiter()
		{
			{
				FScopeLock Lock(&Semaphore.CriticalSection);
				for (int32 i = Semaphore.WaiterHead; i < Semaphore.Waiters.Num(); ++i)
				{
					if (Semaphore.Waiters[i].Handle == Handle)
					{
						Semaphore.Waiters.RemoveAt(i);
						break;
					}
				}
			}
			Handle.resume();
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