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

// AsyncFlowSequenceAwaiter.h — Level sequence playback awaiter
#pragma once

#include "AsyncFlowTask.h"
#include "AsyncFlowTickSubsystem.h"
#include "AsyncFlowAwaiters.h"
#include "MovieSceneSequencePlayer.h"
#include "Engine/World.h"

#include <coroutine>

namespace AsyncFlow
{

// ============================================================================
// PlaySequenceAndWait — plays a sequence and polls until it stops
// ============================================================================

struct FPlaySequenceAwaiter
{
	UMovieSceneSequencePlayer* Player = nullptr;
	std::coroutine_handle<> Continuation;
	Private::FAwaiterAliveFlag AliveFlag;

	FPlaySequenceAwaiter() = default;
	FPlaySequenceAwaiter(FPlaySequenceAwaiter&&) noexcept = default;
	FPlaySequenceAwaiter& operator=(FPlaySequenceAwaiter&&) noexcept = default;
	FPlaySequenceAwaiter(const FPlaySequenceAwaiter&) = delete;
	FPlaySequenceAwaiter& operator=(const FPlaySequenceAwaiter&) = delete;

	bool await_ready() const { return false; }

	void await_suspend(std::coroutine_handle<> Handle)
	{
		Continuation = Handle;

		if (!Player)
		{
			Handle.resume();
			return;
		}

		Player->Play();

		UWorld* World = Player->GetWorld();
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

		TWeakObjectPtr<UMovieSceneSequencePlayer> WeakPlayer = Player;
		Subsystem->ScheduleCondition(Handle, Player, [WeakPlayer]() -> bool
		{
			return !WeakPlayer.IsValid() || !WeakPlayer->IsPlaying();
		}, AliveFlag.Get());
	}

	void await_resume() const {}
};

/** Play a sequence and wait for it to finish. */
[[nodiscard]] inline FPlaySequenceAwaiter PlaySequenceAndWait(UMovieSceneSequencePlayer* Player)
{
	FPlaySequenceAwaiter Aw;
	Aw.Player = Player;
	return Aw;
}

} // namespace AsyncFlow

