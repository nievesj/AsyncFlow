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

// AsyncFlowAnimAwaiters.h — Animation montage awaiters
//
// Wraps UAnimInstance montage playback as co_awaitables. PlayMontageAndWait
// starts a montage and suspends until it ends or is interrupted.
// WaitForMontageEnded waits for an already-playing montage to finish.
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
	// PlayMontageAndWait — start a montage, suspend until it completes
	// ============================================================================

	/**
 * Awaiter that plays a montage on a UAnimInstance and waits for completion.
 * Binds to both OnMontageEnded and OnMontageBlendingOut delegate pairs.
 * Returns true if the montage completed normally, false if interrupted.
 *
 * If AnimInstance or Montage is null, resumes immediately with false.
 */
	struct FPlayMontageAndWaitAwaiter
	{
		UAnimInstance* AnimInstance = nullptr;
		UAnimMontage* Montage = nullptr;
		float PlayRate = 1.0f;
		FName StartSection = NAME_None;
		bool bCompleted = false;
		std::coroutine_handle<> Continuation;
		TSharedPtr<bool> AliveFlag = MakeShared<bool>(true);

		~FPlayMontageAndWaitAwaiter()
		{
			*AliveFlag = false;
		}

		bool await_ready() const
		{
			return false;
		}

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
			EndedDelegate.BindLambda([this, WeakAlive](UAnimMontage* InMontage, bool bInterrupted) {
				if (!WeakAlive.IsValid())
				{
					return;
				}
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
		bool await_resume() const
		{
			return bCompleted;
		}
	};

	/**
 * Play a montage and wait for it to finish.
 *
 * @param AnimInstance   The animation instance to play on.
 * @param Montage        The montage asset.
 * @param PlayRate       Playback speed multiplier. Default 1.0.
 * @param StartSection   Named section to start from. NAME_None starts from the beginning.
 * @return               An awaiter — co_await yields bool (true = completed, false = interrupted).
 */
	[[nodiscard]] inline FPlayMontageAndWaitAwaiter PlayMontageAndWait(
		UAnimInstance* AnimInstance,
		UAnimMontage* Montage,
		float PlayRate = 1.0f,
		FName StartSection = NAME_None)
	{
		return FPlayMontageAndWaitAwaiter{ AnimInstance, Montage, PlayRate, StartSection };
	}

	// ============================================================================
	// WaitForMontageEnded — wait for an already-playing montage to finish
	// ============================================================================

	/**
 * Awaiter that waits for a specific montage to finish on a UAnimInstance.
 * Does NOT start playback — the montage must already be playing.
 * If no matching montage is active, resumes immediately.
 */
	struct FWaitForMontageEndedAwaiter
	{
		UAnimInstance* AnimInstance = nullptr;
		UAnimMontage* Montage = nullptr;
		std::coroutine_handle<> Continuation;
		Private::FAwaiterAliveFlag AliveFlag;

		bool await_ready() const
		{
			return false;
		}

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

			Subsystem->ScheduleCondition(Handle, AnimInstance, [WeakAnim, WeakMontage]() -> bool {
			if (!WeakAnim.IsValid() || !WeakMontage.IsValid())
			{
				return true;
			}
			return !WeakAnim->Montage_IsPlaying(WeakMontage.Get()); }, AliveFlag.Get());
		}

		void await_resume() const
		{
		}
	};

	/**
 * Wait for an already-playing montage to finish.
 *
 * @param AnimInstance  The animation instance.
 * @param Montage       The montage to wait on. Must already be playing.
 * @return              An awaiter — co_await yields void. Resumes when the montage stops playing or the instance is invalidated.
 */
	[[nodiscard]] inline FWaitForMontageEndedAwaiter WaitForMontageEnded(UAnimInstance* AnimInstance, UAnimMontage* Montage)
	{
		return FWaitForMontageEndedAwaiter{ AnimInstance, Montage };
	}

} // namespace AsyncFlow
