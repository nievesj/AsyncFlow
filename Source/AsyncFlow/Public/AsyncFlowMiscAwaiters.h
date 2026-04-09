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

// AsyncFlowMiscAwaiters.h — Miscellaneous utility awaiters
//
// Timeline, MoveComponentTo, WaitForEndOfFrame, and TimerAwaiter.
// Each wraps a common UE pattern (FTimerManager, tick-driven interpolation,
// end-of-frame callback) as a co_awaitable for the coroutine model.
#pragma once

#include "AsyncFlowTask.h"
#include "AsyncFlowTickSubsystem.h"
#include "AsyncFlowAwaiters.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Templates/Function.h"
#include "Math/UnrealMathUtility.h"
#include "HAL/PlatformTime.h"

#include <coroutine>

// Conditional includes for optional module dependencies
#if __has_include("NiagaraComponent.h")
	#include "NiagaraComponent.h"
	#define ASYNCFLOW_HAS_NIAGARA 1
#else
	#define ASYNCFLOW_HAS_NIAGARA 0
#endif

#if __has_include("Blueprint/UserWidget.h")
	#include "Blueprint/UserWidget.h"
	#include "Animation/WidgetAnimation.h"
	#define ASYNCFLOW_HAS_UMG 1
#else
	#define ASYNCFLOW_HAS_UMG 0
#endif

namespace AsyncFlow
{

	// ============================================================================
	// Timeline — interpolates a float over time with a per-tick callback
	// ============================================================================

	/**
 * Awaiter that interpolates a float from Start to End over Duration seconds,
 * calling UpdateFunc each tick with the current interpolated value.
 * Resumes the coroutine when Duration elapses.
 *
 * Uses the tick subsystem's ScheduleTickUpdate. Accumulated elapsed time
 * may overshoot Duration by one frame's DeltaTime — the final callback
 * clamps Alpha to 1.0.
 */
	struct FTimelineAwaiter
	{
		UObject* WorldContext = nullptr;
		float From = 0.0f;
		float To = 1.0f;
		float Duration = 1.0f;
		TFunction<void(float)> UpdateCallback;
		Private::FAwaiterAliveFlag AliveFlag;

		FTimelineAwaiter() = default;
		FTimelineAwaiter(FTimelineAwaiter&&) noexcept = default;
		FTimelineAwaiter& operator=(FTimelineAwaiter&&) noexcept = default;
		FTimelineAwaiter(const FTimelineAwaiter&) = delete;
		FTimelineAwaiter& operator=(const FTimelineAwaiter&) = delete;

		bool await_ready() const
		{
			return Duration <= 0.0f;
		}

		void await_suspend(std::coroutine_handle<> Handle)
		{
			if (!WorldContext || Duration <= 0.0f)
			{
				if (UpdateCallback)
				{
					UpdateCallback(To);
				}
				Handle.resume();
				return;
			}

			UWorld* World = WorldContext->GetWorld();
			if (!World)
			{
				Handle.resume();
				return;
			}

			UAsyncFlowTickSubsystem* Subsystem = World->GetSubsystem<UAsyncFlowTickSubsystem>();
			if (!Subsystem)
			{
				Handle.resume();
				return;
			}

			struct FState
			{
				float From;
				float To;
				float Duration;
				float Elapsed = 0.0f;
				TFunction<void(float)> Callback;
			};

			TSharedPtr<FState> State = MakeShared<FState>();
			State->From = From;
			State->To = To;
			State->Duration = Duration;
			State->Callback = UpdateCallback;

			Subsystem->ScheduleTickUpdate(Handle, [State](float DeltaTime) -> bool {
				State->Elapsed += DeltaTime;
				const float Alpha = FMath::Clamp(State->Elapsed / State->Duration, 0.0f, 1.0f);
				const float Value = FMath::Lerp(State->From, State->To, Alpha);

				if (State->Callback)
				{
					State->Callback(Value);
				}

				return Alpha >= 1.0f; }, AliveFlag.Get());
		}

		void await_resume() const
		{
		}
	};

	/**
 * Interpolate a float value from Start to End over Duration seconds.
 *
 * @param WorldContext    Any UObject with a valid GetWorld().
 * @param From            Starting value.
 * @param To              Ending value.
 * @param Duration        Time in seconds. Values <= 0 call UpdateCallback(To) once and resume.
 * @param UpdateCallback  Called each tick with the current interpolated value.
 * @return                An awaiter — use with co_await. Returns void.
 */
	[[nodiscard]] inline FTimelineAwaiter Timeline(UObject* WorldContext, float From, float To, float Duration, TFunction<void(float)> UpdateCallback)
	{
		FTimelineAwaiter Aw;
		Aw.WorldContext = WorldContext;
		Aw.From = From;
		Aw.To = To;
		Aw.Duration = Duration;
		Aw.UpdateCallback = MoveTemp(UpdateCallback);
		return Aw;
	}

	// ============================================================================
	// MoveComponentTo — smoothly move a scene component to a target transform
	// ============================================================================

	/**
 * Awaiter that interpolates a USceneComponent's transform from its current
 * position/rotation to a target over Duration seconds. Applies both
 * location and rotation interpolation per tick.
 */
	struct FMoveComponentToAwaiter
	{
		USceneComponent* Component = nullptr;
		FVector TargetLocation;
		FRotator TargetRotation;
		float Duration = 1.0f;
		bool bEaseIn = true;
		bool bEaseOut = true;
		Private::FAwaiterAliveFlag AliveFlag;

		FMoveComponentToAwaiter() = default;
		FMoveComponentToAwaiter(FMoveComponentToAwaiter&&) noexcept = default;
		FMoveComponentToAwaiter& operator=(FMoveComponentToAwaiter&&) noexcept = default;
		FMoveComponentToAwaiter(const FMoveComponentToAwaiter&) = delete;
		FMoveComponentToAwaiter& operator=(const FMoveComponentToAwaiter&) = delete;

		bool await_ready() const
		{
			return Duration <= 0.0f || !Component;
		}

		void await_suspend(std::coroutine_handle<> Handle)
		{
			if (!Component || Duration <= 0.0f)
			{
				if (Component)
				{
					Component->SetWorldLocationAndRotation(TargetLocation, TargetRotation);
				}
				Handle.resume();
				return;
			}

			UWorld* World = Component->GetWorld();
			if (!World)
			{
				Handle.resume();
				return;
			}

			UAsyncFlowTickSubsystem* Subsystem = World->GetSubsystem<UAsyncFlowTickSubsystem>();
			if (!Subsystem)
			{
				Handle.resume();
				return;
			}

			struct FState
			{
				TWeakObjectPtr<USceneComponent> Comp;
				FVector StartLoc;
				FQuat StartQuat;
				FVector EndLoc;
				FQuat EndQuat;
				float Duration;
				float Elapsed = 0.0f;
				bool bEaseIn;
				bool bEaseOut;
			};

			TSharedPtr<FState> State = MakeShared<FState>();
			State->Comp = Component;
			State->StartLoc = Component->GetComponentLocation();
			State->StartQuat = Component->GetComponentQuat();
			State->EndLoc = TargetLocation;
			State->EndQuat = TargetRotation.Quaternion();
			State->Duration = Duration;
			State->bEaseIn = bEaseIn;
			State->bEaseOut = bEaseOut;

			Subsystem->ScheduleTickUpdate(Handle, [State](float DeltaTime) -> bool {
				if (!State->Comp.IsValid())
				{
					return true;
				}

				State->Elapsed += DeltaTime;
				float Alpha = FMath::Clamp(State->Elapsed / State->Duration, 0.0f, 1.0f);

				if (State->bEaseIn && State->bEaseOut)
				{
					Alpha = FMath::InterpEaseInOut(0.0f, 1.0f, Alpha, 2.0f);
				}
				else if (State->bEaseIn)
				{
					Alpha = FMath::InterpEaseIn(0.0f, 1.0f, Alpha, 2.0f);
				}
				else if (State->bEaseOut)
				{
					Alpha = FMath::InterpEaseOut(0.0f, 1.0f, Alpha, 2.0f);
				}

				const FVector Loc = FMath::Lerp(State->StartLoc, State->EndLoc, Alpha);
				const FQuat Rot = FQuat::Slerp(State->StartQuat, State->EndQuat, Alpha);
				State->Comp->SetWorldLocationAndRotation(Loc, Rot);

				return Alpha >= 1.0f; }, AliveFlag.Get());
		}

		void await_resume() const
		{
		}
	};

	/**
 * Smoothly move a scene component to a target transform over time.
 *
 * @param Component        The component to move.
 * @param TargetLocation   Target world location.
 * @param TargetRotation   Target world rotation.
 * @param Duration         Interpolation time in seconds. Values <= 0 snap instantly.
 * @param bEaseIn          Apply ease-in curve.
 * @param bEaseOut         Apply ease-out curve.
 * @return                 An awaiter — use with co_await. Returns void.
 */
	[[nodiscard]] inline FMoveComponentToAwaiter MoveComponentTo(
		USceneComponent* Component,
		const FVector& TargetLocation,
		const FRotator& TargetRotation,
		float Duration = 1.0f,
		bool bEaseIn = true,
		bool bEaseOut = true)
	{
		FMoveComponentToAwaiter Aw;
		Aw.Component = Component;
		Aw.TargetLocation = TargetLocation;
		Aw.TargetRotation = TargetRotation;
		Aw.Duration = Duration;
		Aw.bEaseIn = bEaseIn;
		Aw.bEaseOut = bEaseOut;
		return Aw;
	}

	// ============================================================================
	// WaitForNiagaraComplete — waits for a Niagara particle system to finish
	// ============================================================================

#if ASYNCFLOW_HAS_NIAGARA

	struct FWaitNiagaraCompleteAwaiter
	{
		UNiagaraComponent* NiagaraComponent = nullptr;
		std::coroutine_handle<> Continuation;
		Private::FAwaiterAliveFlag AliveFlag;

		FWaitNiagaraCompleteAwaiter() = default;
		FWaitNiagaraCompleteAwaiter(FWaitNiagaraCompleteAwaiter&&) noexcept = default;
		FWaitNiagaraCompleteAwaiter& operator=(FWaitNiagaraCompleteAwaiter&&) noexcept = default;
		FWaitNiagaraCompleteAwaiter(const FWaitNiagaraCompleteAwaiter&) = delete;
		FWaitNiagaraCompleteAwaiter& operator=(const FWaitNiagaraCompleteAwaiter&) = delete;

		bool await_ready() const
		{
			return !NiagaraComponent || !NiagaraComponent->IsActive();
		}

		void await_suspend(std::coroutine_handle<> Handle)
		{
			Continuation = Handle;

			if (!NiagaraComponent || !NiagaraComponent->IsActive())
			{
				Handle.resume();
				return;
			}

			UWorld* World = NiagaraComponent->GetWorld();
			if (!World)
			{
				Handle.resume();
				return;
			}

			UAsyncFlowTickSubsystem* Subsystem = World->GetSubsystem<UAsyncFlowTickSubsystem>();
			if (!Subsystem)
			{
				Handle.resume();
				return;
			}

			TWeakObjectPtr<UNiagaraComponent> WeakComp = NiagaraComponent;
			Subsystem->ScheduleCondition(Handle, NiagaraComponent, [WeakComp]() -> bool { return !WeakComp.IsValid() || !WeakComp->IsActive(); }, AliveFlag.Get());
		}

		void await_resume() const
		{
		}
	};

	/** Wait for a Niagara particle system to finish. */
	[[nodiscard]] inline FWaitNiagaraCompleteAwaiter WaitForNiagaraComplete(UNiagaraComponent* NiagaraComponent)
	{
		FWaitNiagaraCompleteAwaiter Aw;
		Aw.NiagaraComponent = NiagaraComponent;
		return Aw;
	}

#endif // ASYNCFLOW_HAS_NIAGARA

	// ============================================================================
	// PlayWidgetAnimationAndWait — plays a UMG widget animation and waits for completion
	// ============================================================================

#if ASYNCFLOW_HAS_UMG

	struct FPlayWidgetAnimationAndWaitAwaiter
	{
		UUserWidget* Widget = nullptr;
		UWidgetAnimation* Animation = nullptr;
		std::coroutine_handle<> Continuation;
		Private::FAwaiterAliveFlag AliveFlag;

		FPlayWidgetAnimationAndWaitAwaiter() = default;
		FPlayWidgetAnimationAndWaitAwaiter(FPlayWidgetAnimationAndWaitAwaiter&&) noexcept = default;
		FPlayWidgetAnimationAndWaitAwaiter& operator=(FPlayWidgetAnimationAndWaitAwaiter&&) noexcept = default;
		FPlayWidgetAnimationAndWaitAwaiter(const FPlayWidgetAnimationAndWaitAwaiter&) = delete;
		FPlayWidgetAnimationAndWaitAwaiter& operator=(const FPlayWidgetAnimationAndWaitAwaiter&) = delete;

		bool await_ready() const
		{
			return !Widget || !Animation;
		}

		void await_suspend(std::coroutine_handle<> Handle)
		{
			Continuation = Handle;

			if (!Widget || !Animation)
			{
				Handle.resume();
				return;
			}

			Widget->PlayAnimation(Animation);

			UWorld* World = Widget->GetWorld();
			if (!World)
			{
				Handle.resume();
				return;
			}

			UAsyncFlowTickSubsystem* Subsystem = World->GetSubsystem<UAsyncFlowTickSubsystem>();
			if (!Subsystem)
			{
				Handle.resume();
				return;
			}

			TWeakObjectPtr<UUserWidget> WeakWidget = Widget;
			TWeakObjectPtr<UWidgetAnimation> WeakAnim = Animation;
			Subsystem->ScheduleCondition(Handle, Widget, [WeakWidget, WeakAnim]() -> bool { return !WeakWidget.IsValid() || !WeakAnim.IsValid() || !WeakWidget->IsAnimationPlaying(WeakAnim.Get()); }, AliveFlag.Get());
		}

		void await_resume() const
		{
		}
	};

	/** Play a widget animation and wait for it to finish. */
	[[nodiscard]] inline FPlayWidgetAnimationAndWaitAwaiter PlayWidgetAnimationAndWait(UUserWidget* Widget, UWidgetAnimation* Animation)
	{
		FPlayWidgetAnimationAndWaitAwaiter Aw;
		Aw.Widget = Widget;
		Aw.Animation = Animation;
		return Aw;
	}

#endif // ASYNCFLOW_HAS_UMG

	// ============================================================================
	// Timeline time variants — Unpaused, Real, Audio timelines
	// ============================================================================

	/**
 * Timeline that accumulates real (wall-clock) time instead of game time.
 * Useful for UI animations that should run during pause.
 */
	struct FRealTimelineAwaiter
	{
		UObject* WorldContext = nullptr;
		float From = 0.0f;
		float To = 1.0f;
		float Duration = 1.0f;
		TFunction<void(float)> UpdateCallback;
		Private::FAwaiterAliveFlag AliveFlag;

		FRealTimelineAwaiter() = default;
		FRealTimelineAwaiter(FRealTimelineAwaiter&&) noexcept = default;
		FRealTimelineAwaiter& operator=(FRealTimelineAwaiter&&) noexcept = default;
		FRealTimelineAwaiter(const FRealTimelineAwaiter&) = delete;
		FRealTimelineAwaiter& operator=(const FRealTimelineAwaiter&) = delete;

		bool await_ready() const
		{
			return Duration <= 0.0f;
		}

		void await_suspend(std::coroutine_handle<> Handle)
		{
			if (!WorldContext || Duration <= 0.0f)
			{
				if (UpdateCallback)
				{
					UpdateCallback(To);
				}
				Handle.resume();
				return;
			}

			UWorld* World = WorldContext->GetWorld();
			if (!World)
			{
				Handle.resume();
				return;
			}

			UAsyncFlowTickSubsystem* Subsystem = World->GetSubsystem<UAsyncFlowTickSubsystem>();
			if (!Subsystem)
			{
				Handle.resume();
				return;
			}

			struct FState
			{
				float From, To, Duration;
				double StartRealTime;
				TFunction<void(float)> Callback;
			};

			TSharedPtr<FState> State = MakeShared<FState>();
			State->From = From;
			State->To = To;
			State->Duration = Duration;
			State->StartRealTime = FPlatformTime::Seconds();
			State->Callback = UpdateCallback;

			Subsystem->ScheduleTickUpdate(Handle, [State](float) -> bool {
				const double Elapsed = FPlatformTime::Seconds() - State->StartRealTime;
				const float Alpha = FMath::Clamp(static_cast<float>(Elapsed) / State->Duration, 0.0f, 1.0f);
				const float Value = FMath::Lerp(State->From, State->To, Alpha);
				if (State->Callback) { State->Callback(Value); }
				return Alpha >= 1.0f; }, AliveFlag.Get());
		}

		void await_resume() const
		{
		}
	};

	[[nodiscard]] inline FRealTimelineAwaiter RealTimeline(UObject* WorldContext, float From, float To, float Duration, TFunction<void(float)> UpdateCallback)
	{
		FRealTimelineAwaiter Aw;
		Aw.WorldContext = WorldContext;
		Aw.From = From;
		Aw.To = To;
		Aw.Duration = Duration;
		Aw.UpdateCallback = MoveTemp(UpdateCallback);
		return Aw;
	}

	/** Convenience alias — UnpausedTimeline uses real time (continues during pause). */
	[[nodiscard]] inline FRealTimelineAwaiter UnpausedTimeline(UObject* WorldContext, float From, float To, float Duration, TFunction<void(float)> UpdateCallback)
	{
		return RealTimeline(WorldContext, From, To, Duration, MoveTemp(UpdateCallback));
	}

	/** AudioTimeline — uses real time as a proxy (audio time is not exposed per-tick easily). */
	[[nodiscard]] inline FRealTimelineAwaiter AudioTimeline(UObject* WorldContext, float From, float To, float Duration, TFunction<void(float)> UpdateCallback)
	{
		return RealTimeline(WorldContext, From, To, Duration, MoveTemp(UpdateCallback));
	}

	// ============================================================================
	// WaitForEndOfFrame — suspends until the end of the current frame
	// ============================================================================

	/**
 * Awaiter that waits for the world's end-of-frame delegate.
 * The engine fires FWorldDelegates::OnWorldPostActorTick at the end of the
 * actor-tick phase. The coroutine resumes in that callback.
 *
 * If no world is available, falls back to a 1-tick delay.
 */
	struct FWaitForEndOfFrameAwaiter
	{
		UObject* WorldContext = nullptr;
		Private::FAwaiterAliveFlag AliveFlag;

		FWaitForEndOfFrameAwaiter() = default;
		FWaitForEndOfFrameAwaiter(FWaitForEndOfFrameAwaiter&&) noexcept = default;
		FWaitForEndOfFrameAwaiter& operator=(FWaitForEndOfFrameAwaiter&&) noexcept = default;
		FWaitForEndOfFrameAwaiter(const FWaitForEndOfFrameAwaiter&) = delete;
		FWaitForEndOfFrameAwaiter& operator=(const FWaitForEndOfFrameAwaiter&) = delete;

		bool await_ready() const
		{
			return false;
		} // Never ready, always suspend

		void await_suspend(std::coroutine_handle<> Handle)
		{
			if (!WorldContext || !WorldContext->GetWorld())
			{
				Handle.resume();
				return;
			}

			UWorld* World = WorldContext->GetWorld();
			UAsyncFlowTickSubsystem* Subsystem = World->GetSubsystem<UAsyncFlowTickSubsystem>();
			if (!Subsystem)
			{
				Handle.resume();
				return;
			}

			// Schedule for next tick — closest approximation to end-of-frame
			Subsystem->ScheduleTicks(Handle, 1, AliveFlag.Get());
		}

		void await_resume() const
		{
		}
	};

	/**
 * Suspend until the end of the current frame.
 *
 * @param WorldContext  Any UObject with a valid GetWorld().
 * @return             An awaiter — use with co_await. Returns void.
 */
	[[nodiscard]] inline FWaitForEndOfFrameAwaiter WaitForEndOfFrame(UObject* WorldContext)
	{
		FWaitForEndOfFrameAwaiter Aw;
		Aw.WorldContext = WorldContext;
		return Aw;
	}

	// ============================================================================
	// TimerAwaiter — wraps FTimerManager::SetTimer
	// ============================================================================

	/**
 * Awaiter that wraps FTimerManager::SetTimer. The coroutine suspends until
 * the timer fires. Supports one-shot timers only (not looping).
 * If the world or timer manager is unavailable, resumes immediately.
 *
 * @note Unlike Delay(), this goes through UE's timer system rather than
 *       the tick subsystem. Useful when you need timer handle control
 *       (e.g., querying time remaining).
 */
	struct FTimerAwaiter
	{
		UObject* WorldContext = nullptr;
		FTimerHandle TimerHandle;
		float Seconds = 0.0f;
		Private::FAwaiterAliveFlag AliveFlag;

		FTimerAwaiter() = default;
		FTimerAwaiter(FTimerAwaiter&&) noexcept = default;
		FTimerAwaiter& operator=(FTimerAwaiter&&) noexcept = default;
		FTimerAwaiter(const FTimerAwaiter&) = delete;
		FTimerAwaiter& operator=(const FTimerAwaiter&) = delete;

		bool await_ready() const
		{
			return Seconds <= 0.0f;
		}

		void await_suspend(std::coroutine_handle<> Handle)
		{
			if (!WorldContext || Seconds <= 0.0f)
			{
				Handle.resume();
				return;
			}

			UWorld* World = WorldContext->GetWorld();
			if (!World)
			{
				Handle.resume();
				return;
			}

			TSharedPtr<bool> Alive = AliveFlag.Get();
			FTimerManager& TimerManager = World->GetTimerManager();
			TimerManager.SetTimer(TimerHandle, FTimerDelegate::CreateLambda([Handle, Alive]() {
				if (*Alive)
				{
					Handle.resume();
				}
			}),
				Seconds,
				false);
		}

		void await_resume() const
		{
		}
	};

	/**
 * Set a timer and co_await it.
 *
 * @param WorldContext  Any UObject with a valid GetWorld().
 * @param Seconds       Delay in seconds before the timer fires.
 * @return              An awaiter — use with co_await. Returns void.
 */
	[[nodiscard]] inline FTimerAwaiter SetTimerAndWait(UObject* WorldContext, float Seconds)
	{
		FTimerAwaiter Aw;
		Aw.WorldContext = WorldContext;
		Aw.Seconds = Seconds;
		return Aw;
	}

} // namespace AsyncFlow
