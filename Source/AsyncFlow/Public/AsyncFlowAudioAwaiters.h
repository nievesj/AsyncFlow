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

// AsyncFlowAudioAwaiters.h — Audio playback awaiters
#pragma once

#include "AsyncFlowTask.h"
#include "AsyncFlowAwaiters.h"
#include "AsyncFlowTickSubsystem.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"

#include <coroutine>

namespace AsyncFlow
{

// ============================================================================
// PlaySoundAndWait — plays an audio component and polls until it finishes
// ============================================================================

struct FPlaySoundAwaiter
{
	UAudioComponent* AudioComponent = nullptr;
	std::coroutine_handle<> Continuation;
	Private::FAwaiterAliveFlag AliveFlag;

	bool await_ready() const { return false; }

	void await_suspend(std::coroutine_handle<> Handle)
	{
		Continuation = Handle;

		if (!AudioComponent)
		{
			Handle.resume();
			return;
		}

		AudioComponent->Play();

		UWorld* World = AudioComponent->GetWorld();
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

		TWeakObjectPtr<UAudioComponent> WeakAudio = AudioComponent;
		Subsystem->ScheduleCondition(Handle, AudioComponent, [WeakAudio]() -> bool
		{
			return !WeakAudio.IsValid() || !WeakAudio->IsPlaying();
		}, AliveFlag.Get());
	}

	void await_resume() const {}
};

/** Start playing audio and wait until it finishes. */
[[nodiscard]] inline FPlaySoundAwaiter PlaySoundAndWait(UAudioComponent* AudioComponent)
{
	return FPlaySoundAwaiter{AudioComponent};
}

// ============================================================================
// WaitForAudioFinished — waits for an already-playing audio component
// ============================================================================

struct FWaitAudioFinishedAwaiter
{
	UAudioComponent* AudioComponent = nullptr;
	std::coroutine_handle<> Continuation;
	Private::FAwaiterAliveFlag AliveFlag;

	bool await_ready() const
	{
		return !AudioComponent || !AudioComponent->IsPlaying();
	}

	void await_suspend(std::coroutine_handle<> Handle)
	{
		Continuation = Handle;

		if (!AudioComponent || !AudioComponent->IsPlaying())
		{
			Handle.resume();
			return;
		}

		UWorld* World = AudioComponent->GetWorld();
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

		TWeakObjectPtr<UAudioComponent> WeakAudio = AudioComponent;
		Subsystem->ScheduleCondition(Handle, AudioComponent, [WeakAudio]() -> bool
		{
			return !WeakAudio.IsValid() || !WeakAudio->IsPlaying();
		}, AliveFlag.Get());
	}

	void await_resume() const {}
};

/** Wait for an already-playing audio component to finish. */
[[nodiscard]] inline FWaitAudioFinishedAwaiter WaitForAudioFinished(UAudioComponent* AudioComponent)
{
	return FWaitAudioFinishedAwaiter{AudioComponent};
}

} // namespace AsyncFlow

