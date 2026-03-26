// AsyncFlowSequenceAwaiter.h — Level sequence playback awaiter
#pragma once

#include "AsyncFlowTask.h"
#include "AsyncFlowTickSubsystem.h"
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

		// FOnMovieSceneSequencePlayerEvent is DYNAMIC — no AddLambda. Poll status.
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
		});
	}

	void await_resume() const {}
};

/** Play a sequence and wait for it to finish. */
[[nodiscard]] inline FPlaySequenceAwaiter PlaySequenceAndWait(UMovieSceneSequencePlayer* Player)
{
	return FPlaySequenceAwaiter{Player};
}

} // namespace AsyncFlow

