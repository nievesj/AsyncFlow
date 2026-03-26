// AsyncFlowAnimAwaiters.h — Animation montage and notify awaiters
#pragma once

#include "AsyncFlowTask.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"

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

} // namespace AsyncFlow

