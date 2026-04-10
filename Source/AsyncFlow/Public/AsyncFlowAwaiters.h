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
// All awaiters in this file are game-thread-only and use a world context
// (UObject*) to access the UAsyncFlowTickSubsystem for scheduling. In latent
// mode, the world context is resolved automatically via
// Private::GetCurrentWorldContext(). If both the explicit and thread-local
// contexts are null, the awaiter resumes immediately to prevent a permanent hang.
#pragma once

#include "AsyncFlowTask.h"
#include "AsyncFlowTickSubsystem.h"
#include "AsyncFlowLatentAction.h"
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
		TWeakObjectPtr<UObject> WorldContext;
		float Seconds = 0.0f;
		Private::FAwaiterAliveFlag AliveFlag;
		mutable std::coroutine_handle<> StoredHandle;
		mutable UAsyncFlowTickSubsystem* StoredSubsystem = nullptr;

		FDelayAwaiter() = default;
		FDelayAwaiter(FDelayAwaiter&&) noexcept = default;
		FDelayAwaiter& operator=(FDelayAwaiter&&) noexcept = default;
		FDelayAwaiter(const FDelayAwaiter&) = delete;
		FDelayAwaiter& operator=(const FDelayAwaiter&) = delete;

		bool await_ready() const
		{
			return Seconds <= 0.0f;
		}

		void await_suspend(std::coroutine_handle<> Handle) const
		{
			UObject* ResolvedCtx = WorldContext.Get();
			if (!ResolvedCtx || !ResolvedCtx->GetWorld())
			{
				UE_LOG(LogAsyncFlow, Warning, TEXT("Delay: null WorldContext or World — resuming immediately"));
				Handle.resume();
				return;
			}

			// Latent fast-path: register condition directly with the latent action
			FAsyncFlowLatentAction* LatentAction = Private::GetCurrentLatentAction();
			if (LatentAction)
			{
				StoredHandle = Handle;
				UWorld* W = ResolvedCtx->GetWorld();
				const double TargetTime = W->GetTimeSeconds() + Seconds;
				TWeakObjectPtr<UObject> WeakCtx = WorldContext;
				LatentAction->SetCondition(
					[WeakCtx, TargetTime]() -> bool {
						UObject* Ctx = WeakCtx.Get();
						return !Ctx || !Ctx->GetWorld() || Ctx->GetWorld()->GetTimeSeconds() >= TargetTime;
					},
					Handle, AliveFlag.Get());
				return;
			}

			UAsyncFlowTickSubsystem* Subsystem = ResolvedCtx->GetWorld()->GetSubsystem<UAsyncFlowTickSubsystem>();
			if (Subsystem)
			{
				StoredHandle = Handle;
				StoredSubsystem = Subsystem;
				Subsystem->ScheduleDelay(Handle, Seconds, AliveFlag.Get());
			}
			else
			{
				Handle.resume();
			}
		}

		void CancelAwaiter()
		{
			if (StoredHandle)
			{
				if (StoredSubsystem)
				{
					StoredSubsystem->CancelHandle(StoredHandle);
					StoredSubsystem = nullptr;
				}
				auto H = StoredHandle;
				StoredHandle = nullptr;
				H.resume();
			}
		}

		void await_resume() const
		{
		}
	};

	/**
 * Suspend for Seconds of dilated game time.
 *
 * @param Seconds      Duration in game-dilated seconds. Values <= 0 resume immediately.
 * @param WorldContext  Any UObject with a valid GetWorld(). Optional in latent mode.
 * @return             An awaiter — use with co_await.
 */
	[[nodiscard]] inline FDelayAwaiter Delay(float Seconds, UObject* WorldContext = nullptr)
	{
		FDelayAwaiter Aw;
		Aw.WorldContext = WorldContext ? WorldContext : Private::GetCurrentWorldContext();
		Aw.Seconds = Seconds;
		return Aw;
	}

	/** @deprecated Use Delay(Seconds) or Delay(Seconds, WorldContext) instead. */
	[[deprecated("Use Delay(Seconds) or Delay(Seconds, WorldContext)")]] [[nodiscard]] inline FDelayAwaiter Delay(UObject* WorldContext, float Seconds)
	{
		return Delay(Seconds, WorldContext);
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
		TWeakObjectPtr<UObject> WorldContext;
		float Seconds = 0.0f;
		Private::FAwaiterAliveFlag AliveFlag;
		mutable std::coroutine_handle<> StoredHandle;
		mutable UAsyncFlowTickSubsystem* StoredSubsystem = nullptr;

		FRealDelayAwaiter() = default;
		FRealDelayAwaiter(FRealDelayAwaiter&&) noexcept = default;
		FRealDelayAwaiter& operator=(FRealDelayAwaiter&&) noexcept = default;
		FRealDelayAwaiter(const FRealDelayAwaiter&) = delete;
		FRealDelayAwaiter& operator=(const FRealDelayAwaiter&) = delete;

		bool await_ready() const
		{
			return Seconds <= 0.0f;
		}

		void await_suspend(std::coroutine_handle<> Handle) const
		{
			UObject* ResolvedCtx = WorldContext.Get();
			if (!ResolvedCtx || !ResolvedCtx->GetWorld())
			{
				UE_LOG(LogAsyncFlow, Warning, TEXT("RealDelay: null WorldContext or World — resuming immediately"));
				Handle.resume();
				return;
			}

			// Latent fast-path: register condition directly with the latent action
			FAsyncFlowLatentAction* LatentAction = Private::GetCurrentLatentAction();
			if (LatentAction)
			{
				StoredHandle = Handle;
				UWorld* W = ResolvedCtx->GetWorld();
				const double TargetTime = W->GetRealTimeSeconds() + Seconds;
				TWeakObjectPtr<UObject> WeakCtx = WorldContext;
				LatentAction->SetCondition(
					[WeakCtx, TargetTime]() -> bool {
						UObject* Ctx = WeakCtx.Get();
						return !Ctx || !Ctx->GetWorld() || Ctx->GetWorld()->GetRealTimeSeconds() >= TargetTime;
					},
					Handle, AliveFlag.Get());
				return;
			}

			UAsyncFlowTickSubsystem* Subsystem = ResolvedCtx->GetWorld()->GetSubsystem<UAsyncFlowTickSubsystem>();
			if (Subsystem)
			{
				StoredHandle = Handle;
				StoredSubsystem = Subsystem;
				Subsystem->ScheduleRealDelay(Handle, Seconds, AliveFlag.Get());
			}
			else
			{
				Handle.resume();
			}
		}

		void CancelAwaiter()
		{
			if (StoredHandle)
			{
				if (StoredSubsystem)
				{
					StoredSubsystem->CancelHandle(StoredHandle);
					StoredSubsystem = nullptr;
				}
				auto H = StoredHandle;
				StoredHandle = nullptr;
				H.resume();
			}
		}

		void await_resume() const
		{
		}
	};

	/**
 * Suspend for Seconds of real wall-clock time. Ignores pause and dilation.
 *
 * @param Seconds      Duration in real seconds.
 * @param WorldContext  Any UObject with a valid GetWorld(). Optional in latent mode.
 * @return             An awaiter — use with co_await.
 */
	[[nodiscard]] inline FRealDelayAwaiter RealDelay(float Seconds, UObject* WorldContext = nullptr)
	{
		FRealDelayAwaiter Aw;
		Aw.WorldContext = WorldContext ? WorldContext : Private::GetCurrentWorldContext();
		Aw.Seconds = Seconds;
		return Aw;
	}

	/** @deprecated Use RealDelay(Seconds) or RealDelay(Seconds, WorldContext) instead. */
	[[deprecated("Use RealDelay(Seconds) or RealDelay(Seconds, WorldContext)")]] [[nodiscard]] inline FRealDelayAwaiter RealDelay(UObject* WorldContext, float Seconds)
	{
		return RealDelay(Seconds, WorldContext);
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
		TWeakObjectPtr<UObject> WorldContext;
		Private::FAwaiterAliveFlag AliveFlag;
		mutable std::coroutine_handle<> StoredHandle;
		mutable UAsyncFlowTickSubsystem* StoredSubsystem = nullptr;

		FNextTickAwaiter() = default;
		FNextTickAwaiter(FNextTickAwaiter&&) noexcept = default;
		FNextTickAwaiter& operator=(FNextTickAwaiter&&) noexcept = default;
		FNextTickAwaiter(const FNextTickAwaiter&) = delete;
		FNextTickAwaiter& operator=(const FNextTickAwaiter&) = delete;

		bool await_ready() const
		{
			return false;
		}

		void await_suspend(std::coroutine_handle<> Handle) const
		{
			UObject* ResolvedCtx = WorldContext.Get();
			if (!ResolvedCtx || !ResolvedCtx->GetWorld())
			{
				UE_LOG(LogAsyncFlow, Warning, TEXT("NextTick: null WorldContext or World — resuming immediately"));
				Handle.resume();
				return;
			}

			// Latent fast-path: resume on next UpdateOperation call
			FAsyncFlowLatentAction* LatentAction = Private::GetCurrentLatentAction();
			if (LatentAction)
			{
				StoredHandle = Handle;
				LatentAction->SetCondition(
					[]() -> bool { return true; },
					Handle, AliveFlag.Get());
				return;
			}

			UAsyncFlowTickSubsystem* Subsystem = ResolvedCtx->GetWorld()->GetSubsystem<UAsyncFlowTickSubsystem>();
			if (Subsystem)
			{
				StoredHandle = Handle;
				StoredSubsystem = Subsystem;
				Subsystem->ScheduleTicks(Handle, 1, AliveFlag.Get());
			}
			else
			{
				Handle.resume();
			}
		}

		void CancelAwaiter()
		{
			if (StoredHandle)
			{
				if (StoredSubsystem)
				{
					StoredSubsystem->CancelHandle(StoredHandle);
					StoredSubsystem = nullptr;
				}
				auto H = StoredHandle;
				StoredHandle = nullptr;
				H.resume();
			}
		}

		void await_resume() const
		{
		}
	};

	/**
 * Suspend until the next game tick.
 *
 * @param WorldContext  Any UObject with a valid GetWorld(). Optional in latent mode.
 * @return             An awaiter — use with co_await.
 */
	[[nodiscard]] inline FNextTickAwaiter NextTick(UObject* WorldContext = nullptr)
	{
		FNextTickAwaiter Aw;
		Aw.WorldContext = WorldContext ? WorldContext : Private::GetCurrentWorldContext();
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
		TWeakObjectPtr<UObject> WorldContext;
		int32 NumTicks = 0;
		Private::FAwaiterAliveFlag AliveFlag;
		mutable std::coroutine_handle<> StoredHandle;
		mutable UAsyncFlowTickSubsystem* StoredSubsystem = nullptr;

		FTicksAwaiter() = default;
		FTicksAwaiter(FTicksAwaiter&&) noexcept = default;
		FTicksAwaiter& operator=(FTicksAwaiter&&) noexcept = default;
		FTicksAwaiter(const FTicksAwaiter&) = delete;
		FTicksAwaiter& operator=(const FTicksAwaiter&) = delete;

		bool await_ready() const
		{
			return NumTicks <= 0;
		}

		void await_suspend(std::coroutine_handle<> Handle) const
		{
			UObject* ResolvedCtx = WorldContext.Get();
			if (!ResolvedCtx || !ResolvedCtx->GetWorld())
			{
				UE_LOG(LogAsyncFlow, Warning, TEXT("Ticks: null WorldContext or World — resuming immediately"));
				Handle.resume();
				return;
			}

			// Latent fast-path: count down ticks via the latent action
			FAsyncFlowLatentAction* LatentAction = Private::GetCurrentLatentAction();
			if (LatentAction)
			{
				StoredHandle = Handle;
				auto Counter = MakeShared<int32>(NumTicks);
				LatentAction->SetCondition(
					[Counter]() -> bool { return --(*Counter) <= 0; },
					Handle, AliveFlag.Get());
				return;
			}

			UAsyncFlowTickSubsystem* Subsystem = ResolvedCtx->GetWorld()->GetSubsystem<UAsyncFlowTickSubsystem>();
			if (Subsystem)
			{
				StoredHandle = Handle;
				StoredSubsystem = Subsystem;
				Subsystem->ScheduleTicks(Handle, NumTicks, AliveFlag.Get());
			}
			else
			{
				Handle.resume();
			}
		}

		void CancelAwaiter()
		{
			if (StoredHandle)
			{
				if (StoredSubsystem)
				{
					StoredSubsystem->CancelHandle(StoredHandle);
					StoredSubsystem = nullptr;
				}
				auto H = StoredHandle;
				StoredHandle = nullptr;
				H.resume();
			}
		}

		void await_resume() const
		{
		}
	};

	/**
 * Suspend for InNumTicks game ticks.
 *
 * @param InNumTicks   Number of ticks to wait. Clamped to at least 1 in the subsystem.
 * @param WorldContext  Any UObject with a valid GetWorld(). Optional in latent mode.
 * @return             An awaiter — use with co_await.
 */
	[[nodiscard]] inline FTicksAwaiter Ticks(int32 InNumTicks, UObject* WorldContext = nullptr)
	{
		FTicksAwaiter Aw;
		Aw.WorldContext = WorldContext ? WorldContext : Private::GetCurrentWorldContext();
		Aw.NumTicks = InNumTicks;
		return Aw;
	}

	/** @deprecated Use Ticks(NumTicks) or Ticks(NumTicks, WorldContext) instead. */
	[[deprecated("Use Ticks(NumTicks) or Ticks(NumTicks, WorldContext)")]] [[nodiscard]] inline FTicksAwaiter Ticks(UObject* WorldContext, int32 InNumTicks)
	{
		return Ticks(InNumTicks, WorldContext);
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
		TWeakObjectPtr<UObject> Context;
		TFunction<bool()> Predicate;
		Private::FAwaiterAliveFlag AliveFlag;
		mutable std::coroutine_handle<> StoredHandle;
		mutable UAsyncFlowTickSubsystem* StoredSubsystem = nullptr;

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
			UObject* ResolvedCtx = Context.Get();
			if (!ResolvedCtx || !ResolvedCtx->GetWorld())
			{
				UE_LOG(LogAsyncFlow, Warning, TEXT("WaitForCondition: null Context or World — resuming immediately"));
				Handle.resume();
				return;
			}

			// Latent fast-path: poll predicate via the latent action
			FAsyncFlowLatentAction* LatentAction = Private::GetCurrentLatentAction();
			if (LatentAction)
			{
				StoredHandle = Handle;
				auto Pred = Predicate;
				LatentAction->SetCondition(
					[Pred]() -> bool { return Pred(); },
					Handle, AliveFlag.Get());
				return;
			}

			UAsyncFlowTickSubsystem* Subsystem = ResolvedCtx->GetWorld()->GetSubsystem<UAsyncFlowTickSubsystem>();
			if (Subsystem)
			{
				StoredHandle = Handle;
				StoredSubsystem = Subsystem;
				Subsystem->ScheduleCondition(Handle, ResolvedCtx, Predicate, AliveFlag.Get());
			}
			else
			{
				Handle.resume();
			}
		}

		void CancelAwaiter()
		{
			if (StoredHandle)
			{
				if (StoredSubsystem)
				{
					StoredSubsystem->CancelHandle(StoredHandle);
					StoredSubsystem = nullptr;
				}
				auto H = StoredHandle;
				StoredHandle = nullptr;
				H.resume();
			}
		}

		void await_resume() const
		{
		}
	};

	/**
 * Suspend until InPredicate returns true. Evaluated once per tick.
 *
 * @param InPredicate  Callable returning bool. Must be safe to call from the game thread.
 * @param InContext     UObject for world access and lifetime tracking. Optional in latent mode.
 * @return             An awaiter — use with co_await.
 */
	[[nodiscard]] inline FConditionAwaiter WaitForCondition(TFunction<bool()> InPredicate, UObject* InContext = nullptr)
	{
		FConditionAwaiter Aw;
		Aw.Context = InContext ? InContext : Private::GetCurrentWorldContext();
		Aw.Predicate = MoveTemp(InPredicate);
		return Aw;
	}

	/** @deprecated Use WaitForCondition(Predicate) or WaitForCondition(Predicate, Context) instead. */
	[[deprecated("Use WaitForCondition(Predicate) or WaitForCondition(Predicate, Context)")]] [[nodiscard]] inline FConditionAwaiter WaitForCondition(UObject* InContext, TFunction<bool()> InPredicate)
	{
		return WaitForCondition(MoveTemp(InPredicate), InContext);
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
			std::atomic<bool> bResumed{ false };
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
		TSharedPtr<TArray<TFunction<void()>>> CancelFunctions;

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

		void await_resume() const
		{
		}

		/** Cancel all inner tasks and resume the parent coroutine. */
		void CancelAwaiter()
		{
			if (CancelFunctions)
			{
				for (auto& Fn : *CancelFunctions)
				{
					Fn();
				}
			}
			bool bExpected = false;
			if (State->bResumed.compare_exchange_strong(bExpected, true, std::memory_order_acq_rel))
			{
				if (State->Continuation && !State->Continuation.done())
				{
					State->Continuation.resume();
				}
			}
		}
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
	[[nodiscard]] FWhenAllAwaiter WhenAll(TaskTypes&... Tasks)
	{
		constexpr int32 Count = sizeof...(TaskTypes);
		TSharedPtr<Private::FWhenAllState> State = MakeShared<Private::FWhenAllState>(Count);
		TSharedPtr<TArray<TFunction<void()>>> CancelFunctions = MakeShared<TArray<TFunction<void()>>>();
		CancelFunctions->Reserve(Count);

		auto SetupTask = [&State, &CancelFunctions](auto& Task) {
			TSharedPtr<FAsyncFlowState> FlowState = Task.GetFlowState();
			CancelFunctions->Add([FlowState]() { if (FlowState) { FlowState->Cancel(); } });
			Task.OnComplete([State]() {
				const int32 Prev = State->Remaining.fetch_sub(1, std::memory_order_acq_rel);
				if (Prev == 1)
				{
					bool bExpected = false;
					if (State->bResumed.compare_exchange_strong(bExpected, true, std::memory_order_acq_rel))
					{
						if (State->Continuation && !State->Continuation.done())
						{
							State->Continuation.resume();
						}
					}
				}
			});
			Task.StartManaged();
		};

		(SetupTask(Tasks), ...);

		return FWhenAllAwaiter{ State, CancelFunctions };
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
			std::atomic<bool> bTriggered{ false };
			std::atomic<bool> bResumed{ false };
			std::atomic<int32> WinnerIndex{ -1 };
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
		TSharedPtr<TArray<TFunction<void()>>> CancelFunctions;

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

		/** Cancel all inner tasks and resume the parent coroutine. */
		void CancelAwaiter()
		{
			if (CancelFunctions)
			{
				for (auto& Fn : *CancelFunctions)
				{
					Fn();
				}
			}
			bool bExpected = false;
			if (State->bResumed.compare_exchange_strong(bExpected, true, std::memory_order_acq_rel))
			{
				if (State->Continuation && !State->Continuation.done())
				{
					State->Continuation.resume();
				}
			}
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
	[[nodiscard]] FWhenAnyAwaiter WhenAny(TaskTypes&... Tasks)
	{
		TSharedPtr<Private::FWhenAnyState> State = MakeShared<Private::FWhenAnyState>();
		TSharedPtr<TArray<TFunction<void()>>> CancelFunctions = MakeShared<TArray<TFunction<void()>>>();
		CancelFunctions->Reserve(sizeof...(TaskTypes));

		int32 Index = 0;
		auto SetupTask = [&State, &Index, &CancelFunctions](auto& Task) {
			const int32 MyIndex = Index++;
			TSharedPtr<FAsyncFlowState> FlowState = Task.GetFlowState();
			CancelFunctions->Add([FlowState]() { if (FlowState) { FlowState->Cancel(); } });
			Task.OnComplete([State, MyIndex]() {
				bool bExpected = false;
				if (State->bTriggered.compare_exchange_strong(bExpected, true, std::memory_order_acq_rel))
				{
					State->WinnerIndex.store(MyIndex, std::memory_order_release);
					bool bResumeExpected = false;
					if (State->bResumed.compare_exchange_strong(bResumeExpected, true, std::memory_order_acq_rel))
					{
						if (State->Continuation && !State->Continuation.done())
						{
							State->Continuation.resume();
						}
					}
				}
			});
			Task.StartManaged();
		};

		(SetupTask(Tasks), ...);

		return FWhenAnyAwaiter{ State, CancelFunctions };
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

		/** Cancel all inner tasks (including the winner) and resume the parent. */
		void CancelAwaiter()
		{
			if (CancelFunctions)
			{
				for (auto& Fn : *CancelFunctions)
				{
					Fn();
				}
			}
			bool bExpected = false;
			if (State->bResumed.compare_exchange_strong(bExpected, true, std::memory_order_acq_rel))
			{
				if (State->Continuation && !State->Continuation.done())
				{
					State->Continuation.resume();
				}
			}
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
	[[nodiscard]] FRaceAwaiter Race(TaskTypes&... Tasks)
	{
		TSharedPtr<Private::FWhenAnyState> State = MakeShared<Private::FWhenAnyState>();
		TSharedPtr<TArray<TFunction<void()>>> CancelFunctions = MakeShared<TArray<TFunction<void()>>>();
		CancelFunctions->Reserve(sizeof...(TaskTypes));

		int32 Index = 0;
		auto SetupTask = [&State, &Index, &CancelFunctions](auto& Task) {
			const int32 MyIndex = Index++;
			TSharedPtr<FAsyncFlowState> FlowState = Task.GetFlowState();
			CancelFunctions->Add([FlowState]() { if (FlowState) { FlowState->Cancel(); } });

			Task.OnComplete([State, MyIndex, CancelFunctions]() {
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
					bool bResumeExpected = false;
					if (State->bResumed.compare_exchange_strong(bResumeExpected, true, std::memory_order_acq_rel))
					{
						if (State->Continuation && !State->Continuation.done())
						{
							State->Continuation.resume();
						}
					}
				}
			});
			Task.StartManaged();
		};

		(SetupTask(Tasks), ...);

		return FRaceAwaiter{ State, CancelFunctions };
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
	[[nodiscard]] inline FWhenAllAwaiter WhenAll(TArray<TTask<void>*>& Tasks)
	{
		const int32 Count = Tasks.Num();
		TSharedPtr<Private::FWhenAllState> State = MakeShared<Private::FWhenAllState>(Count);
		TSharedPtr<TArray<TFunction<void()>>> CancelFunctions = MakeShared<TArray<TFunction<void()>>>();
		CancelFunctions->Reserve(Count);

		for (TTask<void>* Task : Tasks)
		{
			if (Task)
			{
				TSharedPtr<FAsyncFlowState> FlowState = Task->GetFlowState();
				CancelFunctions->Add([FlowState]() { if (FlowState) { FlowState->Cancel(); } });
				Task->OnComplete([State]() {
					const int32 Prev = State->Remaining.fetch_sub(1, std::memory_order_acq_rel);
					if (Prev == 1)
					{
						bool bExpected = false;
						if (State->bResumed.compare_exchange_strong(bExpected, true, std::memory_order_acq_rel))
						{
							if (State->Continuation && !State->Continuation.done())
							{
								State->Continuation.resume();
							}
						}
					}
				});
				Task->StartManaged();
			}
			else
			{
				CancelFunctions->Add([]() {});
				const int32 Prev = State->Remaining.fetch_sub(1, std::memory_order_acq_rel);
				if (Prev == 1)
				{
					bool bExpected = false;
					if (State->bResumed.compare_exchange_strong(bExpected, true, std::memory_order_acq_rel))
					{
						if (State->Continuation && !State->Continuation.done())
						{
							State->Continuation.resume();
						}
					}
				}
			}
		}

		return FWhenAllAwaiter{ State, CancelFunctions };
	}

	/**
 * Wait until any task in the array completes.
 *
 * @param Tasks  Array of pointers to TTask<void>. Null entries are skipped.
 * @return       An awaiter — co_await yields the 0-based winner index.
 */
	[[nodiscard]] inline FWhenAnyAwaiter WhenAny(TArray<TTask<void>*>& Tasks)
	{
		TSharedPtr<Private::FWhenAnyState> State = MakeShared<Private::FWhenAnyState>();
		TSharedPtr<TArray<TFunction<void()>>> CancelFunctions = MakeShared<TArray<TFunction<void()>>>();
		CancelFunctions->Reserve(Tasks.Num());

		for (int32 Idx = 0; Idx < Tasks.Num(); ++Idx)
		{
			TTask<void>* Task = Tasks[Idx];
			if (!Task)
			{
				CancelFunctions->Add([]() {});
				continue;
			}
			TSharedPtr<FAsyncFlowState> FlowState = Task->GetFlowState();
			CancelFunctions->Add([FlowState]() { if (FlowState) { FlowState->Cancel(); } });
			const int32 MyIndex = Idx;
			Task->OnComplete([State, MyIndex]() {
				bool bExpected = false;
				if (State->bTriggered.compare_exchange_strong(bExpected, true, std::memory_order_acq_rel))
				{
					State->WinnerIndex.store(MyIndex, std::memory_order_release);
					bool bResumeExpected = false;
					if (State->bResumed.compare_exchange_strong(bResumeExpected, true, std::memory_order_acq_rel))
					{
						if (State->Continuation && !State->Continuation.done())
						{
							State->Continuation.resume();
						}
					}
				}
			});
			Task->StartManaged();
		}

		return FWhenAnyAwaiter{ State, CancelFunctions };
	}

	/**
 * Race with a TArray. First to complete wins; all others cancelled.
 *
 * @param Tasks  Array of pointers to TTask<void>. Null entries get a no-op cancel function.
 * @return       An awaiter — co_await yields the 0-based winner index.
 */
	[[nodiscard]] inline FRaceAwaiter Race(TArray<TTask<void>*>& Tasks)
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
			Task->OnComplete([State, MyIndex, CancelFunctions]() {
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
					bool bResumeExpected = false;
					if (State->bResumed.compare_exchange_strong(bResumeExpected, true, std::memory_order_acq_rel))
					{
						if (State->Continuation && !State->Continuation.done())
						{
							State->Continuation.resume();
						}
					}
				}
			});
			Task->StartManaged();
		}

		return FRaceAwaiter{ State, CancelFunctions };
	}

	// ============================================================================
	// Latent::WhenAll / Latent::WhenAny — UObject-lifetime-tracked composition
	// ============================================================================

	/**
	 * Latent composition helpers that tie inner task lifetimes to a UObject.
	 *
	 * When the context UObject is destroyed, all inner tasks are cancelled
	 * automatically via a contract check. Use these in latent coroutines
	 * (spawned from Blueprint) where actor/component lifetime matters.
	 *
	 * The returned awaiters are the same FWhenAllAwaiter / FWhenAnyAwaiter
	 * types — they just pre-register a validity contract on each inner task.
	 */
	namespace Latent
	{
		/**
		 * Wait for ALL tasks to complete, with UObject lifetime tracking.
		 *
		 * If the context object is destroyed, all inner tasks are cancelled.
		 *
		 * @param Context  UObject whose lifetime gates the tasks (typically 'this').
		 * @param Tasks    TTask<void> references (variadic).
		 * @return         An awaiter — co_await yields void.
		 */
		template <typename... TaskTypes>
		[[nodiscard]] FWhenAllAwaiter WhenAll(UObject* Context, TaskTypes&... Tasks)
		{
			constexpr int32 Count = sizeof...(TaskTypes);
			TSharedPtr<Private::FWhenAllState> State = MakeShared<Private::FWhenAllState>(Count);
			TSharedPtr<TArray<TFunction<void()>>> CancelFunctions = MakeShared<TArray<TFunction<void()>>>();
			CancelFunctions->Reserve(Count);

			TWeakObjectPtr<UObject> WeakCtx(Context);

			auto SetupTask = [&State, &CancelFunctions, WeakCtx](auto& Task) {
				TSharedPtr<FAsyncFlowState> FlowState = Task.GetFlowState();

				// Add lifetime contract — inner task cancels if context dies
				if (FlowState)
				{
					FlowState->AddContract([WeakCtx]() { return WeakCtx.IsValid(); });
				}

				CancelFunctions->Add([FlowState]() { if (FlowState) { FlowState->Cancel(); } });
				Task.OnComplete([State]() {
					const int32 Prev = State->Remaining.fetch_sub(1, std::memory_order_acq_rel);
					if (Prev == 1)
					{
						bool bExpected = false;
						if (State->bResumed.compare_exchange_strong(bExpected, true, std::memory_order_acq_rel))
						{
							if (State->Continuation && !State->Continuation.done())
							{
								State->Continuation.resume();
							}
						}
					}
				});
				Task.StartManaged();
			};

			(SetupTask(Tasks), ...);

			return FWhenAllAwaiter{ State, CancelFunctions };
		}

		/**
		 * Wait for ALL tasks to complete (TArray overload), with UObject lifetime tracking.
		 *
		 * @param Context  UObject whose lifetime gates the tasks.
		 * @param Tasks    Array of pointers to TTask<void>. Null entries are counted as immediately complete.
		 * @return         An awaiter — co_await yields void.
		 */
		[[nodiscard]] inline FWhenAllAwaiter WhenAll(UObject* Context, TArray<TTask<void>*>& Tasks)
		{
			const int32 Count = Tasks.Num();
			TSharedPtr<Private::FWhenAllState> State = MakeShared<Private::FWhenAllState>(Count);
			TSharedPtr<TArray<TFunction<void()>>> CancelFunctions = MakeShared<TArray<TFunction<void()>>>();
			CancelFunctions->Reserve(Count);

			TWeakObjectPtr<UObject> WeakCtx(Context);

			for (TTask<void>* Task : Tasks)
			{
				if (Task)
				{
					TSharedPtr<FAsyncFlowState> FlowState = Task->GetFlowState();
					if (FlowState)
					{
						FlowState->AddContract([WeakCtx]() { return WeakCtx.IsValid(); });
					}
					CancelFunctions->Add([FlowState]() { if (FlowState) { FlowState->Cancel(); } });
					Task->OnComplete([State]() {
						const int32 Prev = State->Remaining.fetch_sub(1, std::memory_order_acq_rel);
						if (Prev == 1)
						{
							bool bExpected = false;
							if (State->bResumed.compare_exchange_strong(bExpected, true, std::memory_order_acq_rel))
							{
								if (State->Continuation && !State->Continuation.done())
								{
									State->Continuation.resume();
								}
							}
						}
					});
					Task->StartManaged();
				}
				else
				{
					CancelFunctions->Add([]() {});
					const int32 Prev = State->Remaining.fetch_sub(1, std::memory_order_acq_rel);
					if (Prev == 1)
					{
						bool bExpected = false;
						if (State->bResumed.compare_exchange_strong(bExpected, true, std::memory_order_acq_rel))
						{
							if (State->Continuation && !State->Continuation.done())
							{
								State->Continuation.resume();
							}
						}
					}
				}
			}

			return FWhenAllAwaiter{ State, CancelFunctions };
		}

		/**
		 * Wait for ANY task to complete first, with UObject lifetime tracking.
		 *
		 * If the context object is destroyed, all inner tasks are cancelled.
		 *
		 * @param Context  UObject whose lifetime gates the tasks.
		 * @param Tasks    TTask<void> references (variadic).
		 * @return         An awaiter — co_await yields the 0-based winner index.
		 */
		template <typename... TaskTypes>
		[[nodiscard]] FWhenAnyAwaiter WhenAny(UObject* Context, TaskTypes&... Tasks)
		{
			TSharedPtr<Private::FWhenAnyState> State = MakeShared<Private::FWhenAnyState>();
			TSharedPtr<TArray<TFunction<void()>>> CancelFunctions = MakeShared<TArray<TFunction<void()>>>();
			CancelFunctions->Reserve(sizeof...(TaskTypes));

			TWeakObjectPtr<UObject> WeakCtx(Context);

			int32 Index = 0;
			auto SetupTask = [&State, &Index, &CancelFunctions, WeakCtx](auto& Task) {
				const int32 MyIndex = Index++;
				TSharedPtr<FAsyncFlowState> FlowState = Task.GetFlowState();

				if (FlowState)
				{
					FlowState->AddContract([WeakCtx]() { return WeakCtx.IsValid(); });
				}

				CancelFunctions->Add([FlowState]() { if (FlowState) { FlowState->Cancel(); } });
				Task.OnComplete([State, MyIndex]() {
					bool bExpected = false;
					if (State->bTriggered.compare_exchange_strong(bExpected, true, std::memory_order_acq_rel))
					{
						State->WinnerIndex.store(MyIndex, std::memory_order_release);
						bool bResumeExpected = false;
						if (State->bResumed.compare_exchange_strong(bResumeExpected, true, std::memory_order_acq_rel))
						{
							if (State->Continuation && !State->Continuation.done())
							{
								State->Continuation.resume();
							}
						}
					}
				});
				Task.StartManaged();
			};

			(SetupTask(Tasks), ...);

			return FWhenAnyAwaiter{ State, CancelFunctions };
		}

		/**
		 * Wait for ANY task to complete first (TArray overload), with UObject lifetime tracking.
		 *
		 * @param Context  UObject whose lifetime gates the tasks.
		 * @param Tasks    Array of pointers to TTask<void>. Null entries are skipped.
		 * @return         An awaiter — co_await yields the 0-based winner index.
		 */
		[[nodiscard]] inline FWhenAnyAwaiter WhenAny(UObject* Context, TArray<TTask<void>*>& Tasks)
		{
			TSharedPtr<Private::FWhenAnyState> State = MakeShared<Private::FWhenAnyState>();
			TSharedPtr<TArray<TFunction<void()>>> CancelFunctions = MakeShared<TArray<TFunction<void()>>>();
			CancelFunctions->Reserve(Tasks.Num());

			TWeakObjectPtr<UObject> WeakCtx(Context);

			for (int32 Idx = 0; Idx < Tasks.Num(); ++Idx)
			{
				TTask<void>* Task = Tasks[Idx];
				if (Task)
				{
					TSharedPtr<FAsyncFlowState> FlowState = Task->GetFlowState();
					if (FlowState)
					{
						FlowState->AddContract([WeakCtx]() { return WeakCtx.IsValid(); });
					}
					CancelFunctions->Add([FlowState]() { if (FlowState) { FlowState->Cancel(); } });
					const int32 MyIndex = Idx;
					Task->OnComplete([State, MyIndex]() {
						bool bExpected = false;
						if (State->bTriggered.compare_exchange_strong(bExpected, true, std::memory_order_acq_rel))
						{
							State->WinnerIndex.store(MyIndex, std::memory_order_release);
							bool bResumeExpected = false;
							if (State->bResumed.compare_exchange_strong(bResumeExpected, true, std::memory_order_acq_rel))
							{
								if (State->Continuation && !State->Continuation.done())
								{
									State->Continuation.resume();
								}
							}
						}
					});
					Task->StartManaged();
				}
				else
				{
					CancelFunctions->Add([]() {});
				}
			}

			return FWhenAnyAwaiter{ State, CancelFunctions };
		}

	} // namespace Latent

	// ============================================================================
	// UnpausedDelay — suspends using unpaused time (continues during pause)
	// ============================================================================

	/**
 * Awaiter struct for unpaused-time delays. Ticks even while the game is paused.
 * Uses UWorld::GetUnpausedTimeSeconds() as its time source.
 */
	struct FUnpausedDelayAwaiter
	{
		TWeakObjectPtr<UObject> WorldContext;
		float Seconds = 0.0f;
		Private::FAwaiterAliveFlag AliveFlag;
		mutable std::coroutine_handle<> StoredHandle;
		mutable UAsyncFlowTickSubsystem* StoredSubsystem = nullptr;

		FUnpausedDelayAwaiter() = default;
		FUnpausedDelayAwaiter(FUnpausedDelayAwaiter&&) noexcept = default;
		FUnpausedDelayAwaiter& operator=(FUnpausedDelayAwaiter&&) noexcept = default;
		FUnpausedDelayAwaiter(const FUnpausedDelayAwaiter&) = delete;
		FUnpausedDelayAwaiter& operator=(const FUnpausedDelayAwaiter&) = delete;

		bool await_ready() const
		{
			return Seconds <= 0.0f;
		}

		void await_suspend(std::coroutine_handle<> Handle) const
		{
			UObject* ResolvedCtx = WorldContext.Get();
			if (!ResolvedCtx || !ResolvedCtx->GetWorld())
			{
				UE_LOG(LogAsyncFlow, Warning, TEXT("UnpausedDelay: null WorldContext or World — resuming immediately"));
				Handle.resume();
				return;
			}

			// Latent fast-path: register condition directly with the latent action
			FAsyncFlowLatentAction* LatentAction = Private::GetCurrentLatentAction();
			if (LatentAction)
			{
				StoredHandle = Handle;
				UWorld* W = ResolvedCtx->GetWorld();
				const double TargetTime = W->GetUnpausedTimeSeconds() + Seconds;
				TWeakObjectPtr<UObject> WeakCtx = WorldContext;
				LatentAction->SetCondition(
					[WeakCtx, TargetTime]() -> bool {
						UObject* Ctx = WeakCtx.Get();
						return !Ctx || !Ctx->GetWorld() || Ctx->GetWorld()->GetUnpausedTimeSeconds() >= TargetTime;
					},
					Handle, AliveFlag.Get());
				return;
			}

			UAsyncFlowTickSubsystem* Subsystem = ResolvedCtx->GetWorld()->GetSubsystem<UAsyncFlowTickSubsystem>();
			if (Subsystem)
			{
				StoredHandle = Handle;
				StoredSubsystem = Subsystem;
				Subsystem->ScheduleUnpausedDelay(Handle, Seconds, AliveFlag.Get());
			}
			else
			{
				Handle.resume();
			}
		}

		void CancelAwaiter()
		{
			if (StoredHandle)
			{
				if (StoredSubsystem)
				{
					StoredSubsystem->CancelHandle(StoredHandle);
					StoredSubsystem = nullptr;
				}
				auto H = StoredHandle;
				StoredHandle = nullptr;
				H.resume();
			}
		}

		void await_resume() const
		{
		}
	};

	/**
 * Suspend for Seconds using unpaused time (ticks during pause).
 *
 * @param Seconds      Duration in unpaused seconds.
 * @param WorldContext  Any UObject with a valid GetWorld(). Optional in latent mode.
 * @return             An awaiter — use with co_await.
 */
	[[nodiscard]] inline FUnpausedDelayAwaiter UnpausedDelay(float Seconds, UObject* WorldContext = nullptr)
	{
		FUnpausedDelayAwaiter Aw;
		Aw.WorldContext = WorldContext ? WorldContext : Private::GetCurrentWorldContext();
		Aw.Seconds = Seconds;
		return Aw;
	}

	/** @deprecated Use UnpausedDelay(Seconds) or UnpausedDelay(Seconds, WorldContext) instead. */
	[[deprecated("Use UnpausedDelay(Seconds) or UnpausedDelay(Seconds, WorldContext)")]] [[nodiscard]] inline FUnpausedDelayAwaiter UnpausedDelay(UObject* WorldContext, float Seconds)
	{
		return UnpausedDelay(Seconds, WorldContext);
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
		TWeakObjectPtr<UObject> WorldContext;
		float Seconds = 0.0f;
		Private::FAwaiterAliveFlag AliveFlag;
		mutable std::coroutine_handle<> StoredHandle;
		mutable UAsyncFlowTickSubsystem* StoredSubsystem = nullptr;

		FAudioDelayAwaiter() = default;
		FAudioDelayAwaiter(FAudioDelayAwaiter&&) noexcept = default;
		FAudioDelayAwaiter& operator=(FAudioDelayAwaiter&&) noexcept = default;
		FAudioDelayAwaiter(const FAudioDelayAwaiter&) = delete;
		FAudioDelayAwaiter& operator=(const FAudioDelayAwaiter&) = delete;

		bool await_ready() const
		{
			return Seconds <= 0.0f;
		}

		void await_suspend(std::coroutine_handle<> Handle) const
		{
			UObject* ResolvedCtx = WorldContext.Get();
			if (!ResolvedCtx || !ResolvedCtx->GetWorld())
			{
				UE_LOG(LogAsyncFlow, Warning, TEXT("AudioDelay: null WorldContext or World — resuming immediately"));
				Handle.resume();
				return;
			}

			// Latent fast-path: register condition directly with the latent action
			FAsyncFlowLatentAction* LatentAction = Private::GetCurrentLatentAction();
			if (LatentAction)
			{
				StoredHandle = Handle;
				UWorld* W = ResolvedCtx->GetWorld();
				const double TargetTime = W->GetAudioTimeSeconds() + Seconds;
				TWeakObjectPtr<UObject> WeakCtx = WorldContext;
				LatentAction->SetCondition(
					[WeakCtx, TargetTime]() -> bool {
						UObject* Ctx = WeakCtx.Get();
						return !Ctx || !Ctx->GetWorld() || Ctx->GetWorld()->GetAudioTimeSeconds() >= TargetTime;
					},
					Handle, AliveFlag.Get());
				return;
			}

			UAsyncFlowTickSubsystem* Subsystem = ResolvedCtx->GetWorld()->GetSubsystem<UAsyncFlowTickSubsystem>();
			if (Subsystem)
			{
				StoredHandle = Handle;
				StoredSubsystem = Subsystem;
				Subsystem->ScheduleAudioDelay(Handle, Seconds, AliveFlag.Get());
			}
			else
			{
				Handle.resume();
			}
		}

		void CancelAwaiter()
		{
			if (StoredHandle)
			{
				if (StoredSubsystem)
				{
					StoredSubsystem->CancelHandle(StoredHandle);
					StoredSubsystem = nullptr;
				}
				auto H = StoredHandle;
				StoredHandle = nullptr;
				H.resume();
			}
		}

		void await_resume() const
		{
		}
	};

	/**
 * Suspend for Seconds using audio time.
 *
 * @param Seconds      Duration in audio-clock seconds.
 * @param WorldContext  Any UObject with a valid GetWorld(). Optional in latent mode.
 * @return             An awaiter — use with co_await.
 */
	[[nodiscard]] inline FAudioDelayAwaiter AudioDelay(float Seconds, UObject* WorldContext = nullptr)
	{
		FAudioDelayAwaiter Aw;
		Aw.WorldContext = WorldContext ? WorldContext : Private::GetCurrentWorldContext();
		Aw.Seconds = Seconds;
		return Aw;
	}

	/** @deprecated Use AudioDelay(Seconds) or AudioDelay(Seconds, WorldContext) instead. */
	[[deprecated("Use AudioDelay(Seconds) or AudioDelay(Seconds, WorldContext)")]] [[nodiscard]] inline FAudioDelayAwaiter AudioDelay(UObject* WorldContext, float Seconds)
	{
		return AudioDelay(Seconds, WorldContext);
	}

	// ============================================================================
	// UntilTime — suspend until an absolute game-time target
	// ============================================================================

	/**
	 * Awaiter that suspends until UWorld::GetTimeSeconds() >= TargetTime.
	 * Respects global time dilation and pause. If the target time has already
	 * passed, await_ready returns true (no-op).
	 */
	struct FUntilTimeAwaiter
	{
		TWeakObjectPtr<UObject> WorldContext;
		double TargetTime = 0.0;
		Private::FAwaiterAliveFlag AliveFlag;
		mutable std::coroutine_handle<> StoredHandle;
		mutable UAsyncFlowTickSubsystem* StoredSubsystem = nullptr;

		FUntilTimeAwaiter() = default;
		FUntilTimeAwaiter(FUntilTimeAwaiter&&) noexcept = default;
		FUntilTimeAwaiter& operator=(FUntilTimeAwaiter&&) noexcept = default;
		FUntilTimeAwaiter(const FUntilTimeAwaiter&) = delete;
		FUntilTimeAwaiter& operator=(const FUntilTimeAwaiter&) = delete;

		bool await_ready() const
		{
			UObject* Ctx = WorldContext.Get();
			if (!Ctx || !Ctx->GetWorld()) return true;
			return Ctx->GetWorld()->GetTimeSeconds() >= TargetTime;
		}

		void await_suspend(std::coroutine_handle<> Handle) const
		{
			UObject* ResolvedCtx = WorldContext.Get();
			if (!ResolvedCtx || !ResolvedCtx->GetWorld())
			{
				Handle.resume();
				return;
			}

			// Latent fast-path
			FAsyncFlowLatentAction* LatentAction = Private::GetCurrentLatentAction();
			if (LatentAction)
			{
				StoredHandle = Handle;
				TWeakObjectPtr<UObject> WeakCtx = WorldContext;
				const double Target = TargetTime;
				LatentAction->SetCondition(
					[WeakCtx, Target]() -> bool {
						UObject* Ctx = WeakCtx.Get();
						return !Ctx || !Ctx->GetWorld() || Ctx->GetWorld()->GetTimeSeconds() >= Target;
					},
					Handle, AliveFlag.Get());
				return;
			}

			UAsyncFlowTickSubsystem* Subsystem = ResolvedCtx->GetWorld()->GetSubsystem<UAsyncFlowTickSubsystem>();
			if (Subsystem)
			{
				StoredHandle = Handle;
				StoredSubsystem = Subsystem;
				Subsystem->ScheduleUntilTime(Handle, TargetTime, AliveFlag.Get());
			}
			else
			{
				Handle.resume();
			}
		}

		void CancelAwaiter()
		{
			if (StoredHandle)
			{
				if (StoredSubsystem)
				{
					StoredSubsystem->CancelHandle(StoredHandle);
					StoredSubsystem = nullptr;
				}
				auto H = StoredHandle;
				StoredHandle = nullptr;
				H.resume();
			}
		}

		void await_resume() const {}
	};

	/**
	 * Suspend until game time reaches TargetTime.
	 * @param InTargetTime  Absolute game-time target (UWorld::GetTimeSeconds()).
	 * @param WorldContext  Any UObject with a valid GetWorld(). Optional in latent mode.
	 */
	[[nodiscard]] inline FUntilTimeAwaiter UntilTime(double InTargetTime, UObject* WorldContext = nullptr)
	{
		FUntilTimeAwaiter Aw;
		Aw.WorldContext = WorldContext ? WorldContext : Private::GetCurrentWorldContext();
		Aw.TargetTime = InTargetTime;
		return Aw;
	}

	// ============================================================================
	// UntilRealTime — suspend until an absolute real-time target
	// ============================================================================

	/**
	 * Awaiter that suspends until FPlatformTime::Seconds() >= TargetTime.
	 * Ignores pause and time dilation.
	 */
	struct FUntilRealTimeAwaiter
	{
		TWeakObjectPtr<UObject> WorldContext;
		double TargetTime = 0.0;
		Private::FAwaiterAliveFlag AliveFlag;
		mutable std::coroutine_handle<> StoredHandle;
		mutable UAsyncFlowTickSubsystem* StoredSubsystem = nullptr;

		FUntilRealTimeAwaiter() = default;
		FUntilRealTimeAwaiter(FUntilRealTimeAwaiter&&) noexcept = default;
		FUntilRealTimeAwaiter& operator=(FUntilRealTimeAwaiter&&) noexcept = default;
		FUntilRealTimeAwaiter(const FUntilRealTimeAwaiter&) = delete;
		FUntilRealTimeAwaiter& operator=(const FUntilRealTimeAwaiter&) = delete;

		bool await_ready() const
		{
			return FPlatformTime::Seconds() >= TargetTime;
		}

		void await_suspend(std::coroutine_handle<> Handle) const
		{
			UObject* ResolvedCtx = WorldContext.Get();
			if (!ResolvedCtx || !ResolvedCtx->GetWorld())
			{
				Handle.resume();
				return;
			}

			// Latent fast-path
			FAsyncFlowLatentAction* LatentAction = Private::GetCurrentLatentAction();
			if (LatentAction)
			{
				StoredHandle = Handle;
				const double Target = TargetTime;
				LatentAction->SetCondition(
					[Target]() -> bool { return FPlatformTime::Seconds() >= Target; },
					Handle, AliveFlag.Get());
				return;
			}

			UAsyncFlowTickSubsystem* Subsystem = ResolvedCtx->GetWorld()->GetSubsystem<UAsyncFlowTickSubsystem>();
			if (Subsystem)
			{
				StoredHandle = Handle;
				StoredSubsystem = Subsystem;
				Subsystem->ScheduleUntilRealTime(Handle, TargetTime, AliveFlag.Get());
			}
			else
			{
				Handle.resume();
			}
		}

		void CancelAwaiter()
		{
			if (StoredHandle)
			{
				if (StoredSubsystem)
				{
					StoredSubsystem->CancelHandle(StoredHandle);
					StoredSubsystem = nullptr;
				}
				auto H = StoredHandle;
				StoredHandle = nullptr;
				H.resume();
			}
		}

		void await_resume() const {}
	};

	/**
	 * Suspend until real time reaches TargetTime.
	 * @param InTargetTime  Absolute real-time target (FPlatformTime::Seconds()).
	 * @param WorldContext  Any UObject with a valid GetWorld(). Optional in latent mode.
	 */
	[[nodiscard]] inline FUntilRealTimeAwaiter UntilRealTime(double InTargetTime, UObject* WorldContext = nullptr)
	{
		FUntilRealTimeAwaiter Aw;
		Aw.WorldContext = WorldContext ? WorldContext : Private::GetCurrentWorldContext();
		Aw.TargetTime = InTargetTime;
		return Aw;
	}

	// ============================================================================
	// UntilUnpausedTime — suspend until an absolute unpaused-time target
	// ============================================================================

	/**
	 * Awaiter that suspends until UWorld::GetUnpausedTimeSeconds() >= TargetTime.
	 * Ticks during pause, respects global dilation.
	 */
	struct FUntilUnpausedTimeAwaiter
	{
		TWeakObjectPtr<UObject> WorldContext;
		double TargetTime = 0.0;
		Private::FAwaiterAliveFlag AliveFlag;
		mutable std::coroutine_handle<> StoredHandle;
		mutable UAsyncFlowTickSubsystem* StoredSubsystem = nullptr;

		FUntilUnpausedTimeAwaiter() = default;
		FUntilUnpausedTimeAwaiter(FUntilUnpausedTimeAwaiter&&) noexcept = default;
		FUntilUnpausedTimeAwaiter& operator=(FUntilUnpausedTimeAwaiter&&) noexcept = default;
		FUntilUnpausedTimeAwaiter(const FUntilUnpausedTimeAwaiter&) = delete;
		FUntilUnpausedTimeAwaiter& operator=(const FUntilUnpausedTimeAwaiter&) = delete;

		bool await_ready() const
		{
			UObject* Ctx = WorldContext.Get();
			if (!Ctx || !Ctx->GetWorld()) return true;
			return Ctx->GetWorld()->GetUnpausedTimeSeconds() >= TargetTime;
		}

		void await_suspend(std::coroutine_handle<> Handle) const
		{
			UObject* ResolvedCtx = WorldContext.Get();
			if (!ResolvedCtx || !ResolvedCtx->GetWorld())
			{
				Handle.resume();
				return;
			}

			// Latent fast-path
			FAsyncFlowLatentAction* LatentAction = Private::GetCurrentLatentAction();
			if (LatentAction)
			{
				StoredHandle = Handle;
				TWeakObjectPtr<UObject> WeakCtx = WorldContext;
				const double Target = TargetTime;
				LatentAction->SetCondition(
					[WeakCtx, Target]() -> bool {
						UObject* Ctx = WeakCtx.Get();
						return !Ctx || !Ctx->GetWorld() || Ctx->GetWorld()->GetUnpausedTimeSeconds() >= Target;
					},
					Handle, AliveFlag.Get());
				return;
			}

			UAsyncFlowTickSubsystem* Subsystem = ResolvedCtx->GetWorld()->GetSubsystem<UAsyncFlowTickSubsystem>();
			if (Subsystem)
			{
				StoredHandle = Handle;
				StoredSubsystem = Subsystem;
				Subsystem->ScheduleUntilUnpausedTime(Handle, TargetTime, AliveFlag.Get());
			}
			else
			{
				Handle.resume();
			}
		}

		void CancelAwaiter()
		{
			if (StoredHandle)
			{
				if (StoredSubsystem)
				{
					StoredSubsystem->CancelHandle(StoredHandle);
					StoredSubsystem = nullptr;
				}
				auto H = StoredHandle;
				StoredHandle = nullptr;
				H.resume();
			}
		}

		void await_resume() const {}
	};

	/**
	 * Suspend until unpaused time reaches TargetTime.
	 * @param InTargetTime  Absolute unpaused-time target (UWorld::GetUnpausedTimeSeconds()).
	 * @param WorldContext  Any UObject with a valid GetWorld(). Optional in latent mode.
	 */
	[[nodiscard]] inline FUntilUnpausedTimeAwaiter UntilUnpausedTime(double InTargetTime, UObject* WorldContext = nullptr)
	{
		FUntilUnpausedTimeAwaiter Aw;
		Aw.WorldContext = WorldContext ? WorldContext : Private::GetCurrentWorldContext();
		Aw.TargetTime = InTargetTime;
		return Aw;
	}

	// ============================================================================
	// UntilAudioTime — suspend until an absolute audio-time target
	// ============================================================================

	/**
	 * Awaiter that suspends until UWorld::GetAudioTimeSeconds() >= TargetTime.
	 * Uses the audio engine clock.
	 */
	struct FUntilAudioTimeAwaiter
	{
		TWeakObjectPtr<UObject> WorldContext;
		double TargetTime = 0.0;
		Private::FAwaiterAliveFlag AliveFlag;
		mutable std::coroutine_handle<> StoredHandle;
		mutable UAsyncFlowTickSubsystem* StoredSubsystem = nullptr;

		FUntilAudioTimeAwaiter() = default;
		FUntilAudioTimeAwaiter(FUntilAudioTimeAwaiter&&) noexcept = default;
		FUntilAudioTimeAwaiter& operator=(FUntilAudioTimeAwaiter&&) noexcept = default;
		FUntilAudioTimeAwaiter(const FUntilAudioTimeAwaiter&) = delete;
		FUntilAudioTimeAwaiter& operator=(const FUntilAudioTimeAwaiter&) = delete;

		bool await_ready() const
		{
			UObject* Ctx = WorldContext.Get();
			if (!Ctx || !Ctx->GetWorld()) return true;
			return Ctx->GetWorld()->GetAudioTimeSeconds() >= TargetTime;
		}

		void await_suspend(std::coroutine_handle<> Handle) const
		{
			UObject* ResolvedCtx = WorldContext.Get();
			if (!ResolvedCtx || !ResolvedCtx->GetWorld())
			{
				Handle.resume();
				return;
			}

			// Latent fast-path
			FAsyncFlowLatentAction* LatentAction = Private::GetCurrentLatentAction();
			if (LatentAction)
			{
				StoredHandle = Handle;
				TWeakObjectPtr<UObject> WeakCtx = WorldContext;
				const double Target = TargetTime;
				LatentAction->SetCondition(
					[WeakCtx, Target]() -> bool {
						UObject* Ctx = WeakCtx.Get();
						return !Ctx || !Ctx->GetWorld() || Ctx->GetWorld()->GetAudioTimeSeconds() >= Target;
					},
					Handle, AliveFlag.Get());
				return;
			}

			UAsyncFlowTickSubsystem* Subsystem = ResolvedCtx->GetWorld()->GetSubsystem<UAsyncFlowTickSubsystem>();
			if (Subsystem)
			{
				StoredHandle = Handle;
				StoredSubsystem = Subsystem;
				Subsystem->ScheduleUntilAudioTime(Handle, TargetTime, AliveFlag.Get());
			}
			else
			{
				Handle.resume();
			}
		}

		void CancelAwaiter()
		{
			if (StoredHandle)
			{
				if (StoredSubsystem)
				{
					StoredSubsystem->CancelHandle(StoredHandle);
					StoredSubsystem = nullptr;
				}
				auto H = StoredHandle;
				StoredHandle = nullptr;
				H.resume();
			}
		}

		void await_resume() const {}
	};

	/**
	 * Suspend until audio time reaches TargetTime.
	 * @param InTargetTime  Absolute audio-time target (UWorld::GetAudioTimeSeconds()).
	 * @param WorldContext  Any UObject with a valid GetWorld(). Optional in latent mode.
	 */
	[[nodiscard]] inline FUntilAudioTimeAwaiter UntilAudioTime(double InTargetTime, UObject* WorldContext = nullptr)
	{
		FUntilAudioTimeAwaiter Aw;
		Aw.WorldContext = WorldContext ? WorldContext : Private::GetCurrentWorldContext();
		Aw.TargetTime = InTargetTime;
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
		TWeakObjectPtr<AActor> Actor;
		float Seconds = 0.0f;
		Private::FAwaiterAliveFlag AliveFlag;
		mutable std::coroutine_handle<> StoredHandle;
		mutable UAsyncFlowTickSubsystem* StoredSubsystem = nullptr;

		FActorDilatedDelayAwaiter() = default;
		FActorDilatedDelayAwaiter(FActorDilatedDelayAwaiter&&) noexcept = default;
		FActorDilatedDelayAwaiter& operator=(FActorDilatedDelayAwaiter&&) noexcept = default;
		FActorDilatedDelayAwaiter(const FActorDilatedDelayAwaiter&) = delete;
		FActorDilatedDelayAwaiter& operator=(const FActorDilatedDelayAwaiter&) = delete;

		bool await_ready() const
		{
			return Seconds <= 0.0f || !Actor.IsValid();
		}

		void await_suspend(std::coroutine_handle<> Handle) const
		{
			AActor* ResolvedActor = Actor.Get();
			if (!ResolvedActor || !ResolvedActor->GetWorld())
			{
				UE_LOG(LogAsyncFlow, Warning, TEXT("SecondsForActor: null Actor or World — resuming immediately"));
				Handle.resume();
				return;
			}
			UAsyncFlowTickSubsystem* Subsystem = ResolvedActor->GetWorld()->GetSubsystem<UAsyncFlowTickSubsystem>();
			if (Subsystem)
			{
				StoredHandle = Handle;
				StoredSubsystem = Subsystem;
				Subsystem->ScheduleActorDilatedDelay(Handle, ResolvedActor, Seconds, AliveFlag.Get());
			}
			else
			{
				Handle.resume();
			}
		}

		void CancelAwaiter()
		{
			if (StoredHandle)
			{
				if (StoredSubsystem)
				{
					StoredSubsystem->CancelHandle(StoredHandle);
					StoredSubsystem = nullptr;
				}
				auto H = StoredHandle;
				StoredHandle = nullptr;
				H.resume();
			}
		}

		void await_resume() const
		{
		}
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
		TWeakObjectPtr<UObject> WorldContext;

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
	 * @param Ms             Budget in milliseconds per tick (e.g. 2.0 = 2ms).
	 * @param InWorldContext  Any UObject with a valid GetWorld(). Optional in latent mode.
	 * @return               A configured FTickTimeBudget awaiter.
	 */
		static FTickTimeBudget Milliseconds(double Ms, UObject* InWorldContext = nullptr)
		{
			FTickTimeBudget Budget;
			Budget.WorldContext = InWorldContext ? InWorldContext : Private::GetCurrentWorldContext();
			Budget.BudgetSeconds = Ms * 0.001;
			return Budget;
		}

		/** @deprecated Use Milliseconds(Ms) or Milliseconds(Ms, WorldContext) instead. */
		[[deprecated("Use Milliseconds(Ms) or Milliseconds(Ms, WorldContext)")]]
		static FTickTimeBudget Milliseconds(UObject* InWorldContext, double Ms)
		{
			return Milliseconds(Ms, InWorldContext);
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
			UObject* ResolvedCtx = WorldContext.Get();
			if (!ResolvedCtx)
			{
				Handle.resume();
				return;
			}
			UWorld* World = ResolvedCtx->GetWorld();
			if (!World)
			{
				Handle.resume();
				return;
			}
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
