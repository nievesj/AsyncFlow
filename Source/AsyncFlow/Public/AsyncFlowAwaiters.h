// AsyncFlowAwaiters.h — Core timing and flow control awaiters
#pragma once

#include "AsyncFlowTask.h"
#include "AsyncFlowTickSubsystem.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"
#include "Templates/Tuple.h"

#include <coroutine>
#include <atomic>
#include <tuple>

namespace AsyncFlow
{

// ============================================================================
// Delay — suspends for N seconds using game-dilated time
// ============================================================================

struct FDelayAwaiter
{
	UObject* WorldContext;
	float Seconds;

	bool await_ready() const { return Seconds <= 0.0f; }

	void await_suspend(std::coroutine_handle<> Handle) const
	{
		if (!WorldContext)
		{
			return;
		}
		UWorld* World = WorldContext->GetWorld();
		if (!World)
		{
			return;
		}
		UAsyncFlowTickSubsystem* Subsystem = World->GetSubsystem<UAsyncFlowTickSubsystem>();
		if (Subsystem)
		{
			Subsystem->ScheduleDelay(Handle, Seconds);
		}
	}

	void await_resume() const {}
};

/** Suspend for Seconds (dilated game time). Requires a world context. */
[[nodiscard]] inline FDelayAwaiter Delay(UObject* WorldContext, float Seconds)
{
	return FDelayAwaiter{WorldContext, Seconds};
}

// ============================================================================
// RealDelay — suspends for N seconds using wall-clock time
// ============================================================================

struct FRealDelayAwaiter
{
	UObject* WorldContext;
	float Seconds;

	bool await_ready() const { return Seconds <= 0.0f; }

	void await_suspend(std::coroutine_handle<> Handle) const
	{
		if (!WorldContext)
		{
			return;
		}
		UWorld* World = WorldContext->GetWorld();
		if (!World)
		{
			return;
		}
		UAsyncFlowTickSubsystem* Subsystem = World->GetSubsystem<UAsyncFlowTickSubsystem>();
		if (Subsystem)
		{
			Subsystem->ScheduleRealDelay(Handle, Seconds);
		}
	}

	void await_resume() const {}
};

/** Suspend for Seconds (real wall-clock time, ignores pause/dilation). */
[[nodiscard]] inline FRealDelayAwaiter RealDelay(UObject* WorldContext, float Seconds)
{
	return FRealDelayAwaiter{WorldContext, Seconds};
}

// ============================================================================
// NextTick — suspends until the next frame
// ============================================================================

struct FNextTickAwaiter
{
	UObject* WorldContext;

	bool await_ready() const { return false; }

	void await_suspend(std::coroutine_handle<> Handle) const
	{
		if (!WorldContext)
		{
			return;
		}
		UWorld* World = WorldContext->GetWorld();
		if (!World)
		{
			return;
		}
		UAsyncFlowTickSubsystem* Subsystem = World->GetSubsystem<UAsyncFlowTickSubsystem>();
		if (Subsystem)
		{
			Subsystem->ScheduleTicks(Handle, 1);
		}
	}

	void await_resume() const {}
};

/** Suspend until the next tick. */
[[nodiscard]] inline FNextTickAwaiter NextTick(UObject* WorldContext)
{
	return FNextTickAwaiter{WorldContext};
}

// ============================================================================
// Ticks — suspends for N frames
// ============================================================================

struct FTicksAwaiter
{
	UObject* WorldContext;
	int32 NumTicks;

	bool await_ready() const { return NumTicks <= 0; }

	void await_suspend(std::coroutine_handle<> Handle) const
	{
		if (!WorldContext)
		{
			return;
		}
		UWorld* World = WorldContext->GetWorld();
		if (!World)
		{
			return;
		}
		UAsyncFlowTickSubsystem* Subsystem = World->GetSubsystem<UAsyncFlowTickSubsystem>();
		if (Subsystem)
		{
			Subsystem->ScheduleTicks(Handle, NumTicks);
		}
	}

	void await_resume() const {}
};

/** Suspend for N ticks. */
[[nodiscard]] inline FTicksAwaiter Ticks(UObject* WorldContext, int32 NumTicks)
{
	return FTicksAwaiter{WorldContext, NumTicks};
}

// ============================================================================
// WaitForCondition — polls predicate each tick, resumes when true
// ============================================================================

struct FConditionAwaiter
{
	UObject* Context;
	TFunction<bool()> Predicate;

	bool await_ready()
	{
		return Predicate && Predicate();
	}

	void await_suspend(std::coroutine_handle<> Handle) const
	{
		if (!Context)
		{
			return;
		}
		UWorld* World = Context->GetWorld();
		if (!World)
		{
			return;
		}
		UAsyncFlowTickSubsystem* Subsystem = World->GetSubsystem<UAsyncFlowTickSubsystem>();
		if (Subsystem)
		{
			Subsystem->ScheduleCondition(Handle, Context, Predicate);
		}
	}

	void await_resume() const {}
};

/** Suspend until Predicate returns true. Context is used to get world and for validity checks. */
[[nodiscard]] inline FConditionAwaiter WaitForCondition(UObject* Context, TFunction<bool()> Predicate)
{
	return FConditionAwaiter{Context, MoveTemp(Predicate)};
}

// ============================================================================
// WhenAll — waits for all tasks to complete
// ============================================================================

namespace Private
{

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

struct FWhenAllAwaiter
{
	TSharedPtr<Private::FWhenAllState> State;

	bool await_ready() const
	{
		return State->Remaining.load(std::memory_order_acquire) <= 0;
	}

	void await_suspend(std::coroutine_handle<> Handle)
	{
		State->Continuation = Handle;
		// If all already completed before we suspended, resume immediately
		if (State->Remaining.load(std::memory_order_acquire) <= 0)
		{
			Handle.resume();
		}
	}

	void await_resume() const {}
};

/** Wait until all provided tasks complete. Tasks are started automatically. */
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

struct FWhenAnyState
{
	std::atomic<bool> bTriggered{false};
	std::atomic<int32> WinnerIndex{-1};
	std::coroutine_handle<> Continuation;
};

} // namespace Private

struct FWhenAnyAwaiter
{
	TSharedPtr<Private::FWhenAnyState> State;

	bool await_ready() const
	{
		return State->bTriggered.load(std::memory_order_acquire);
	}

	void await_suspend(std::coroutine_handle<> Handle)
	{
		State->Continuation = Handle;
		if (State->bTriggered.load(std::memory_order_acquire))
		{
			Handle.resume();
		}
	}

	int32 await_resume() const
	{
		return State->WinnerIndex.load(std::memory_order_acquire);
	}
};

/** Wait until any one of the provided tasks completes. Returns the 0-based index of the winner. */
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

struct FRaceAwaiter
{
	TSharedPtr<Private::FWhenAnyState> State;
	TSharedPtr<TArray<TFunction<void()>>> CancelFunctions;

	bool await_ready() const
	{
		return State->bTriggered.load(std::memory_order_acquire);
	}

	void await_suspend(std::coroutine_handle<> Handle)
	{
		State->Continuation = Handle;
		if (State->bTriggered.load(std::memory_order_acquire))
		{
			// Cancel losers then resume
			const int32 Winner = State->WinnerIndex.load(std::memory_order_acquire);
			for (int32 Idx = 0; Idx < CancelFunctions->Num(); ++Idx)
			{
				if (Idx != Winner)
				{
					(*CancelFunctions)[Idx]();
				}
			}
			Handle.resume();
		}
	}

	int32 await_resume() const
	{
		return State->WinnerIndex.load(std::memory_order_acquire);
	}
};

/** Race: first task to complete wins; all others are cancelled. Returns the winner's 0-based index. */
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
		CancelFunctions->Add([&Task]() { Task.Cancel(); });

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

} // namespace AsyncFlow

