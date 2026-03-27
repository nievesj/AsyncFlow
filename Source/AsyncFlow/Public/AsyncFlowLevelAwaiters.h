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

// AsyncFlowLevelAwaiters.h — Level streaming awaiters
#pragma once

#include "AsyncFlowTask.h"
#include "AsyncFlowAwaiters.h"
#include "AsyncFlowTickSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Engine/LevelStreaming.h"

#include <coroutine>
#include <atomic>

namespace AsyncFlow
{

namespace Private
{
	/** Thread-safe counter for unique latent action UUIDs. */
	inline int32 GenerateLatentUUID()
	{
		static std::atomic<int32> Counter{100000};
		return Counter.fetch_add(1, std::memory_order_relaxed);
	}
} // namespace Private

// ============================================================================
// LoadStreamLevel — polls until the level is fully loaded (and visible)
// ============================================================================

struct FLoadStreamLevelAwaiter
{
	UObject* WorldContext;
	FName LevelName;
	bool bMakeVisibleAfterLoad;
	TSharedPtr<bool> SharedSuccess = MakeShared<bool>(false);
	std::coroutine_handle<> Continuation;
	Private::FAwaiterAliveFlag AliveFlag;

	bool await_ready() const { return false; }

	void await_suspend(std::coroutine_handle<> Handle)
	{
		Continuation = Handle;

		if (!WorldContext)
		{
			Handle.resume();
			return;
		}

		UWorld* World = WorldContext->GetWorld();
		if (!World)
		{
			Handle.resume();
			return;
		}

		FLatentActionInfo LatentInfo;
		LatentInfo.UUID = Private::GenerateLatentUUID();
		LatentInfo.CallbackTarget = nullptr;

		UGameplayStatics::LoadStreamLevel(WorldContext, LevelName, bMakeVisibleAfterLoad, false, LatentInfo);

		UAsyncFlowTickSubsystem* Subsystem = World->GetSubsystem<UAsyncFlowTickSubsystem>();
		if (!Subsystem)
		{
			Handle.resume();
			return;
		}

		const bool bNeedVisible = bMakeVisibleAfterLoad;
		TSharedPtr<bool> Success = SharedSuccess;
		TWeakObjectPtr<UObject> WeakContext = WorldContext;
		Subsystem->ScheduleCondition(Handle, WorldContext, [WeakContext, LevelName = LevelName, bNeedVisible, Success]() -> bool
		{
			if (!WeakContext.IsValid()) { return false; }
			ULevelStreaming* StreamingLevel = UGameplayStatics::GetStreamingLevel(WeakContext.Get(), LevelName);
			if (!StreamingLevel)
			{
				return false;
			}

			const bool bLoaded = StreamingLevel->IsLevelLoaded();
			const bool bVisible = StreamingLevel->IsLevelVisible();

			if (bNeedVisible ? (bLoaded && bVisible) : bLoaded)
			{
				*Success = true;
				return true;
			}
			return false;
		}, AliveFlag.Get());
	}

	bool await_resume() const { return *SharedSuccess; }
};

/** Asynchronously load a streaming level. Returns true on success. */
[[nodiscard]] inline FLoadStreamLevelAwaiter LoadStreamLevel(UObject* WorldContext, FName LevelName, bool bMakeVisible = true)
{
	return FLoadStreamLevelAwaiter{WorldContext, LevelName, bMakeVisible};
}

// ============================================================================
// UnloadStreamLevel — polls until the level is fully unloaded
// ============================================================================

struct FUnloadStreamLevelAwaiter
{
	UObject* WorldContext;
	FName LevelName;
	TSharedPtr<bool> SharedSuccess = MakeShared<bool>(false);
	std::coroutine_handle<> Continuation;
	Private::FAwaiterAliveFlag AliveFlag;

	bool await_ready() const { return false; }

	void await_suspend(std::coroutine_handle<> Handle)
	{
		Continuation = Handle;

		if (!WorldContext)
		{
			Handle.resume();
			return;
		}

		UWorld* World = WorldContext->GetWorld();
		if (!World)
		{
			Handle.resume();
			return;
		}

		FLatentActionInfo LatentInfo;
		LatentInfo.UUID = Private::GenerateLatentUUID();
		LatentInfo.CallbackTarget = nullptr;

		UGameplayStatics::UnloadStreamLevel(WorldContext, LevelName, LatentInfo, false);

		UAsyncFlowTickSubsystem* Subsystem = World->GetSubsystem<UAsyncFlowTickSubsystem>();
		if (!Subsystem)
		{
			Handle.resume();
			return;
		}

		TSharedPtr<bool> Success = SharedSuccess;
		TWeakObjectPtr<UObject> WeakContext = WorldContext;
		Subsystem->ScheduleCondition(Handle, WorldContext, [WeakContext, LevelName = LevelName, Success]() -> bool
		{
			if (!WeakContext.IsValid()) { return false; }
			ULevelStreaming* StreamingLevel = UGameplayStatics::GetStreamingLevel(WeakContext.Get(), LevelName);
			if (!StreamingLevel || !StreamingLevel->IsLevelLoaded())
			{
				*Success = true;
				return true;
			}
			return false;
		}, AliveFlag.Get());
	}

	bool await_resume() const { return *SharedSuccess; }
};

/** Asynchronously unload a streaming level. Returns true on success. */
[[nodiscard]] inline FUnloadStreamLevelAwaiter UnloadStreamLevel(UObject* WorldContext, FName LevelName)
{
	return FUnloadStreamLevelAwaiter{WorldContext, LevelName};
}

} // namespace AsyncFlow

