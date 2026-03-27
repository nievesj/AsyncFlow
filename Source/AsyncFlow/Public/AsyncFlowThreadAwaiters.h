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

// AsyncFlowThreadAwaiters.h — Background-thread / multithreading awaiters
//
// Offload heavy computation to worker threads and marshal resumption back to
// the game thread. The coroutine ALWAYS resumes on the game thread.
//
// SAFETY: UObject access, GC interaction, and world queries are FORBIDDEN
// inside background lambdas. Only pure computation should run off-thread.
// The API enforces this by accepting TFunction<T()> and returning T on the
// game thread — the "offload-and-return" pattern.
#pragma once

#include "AsyncFlowTask.h"
#include "AsyncFlowAwaiters.h"
#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"

#include <coroutine>
#include <atomic>

// UE::Tasks (UE 5.4+)
#if __has_include("Tasks/Task.h")
#include "Tasks/Task.h"
#define ASYNCFLOW_HAS_UE_TASKS 1
#else
#define ASYNCFLOW_HAS_UE_TASKS 0
#endif

namespace AsyncFlow
{

// ============================================================================
// Shared state for background awaiters
// ============================================================================

namespace Private
{

/**
 * Heap-allocated state shared between the awaiter (coroutine frame) and the
 * game-thread callback dispatched from a background thread. Both the awaiter
 * destructor and the callback run on the game thread, so no mutex is needed.
 */
template <typename T>
struct FBackgroundSharedState
{
	TOptional<T> Result;
	std::coroutine_handle<> Handle;
	bool bAlive = true;
};

template <>
struct FBackgroundSharedState<void>
{
	std::coroutine_handle<> Handle;
	bool bAlive = true;
};

} // namespace Private

// ============================================================================
// RunOnBackgroundThread — offloads a lambda to the thread pool
// ============================================================================

/**
 * Awaiter that dispatches Work to the thread pool, stores the result,
 * and resumes the coroutine on the game thread.
 *
 * WARNING: The Work lambda runs OFF the game thread. Do NOT access UObjects,
 * GC-managed memory, or world state inside Work.
 */
template <typename T>
struct FBackgroundTaskAwaiter
{
	TFunction<T()> Work;
	TSharedPtr<Private::FBackgroundSharedState<T>> State;

	explicit FBackgroundTaskAwaiter(TFunction<T()> InWork)
		: Work(MoveTemp(InWork))
		, State(MakeShared<Private::FBackgroundSharedState<T>>())
	{
	}

	~FBackgroundTaskAwaiter()
	{
		if (State)
		{
			State->bAlive = false;
		}
	}

	FBackgroundTaskAwaiter(FBackgroundTaskAwaiter&&) noexcept = default;
	FBackgroundTaskAwaiter& operator=(FBackgroundTaskAwaiter&&) noexcept = default;
	FBackgroundTaskAwaiter(const FBackgroundTaskAwaiter&) = delete;
	FBackgroundTaskAwaiter& operator=(const FBackgroundTaskAwaiter&) = delete;

	bool await_ready() const { return false; }

	void await_suspend(std::coroutine_handle<> InHandle)
	{
		State->Handle = InHandle;
		TSharedPtr<Private::FBackgroundSharedState<T>> SharedState = State;
		TFunction<T()> WorkCopy = MoveTemp(Work);

		Async(EAsyncExecution::ThreadPool, [SharedState, WorkCopy = MoveTemp(WorkCopy)]() mutable
		{
			T Computed = WorkCopy();
			AsyncTask(ENamedThreads::GameThread, [SharedState, Computed = MoveTemp(Computed)]() mutable
			{
				if (SharedState->bAlive)
				{
					SharedState->Result.Emplace(MoveTemp(Computed));
					SharedState->Handle.resume();
				}
			});
		});
	}

	T await_resume()
	{
		return MoveTemp(State->Result.GetValue());
	}
};

/** Void specialization — no result to marshal back. */
template <>
struct FBackgroundTaskAwaiter<void>
{
	TFunction<void()> Work;
	TSharedPtr<Private::FBackgroundSharedState<void>> State;

	explicit FBackgroundTaskAwaiter(TFunction<void()> InWork)
		: Work(MoveTemp(InWork))
		, State(MakeShared<Private::FBackgroundSharedState<void>>())
	{
	}

	~FBackgroundTaskAwaiter()
	{
		if (State)
		{
			State->bAlive = false;
		}
	}

	FBackgroundTaskAwaiter(FBackgroundTaskAwaiter&&) noexcept = default;
	FBackgroundTaskAwaiter& operator=(FBackgroundTaskAwaiter&&) noexcept = default;
	FBackgroundTaskAwaiter(const FBackgroundTaskAwaiter&) = delete;
	FBackgroundTaskAwaiter& operator=(const FBackgroundTaskAwaiter&) = delete;

	bool await_ready() const { return false; }

	void await_suspend(std::coroutine_handle<> InHandle)
	{
		State->Handle = InHandle;
		TSharedPtr<Private::FBackgroundSharedState<void>> SharedState = State;
		TFunction<void()> WorkCopy = MoveTemp(Work);

		Async(EAsyncExecution::ThreadPool, [SharedState, WorkCopy = MoveTemp(WorkCopy)]() mutable
		{
			WorkCopy();
			AsyncTask(ENamedThreads::GameThread, [SharedState]()
			{
				if (SharedState->bAlive)
				{
					SharedState->Handle.resume();
				}
			});
		});
	}

	void await_resume() {}
};

/**
 * Offload a lambda to the thread pool. The result is returned on the game
 * thread when the coroutine resumes.
 *
 * Usage:
 *   int32 Result = co_await AsyncFlow::RunOnBackgroundThread([](){ return HeavyCompute(); });
 *   co_await AsyncFlow::RunOnBackgroundThread([](){ ExpensiveWork(); });
 */
template <typename FuncType>
auto RunOnBackgroundThread(FuncType&& Work) -> FBackgroundTaskAwaiter<decltype(Work())>
{
	using ReturnType = decltype(Work());
	return FBackgroundTaskAwaiter<ReturnType>(TFunction<ReturnType()>(Forward<FuncType>(Work)));
}

// ============================================================================
// AwaitFuture — wraps TFuture<T> as a co_awaitable
// ============================================================================

/**
 * Awaiter that wraps an existing TFuture<T>. Blocks a thread-pool worker
 * until the future resolves, then resumes the coroutine on the game thread.
 *
 * TFuture::Then() thread affinity varies by UE version, so this awaiter
 * always marshals back via AsyncTask(GameThread) regardless.
 */
template <typename T>
struct FFutureAwaiter
{
	TFuture<T> Future;
	TSharedPtr<Private::FBackgroundSharedState<T>> State;

	explicit FFutureAwaiter(TFuture<T>&& InFuture)
		: Future(MoveTemp(InFuture))
		, State(MakeShared<Private::FBackgroundSharedState<T>>())
	{
	}

	~FFutureAwaiter()
	{
		if (State)
		{
			State->bAlive = false;
		}
	}

	FFutureAwaiter(FFutureAwaiter&&) noexcept = default;
	FFutureAwaiter& operator=(FFutureAwaiter&&) noexcept = default;
	FFutureAwaiter(const FFutureAwaiter&) = delete;
	FFutureAwaiter& operator=(const FFutureAwaiter&) = delete;

	bool await_ready()
	{
		if (Future.IsReady())
		{
			// Already resolved — grab the result synchronously
			State->Result.Emplace(Future.Get());
			return true;
		}
		return false;
	}

	void await_suspend(std::coroutine_handle<> InHandle)
	{
		State->Handle = InHandle;
		TSharedPtr<Private::FBackgroundSharedState<T>> SharedState = State;

		// Wait on a thread-pool worker and marshal back to game thread
		Async(EAsyncExecution::ThreadPool, [SharedState, Future = MoveTemp(Future)]() mutable
		{
			T Value = Future.Get();
			AsyncTask(ENamedThreads::GameThread, [SharedState, Value = MoveTemp(Value)]() mutable
			{
				if (SharedState->bAlive)
				{
					SharedState->Result.Emplace(MoveTemp(Value));
					SharedState->Handle.resume();
				}
			});
		});
	}

	T await_resume()
	{
		return MoveTemp(State->Result.GetValue());
	}
};

/** Void specialization for TFuture<void>. */
template <>
struct FFutureAwaiter<void>
{
	TFuture<void> Future;
	TSharedPtr<Private::FBackgroundSharedState<void>> State;

	explicit FFutureAwaiter(TFuture<void>&& InFuture)
		: Future(MoveTemp(InFuture))
		, State(MakeShared<Private::FBackgroundSharedState<void>>())
	{
	}

	~FFutureAwaiter()
	{
		if (State)
		{
			State->bAlive = false;
		}
	}

	FFutureAwaiter(FFutureAwaiter&&) noexcept = default;
	FFutureAwaiter& operator=(FFutureAwaiter&&) noexcept = default;
	FFutureAwaiter(const FFutureAwaiter&) = delete;
	FFutureAwaiter& operator=(const FFutureAwaiter&) = delete;

	bool await_ready()
	{
		if (Future.IsReady())
		{
			Future.Get();
			return true;
		}
		return false;
	}

	void await_suspend(std::coroutine_handle<> InHandle)
	{
		State->Handle = InHandle;
		TSharedPtr<Private::FBackgroundSharedState<void>> SharedState = State;

		Async(EAsyncExecution::ThreadPool, [SharedState, Future = MoveTemp(Future)]() mutable
		{
			Future.Get();
			AsyncTask(ENamedThreads::GameThread, [SharedState]()
			{
				if (SharedState->bAlive)
				{
					SharedState->Handle.resume();
				}
			});
		});
	}

	void await_resume() {}
};

/**
 * co_await a TFuture<T>. Resumes on the game thread when the future resolves.
 *
 * Usage:
 *   TFuture<FString> Future = SomeAsyncAPI();
 *   FString Result = co_await AsyncFlow::AwaitFuture(MoveTemp(Future));
 */
template <typename T>
[[nodiscard]] FFutureAwaiter<T> AwaitFuture(TFuture<T>&& Future)
{
	return FFutureAwaiter<T>(MoveTemp(Future));
}

// ============================================================================
// ParallelForAsync — runs ParallelFor off the game thread
// ============================================================================

/**
 * Offload a ParallelFor to a background thread. Each iteration calls
 * Body(Index). The coroutine resumes on the game thread when all iterations
 * are done.
 *
 * ParallelFor itself distributes work across multiple cores. Wrapping it
 * in RunOnBackgroundThread prevents it from blocking the game thread while
 * the worker threads execute.
 *
 * WARNING: Body runs OFF the game thread. Do NOT access UObjects inside Body.
 *
 * Usage:
 *   co_await AsyncFlow::ParallelForAsync(1000, [&Results](int32 Index)
 *   {
 *       Results[Index] = HeavyCompute(Index);
 *   });
 */
template <typename BodyType>
[[nodiscard]] FBackgroundTaskAwaiter<void> ParallelForAsync(int32 Num, BodyType&& Body)
{
	return RunOnBackgroundThread([Num, Body = Forward<BodyType>(Body)]()
	{
		ParallelFor(Num, [&Body](int32 Index)
		{
			Body(Index);
		});
	});
}

// ============================================================================
// AwaitUETask — wraps UE::Tasks::TTask<T> as co_awaitable (UE 5.4+)
// ============================================================================

#if ASYNCFLOW_HAS_UE_TASKS

/**
 * Awaiter for UE::Tasks::TTask<T>. Waits for the task on a thread-pool
 * worker and resumes the coroutine on the game thread.
 *
 * This is the recommended awaiter for the modern UE threading API.
 */
template <typename T>
struct FUETaskAwaiter
{
	UE::Tasks::TTask<T> Task;
	TSharedPtr<Private::FBackgroundSharedState<T>> State;

	explicit FUETaskAwaiter(UE::Tasks::TTask<T>&& InTask)
		: Task(MoveTemp(InTask))
		, State(MakeShared<Private::FBackgroundSharedState<T>>())
	{
	}

	~FUETaskAwaiter()
	{
		if (State)
		{
			State->bAlive = false;
		}
	}

	FUETaskAwaiter(FUETaskAwaiter&&) noexcept = default;
	FUETaskAwaiter& operator=(FUETaskAwaiter&&) noexcept = default;
	FUETaskAwaiter(const FUETaskAwaiter&) = delete;
	FUETaskAwaiter& operator=(const FUETaskAwaiter&) = delete;

	bool await_ready()
	{
		if (Task.IsCompleted())
		{
			State->Result.Emplace(Task.GetResult());
			return true;
		}
		return false;
	}

	void await_suspend(std::coroutine_handle<> InHandle)
	{
		State->Handle = InHandle;
		TSharedPtr<Private::FBackgroundSharedState<T>> SharedState = State;

		Async(EAsyncExecution::ThreadPool, [SharedState, Task = MoveTemp(Task)]() mutable
		{
			Task.Wait();
			T Value = Task.GetResult();
			AsyncTask(ENamedThreads::GameThread, [SharedState, Value = MoveTemp(Value)]() mutable
			{
				if (SharedState->bAlive)
				{
					SharedState->Result.Emplace(MoveTemp(Value));
					SharedState->Handle.resume();
				}
			});
		});
	}

	T await_resume()
	{
		return MoveTemp(State->Result.GetValue());
	}
};

/** Void specialization for UE::Tasks::FTask. */
template <>
struct FUETaskAwaiter<void>
{
	UE::Tasks::FTask Task;
	TSharedPtr<Private::FBackgroundSharedState<void>> State;

	explicit FUETaskAwaiter(UE::Tasks::FTask&& InTask)
		: Task(MoveTemp(InTask))
		, State(MakeShared<Private::FBackgroundSharedState<void>>())
	{
	}

	~FUETaskAwaiter()
	{
		if (State)
		{
			State->bAlive = false;
		}
	}

	FUETaskAwaiter(FUETaskAwaiter&&) noexcept = default;
	FUETaskAwaiter& operator=(FUETaskAwaiter&&) noexcept = default;
	FUETaskAwaiter(const FUETaskAwaiter&) = delete;
	FUETaskAwaiter& operator=(const FUETaskAwaiter&) = delete;

	bool await_ready()
	{
		return Task.IsCompleted();
	}

	void await_suspend(std::coroutine_handle<> InHandle)
	{
		State->Handle = InHandle;
		TSharedPtr<Private::FBackgroundSharedState<void>> SharedState = State;

		Async(EAsyncExecution::ThreadPool, [SharedState, Task = MoveTemp(Task)]() mutable
		{
			(void)Task.Wait();
			AsyncTask(ENamedThreads::GameThread, [SharedState]()
			{
				if (SharedState->bAlive)
				{
					SharedState->Handle.resume();
				}
			});
		});
	}

	void await_resume() {}
};

/**
 * co_await a UE::Tasks::TTask<T>. Resumes on the game thread when the task
 * completes.
 *
 * Usage:
 *   UE::Tasks::TTask<int32> Task = UE::Tasks::Launch(TEXT("Compute"), [](){ return 42; });
 *   int32 Result = co_await AsyncFlow::AwaitUETask(MoveTemp(Task));
 */
template <typename T>
[[nodiscard]] FUETaskAwaiter<T> AwaitUETask(UE::Tasks::TTask<T>&& Task)
{
	return FUETaskAwaiter<T>(MoveTemp(Task));
}

/** Overload for void tasks (UE::Tasks::FTask). */
[[nodiscard]] inline FUETaskAwaiter<void> AwaitUETask(UE::Tasks::FTask&& Task)
{
	return FUETaskAwaiter<void>(MoveTemp(Task));
}

#endif // ASYNCFLOW_HAS_UE_TASKS

// ============================================================================
// Thread-migration awaiters — move the coroutine body between threads
// ============================================================================

/**
 * Resume the coroutine on the game thread. Use after MoveToThread/MoveToTask
 * to safely access UObjects again.
 */
struct FMoveToGameThreadAwaiter
{
	Private::FAwaiterAliveFlag AliveFlag;

	bool await_ready() const { return IsInGameThread(); }

	void await_suspend(std::coroutine_handle<> Handle)
	{
		TSharedPtr<bool> Alive = AliveFlag.Get();
		AsyncTask(ENamedThreads::GameThread, [Handle, Alive]()
		{
			if (*Alive)
			{
				Handle.resume();
			}
		});
	}

	void await_resume() const {}
};

[[nodiscard]] inline FMoveToGameThreadAwaiter MoveToGameThread()
{
	return FMoveToGameThreadAwaiter{};
}

/**
 * Resume the coroutine on a named UE thread (e.g., ENamedThreads::AnyBackgroundThreadNormalTask).
 *
 * WARNING: UObject access is FORBIDDEN on non-game threads.
 */
struct FMoveToThreadAwaiter
{
	ENamedThreads::Type TargetThread;
	Private::FAwaiterAliveFlag AliveFlag;

	bool await_ready() const { return false; }

	void await_suspend(std::coroutine_handle<> Handle)
	{
		TSharedPtr<bool> Alive = AliveFlag.Get();
		AsyncTask(TargetThread, [Handle, Alive]()
		{
			if (*Alive)
			{
				Handle.resume();
			}
		});
	}

	void await_resume() const {}
};

[[nodiscard]] inline FMoveToThreadAwaiter MoveToThread(ENamedThreads::Type Thread)
{
	return FMoveToThreadAwaiter{Thread};
}

/**
 * Resume the coroutine on a UE::Tasks worker thread.
 * Efficient for sustained background computation.
 *
 * WARNING: UObject access is FORBIDDEN on task threads.
 */
struct FMoveToTaskAwaiter
{
	Private::FAwaiterAliveFlag AliveFlag;

	bool await_ready() const { return false; }

	void await_suspend(std::coroutine_handle<> Handle)
	{
		TSharedPtr<bool> Alive = AliveFlag.Get();
		Async(EAsyncExecution::ThreadPool, [Handle, Alive]()
		{
			if (*Alive)
			{
				Handle.resume();
			}
		});
	}

	void await_resume() const {}
};

[[nodiscard]] inline FMoveToTaskAwaiter MoveToTask()
{
	return FMoveToTaskAwaiter{};
}

/**
 * Resume the coroutine on a brand-new dedicated thread.
 * Suitable for blocking I/O or long-running operations.
 *
 * WARNING: UObject access is FORBIDDEN on the new thread.
 */
struct FMoveToNewThreadAwaiter
{
	Private::FAwaiterAliveFlag AliveFlag;

	bool await_ready() const { return false; }

	void await_suspend(std::coroutine_handle<> Handle)
	{
		TSharedPtr<bool> Alive = AliveFlag.Get();
		Async(EAsyncExecution::Thread, [Handle, Alive]()
		{
			if (*Alive)
			{
				Handle.resume();
			}
		});
	}

	void await_resume() const {}
};

[[nodiscard]] inline FMoveToNewThreadAwaiter MoveToNewThread()
{
	return FMoveToNewThreadAwaiter{};
}

// ============================================================================
// Yield — suspend and resume on the game thread next opportunity
// ============================================================================

/**
 * Yield the coroutine to the game thread scheduler. Does not require a world context.
 * If already on the game thread, schedules resumption via AsyncTask.
 */
struct FYieldAwaiter
{
	Private::FAwaiterAliveFlag AliveFlag;

	bool await_ready() const { return false; }

	void await_suspend(std::coroutine_handle<> Handle)
	{
		TSharedPtr<bool> Alive = AliveFlag.Get();
		AsyncTask(ENamedThreads::GameThread, [Handle, Alive]()
		{
			if (*Alive)
			{
				Handle.resume();
			}
		});
	}

	void await_resume() const {}
};

[[nodiscard]] inline FYieldAwaiter Yield()
{
	return FYieldAwaiter{};
}

// ============================================================================
// PlatformSeconds — free-threaded delay using FPlatformProcess::Sleep
// ============================================================================

/**
 * Sleep for the specified duration on a worker thread, then resume on the game thread.
 * Does not require a world context or tick subsystem.
 */
struct FPlatformSecondsAwaiter
{
	double Seconds;
	Private::FAwaiterAliveFlag AliveFlag;

	bool await_ready() const { return Seconds <= 0.0; }

	void await_suspend(std::coroutine_handle<> Handle)
	{
		const double SleepDuration = Seconds;
		TSharedPtr<bool> Alive = AliveFlag.Get();
		Async(EAsyncExecution::ThreadPool, [Handle, SleepDuration, Alive]()
		{
			FPlatformProcess::Sleep(static_cast<float>(SleepDuration));
			AsyncTask(ENamedThreads::GameThread, [Handle, Alive]()
			{
				if (*Alive)
				{
					Handle.resume();
				}
			});
		});
	}

	void await_resume() const {}
};

/** Free-threaded delay. Sleeps on a worker, resumes on the game thread. No world context required. */
[[nodiscard]] inline FPlatformSecondsAwaiter PlatformSeconds(double InSeconds)
{
	return FPlatformSecondsAwaiter{InSeconds};
}

} // namespace AsyncFlow



