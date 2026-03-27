// AsyncFlowAwaiters.h — Core timing and flow control awaiters
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
// Exposed so inline awaiter functions can reuse the pattern without duplication.
// ============================================================================

namespace Private
{

/**
 * RAII bAlive flag shared between an awaiter and its tick-subsystem entry.
 * The awaiter sets *Flag = false on destruction; the subsystem reads it before
 * calling Handle.resume(). Null means "no tracking" (legacy code path).
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

/** Suspend for Seconds (dilated game time). Requires a world context. */
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

/** Suspend for Seconds (real wall-clock time, ignores pause/dilation). */
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

/** Suspend until the next tick. */
[[nodiscard]] inline FNextTickAwaiter NextTick(UObject* WorldContext)
{
	FNextTickAwaiter Aw;
	Aw.WorldContext = WorldContext;
	return Aw;
}

// ============================================================================
// Ticks — suspends for N frames
// ============================================================================

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

/** Suspend for N ticks. */
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

/** Suspend until Predicate returns true. Context is used to get world and for validity checks. */
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

	bool await_suspend(std::coroutine_handle<> Handle)
	{
		State->Continuation = Handle;
		// If all already completed before we suspended, don't suspend
		return State->Remaining.load(std::memory_order_acquire) > 0;
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

/** Wait until all tasks in the array complete. Tasks are started automatically. */
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

/** Wait until any task in the array completes. Returns the 0-based winner index. */
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

/** Race with a TArray. First to complete wins; all others cancelled. */
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
// UnpausedDelay — suspends using unpaused time (ticks while paused)
// ============================================================================

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

/** Suspend for Seconds using unpaused time (continues during pause). */
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

/** Suspend for Seconds using audio time. */
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

/** Suspend for Seconds factoring in an actor's CustomTimeDilation. */
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
 * Awaiter for time-sliced loop processing. Returns immediately if the tick
 * budget has not been exhausted; yields to next tick when budget runs out.
 * The AliveFlag is shared with the subsystem entry so that if the enclosing
 * coroutine frame is destroyed mid-suspension, the pending tick entry is
 * discarded safely.
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
	UObject* WorldContext = nullptr;
	double BudgetSeconds = 0.001;
	double TickStartTime = 0.0;
	bool bInitialized = false;
	Private::FAwaiterAliveFlag AliveFlag;

	FTickTimeBudget() = default;
	FTickTimeBudget(FTickTimeBudget&&) noexcept = default;
	FTickTimeBudget& operator=(FTickTimeBudget&&) noexcept = default;
	FTickTimeBudget(const FTickTimeBudget&) = delete;
	FTickTimeBudget& operator=(const FTickTimeBudget&) = delete;

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

