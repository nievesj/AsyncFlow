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

// AsyncFlowAwaiters.h — Core timing and flow control awaiters
//
// All awaiters in this file are game-thread-only and require a world context
// (UObject*) to access the UAsyncFlowTickSubsystem for scheduling. If the
// world or subsystem is unavailable, the awaiter resumes immediately to
// prevent a permanent hang.
#pragma once

#include "AsyncFlowTask.h"
#include "AsyncFlowTickSubsystem.h"
#include "AsyncFlowLogging.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"
#include "Templates/Tuple.h"

#include <coroutine>
#include <atomic>
#include <tuple>

namespace AsyncFlow
{

// ============================================================================
// Internal helper — creates the shared bAlive flag lazily in await_suspend.
// ============================================================================

namespace Private
{

/**
 * RAII flag shared between an awaiter and its tick-subsystem entry.
 *
 * When the awaiter is destroyed (coroutine frame teardown), *Flag is set
 * to false. The subsystem reads it before calling Handle.resume() — if
 * false, the pending entry is discarded instead of resuming a dead frame.
 *
 * Created lazily on first call to Get(). Null means "no tracking" (legacy path).
 * Move-only because the flag ownership must be unique.
 */
struct FAwaiterAliveFlag
{
	mutable TSharedPtr<bool> Flag;

	FAwaiterAliveFlag() = default;
	FAwaiterAliveFlag(FAwaiterAliveFlag&&) noexcept = default;
	FAwaiterAliveFlag& operator=(FAwaiterAliveFlag&&) noexcept = default;
	FAwaiterAliveFlag(const FAwaiterAliveFlag&) = delete;
	FAwaiterAliveFlag& operator=(const FAwaiterAliveFlag&) = delete;

	~FAwaiterAliveFlag()
	{
		if (Flag)
		{
			*Flag = false;
		}
	}

	/** Returns the flag, creating it on first call. Safe to call from const await_suspend. */
	TSharedPtr<bool> Get() const
	{
		if (!Flag)
		{
			Flag = MakeShared<bool>(true);
		}
		return Flag;
	}
};

} // namespace Private

// ============================================================================
// Delay — suspends for N seconds using game-dilated time
// ============================================================================

/**
 * Awaiter struct for game-time delays. Respects global time dilation and pause.
 * Resumes immediately if Seconds <= 0 or the world context is null.
 */
struct FDelayAwaiter
{
	UObject* WorldContext = nullptr;
	float Seconds = 0.0f;
	Private::FAwaiterAliveFlag AliveFlag;

	FDelayAwaiter() = default;
	FDelayAwaiter(FDelayAwaiter&&) noexcept = default;
	FDelayAwaiter& operator=(FDelayAwaiter&&) noexcept = default;
	FDelayAwaiter(const FDelayAwaiter&) = delete;
	FDelayAwaiter& operator=(const FDelayAwaiter&) = delete;

	bool await_ready() const { return Seconds <= 0.0f; }

	void await_suspend(std::coroutine_handle<> Handle) const
	{
		if (!WorldContext || !WorldContext->GetWorld())
		{
			UE_LOG(LogAsyncFlow, Warning, TEXT("Delay: null WorldContext or World — resuming immediately"));
			Handle.resume();
			return;
		}
		UAsyncFlowTickSubsystem* Subsystem = WorldContext->GetWorld()->GetSubsystem<UAsyncFlowTickSubsystem>();
		if (Subsystem)
		{
			Subsystem->ScheduleDelay(Handle, Seconds, AliveFlag.Get());
		}
		else
		{
			Handle.resume();
		}
	}

	void await_resume() const {}
};

/**
 * Suspend for Seconds of dilated game time.
 *
 * @param WorldContext  Any UObject with a valid GetWorld() (actor, component, etc.).
 * @param Seconds      Duration in game-dilated seconds. Values <= 0 resume immediately.
 * @return             An awaiter — use with co_await.
 */
[[nodiscard]] inline FDelayAwaiter Delay(UObject* WorldContext, float Seconds)
{
	FDelayAwaiter Aw;
	Aw.WorldContext = WorldContext;
	Aw.Seconds = Seconds;
	return Aw;
}

// ============================================================================
// RealDelay — suspends for N seconds using wall-clock time
// ============================================================================

/**
 * Awaiter struct for wall-clock delays. Ignores pause and time dilation.
 * Useful for UI animations or meta-game timers that should not freeze with the game.
 */
struct FRealDelayAwaiter
{
	UObject* WorldContext = nullptr;
	float Seconds = 0.0f;
	Private::FAwaiterAliveFlag AliveFlag;

	FRealDelayAwaiter() = default;
	FRealDelayAwaiter(FRealDelayAwaiter&&) noexcept = default;
	FRealDelayAwaiter& operator=(FRealDelayAwaiter&&) noexcept = default;
	FRealDelayAwaiter(const FRealDelayAwaiter&) = delete;
	FRealDelayAwaiter& operator=(const FRealDelayAwaiter&) = delete;

	bool await_ready() const { return Seconds <= 0.0f; }

	void await_suspend(std::coroutine_handle<> Handle) const
	{
		if (!WorldContext || !WorldContext->GetWorld())
		{
			UE_LOG(LogAsyncFlow, Warning, TEXT("RealDelay: null WorldContext or World — resuming immediately"));
			Handle.resume();
			return;
		}
		UAsyncFlowTickSubsystem* Subsystem = WorldContext->GetWorld()->GetSubsystem<UAsyncFlowTickSubsystem>();
		if (Subsystem)
		{
			Subsystem->ScheduleRealDelay(Handle, Seconds, AliveFlag.Get());
		}
		else
		{
			Handle.resume();
		}
	}

	void await_resume() const {}
};

/**
 * Suspend for Seconds of real wall-clock time. Ignores pause and dilation.
 *
 * @param WorldContext  Any UObject with a valid GetWorld().
 * @param Seconds      Duration in real seconds.
 * @return             An awaiter — use with co_await.
 */
[[nodiscard]] inline FRealDelayAwaiter RealDelay(UObject* WorldContext, float Seconds)
{
	FRealDelayAwaiter Aw;
	Aw.WorldContext = WorldContext;
	Aw.Seconds = Seconds;
	return Aw;
}

// ============================================================================
// NextTick — suspends until the next frame
// ============================================================================

/**
 * Awaiter struct that yields for exactly one tick. Always suspends
 * (await_ready returns false). Equivalent to Ticks(WorldContext, 1).
 */
struct FNextTickAwaiter
{
	UObject* WorldContext = nullptr;
	Private::FAwaiterAliveFlag AliveFlag;

	FNextTickAwaiter() = default;
	FNextTickAwaiter(FNextTickAwaiter&&) noexcept = default;
	FNextTickAwaiter& operator=(FNextTickAwaiter&&) noexcept = default;
	FNextTickAwaiter(const FNextTickAwaiter&) = delete;
	FNextTickAwaiter& operator=(const FNextTickAwaiter&) = delete;

	bool await_ready() const { return false; }

	void await_suspend(std::coroutine_handle<> Handle) const
	{
		if (!WorldContext || !WorldContext->GetWorld())
		{
			UE_LOG(LogAsyncFlow, Warning, TEXT("NextTick: null WorldContext or World — resuming immediately"));
			Handle.resume();
			return;
		}
		UAsyncFlowTickSubsystem* Subsystem = WorldContext->GetWorld()->GetSubsystem<UAsyncFlowTickSubsystem>();
		if (Subsystem)
		{
			Subsystem->ScheduleTicks(Handle, 1, AliveFlag.Get());
		}
		else
		{
			Handle.resume();
		}
	}

	void await_resume() const {}
};

/**
 * Suspend until the next game tick.
 *
 * @param WorldContext  Any UObject with a valid GetWorld().
 * @return             An awaiter — use with co_await.
 */
[[nodiscard]] inline FNextTickAwaiter NextTick(UObject* WorldContext)
{
	FNextTickAwaiter Aw;
	Aw.WorldContext = WorldContext;
	return Aw;
}

// ============================================================================
// Ticks — suspends for N frames
// ============================================================================

/**
 * Awaiter struct that yields for a specified number of ticks.
 * Resumes immediately if NumTicks <= 0.
 */
struct FTicksAwaiter
{
	UObject* WorldContext = nullptr;
	int32 NumTicks = 0;
	Private::FAwaiterAliveFlag AliveFlag;

	FTicksAwaiter() = default;
	FTicksAwaiter(FTicksAwaiter&&) noexcept = default;
	FTicksAwaiter& operator=(FTicksAwaiter&&) noexcept = default;
	FTicksAwaiter(const FTicksAwaiter&) = delete;
	FTicksAwaiter& operator=(const FTicksAwaiter&) = delete;

	bool await_ready() const { return NumTicks <= 0; }

	void await_suspend(std::coroutine_handle<> Handle) const
	{
		if (!WorldContext || !WorldContext->GetWorld())
		{
			UE_LOG(LogAsyncFlow, Warning, TEXT("Ticks: null WorldContext or World — resuming immediately"));
			Handle.resume();
			return;
		}
		UAsyncFlowTickSubsystem* Subsystem = WorldContext->GetWorld()->GetSubsystem<UAsyncFlowTickSubsystem>();
		if (Subsystem)
		{
			Subsystem->ScheduleTicks(Handle, NumTicks, AliveFlag.Get());
		}
		else
		{
			Handle.resume();
		}
	}

	void await_resume() const {}
};

/**
 * Suspend for InNumTicks game ticks.
 *
 * @param WorldContext  Any UObject with a valid GetWorld().
 * @param InNumTicks   Number of ticks to wait. Clamped to at least 1 in the subsystem.
 * @return             An awaiter — use with co_await.
 */
[[nodiscard]] inline FTicksAwaiter Ticks(UObject* WorldContext, int32 InNumTicks)
{
	FTicksAwaiter Aw;
	Aw.WorldContext = WorldContext;
	Aw.NumTicks = InNumTicks;
	return Aw;
}

// ============================================================================
// WaitForCondition — polls predicate each tick, resumes when true
// ============================================================================

/**
 * Awaiter struct that checks a predicate every tick and resumes
 * the coroutine when the predicate returns true. If the predicate
 * is already true at the point of co_await, no suspension occurs.
 *
 * The Context UObject is held via TWeakObjectPtr — if it is GC'd,
 * the condition entry is silently removed from the subsystem.
 */
struct FConditionAwaiter
{
	UObject* Context = nullptr;
	TFunction<bool()> Predicate;
	Private::FAwaiterAliveFlag AliveFlag;

	FConditionAwaiter() = default;
	FConditionAwaiter(FConditionAwaiter&&) noexcept = default;
	FConditionAwaiter& operator=(FConditionAwaiter&&) noexcept = default;
	FConditionAwaiter(const FConditionAwaiter&) = delete;
	FConditionAwaiter& operator=(const FConditionAwaiter&) = delete;

	bool await_ready() const
	{
		return Predicate && Predicate();
	}

	void await_suspend(std::coroutine_handle<> Handle) const
	{
		if (!Context || !Context->GetWorld())
		{
			UE_LOG(LogAsyncFlow, Warning, TEXT("WaitForCondition: null Context or World — resuming immediately"));
			Handle.resume();
			return;
		}
		UAsyncFlowTickSubsystem* Subsystem = Context->GetWorld()->GetSubsystem<UAsyncFlowTickSubsystem>();
		if (Subsystem)
		{
			Subsystem->ScheduleCondition(Handle, Context, Predicate, AliveFlag.Get());
		}
		else
		{
			Handle.resume();
		}
	}

	void await_resume() const {}
};

/**
 * Suspend until InPredicate returns true. Evaluated once per tick.
 *
 * @param InContext     UObject for world access and lifetime tracking.
 * @param InPredicate  Callable returning bool. Must be safe to call from the game thread.
 * @return             An awaiter — use with co_await.
 */
[[nodiscard]] inline FConditionAwaiter WaitForCondition(UObject* InContext, TFunction<bool()> InPredicate)
{
	FConditionAwaiter Aw;
	Aw.Context = InContext;
	Aw.Predicate = MoveTemp(InPredicate);
	return Aw;
}

// ============================================================================
// WhenAll — waits for all tasks to complete
// ============================================================================

namespace Private
{

/**
 * Shared state for WhenAll. Tracks remaining task count and the
 * parent coroutine's continuation handle. When Remaining hits 0,
 * the continuation is resumed.
 */
struct FWhenAllState
{
	std::atomic<int32> Remaining;
	std::coroutine_handle<> Continuation;

	explicit FWhenAllState(int32 Count)
		: Remaining(Count)
		, Continuation(nullptr)
	{
	}
};

} // namespace Private

/**
 * Awaiter struct for WhenAll. Suspends until all tracked tasks complete.
 * If all tasks are already done, await_ready returns true.
 */
struct FWhenAllAwaiter
{
	TSharedPtr<Private::FWhenAllState> State;

	bool await_ready() const
	{
		return State->Remaining.load(std::memory_order_acquire) <= 0;
	}

	bool await_suspend(std::coroutine_handle<> Handle)
	{
		State->Continuation = Handle;
		// If all already completed before we suspended, don't suspend
		return State->Remaining.load(std::memory_order_acquire) > 0;
	}

	void await_resume() const {}
};

/**
 * Wait until all provided tasks complete. Each task is Start()-ed automatically.
 *
 * @param Tasks  Variadic pack of TTask references.
 * @return       An awaiter — use with co_await.
 *
 * @warning Each task's OnComplete slot is consumed. Do not register
 *          your own OnComplete before passing tasks to WhenAll.
 */
template <typename... TaskTypes>
FWhenAllAwaiter WhenAll(TaskTypes&... Tasks)
{
	constexpr int32 Count = sizeof...(TaskTypes);
	TSharedPtr<Private::FWhenAllState> State = MakeShared<Private::FWhenAllState>(Count);

	auto SetupTask = [&State](auto& Task)
	{
		Task.OnComplete([State]()
		{
			const int32 Prev = State->Remaining.fetch_sub(1, std::memory_order_acq_rel);
			if (Prev == 1 && State->Continuation && !State->Continuation.done())
			{
				State->Continuation.resume();
			}
		});
		Task.Start();
	};

	(SetupTask(Tasks), ...);

	return FWhenAllAwaiter{State};
}

// ============================================================================
// WhenAny — waits for the first task to complete, returns winner index
// ============================================================================

namespace Private
{

/**
 * Shared state for WhenAny and Race. Tracks which task completed first
 * via an atomic compare-exchange on bTriggered/WinnerIndex.
 */
struct FWhenAnyState
{
	std::atomic<bool> bTriggered{false};
	std::atomic<int32> WinnerIndex{-1};
	std::coroutine_handle<> Continuation;
};

} // namespace Private

/**
 * Awaiter struct for WhenAny. Suspends until one task completes.
 * await_resume() returns the 0-based index of the winner.
 */
struct FWhenAnyAwaiter
{
	TSharedPtr<Private::FWhenAnyState> State;

	bool await_ready() const
	{
		return State->bTriggered.load(std::memory_order_acquire);
	}

	bool await_suspend(std::coroutine_handle<> Handle)
	{
		State->Continuation = Handle;
		return !State->bTriggered.load(std::memory_order_acquire);
	}

	int32 await_resume() const
	{
		return State->WinnerIndex.load(std::memory_order_acquire);
	}
};

/**
 * Wait until any one of the provided tasks completes. Tasks are Start()-ed automatically.
 *
 * @param Tasks  Variadic pack of TTask references.
 * @return       An awaiter — co_await yields the 0-based index of the first task to finish.
 *
 * @warning Non-winning tasks continue running. Use Race() to auto-cancel losers.
 */
template <typename... TaskTypes>
FWhenAnyAwaiter WhenAny(TaskTypes&... Tasks)
{
	TSharedPtr<Private::FWhenAnyState> State = MakeShared<Private::FWhenAnyState>();

	int32 Index = 0;
	auto SetupTask = [&State, &Index](auto& Task)
	{
		const int32 MyIndex = Index++;
		Task.OnComplete([State, MyIndex]()
		{
			bool bExpected = false;
			if (State->bTriggered.compare_exchange_strong(bExpected, true, std::memory_order_acq_rel))
			{
				State->WinnerIndex.store(MyIndex, std::memory_order_release);
				if (State->Continuation && !State->Continuation.done())
				{
					State->Continuation.resume();
				}
			}
		});
		Task.Start();
	};

	(SetupTask(Tasks), ...);

	return FWhenAnyAwaiter{State};
}

// ============================================================================
// Race — like WhenAny but cancels all other tasks when the first completes
// ============================================================================

/**
 * Awaiter struct for Race. Same as WhenAny but auto-cancels all
 * non-winning tasks when the first one completes.
 */
struct FRaceAwaiter
{
	TSharedPtr<Private::FWhenAnyState> State;
	TSharedPtr<TArray<TFunction<void()>>> CancelFunctions;

	bool await_ready() const
	{
		return State->bTriggered.load(std::memory_order_acquire);
	}

	bool await_suspend(std::coroutine_handle<> Handle)
	{
		State->Continuation = Handle;
		if (State->bTriggered.load(std::memory_order_acquire))
		{
			// Cancel losers before resuming
			const int32 Winner = State->WinnerIndex.load(std::memory_order_acquire);
			for (int32 Idx = 0; Idx < CancelFunctions->Num(); ++Idx)
			{
				if (Idx != Winner)
				{
					(*CancelFunctions)[Idx]();
				}
			}
			return false; // Don't suspend — already have a winner
		}
		return true;
	}

	int32 await_resume() const
	{
		return State->WinnerIndex.load(std::memory_order_acquire);
	}
};

/**
 * Race: first task to complete wins; all losers are cancelled.
 * Tasks are Start()-ed automatically.
 *
 * @param Tasks  Variadic pack of TTask references.
 * @return       An awaiter — co_await yields the 0-based index of the winner.
 */
template <typename... TaskTypes>
FRaceAwaiter Race(TaskTypes&... Tasks)
{
	TSharedPtr<Private::FWhenAnyState> State = MakeShared<Private::FWhenAnyState>();
	TSharedPtr<TArray<TFunction<void()>>> CancelFunctions = MakeShared<TArray<TFunction<void()>>>();
	CancelFunctions->Reserve(sizeof...(TaskTypes));

	int32 Index = 0;
	auto SetupTask = [&State, &Index, &CancelFunctions](auto& Task)
	{
		const int32 MyIndex = Index++;
		TSharedPtr<FAsyncFlowState> FlowState = Task.GetFlowState();
		CancelFunctions->Add([FlowState]() { if (FlowState) { FlowState->Cancel(); } });

		Task.OnComplete([State, MyIndex, CancelFunctions]()
		{
			bool bExpected = false;
			if (State->bTriggered.compare_exchange_strong(bExpected, true, std::memory_order_acq_rel))
			{
				State->WinnerIndex.store(MyIndex, std::memory_order_release);
				// Cancel losers
				for (int32 Idx = 0; Idx < CancelFunctions->Num(); ++Idx)
				{
					if (Idx != MyIndex)
					{
						(*CancelFunctions)[Idx]();
					}
				}
				if (State->Continuation && !State->Continuation.done())
				{
					State->Continuation.resume();
				}
			}
		});
		Task.Start();
	};

	(SetupTask(Tasks), ...);

	return FRaceAwaiter{State, CancelFunctions};
}

// ============================================================================
// TArray overloads for WhenAll, WhenAny, Race
// ============================================================================

/**
 * Wait until all tasks in the array complete. Tasks are Start()-ed automatically.
 * Null entries in the array are counted as immediately complete.
 *
 * @param Tasks  Array of pointers to TTask<void>. Null entries are skipped.
 * @return       An awaiter — use with co_await.
 */
inline FWhenAllAwaiter WhenAll(TArray<TTask<void>*>& Tasks)
{
	const int32 Count = Tasks.Num();
	TSharedPtr<Private::FWhenAllState> State = MakeShared<Private::FWhenAllState>(Count);

	for (TTask<void>* Task : Tasks)
	{
		if (Task)
		{
			Task->OnComplete([State]()
			{
				const int32 Prev = State->Remaining.fetch_sub(1, std::memory_order_acq_rel);
				if (Prev == 1 && State->Continuation && !State->Continuation.done())
				{
					State->Continuation.resume();
				}
			});
			Task->Start();
		}
		else
		{
			const int32 Prev = State->Remaining.fetch_sub(1, std::memory_order_acq_rel);
			if (Prev == 1 && State->Continuation && !State->Continuation.done())
			{
				State->Continuation.resume();
			}
		}
	}

	return FWhenAllAwaiter{State};
}

/**
 * Wait until any task in the array completes.
 *
 * @param Tasks  Array of pointers to TTask<void>. Null entries are skipped.
 * @return       An awaiter — co_await yields the 0-based winner index.
 */
inline FWhenAnyAwaiter WhenAny(TArray<TTask<void>*>& Tasks)
{
	TSharedPtr<Private::FWhenAnyState> State = MakeShared<Private::FWhenAnyState>();

	for (int32 Idx = 0; Idx < Tasks.Num(); ++Idx)
	{
		TTask<void>* Task = Tasks[Idx];
		if (!Task)
		{
			continue;
		}
		const int32 MyIndex = Idx;
		Task->OnComplete([State, MyIndex]()
		{
			bool bExpected = false;
			if (State->bTriggered.compare_exchange_strong(bExpected, true, std::memory_order_acq_rel))
			{
				State->WinnerIndex.store(MyIndex, std::memory_order_release);
				if (State->Continuation && !State->Continuation.done())
				{
					State->Continuation.resume();
				}
			}
		});
		Task->Start();
	}

	return FWhenAnyAwaiter{State};
}

/**
 * Race with a TArray. First to complete wins; all others cancelled.
 *
 * @param Tasks  Array of pointers to TTask<void>. Null entries get a no-op cancel function.
 * @return       An awaiter — co_await yields the 0-based winner index.
 */
inline FRaceAwaiter Race(TArray<TTask<void>*>& Tasks)
{
	TSharedPtr<Private::FWhenAnyState> State = MakeShared<Private::FWhenAnyState>();
	TSharedPtr<TArray<TFunction<void()>>> CancelFunctions = MakeShared<TArray<TFunction<void()>>>();
	CancelFunctions->Reserve(Tasks.Num());

	for (int32 Idx = 0; Idx < Tasks.Num(); ++Idx)
	{
		TTask<void>* Task = Tasks[Idx];
		if (Task)
		{
			TSharedPtr<FAsyncFlowState> FlowState = Task->GetFlowState();
			CancelFunctions->Add([FlowState]() { if (FlowState) { FlowState->Cancel(); } });
		}
		else
		{
			CancelFunctions->Add([]() {});
		}
	}

	for (int32 Idx = 0; Idx < Tasks.Num(); ++Idx)
	{
		TTask<void>* Task = Tasks[Idx];
		if (!Task)
		{
			continue;
		}
		const int32 MyIndex = Idx;
		Task->OnComplete([State, MyIndex, CancelFunctions]()
		{
			bool bExpected = false;
			if (State->bTriggered.compare_exchange_strong(bExpected, true, std::memory_order_acq_rel))
			{
				State->WinnerIndex.store(MyIndex, std::memory_order_release);
				for (int32 CIdx = 0; CIdx < CancelFunctions->Num(); ++CIdx)
				{
					if (CIdx != MyIndex)
					{
						(*CancelFunctions)[CIdx]();
					}
				}
				if (State->Continuation && !State->Continuation.done())
				{
					State->Continuation.resume();
				}
			}
		});
		Task->Start();
	}

	return FRaceAwaiter{State, CancelFunctions};
}

// ============================================================================
// UnpausedDelay — suspends using unpaused time (continues during pause)
// ============================================================================

/**
 * Awaiter struct for unpaused-time delays. Ticks even while the game is paused.
 * Uses UWorld::GetUnpausedTimeSeconds() as its time source.
 */
struct FUnpausedDelayAwaiter
{
	UObject* WorldContext = nullptr;
	float Seconds = 0.0f;
	Private::FAwaiterAliveFlag AliveFlag;

	FUnpausedDelayAwaiter() = default;
	FUnpausedDelayAwaiter(FUnpausedDelayAwaiter&&) noexcept = default;
	FUnpausedDelayAwaiter& operator=(FUnpausedDelayAwaiter&&) noexcept = default;
	FUnpausedDelayAwaiter(const FUnpausedDelayAwaiter&) = delete;
	FUnpausedDelayAwaiter& operator=(const FUnpausedDelayAwaiter&) = delete;

	bool await_ready() const { return Seconds <= 0.0f; }

	void await_suspend(std::coroutine_handle<> Handle) const
	{
		if (!WorldContext || !WorldContext->GetWorld())
		{
			UE_LOG(LogAsyncFlow, Warning, TEXT("UnpausedDelay: null WorldContext or World — resuming immediately"));
			Handle.resume();
			return;
		}
		UAsyncFlowTickSubsystem* Subsystem = WorldContext->GetWorld()->GetSubsystem<UAsyncFlowTickSubsystem>();
		if (Subsystem)
		{
			Subsystem->ScheduleUnpausedDelay(Handle, Seconds, AliveFlag.Get());
		}
		else
		{
			Handle.resume();
		}
	}

	void await_resume() const {}
};

/**
 * Suspend for Seconds using unpaused time (ticks during pause).
 *
 * @param WorldContext  Any UObject with a valid GetWorld().
 * @param Seconds      Duration in unpaused seconds.
 * @return             An awaiter — use with co_await.
 */
[[nodiscard]] inline FUnpausedDelayAwaiter UnpausedDelay(UObject* WorldContext, float Seconds)
{
	FUnpausedDelayAwaiter Aw;
	Aw.WorldContext = WorldContext;
	Aw.Seconds = Seconds;
	return Aw;
}

// ============================================================================
// AudioDelay — suspends using audio time
// ============================================================================

/**
 * Awaiter struct for audio-time delays. Uses UWorld::GetAudioTimeSeconds().
 * Audio time may drift from game time depending on engine audio settings.
 */
struct FAudioDelayAwaiter
{
	UObject* WorldContext = nullptr;
	float Seconds = 0.0f;
	Private::FAwaiterAliveFlag AliveFlag;

	FAudioDelayAwaiter() = default;
	FAudioDelayAwaiter(FAudioDelayAwaiter&&) noexcept = default;
	FAudioDelayAwaiter& operator=(FAudioDelayAwaiter&&) noexcept = default;
	FAudioDelayAwaiter(const FAudioDelayAwaiter&) = delete;
	FAudioDelayAwaiter& operator=(const FAudioDelayAwaiter&) = delete;

	bool await_ready() const { return Seconds <= 0.0f; }

	void await_suspend(std::coroutine_handle<> Handle) const
	{
		if (!WorldContext || !WorldContext->GetWorld())
		{
			UE_LOG(LogAsyncFlow, Warning, TEXT("AudioDelay: null WorldContext or World — resuming immediately"));
			Handle.resume();
			return;
		}
		UAsyncFlowTickSubsystem* Subsystem = WorldContext->GetWorld()->GetSubsystem<UAsyncFlowTickSubsystem>();
		if (Subsystem)
		{
			Subsystem->ScheduleAudioDelay(Handle, Seconds, AliveFlag.Get());
		}
		else
		{
			Handle.resume();
		}
	}

	void await_resume() const {}
};

/**
 * Suspend for Seconds using audio time.
 *
 * @param WorldContext  Any UObject with a valid GetWorld().
 * @param Seconds      Duration in audio-clock seconds.
 * @return             An awaiter — use with co_await.
 */
[[nodiscard]] inline FAudioDelayAwaiter AudioDelay(UObject* WorldContext, float Seconds)
{
	FAudioDelayAwaiter Aw;
	Aw.WorldContext = WorldContext;
	Aw.Seconds = Seconds;
	return Aw;
}

// ============================================================================
// SecondsForActor — suspends using per-actor custom time dilation
// ============================================================================

/**
 * Awaiter struct for actor-dilated delays. The delay is scaled by the
 * actor's CustomTimeDilation each tick: a dilation of 2.0 halves the
 * real wait time, 0.5 doubles it.
 *
 * If the actor is destroyed mid-wait, the entry is discarded.
 */
struct FActorDilatedDelayAwaiter
{
	AActor* Actor = nullptr;
	float Seconds = 0.0f;
	Private::FAwaiterAliveFlag AliveFlag;

	FActorDilatedDelayAwaiter() = default;
	FActorDilatedDelayAwaiter(FActorDilatedDelayAwaiter&&) noexcept = default;
	FActorDilatedDelayAwaiter& operator=(FActorDilatedDelayAwaiter&&) noexcept = default;
	FActorDilatedDelayAwaiter(const FActorDilatedDelayAwaiter&) = delete;
	FActorDilatedDelayAwaiter& operator=(const FActorDilatedDelayAwaiter&) = delete;

	bool await_ready() const { return Seconds <= 0.0f || !Actor; }

	void await_suspend(std::coroutine_handle<> Handle) const
	{
		if (!Actor || !Actor->GetWorld())
		{
			UE_LOG(LogAsyncFlow, Warning, TEXT("SecondsForActor: null Actor or World — resuming immediately"));
			Handle.resume();
			return;
		}
		UAsyncFlowTickSubsystem* Subsystem = Actor->GetWorld()->GetSubsystem<UAsyncFlowTickSubsystem>();
		if (Subsystem)
		{
			Subsystem->ScheduleActorDilatedDelay(Handle, Actor, Seconds, AliveFlag.Get());
		}
		else
		{
			Handle.resume();
		}
	}

	void await_resume() const {}
};

/**
 * Suspend for Seconds scaled by an actor's CustomTimeDilation.
 *
 * @param InActor  The actor whose CustomTimeDilation scales the delay.
 * @param Seconds  Base duration before dilation is applied.
 * @return         An awaiter — use with co_await.
 */
[[nodiscard]] inline FActorDilatedDelayAwaiter SecondsForActor(AActor* InActor, float Seconds)
{
	FActorDilatedDelayAwaiter Aw;
	Aw.Actor = InActor;
	Aw.Seconds = Seconds;
	return Aw;
}

// ============================================================================
// FTickTimeBudget — time-sliced processing within a tick budget
// ============================================================================

/**
 * Awaiter for time-sliced loop processing. Checks wall-clock time at each
 * co_await point: if the elapsed time since the tick started is still within
 * the budget, await_ready returns true and no suspension occurs. When the
 * budget runs out, the coroutine yields to the next tick.
 *
 * Use this inside tight loops that process large datasets to avoid frame spikes.
 *
 * Usage:
 *   auto Budget = AsyncFlow::FTickTimeBudget::Milliseconds(WorldContext, 2.0);
 *   for (auto& Item : BigArray)
 *   {
 *       ProcessItem(Item);
 *       co_await Budget;
 *   }
 */
struct FTickTimeBudget
{
	/** World context for accessing the tick subsystem when yielding. */
	UObject* WorldContext = nullptr;

	/** Maximum wall-clock seconds allowed per tick before yielding. */
	double BudgetSeconds = 0.001;

	/** Wall-clock timestamp when the current tick's budget window started. */
	double TickStartTime = 0.0;

	/** False until the first co_await, when the start time is captured. */
	bool bInitialized = false;

	Private::FAwaiterAliveFlag AliveFlag;

	FTickTimeBudget() = default;
	FTickTimeBudget(FTickTimeBudget&&) noexcept = default;
	FTickTimeBudget& operator=(FTickTimeBudget&&) noexcept = default;
	FTickTimeBudget(const FTickTimeBudget&) = delete;
	FTickTimeBudget& operator=(const FTickTimeBudget&) = delete;

	/**
	 * Create a budget with a millisecond limit.
	 *
	 * @param InWorldContext  Any UObject with a valid GetWorld().
	 * @param Ms             Budget in milliseconds per tick (e.g. 2.0 = 2ms).
	 * @return               A configured FTickTimeBudget awaiter.
	 */
	static FTickTimeBudget Milliseconds(UObject* InWorldContext, double Ms)
	{
		FTickTimeBudget Budget;
		Budget.WorldContext = InWorldContext;
		Budget.BudgetSeconds = Ms * 0.001;
		return Budget;
	}

	bool await_ready()
	{
		const double Now = FPlatformTime::Seconds();
		if (!bInitialized)
		{
			TickStartTime = Now;
			bInitialized = true;
		}
		// Still within budget — don't yield
		return (Now - TickStartTime) < BudgetSeconds;
	}

	void await_suspend(std::coroutine_handle<> Handle)
	{
		if (!WorldContext) { Handle.resume(); return; }
		UWorld* World = WorldContext->GetWorld();
		if (!World) { Handle.resume(); return; }
		UAsyncFlowTickSubsystem* Subsystem = World->GetSubsystem<UAsyncFlowTickSubsystem>();
		if (Subsystem)
		{
			Subsystem->ScheduleTicks(Handle, 1, AliveFlag.Get());
		}
		else
		{
			Handle.resume();
		}
	}

	void await_resume()
	{
		// Reset budget timer for the new tick
		TickStartTime = FPlatformTime::Seconds();
	}
};

} // namespace AsyncFlow

