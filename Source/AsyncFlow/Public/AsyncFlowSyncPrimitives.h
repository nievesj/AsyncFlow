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

#include "AsyncFlowTask.h"
#include "HAL/CriticalSection.h"
#include "Async/Async.h"
#include "Containers/Array.h"

#include <coroutine>
#include <atomic>

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
	bool IsSignaled() const { return bSignaled; }

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
				AsyncTask(ENamedThreads::GameThread, [Handle]()
				{
					Handle.resume();
				});
			}
		}
	}

	/**
	 * Reset the event to the non-signaled state.
	 * Future co_awaits will suspend again. Does not affect already-resumed coroutines.
	 */
	void Reset() { bSignaled = false; }

	// co_await interface
	bool await_ready() const { return bSignaled; }

	void await_suspend(std::coroutine_handle<> Handle)
	{
		FScopeLock Lock(&CriticalSection);
		if (bSignaled)
		{
			return; // Already signaled — don't suspend
		}
		Waiters.Add(Handle);
	}

	void await_resume() const {}

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
	int32 GetAvailable() const { return MaxCount - CurrentCount; }

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
				HandleToResume = Waiters[0];
				Waiters.RemoveAt(0);
			}
			else
			{
				++CurrentCount;
			}
		}

		if (HandleToResume && !HandleToResume.done())
		{
			AsyncTask(ENamedThreads::GameThread, [HandleToResume]()
			{
				HandleToResume.resume();
			});
		}
	}

	// co_await interface — acquires one permit
	bool await_ready() const { return CurrentCount < MaxCount; }

	void await_suspend(std::coroutine_handle<> Handle)
	{
		FScopeLock Lock(&CriticalSection);
		if (CurrentCount < MaxCount)
		{
			++CurrentCount;
			return; // Acquired — don't suspend
		}
		Waiters.Add(Handle);
	}

	void await_resume() {}

private:
	int32 MaxCount = 1;
	int32 CurrentCount = 0;
	mutable FCriticalSection CriticalSection;

	/** FIFO queue of coroutines waiting for a permit. */
	TArray<std::coroutine_handle<>> Waiters;
};

} // namespace AsyncFlow

