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
// Game-thread-only. Not suitable for cross-thread synchronization.
#pragma once

#include "Async/Async.h"
#include "AsyncFlowTask.h"
#include "Containers/Array.h"
#include "HAL/CriticalSection.h"

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
	 *   // In game logic:
	 *   DoorOpened.Signal(); // resumes all waiters
	 */
	struct FAwaitableEvent
	{
		/**
		 * @return true if the event is currently in the signaled state.
		 * co_await on a signaled event returns immediately.
		 */
		bool IsSignaled() const
		{
			return bSignaled;
		}

		/**
		 * Signal the event. All currently suspended coroutines resume.
		 * Subsequent co_awaits skip suspension until Reset() is called.
		 */
		void Signal()
		{
			TArray<std::coroutine_handle<>> LocalWaiters;
			{
				FScopeLock Lock(&CriticalSection);
				bSignaled = true;
				LocalWaiters = MoveTemp(Waiters);
			}

			for (std::coroutine_handle<> Handle : LocalWaiters)
			{
				if (Handle && !Handle.done())
				{
					AsyncTask(ENamedThreads::GameThread, [Handle]() { Handle.resume(); });
				}
			}
		}

		/**
		 * Reset the event to the non-signaled state.
		 * Future co_awaits will suspend again. Does not affect already-resumed coroutines.
		 */
		void Reset()
		{
			bSignaled = false;
		}

		// co_await interface
		bool await_ready() const
		{
			return bSignaled;
		}

		bool await_suspend(std::coroutine_handle<> Handle)
		{
			FScopeLock Lock(&CriticalSection);
			if (bSignaled)
			{
				return false; // Already signaled between await_ready and here — don't suspend
			}
			Waiters.Add(Handle);
			return true;
		}

		void await_resume() const
		{
		}

	private:
		bool bSignaled = false;
		mutable FCriticalSection CriticalSection;

		/** Suspended coroutine handles waiting for Signal(). */
		TArray<std::coroutine_handle<>> Waiters;
	};

	// ============================================================================
	// FAwaitableSemaphore — counting semaphore for coroutines
	// ============================================================================

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
		/**
		 * @param InMaxCount  Maximum number of concurrent permits.
		 *                    Must be >= 1.
		 */
		explicit FAwaitableSemaphore(int32 InMaxCount = 1)
			: MaxCount(InMaxCount)
		{
			check(InMaxCount >= 1);
		}

		/** @return the number of currently available permits. */
		int32 GetAvailable() const
		{
			return MaxCount - CurrentCount;
		}

		/**
		 * Return one permit. Resumes the oldest waiting coroutine, if any.
		 *
		 * @warning Calling Release() more times than permits were acquired
		 *          is a programming error (CurrentCount would go negative).
		 */
		void Release()
		{
			std::coroutine_handle<> HandleToResume;
			{
				FScopeLock Lock(&CriticalSection);
				if (Waiters.Num() > 0)
				{
					// Hand the released permit directly to the next waiter.
					// CurrentCount stays the same — one released, one acquired.
					HandleToResume = Waiters[0];
					Waiters.RemoveAt(0);
				}
				else
				{
					--CurrentCount;
					ensureMsgf(CurrentCount >= 0, TEXT("FAwaitableSemaphore::Release() called more times than acquired"));
				}
			}

			if (HandleToResume && !HandleToResume.done())
			{
				AsyncTask(ENamedThreads::GameThread, [HandleToResume]() { HandleToResume.resume(); });
			}
		}

		// co_await interface — acquires one permit.
		// Always enters await_suspend to acquire under the lock atomically.
		bool await_ready() const
		{
			return false;
		}

		bool await_suspend(std::coroutine_handle<> Handle)
		{
			FScopeLock Lock(&CriticalSection);
			if (CurrentCount < MaxCount)
			{
				++CurrentCount;
				return false; // Acquired — don't suspend
			}
			Waiters.Add(Handle);
			return true;
		}

		void await_resume()
		{
		}

	private:
		int32 MaxCount = 1;
		int32 CurrentCount = 0;
		mutable FCriticalSection CriticalSection;

		/** FIFO queue of coroutines waiting for a permit. */
		TArray<std::coroutine_handle<>> Waiters;
	};

	// ============================================================================
	// FSemaphoreGuard — RAII permit holder (Rule 19: no permit leaks on throw)
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
	 * Combines co_await + RAII in one step.
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

		bool await_suspend(std::coroutine_handle<> Handle)
		{
			// Delegate to the semaphore's own await_suspend
			return Semaphore.await_suspend(Handle);
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