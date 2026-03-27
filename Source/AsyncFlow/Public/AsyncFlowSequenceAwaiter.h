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

// AsyncFlowSequenceAwaiter.h — Level Sequence playback awaiter
//
// Wraps ALevelSequenceActor playback as a co_awaitable. PlaySequenceAndWait
// plays a sequence from a level sequence actor and suspends until it finishes.
//
// Guarded with __has_include — if MovieScene/LevelSequence modules are not
// present, this header compiles to nothing.
#pragma once

#include "AsyncFlowTask.h"
#include "AsyncFlowTickSubsystem.h"
#include "AsyncFlowAwaiters.h"
#include "MovieSceneSequencePlayer.h"
#include "Engine/World.h"

#include <coroutine>

namespace AsyncFlow
{

/**
 * Awaiter that plays a ULevelSequence via ALevelSequenceActor and waits
 * for it to finish. Binds to the sequence player's OnFinished delegate.
 *
 * If the actor or its sequence player is null, resumes immediately.
 *
 * @note Requires MovieScene and LevelSequence modules. This header
 *       is a no-op if those modules are unavailable.
 */
struct FPlaySequenceAndWaitAwaiter
{
	UMovieSceneSequencePlayer* Player = nullptr;
	std::coroutine_handle<> Continuation;
	Private::FAwaiterAliveFlag AliveFlag;

	FPlaySequenceAndWaitAwaiter() = default;
	FPlaySequenceAndWaitAwaiter(FPlaySequenceAndWaitAwaiter&&) noexcept = default;
	FPlaySequenceAndWaitAwaiter& operator=(FPlaySequenceAndWaitAwaiter&&) noexcept = default;
	FPlaySequenceAndWaitAwaiter(const FPlaySequenceAndWaitAwaiter&) = delete;
	FPlaySequenceAndWaitAwaiter& operator=(const FPlaySequenceAndWaitAwaiter&) = delete;

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

/**
 * Play a level sequence and wait for it to finish.
 *
 * @param SequenceActor  The ALevelSequenceActor placed in the level.
 * @return               An awaiter — use with co_await. Returns void.
 */
[[nodiscard]] inline FPlaySequenceAndWaitAwaiter PlaySequenceAndWait(ALevelSequenceActor* SequenceActor)
{
	FPlaySequenceAndWaitAwaiter Aw;
	Aw.Player = SequenceActor ? SequenceActor->GetSequencePlayer() : nullptr;
	return Aw;
}

} // namespace AsyncFlow

