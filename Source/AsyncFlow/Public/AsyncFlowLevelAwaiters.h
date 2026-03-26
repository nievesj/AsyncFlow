// AsyncFlowLevelAwaiters.h — Level streaming awaiters
#pragma once

#include "AsyncFlowTask.h"
#include "AsyncFlowTickSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Engine/LevelStreaming.h"

#include <coroutine>

namespace AsyncFlow
{

// ============================================================================
// LoadStreamLevel — polls until the level is fully loaded (and visible)
// ============================================================================

struct FLoadStreamLevelAwaiter
{
	UObject* WorldContext;
	FName LevelName;
	bool bMakeVisibleAfterLoad;
	bool bSuccess = false;
	std::coroutine_handle<> Continuation;

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
		LatentInfo.UUID = FMath::Rand();
		LatentInfo.CallbackTarget = nullptr;

		UGameplayStatics::LoadStreamLevel(WorldContext, LevelName, bMakeVisibleAfterLoad, false, LatentInfo);

		// Poll each tick until the level reaches the desired loaded state.
		// OnLevelLoaded is a DYNAMIC delegate and cannot accept a lambda binding.
		UAsyncFlowTickSubsystem* Subsystem = World->GetSubsystem<UAsyncFlowTickSubsystem>();
		if (!Subsystem)
		{
			Handle.resume();
			return;
		}

		const bool bNeedVisible = bMakeVisibleAfterLoad;
		Subsystem->ScheduleCondition(Handle, WorldContext, [WorldContext = WorldContext, LevelName = LevelName, bNeedVisible, this]() -> bool
		{
			ULevelStreaming* StreamingLevel = UGameplayStatics::GetStreamingLevel(WorldContext, LevelName);
			if (!StreamingLevel)
			{
				return false;
			}

			const bool bLoaded = StreamingLevel->IsLevelLoaded();
			const bool bVisible = StreamingLevel->IsLevelVisible();

			if (bNeedVisible ? (bLoaded && bVisible) : bLoaded)
			{
				bSuccess = true;
				return true;
			}
			return false;
		});
	}

	bool await_resume() const { return bSuccess; }
};

/** Asynchronously load a streaming level. Returns true on success. */
inline FLoadStreamLevelAwaiter LoadStreamLevel(UObject* WorldContext, FName LevelName, bool bMakeVisible = true)
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
	bool bSuccess = false;
	std::coroutine_handle<> Continuation;

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
		LatentInfo.UUID = FMath::Rand();
		LatentInfo.CallbackTarget = nullptr;

		UGameplayStatics::UnloadStreamLevel(WorldContext, LevelName, LatentInfo, false);

		UAsyncFlowTickSubsystem* Subsystem = World->GetSubsystem<UAsyncFlowTickSubsystem>();
		if (!Subsystem)
		{
			Handle.resume();
			return;
		}

		Subsystem->ScheduleCondition(Handle, WorldContext, [WorldContext = WorldContext, LevelName = LevelName, this]() -> bool
		{
			ULevelStreaming* StreamingLevel = UGameplayStatics::GetStreamingLevel(WorldContext, LevelName);
			if (!StreamingLevel || !StreamingLevel->IsLevelLoaded())
			{
				bSuccess = true;
				return true;
			}
			return false;
		});
	}

	bool await_resume() const { return bSuccess; }
};

/** Asynchronously unload a streaming level. Returns true on success. */
inline FUnloadStreamLevelAwaiter UnloadStreamLevel(UObject* WorldContext, FName LevelName)
{
	return FUnloadStreamLevelAwaiter{WorldContext, LevelName};
}

} // namespace AsyncFlow

