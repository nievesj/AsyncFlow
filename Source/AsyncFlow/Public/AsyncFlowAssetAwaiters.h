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

// AsyncFlowAssetAwaiters.h — Async asset loading awaiters
//
// Wraps FStreamableManager and UAssetManager for non-blocking asset loading.
// Each awaiter requests a load and resumes the coroutine when the asset is
// ready. AliveFlag guards against resuming a dead coroutine frame if the
// awaiter is destroyed mid-load (e.g., task cancellation).
//
// Single and batch variants: AsyncLoadObject, AsyncLoadClass, AsyncLoadObjects,
// AsyncLoadClasses, AsyncLoadPrimaryAsset, AsyncLoadPrimaryAssets, AsyncLoadPackage.
#pragma once

#include "AsyncFlowTask.h"
#include "AsyncFlowAwaiters.h"
#include "Engine/StreamableManager.h"
#include "Engine/AssetManager.h"
#include "UObject/SoftObjectPtr.h"

#include <coroutine>

namespace AsyncFlow
{

	// ============================================================================
	// AsyncLoadObject — load a single soft pointer asynchronously
	// ============================================================================

	/**
 * Awaiter that loads a single soft object pointer via FStreamableManager.
 * If the asset is already loaded, await_ready returns true (no suspension).
 * If the soft pointer is null, resumes immediately with nullptr.
 *
 * @tparam T  The UObject-derived type pointed to by the soft pointer.
 */
	template <typename T>
	struct TAsyncLoadObjectAwaiter
	{
		TSoftObjectPtr<T> SoftPtr;
		T* LoadedObject = nullptr;
		std::coroutine_handle<> Continuation;
		Private::FAwaiterAliveFlag AliveFlag;

		bool await_ready() const
		{
			return SoftPtr.IsValid();
		}

		void await_suspend(std::coroutine_handle<> Handle)
		{
			Continuation = Handle;

			if (SoftPtr.IsNull())
			{
				LoadedObject = nullptr;
				Handle.resume();
				return;
			}

			if (SoftPtr.IsValid())
			{
				LoadedObject = SoftPtr.Get();
				Handle.resume();
				return;
			}

			FStreamableManager& StreamableManager = UAssetManager::GetStreamableManager();
			const FSoftObjectPath Path = SoftPtr.ToSoftObjectPath();

			TSharedPtr<bool> Alive = AliveFlag.Get();
			StreamableManager.RequestAsyncLoad(
				Path,
				FStreamableDelegate::CreateLambda([this, Alive]() {
					if (!*Alive)
					{
						return;
					}
					LoadedObject = SoftPtr.Get();
					if (Continuation)
					{
						Continuation.resume();
					}
				}));
		}

		T* await_resume() const
		{
			return SoftPtr.IsValid() ? SoftPtr.Get() : LoadedObject;
		}
	};

	/**
 * Asynchronously load a soft object pointer.
 *
 * @tparam T      The asset type.
 * @param SoftPtr The soft pointer to load.
 * @return        An awaiter — co_await yields T* (nullptr on failure or null input).
 */
	template <typename T>
	[[nodiscard]] TAsyncLoadObjectAwaiter<T> AsyncLoadObject(TSoftObjectPtr<T> SoftPtr)
	{
		return TAsyncLoadObjectAwaiter<T>{ MoveTemp(SoftPtr) };
	}

	// ============================================================================
	// AsyncLoadClass — load a soft class pointer asynchronously
	// ============================================================================

	/**
 * Awaiter that loads a soft class pointer via FStreamableManager.
 * If already loaded, skips suspension. If the soft pointer is null,
 * resumes immediately with nullptr.
 *
 * @tparam T  The base UObject type of the class asset.
 */
	template <typename T>
	struct TAsyncLoadClassAwaiter
	{
		TSoftClassPtr<T> SoftClassPtr;
		UClass* LoadedClass = nullptr;
		std::coroutine_handle<> Continuation;
		Private::FAwaiterAliveFlag AliveFlag;

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

			TSharedPtr<bool> Alive = AliveFlag.Get();
			StreamableManager.RequestAsyncLoad(
				Path,
				FStreamableDelegate::CreateLambda([this, Alive]() {
					if (!*Alive)
					{
						return;
					}
					LoadedClass = SoftClassPtr.Get();
					if (Continuation)
					{
						Continuation.resume();
					}
				}));
		}

		UClass* await_resume() const
		{
			return SoftClassPtr.Get() ? SoftClassPtr.Get() : LoadedClass;
		}
	};

	/**
 * Asynchronously load a soft class pointer.
 *
 * @tparam T           The base type.
 * @param SoftClassPtr The soft class pointer to load.
 * @return             An awaiter — co_await yields UClass* (nullptr on failure).
 */
	template <typename T>
	[[nodiscard]] TAsyncLoadClassAwaiter<T> AsyncLoadClass(TSoftClassPtr<T> SoftClassPtr)
	{
		return TAsyncLoadClassAwaiter<T>{ MoveTemp(SoftClassPtr) };
	}

	// ============================================================================
	// AsyncLoadPrimaryAsset — load via AssetManager by FPrimaryAssetId
	// ============================================================================

	/**
 * Awaiter that loads a primary asset by ID through UAssetManager.
 * Uses LoadPrimaryAsset with optional bundle names. Handles the edge
 * case where the callback fires synchronously during LoadPrimaryAsset.
 */
	struct FAsyncLoadPrimaryAssetAwaiter
	{
		FPrimaryAssetId AssetId;
		TArray<FName> Bundles;
		UObject* LoadedObject = nullptr;
		std::coroutine_handle<> Continuation;
		TSharedPtr<FStreamableHandle> StreamableHandle;
		bool bCallbackFired = false;
		Private::FAwaiterAliveFlag AliveFlag;

		bool await_ready() const
		{
			return false;
		}

		void await_suspend(std::coroutine_handle<> Handle)
		{
			Continuation = Handle;

			UAssetManager* AssetManager = UAssetManager::GetIfInitialized();
			if (!AssetManager)
			{
				Handle.resume();
				return;
			}

			TSharedPtr<bool> Alive = AliveFlag.Get();
			StreamableHandle = AssetManager->LoadPrimaryAsset(
				AssetId,
				Bundles,
				FStreamableDelegate::CreateLambda([this, Alive]() {
					if (!*Alive)
					{
						return;
					}
					bCallbackFired = true;
					UAssetManager* AM = UAssetManager::GetIfInitialized();
					if (AM)
					{
						LoadedObject = AM->GetPrimaryAssetObject(AssetId);
					}
					if (Continuation)
					{
						Continuation.resume();
					}
				}));

			// If the callback already fired synchronously during LoadPrimaryAsset, skip the post-check
			if (!bCallbackFired && StreamableHandle.IsValid() && StreamableHandle->HasLoadCompleted())
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

	/**
 * Asynchronously load a primary asset by ID.
 *
 * @param AssetId  The primary asset ID to load.
 * @param Bundles  Optional bundle names for the load request.
 * @return         An awaiter — co_await yields UObject* (nullptr if AssetManager is unavailable).
 */
	inline FAsyncLoadPrimaryAssetAwaiter AsyncLoadPrimaryAsset(FPrimaryAssetId AssetId, TArray<FName> Bundles = {})
	{
		return FAsyncLoadPrimaryAssetAwaiter{ MoveTemp(AssetId), MoveTemp(Bundles) };
	}

	// ============================================================================
	// AsyncLoadObjects — batch load multiple soft object pointers
	// ============================================================================

	/**
 * Awaiter that batch-loads multiple soft object pointers in a single
 * FStreamableManager request. More efficient than individual loads when
 * you need several assets ready at the same time.
 *
 * @tparam T  The UObject-derived type of the assets.
 */
	template <typename T>
	struct TAsyncLoadObjectsAwaiter
	{
		TArray<TSoftObjectPtr<T>> SoftPtrs;
		TArray<T*> Results;
		std::coroutine_handle<> Continuation;
		TSharedPtr<FStreamableHandle> StreamableHandle;
		Private::FAwaiterAliveFlag AliveFlag;

		bool await_ready() const
		{
			return SoftPtrs.Num() == 0;
		}

		void await_suspend(std::coroutine_handle<> Handle)
		{
			Continuation = Handle;

			TArray<FSoftObjectPath> Paths;
			Paths.Reserve(SoftPtrs.Num());
			for (const TSoftObjectPtr<T>& Ptr : SoftPtrs)
			{
				if (!Ptr.IsNull())
				{
					Paths.Add(Ptr.ToSoftObjectPath());
				}
			}

			if (Paths.Num() == 0)
			{
				Handle.resume();
				return;
			}

			FStreamableManager& StreamableManager = UAssetManager::GetStreamableManager();
			TSharedPtr<bool> Alive = AliveFlag.Get();
			StreamableHandle = StreamableManager.RequestAsyncLoad(
				Paths,
				FStreamableDelegate::CreateLambda([this, Alive]() {
					if (!*Alive)
					{
						return;
					}
					Results.Reserve(SoftPtrs.Num());
					for (const TSoftObjectPtr<T>& Ptr : SoftPtrs)
					{
						Results.Add(Ptr.Get());
					}
					if (Continuation)
					{
						Continuation.resume();
					}
				}));
		}

		TArray<T*> await_resume()
		{
			return MoveTemp(Results);
		}
	};

	/**
 * Asynchronously load multiple soft object pointers in a single batch.
 *
 * @tparam T       The asset type.
 * @param SoftPtrs Array of soft pointers to load.
 * @return         An awaiter — co_await yields TArray<T*>.
 */
	template <typename T>
	[[nodiscard]] TAsyncLoadObjectsAwaiter<T> AsyncLoadObjects(TArray<TSoftObjectPtr<T>> SoftPtrs)
	{
		return TAsyncLoadObjectsAwaiter<T>{ MoveTemp(SoftPtrs) };
	}

	// ============================================================================
	// AsyncLoadClasses — batch load multiple soft class pointers
	// ============================================================================

	/**
 * Awaiter that batch-loads multiple soft class pointers.
 *
 * @tparam T  The base UObject type.
 */
	template <typename T>
	struct TAsyncLoadClassesAwaiter
	{
		TArray<TSoftClassPtr<T>> SoftClassPtrs;
		TArray<UClass*> Results;
		std::coroutine_handle<> Continuation;
		TSharedPtr<FStreamableHandle> StreamableHandle;
		Private::FAwaiterAliveFlag AliveFlag;

		bool await_ready() const
		{
			return SoftClassPtrs.Num() == 0;
		}

		void await_suspend(std::coroutine_handle<> Handle)
		{
			Continuation = Handle;

			TArray<FSoftObjectPath> Paths;
			Paths.Reserve(SoftClassPtrs.Num());
			for (const TSoftClassPtr<T>& Ptr : SoftClassPtrs)
			{
				if (!Ptr.IsNull())
				{
					Paths.Add(Ptr.ToSoftObjectPath());
				}
			}

			if (Paths.Num() == 0)
			{
				Handle.resume();
				return;
			}

			FStreamableManager& StreamableManager = UAssetManager::GetStreamableManager();
			TSharedPtr<bool> Alive = AliveFlag.Get();
			StreamableHandle = StreamableManager.RequestAsyncLoad(
				Paths,
				FStreamableDelegate::CreateLambda([this, Alive]() {
					if (!*Alive)
					{
						return;
					}
					Results.Reserve(SoftClassPtrs.Num());
					for (const TSoftClassPtr<T>& Ptr : SoftClassPtrs)
					{
						Results.Add(Ptr.Get());
					}
					if (Continuation)
					{
						Continuation.resume();
					}
				}));
		}

		TArray<UClass*> await_resume()
		{
			return MoveTemp(Results);
		}
	};

	/**
 * Asynchronously load multiple soft class pointers in a single batch.
 *
 * @tparam T            The base type.
 * @param SoftClassPtrs Array of soft class pointers to load.
 * @return              An awaiter — co_await yields TArray<UClass*>.
 */
	template <typename T>
	[[nodiscard]] TAsyncLoadClassesAwaiter<T> AsyncLoadClasses(TArray<TSoftClassPtr<T>> SoftClassPtrs)
	{
		return TAsyncLoadClassesAwaiter<T>{ MoveTemp(SoftClassPtrs) };
	}

	// ============================================================================
	// AsyncLoadPrimaryAssets — batch load by FPrimaryAssetId array
	// ============================================================================

	/**
 * Awaiter that batch-loads primary assets by an array of FPrimaryAssetId.
 */
	struct FAsyncLoadPrimaryAssetsAwaiter
	{
		TArray<FPrimaryAssetId> AssetIds;
		TArray<FName> Bundles;
		TArray<UObject*> Results;
		std::coroutine_handle<> Continuation;
		TSharedPtr<FStreamableHandle> StreamableHandle;
		Private::FAwaiterAliveFlag AliveFlag;

		bool await_ready() const
		{
			return AssetIds.Num() == 0;
		}

		void await_suspend(std::coroutine_handle<> Handle)
		{
			Continuation = Handle;

			UAssetManager* AssetManager = UAssetManager::GetIfInitialized();
			if (!AssetManager)
			{
				Handle.resume();
				return;
			}

			TSharedPtr<bool> Alive = AliveFlag.Get();
			StreamableHandle = AssetManager->LoadPrimaryAssets(
				AssetIds,
				Bundles,
				FStreamableDelegate::CreateLambda([this, Alive]() {
					if (!*Alive)
					{
						return;
					}
					UAssetManager* AM = UAssetManager::GetIfInitialized();
					if (AM)
					{
						Results.Reserve(AssetIds.Num());
						for (const FPrimaryAssetId& Id : AssetIds)
						{
							Results.Add(AM->GetPrimaryAssetObject(Id));
						}
					}
					if (Continuation)
					{
						Continuation.resume();
					}
				}));
		}

		TArray<UObject*> await_resume()
		{
			return MoveTemp(Results);
		}
	};

	/**
 * Asynchronously load multiple primary assets.
 *
 * @param AssetIds Array of primary asset IDs.
 * @param Bundles  Optional bundle names.
 * @return         An awaiter — co_await yields TArray<UObject*>.
 */
	[[nodiscard]] inline FAsyncLoadPrimaryAssetsAwaiter AsyncLoadPrimaryAssets(TArray<FPrimaryAssetId> AssetIds, TArray<FName> Bundles = {})
	{
		return FAsyncLoadPrimaryAssetsAwaiter{ MoveTemp(AssetIds), MoveTemp(Bundles) };
	}

	// ============================================================================
	// AsyncLoadPackage — load a package asynchronously
	// ============================================================================

	/**
 * Awaiter that loads a UPackage asynchronously by path.
 */
	struct FAsyncLoadPackageAwaiter
	{
		FString PackagePath;
		UPackage* LoadedPackage = nullptr;
		std::coroutine_handle<> Continuation;
		Private::FAwaiterAliveFlag AliveFlag;

		bool await_ready() const
		{
			return false;
		}

		void await_suspend(std::coroutine_handle<> Handle)
		{
			Continuation = Handle;

			TSharedPtr<bool> Alive = AliveFlag.Get();
			LoadPackageAsync(PackagePath,
				FLoadPackageAsyncDelegate::CreateLambda([this, Alive](const FName&, UPackage* Package, EAsyncLoadingResult::Type) {
					if (!*Alive)
					{
						return;
					}
					LoadedPackage = Package;
					if (Continuation && !Continuation.done())
					{
						Continuation.resume();
					}
				}));
		}

		UPackage* await_resume() const
		{
			return LoadedPackage;
		}
	};

	/**
 * Asynchronously load a package by path.
 *
 * @param PackagePath  The package path string (e.g., "/Game/Maps/MyLevel").
 * @return             An awaiter — co_await yields UPackage* (nullptr on failure).
 */
	[[nodiscard]] inline FAsyncLoadPackageAwaiter AsyncLoadPackage(const FString& PackagePath)
	{
		return FAsyncLoadPackageAwaiter{ PackagePath };
	}

} // namespace AsyncFlow
