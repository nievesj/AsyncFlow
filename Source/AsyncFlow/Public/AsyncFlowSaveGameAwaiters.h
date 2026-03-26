// AsyncFlowSaveGameAwaiters.h — Async save/load game awaiters
#pragma once

#include "AsyncFlowTask.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/SaveGame.h"

#include <coroutine>

namespace AsyncFlow
{

// ============================================================================
// AsyncSaveGame — wraps UGameplayStatics::AsyncSaveGameToSlot
// ============================================================================

struct FAsyncSaveGameAwaiter
{
	USaveGame* SaveGame = nullptr;
	FString SlotName;
	int32 UserIndex = 0;
	bool bSuccess = false;
	std::coroutine_handle<> Continuation;
	TSharedPtr<bool> AliveFlag = MakeShared<bool>(true);

	~FAsyncSaveGameAwaiter() { *AliveFlag = false; }

	bool await_ready() const { return false; }

	void await_suspend(std::coroutine_handle<> Handle)
	{
		Continuation = Handle;

		if (!SaveGame)
		{
			Handle.resume();
			return;
		}

		TWeakPtr<bool> WeakAlive = AliveFlag;
		UGameplayStatics::AsyncSaveGameToSlot(
			SaveGame,
			SlotName,
			UserIndex,
			FAsyncSaveGameToSlotDelegate::CreateLambda([this, WeakAlive](const FString& InSlotName, int32 InUserIndex, bool bWasSuccessful)
			{
				if (!WeakAlive.IsValid()) { return; }
				bSuccess = bWasSuccessful;
				if (Continuation && !Continuation.done())
				{
					Continuation.resume();
				}
			})
		);
	}

	bool await_resume() const { return bSuccess; }
};

/** Asynchronously save a game object to a slot. Returns true on success. */
[[nodiscard]] inline FAsyncSaveGameAwaiter AsyncSaveGame(USaveGame* SaveGame, const FString& SlotName, int32 UserIndex = 0)
{
	return FAsyncSaveGameAwaiter{SaveGame, SlotName, UserIndex};
}

// ============================================================================
// AsyncLoadGame — wraps UGameplayStatics::AsyncLoadGameFromSlot
// ============================================================================

struct FAsyncLoadGameAwaiter
{
	FString SlotName;
	int32 UserIndex = 0;
	USaveGame* LoadedSaveGame = nullptr;
	std::coroutine_handle<> Continuation;
	TSharedPtr<bool> AliveFlag = MakeShared<bool>(true);

	~FAsyncLoadGameAwaiter() { *AliveFlag = false; }

	bool await_ready() const { return false; }

	void await_suspend(std::coroutine_handle<> Handle)
	{
		Continuation = Handle;

		TWeakPtr<bool> WeakAlive = AliveFlag;
		UGameplayStatics::AsyncLoadGameFromSlot(
			SlotName,
			UserIndex,
			FAsyncLoadGameFromSlotDelegate::CreateLambda([this, WeakAlive](const FString& InSlotName, int32 InUserIndex, USaveGame* InSaveGame)
			{
				if (!WeakAlive.IsValid()) { return; }
				LoadedSaveGame = InSaveGame;
				if (Continuation && !Continuation.done())
				{
					Continuation.resume();
				}
			})
		);
	}

	USaveGame* await_resume() const { return LoadedSaveGame; }
};

/** Asynchronously load a save game from a slot. Returns nullptr on failure. */
[[nodiscard]] inline FAsyncLoadGameAwaiter AsyncLoadGame(const FString& SlotName, int32 UserIndex = 0)
{
	return FAsyncLoadGameAwaiter{SlotName, UserIndex};
}

} // namespace AsyncFlow

