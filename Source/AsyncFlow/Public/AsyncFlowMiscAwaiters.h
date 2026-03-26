// AsyncFlowMiscAwaiters.h — Utility awaiters (Timeline, MoveComponent, Niagara, Widget animation)
#pragma once

#include "AsyncFlowTask.h"
#include "AsyncFlowTickSubsystem.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Templates/Function.h"
#include "Math/UnrealMathUtility.h"

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
// Timeline — per-tick interpolation from Start to End over Duration
// ============================================================================

struct FTimelineAwaiter
{
	UObject* WorldContext = nullptr;
	float From = 0.0f;
	float To = 1.0f;
	float Duration = 1.0f;
	TFunction<void(float)> UpdateCallback;

	bool await_ready() const { return Duration <= 0.0f; }

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

		Subsystem->ScheduleTickUpdate(Handle,
			[State](float DeltaTime) -> bool
			{
				State->Elapsed += DeltaTime;
				const float Alpha = FMath::Clamp(State->Elapsed / State->Duration, 0.0f, 1.0f);
				const float Value = FMath::Lerp(State->From, State->To, Alpha);

				if (State->Callback)
				{
					State->Callback(Value);
				}

				return Alpha >= 1.0f;
			}
		);
	}

	void await_resume() const {}
};

/**
 * Run a timeline interpolation from From to To over Duration seconds.
 * UpdateCallback is called each tick with the interpolated value.
 */
[[nodiscard]] inline FTimelineAwaiter Timeline(UObject* WorldContext, float From, float To, float Duration, TFunction<void(float)> UpdateCallback)
{
	return FTimelineAwaiter{WorldContext, From, To, Duration, MoveTemp(UpdateCallback)};
}

// ============================================================================
// MoveComponentTo — smoothly move a scene component to target location/rotation
// ============================================================================

struct FMoveComponentToAwaiter
{
	USceneComponent* Component = nullptr;
	FVector TargetLocation;
	FRotator TargetRotation;
	float Duration = 1.0f;
	bool bEaseIn = true;
	bool bEaseOut = true;

	bool await_ready() const { return Duration <= 0.0f || !Component; }

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
			FRotator StartRot;
			FVector EndLoc;
			FRotator EndRot;
			float Duration;
			float Elapsed = 0.0f;
			bool bEaseIn;
			bool bEaseOut;
		};

		TSharedPtr<FState> State = MakeShared<FState>();
		State->Comp = Component;
		State->StartLoc = Component->GetComponentLocation();
		State->StartRot = Component->GetComponentRotation();
		State->EndLoc = TargetLocation;
		State->EndRot = TargetRotation;
		State->Duration = Duration;
		State->bEaseIn = bEaseIn;
		State->bEaseOut = bEaseOut;

		Subsystem->ScheduleTickUpdate(Handle,
			[State](float DeltaTime) -> bool
			{
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
				const FRotator Rot = FMath::Lerp(State->StartRot, State->EndRot, Alpha);
				State->Comp->SetWorldLocationAndRotation(Loc, Rot);

				return Alpha >= 1.0f;
			}
		);
	}

	void await_resume() const {}
};

/** Smoothly move a scene component to a target location and rotation over Duration seconds. */
[[nodiscard]] inline FMoveComponentToAwaiter MoveComponentTo(
	USceneComponent* Component,
	const FVector& TargetLocation,
	const FRotator& TargetRotation,
	float Duration = 1.0f,
	bool bEaseIn = true,
	bool bEaseOut = true)
{
	return FMoveComponentToAwaiter{Component, TargetLocation, TargetRotation, Duration, bEaseIn, bEaseOut};
}

// ============================================================================
// WaitForNiagaraComplete — waits for a Niagara particle system to finish
// ============================================================================

#if ASYNCFLOW_HAS_NIAGARA

struct FWaitNiagaraCompleteAwaiter
{
	UNiagaraComponent* NiagaraComponent = nullptr;
	std::coroutine_handle<> Continuation;

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

		// FOnNiagaraSystemFinished is DYNAMIC — no AddLambda. Poll IsActive() each tick.
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
		Subsystem->ScheduleCondition(Handle, NiagaraComponent, [WeakComp]() -> bool
		{
			return !WeakComp.IsValid() || !WeakComp->IsActive();
		});
	}

	void await_resume() const {}
};

/** Wait for a Niagara particle system to finish. */
[[nodiscard]] inline FWaitNiagaraCompleteAwaiter WaitForNiagaraComplete(UNiagaraComponent* NiagaraComponent)
{
	return FWaitNiagaraCompleteAwaiter{NiagaraComponent};
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

	bool await_ready() const
	{
		return !Widget || !Animation || !Widget->IsAnimationPlaying(Animation);
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

		// FWidgetAnimationDynamicEvent is DYNAMIC — use polling instead.
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
		Subsystem->ScheduleCondition(Handle, Widget, [WeakWidget, WeakAnim]() -> bool
		{
			return !WeakWidget.IsValid() || !WeakAnim.IsValid() || !WeakWidget->IsAnimationPlaying(WeakAnim.Get());
		});
	}

	void await_resume() const {}
};

/** Play a widget animation and wait for it to finish. */
[[nodiscard]] inline FPlayWidgetAnimationAndWaitAwaiter PlayWidgetAnimationAndWait(UUserWidget* Widget, UWidgetAnimation* Animation)
{
	return FPlayWidgetAnimationAndWaitAwaiter{Widget, Animation};
}

#endif // ASYNCFLOW_HAS_UMG

} // namespace AsyncFlow

