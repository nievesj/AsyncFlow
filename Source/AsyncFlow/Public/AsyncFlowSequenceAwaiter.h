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

