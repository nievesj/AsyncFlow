// AsyncFlowAnimAwaiters.h — Animation montage and notify awaiters
#pragma once

#include "AsyncFlowTask.h"
#include "AsyncFlowTickSubsystem.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Engine/World.h"

#include <coroutine>

namespace AsyncFlow
{

// ============================================================================
// PlayMontageAndWait — plays a montage, resumes when it ends or is interrupted
// ============================================================================

struct FPlayMontageAwaiter
{
	UAnimInstance* AnimInstance = nullptr;
	UAnimMontage* Montage = nullptr;
	float PlayRate = 1.0f;
	FName StartSection = NAME_None;
	bool bCompleted = false;
	std::coroutine_handle<> Continuation;
	TSharedPtr<bool> AliveFlag = MakeShared<bool>(true);

	~FPlayMontageAwaiter() { *AliveFlag = false; }

	bool await_ready() const { return false; }

	void await_suspend(std::coroutine_handle<> Handle)
	{
		Continuation = Handle;

		if (!AnimInstance || !Montage)
		{
			bCompleted = false;
			Handle.resume();
			return;
		}

		FOnMontageEnded EndedDelegate;
		TWeakPtr<bool> WeakAlive = AliveFlag;
		EndedDelegate.BindLambda([this, WeakAlive](UAnimMontage* InMontage, bool bInterrupted)
		{
			if (!WeakAlive.IsValid()) { return; }
			bCompleted = !bInterrupted;
			if (Continuation && !Continuation.done())
			{
				Continuation.resume();
			}
		});

		AnimInstance->Montage_Play(Montage, PlayRate);

		if (StartSection != NAME_None)
		{
			AnimInstance->Montage_JumpToSection(StartSection, Montage);
		}

		AnimInstance->Montage_SetEndDelegate(EndedDelegate, Montage);
	}

	/** Returns true if the montage completed normally, false if interrupted. */
	bool await_resume() const { return bCompleted; }
};

/** Play a montage and wait for it to end. Returns true if completed, false if interrupted. */
[[nodiscard]] inline FPlayMontageAwaiter PlayMontageAndWait(
	UAnimInstance* AnimInstance,
	UAnimMontage* Montage,
	float PlayRate = 1.0f,
	FName StartSection = NAME_None)
{
	return FPlayMontageAwaiter{AnimInstance, Montage, PlayRate, StartSection};
}

// ============================================================================
// WaitForMontageBlendOut — resumes when blend out starts
// ============================================================================

struct FWaitMontageBlendOutAwaiter
{
	UAnimInstance* AnimInstance = nullptr;
	UAnimMontage* Montage = nullptr;
	std::coroutine_handle<> Continuation;
	TSharedPtr<bool> AliveFlag = MakeShared<bool>(true);

	~FWaitMontageBlendOutAwaiter() { *AliveFlag = false; }

	bool await_ready() const { return false; }

	void await_suspend(std::coroutine_handle<> Handle)
	{
		Continuation = Handle;

		if (!AnimInstance || !Montage)
		{
			Handle.resume();
			return;
		}

		FOnMontageBlendingOutStarted BlendOutDelegate;
		TWeakPtr<bool> WeakAlive = AliveFlag;
		BlendOutDelegate.BindLambda([this, WeakAlive](UAnimMontage* InMontage, bool bInterrupted)
		{
			if (!WeakAlive.IsValid()) { return; }
			if (Continuation && !Continuation.done())
			{
				Continuation.resume();
			}
		});

		AnimInstance->Montage_SetBlendingOutDelegate(BlendOutDelegate, Montage);
	}

	void await_resume() const {}
};

/** Wait for a montage's blend-out to start. */
[[nodiscard]] inline FWaitMontageBlendOutAwaiter WaitForMontageBlendOut(UAnimInstance* AnimInstance, UAnimMontage* Montage)
{
	return FWaitMontageBlendOutAwaiter{AnimInstance, Montage};
}

// ============================================================================
// NextNotify — waits for a specific anim notify by name during montage playback
// Uses polling via tick subsystem since OnPlayMontageNotifyBegin is a dynamic delegate.
// ============================================================================

struct FNextNotifyAwaiter
{
	UAnimInstance* AnimInstance = nullptr;
	UAnimMontage* Montage = nullptr;
	FName NotifyName;
	std::coroutine_handle<> Continuation;
	Private::FAwaiterAliveFlag AliveFlag;

	bool await_ready() const { return false; }

	void await_suspend(std::coroutine_handle<> Handle)
	{
		Continuation = Handle;

		if (!AnimInstance || !Montage)
		{
			Handle.resume();
			return;
		}

		UWorld* World = AnimInstance->GetWorld();
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

		// POLLING STUB: waits for montage to stop, does NOT detect the actual notify.
		// For production use, implement a UObject relay bound to the dynamic delegate.
		TWeakObjectPtr<UAnimInstance> WeakAnim = AnimInstance;
		TWeakObjectPtr<UAnimMontage> WeakMontage = Montage;

		Subsystem->ScheduleCondition(Handle, AnimInstance, [WeakAnim, WeakMontage]() -> bool
		{
			if (!WeakAnim.IsValid() || !WeakMontage.IsValid())
			{
				return true;
			}
			if (!WeakAnim->Montage_IsPlaying(WeakMontage.Get()))
			{
				return true;
			}
			return false;
		}, AliveFlag.Get());
	}

	void await_resume() const {}
};

/**
 * Wait for a montage to end — proxy for notify waiting.
 * WARNING: This is a polling stub that waits for montage completion, not the actual notify.
 * See docs for dynamic delegate relay pattern for true notify detection.
 */
[[nodiscard]] inline FNextNotifyAwaiter NextNotify(UAnimInstance* AnimInstance, UAnimMontage* Montage, FName NotifyName)
{
	return FNextNotifyAwaiter{AnimInstance, Montage, NotifyName};
}

// ============================================================================
// WaitForMontageNotifyEnd — waits until a montage is no longer playing
// Proxy implementation pending dynamic delegate relay (see docs).
// ============================================================================

struct FWaitMontageNotifyEndAwaiter
{
	UAnimInstance* AnimInstance = nullptr;
	UAnimMontage* Montage = nullptr;
	FName NotifyName;
	std::coroutine_handle<> Continuation;
	Private::FAwaiterAliveFlag AliveFlag;

	bool await_ready() const { return false; }

	void await_suspend(std::coroutine_handle<> Handle)
	{
		Continuation = Handle;

		if (!AnimInstance || !Montage)
		{
			Handle.resume();
			return;
		}

		UWorld* World = AnimInstance->GetWorld();
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

		TWeakObjectPtr<UAnimInstance> WeakAnim = AnimInstance;
		TWeakObjectPtr<UAnimMontage> WeakMontage = Montage;

		Subsystem->ScheduleCondition(Handle, AnimInstance, [WeakAnim, WeakMontage]() -> bool
		{
			if (!WeakAnim.IsValid() || !WeakMontage.IsValid())
			{
				return true;
			}
			return !WeakAnim->Montage_IsPlaying(WeakMontage.Get());
		}, AliveFlag.Get());
	}

	void await_resume() const {}
};

/** Wait for a montage notify end. Falls back to montage completion polling. */
[[nodiscard]] inline FWaitMontageNotifyEndAwaiter WaitForMontageNotifyEnd(UAnimInstance* AnimInstance, UAnimMontage* Montage, FName NotifyName)
{
	return FWaitMontageNotifyEndAwaiter{AnimInstance, Montage, NotifyName};
}

} // namespace AsyncFlow

