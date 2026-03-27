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

// AsyncFlowSyncPrimitives.h — Thread-safe synchronization awaiters
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
// FAwaitableEvent — thread-safe manual-reset event that coroutines can co_await
// ============================================================================

/**
 * A manual-reset event. Coroutines that co_await a triggered event resume
 * immediately. Coroutines that co_await before Trigger() is called are
 * suspended until Trigger(). All waiters resume on the game thread.
 *
 * Usage:
 *   FAwaitableEvent Event;
 *   // In coroutine A:
 *   co_await Event;
 *   // In code B:
 *   Event.Trigger();
 */
class FAwaitableEvent
{
public:
	FAwaitableEvent() = default;

	/** Trigger the event, resuming all waiting coroutines on the game thread. */
	void Trigger()
	{
		TArray<std::coroutine_handle<>> LocalWaiters;
		{
			FScopeLock Lock(&CriticalSection);
			bTriggered.store(true, std::memory_order_release);
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

	/** Reset the event so future co_awaits will suspend again. */
	void Reset()
	{
		FScopeLock Lock(&CriticalSection);
		bTriggered.store(false, std::memory_order_release);
	}

	bool IsTriggered() const { return bTriggered.load(std::memory_order_acquire); }

	// Awaitable interface
	bool await_ready() const { return bTriggered.load(std::memory_order_acquire); }

	bool await_suspend(std::coroutine_handle<> Handle)
	{
		FScopeLock Lock(&CriticalSection);
		if (bTriggered.load(std::memory_order_relaxed))
		{
			return false; // Already triggered — don't suspend
		}
		Waiters.Add(Handle);
		return true;
	}

	void await_resume() const {}

private:
	FCriticalSection CriticalSection;
	TArray<std::coroutine_handle<>> Waiters;
	std::atomic<bool> bTriggered{false};
};

// ============================================================================
// FAwaitableSemaphore — thread-safe counting semaphore for coroutines
// ============================================================================

/**
 * A counting semaphore. Acquire() suspends if count == 0; Release()
 * increments the count and resumes one waiter. Waiters resume on the game thread.
 *
 * Usage:
 *   FAwaitableSemaphore Sem(3); // 3 concurrent slots
 *   co_await Sem.Acquire();
 *   // ... do work ...
 *   Sem.Release();
 */
class FAwaitableSemaphore
{
public:
	explicit FAwaitableSemaphore(int32 InitialCount = 0)
		: Count(InitialCount)
	{
	}

	/** Release one slot, potentially resuming a waiting coroutine. */
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
				++Count;
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

	/** Awaiter returned by Acquire(). Suspends if count == 0. */
	struct FAcquireAwaiter
	{
		FAwaitableSemaphore& Semaphore;

		bool await_ready()
		{
			FScopeLock Lock(&Semaphore.CriticalSection);
			if (Semaphore.Count > 0)
			{
				--Semaphore.Count;
				return true;
			}
			return false;
		}

		bool await_suspend(std::coroutine_handle<> Handle)
		{
			FScopeLock Lock(&Semaphore.CriticalSection);
			if (Semaphore.Count > 0)
			{
				--Semaphore.Count;
				return false; // Acquired — don't suspend
			}
			Semaphore.Waiters.Add(Handle);
			return true;
		}

		void await_resume() const {}
	};

	/** Returns an awaiter that suspends until a slot is available. */
	[[nodiscard]] FAcquireAwaiter Acquire()
	{
		return FAcquireAwaiter{*this};
	}

private:
	FCriticalSection CriticalSection;
	TArray<std::coroutine_handle<>> Waiters;
	int32 Count = 0;
};

} // namespace AsyncFlow

