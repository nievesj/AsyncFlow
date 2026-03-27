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

