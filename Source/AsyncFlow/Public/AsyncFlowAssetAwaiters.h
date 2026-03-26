// AsyncFlowAssetAwaiters.h — Async asset loading awaiters
#pragma once

#include "AsyncFlowTask.h"
#include "Engine/StreamableManager.h"
#include "Engine/AssetManager.h"
#include "UObject/SoftObjectPtr.h"

#include <coroutine>

namespace AsyncFlow
{

// ============================================================================
// AsyncLoadObject — load a single soft pointer asynchronously
// ============================================================================

template <typename T>
struct TAsyncLoadObjectAwaiter
{
	TSoftObjectPtr<T> SoftPtr;
	T* LoadedObject = nullptr;
	std::coroutine_handle<> Continuation;

	bool await_ready() const
	{
		return SoftPtr.IsValid();
	}

	void await_suspend(std::coroutine_handle<> Handle)
	{
		Continuation = Handle;

		if (SoftPtr.IsNull())
		{
			// Nothing to load
			LoadedObject = nullptr;
			Handle.resume();
			return;
		}

		// Already loaded (resolved)
		if (SoftPtr.IsValid())
		{
			LoadedObject = SoftPtr.Get();
			Handle.resume();
			return;
		}

		FStreamableManager& StreamableManager = UAssetManager::GetStreamableManager();
		const FSoftObjectPath Path = SoftPtr.ToSoftObjectPath();

		StreamableManager.RequestAsyncLoad(
			Path,
			FStreamableDelegate::CreateLambda([this]()
			{
				LoadedObject = SoftPtr.Get();
				if (Continuation)
				{
					Continuation.resume();
				}
			})
		);
	}

	T* await_resume() const
	{
		return SoftPtr.IsValid() ? SoftPtr.Get() : LoadedObject;
	}
};

/** Asynchronously load a soft object pointer. Returns the hard pointer when loaded. */
template <typename T>
TAsyncLoadObjectAwaiter<T> AsyncLoadObject(TSoftObjectPtr<T> SoftPtr)
{
	return TAsyncLoadObjectAwaiter<T>{MoveTemp(SoftPtr)};
}

// ============================================================================
// AsyncLoadClass — load a soft class pointer asynchronously
// ============================================================================

template <typename T>
struct TAsyncLoadClassAwaiter
{
	TSoftClassPtr<T> SoftClassPtr;
	UClass* LoadedClass = nullptr;
	std::coroutine_handle<> Continuation;

	bool await_ready() const
	{
		return !SoftClassPtr.IsNull() && SoftClassPtr.Get() != nullptr;
	}

	void await_suspend(std::coroutine_handle<> Handle)
	{
		Continuation = Handle;

		if (SoftClassPtr.IsNull())
		{
			LoadedClass = nullptr;
			Handle.resume();
			return;
		}

		FStreamableManager& StreamableManager = UAssetManager::GetStreamableManager();
		const FSoftObjectPath Path = SoftClassPtr.ToSoftObjectPath();

		StreamableManager.RequestAsyncLoad(
			Path,
			FStreamableDelegate::CreateLambda([this]()
			{
				LoadedClass = SoftClassPtr.Get();
				if (Continuation)
				{
					Continuation.resume();
				}
			})
		);
	}

	UClass* await_resume() const
	{
		return SoftClassPtr.Get() ? SoftClassPtr.Get() : LoadedClass;
	}
};

/** Asynchronously load a soft class pointer. Returns UClass* when loaded. */
template <typename T>
TAsyncLoadClassAwaiter<T> AsyncLoadClass(TSoftClassPtr<T> SoftClassPtr)
{
	return TAsyncLoadClassAwaiter<T>{MoveTemp(SoftClassPtr)};
}

// ============================================================================
// AsyncLoadPrimaryAsset — load via AssetManager by FPrimaryAssetId
// ============================================================================

struct FAsyncLoadPrimaryAssetAwaiter
{
	FPrimaryAssetId AssetId;
	TArray<FName> Bundles;
	UObject* LoadedObject = nullptr;
	std::coroutine_handle<> Continuation;
	TSharedPtr<FStreamableHandle> StreamableHandle;

	bool await_ready() const { return false; }

	void await_suspend(std::coroutine_handle<> Handle)
	{
		Continuation = Handle;

		UAssetManager* AssetManager = UAssetManager::GetIfInitialized();
		if (!AssetManager)
		{
			Handle.resume();
			return;
		}

		StreamableHandle = AssetManager->LoadPrimaryAsset(
			AssetId,
			Bundles,
			FStreamableDelegate::CreateLambda([this]()
			{
				UAssetManager* AM = UAssetManager::GetIfInitialized();
				if (AM)
				{
					LoadedObject = AM->GetPrimaryAssetObject(AssetId);
				}
				if (Continuation)
				{
					Continuation.resume();
				}
			})
		);

		// If handle completed synchronously
		if (StreamableHandle.IsValid() && StreamableHandle->HasLoadCompleted())
		{
			UAssetManager* AM = UAssetManager::GetIfInitialized();
			if (AM)
			{
				LoadedObject = AM->GetPrimaryAssetObject(AssetId);
			}
		}
	}

	UObject* await_resume() const
	{
		return LoadedObject;
	}
};

/** Asynchronously load a primary asset by ID. Returns UObject* when loaded. */
inline FAsyncLoadPrimaryAssetAwaiter AsyncLoadPrimaryAsset(FPrimaryAssetId AssetId, TArray<FName> Bundles = {})
{
	return FAsyncLoadPrimaryAssetAwaiter{MoveTemp(AssetId), MoveTemp(Bundles)};
}

} // namespace AsyncFlow

