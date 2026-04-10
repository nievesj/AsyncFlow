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

// AsyncFlowLatentAction.h — FPendingLatentAction integration for Blueprint latent nodes
#pragma once

#include "AsyncFlowTask.h"
#include "Engine/LatentActionManager.h"
#include "LatentActions.h"
#include "Engine/World.h"

namespace AsyncFlow
{

	/**
 * FAsyncFlowLatentAction — bridges a TTask<void> coroutine to the engine's
 * FPendingLatentAction system, enabling latent UFUNCTION support.
 *
 * When the coroutine completes or the owning UObject is destroyed, the
 * latent action finishes and the output exec pin fires.
 *
 * Two construction paths:
 * 1. Legacy: Takes a TTask<void> directly (used by StartLatentCoroutine helper).
 * 2. Control block: Takes a TSharedPtr<FCoroutineControlBlock<void>> (used by
 *    auto-detected latent mode in TTask<void>::Start()).
 *
 * Usage in a UFUNCTION (automatic latent detection — Phase 2):
 *   UFUNCTION(BlueprintCallable, meta=(Latent, LatentInfo="LatentInfo"))
 *   TTask<void> MyLatentFunc(UObject* WorldContextObject, FLatentActionInfo LatentInfo)
 *   {
 *       co_await Delay(2.0f);
 *       co_return;
 *   }
 *
 * Usage in a UFUNCTION (explicit helper — Phase 1 legacy):
 *   UFUNCTION(BlueprintCallable, meta=(Latent, LatentInfo="LatentInfo"))
 *   void MyLatentFunc(UObject* WorldContextObject, FLatentActionInfo LatentInfo)
 *   {
 *       AsyncFlow::StartLatentCoroutine(WorldContextObject, LatentInfo, MyCoroutine());
 *   }
 */
	class FAsyncFlowLatentAction : public FPendingLatentAction
	{
	public:
		/** Legacy constructor: takes ownership of a TTask<void> and starts it. */
		FAsyncFlowLatentAction(const FLatentActionInfo& InLatentInfo, TTask<void>&& InTask)
			: Task(MoveTemp(InTask))
			, ExecutionFunction(InLatentInfo.ExecutionFunction)
			, OutputLink(InLatentInfo.Linkage)
			, CallbackTarget(InLatentInfo.CallbackTarget)
		{
			Task.OnComplete([this]() {
				bTaskCompleted = true;
			});
			Task.Start();
		}

		/** Control-block constructor: monitors an already-started coroutine (auto-detected latent mode). */
		FAsyncFlowLatentAction(const FLatentActionInfo& InLatentInfo, TSharedPtr<FCoroutineControlBlock<void>> InCB)
			: ControlBlock(MoveTemp(InCB))
			, ExecutionFunction(InLatentInfo.ExecutionFunction)
			, OutputLink(InLatentInfo.Linkage)
			, CallbackTarget(InLatentInfo.CallbackTarget)
		{
		}

		virtual void UpdateOperation(FLatentResponse& Response) override
		{
			bool bDone;
			if (ControlBlock)
			{
				bDone = ControlBlock->bCompleted.load(std::memory_order_acquire)
					|| (ControlBlock->FlowState && ControlBlock->FlowState->IsCancelled());
			}
			else
			{
				bDone = bTaskCompleted || Task.IsCancelled();
			}

			if (bDone)
			{
				Response.FinishAndTriggerIf(true, ExecutionFunction, OutputLink, CallbackTarget.Get());
			}
		}

		virtual ~FAsyncFlowLatentAction() override
		{
			if (ControlBlock)
			{
				if (ControlBlock->FlowState && !ControlBlock->bCompleted.load(std::memory_order_acquire))
				{
					ControlBlock->FlowState->Cancel();
					if (ControlBlock->CancelCurrentAwaiterFunc)
					{
						auto CancelFunc = MoveTemp(ControlBlock->CancelCurrentAwaiterFunc);
						ControlBlock->CancelCurrentAwaiterFunc = nullptr;
						CancelFunc();
					}
				}
			}
			else if (Task.IsValid() && !Task.IsCompleted())
			{
				Task.Cancel();
			}
		}

#if WITH_EDITOR
		virtual FString GetDescription() const override
		{
			if (ControlBlock && ControlBlock->FlowState)
			{
				return FString::Printf(TEXT("AsyncFlow Latent [%s]"), *ControlBlock->FlowState->DebugName);
			}
			return FString::Printf(TEXT("AsyncFlow Latent [%s]"), *Task.GetDebugName());
		}
#endif

	private:
		TTask<void> Task;
		TSharedPtr<FCoroutineControlBlock<void>> ControlBlock;
		FName ExecutionFunction;
		int32 OutputLink = 0;
		TWeakObjectPtr<UObject> CallbackTarget;
		bool bTaskCompleted = false;
	};

	/**
 * Helper to start a latent coroutine from a UFUNCTION.
 * Registers the latent action with the engine and starts the coroutine.
 *
 * Returns false if the world or latent action manager is unavailable.
 *
 * @note This is the legacy (Phase 1) API. With Phase 2, coroutine functions
 *       that accept FLatentActionInfo auto-detect latent mode — no helper needed.
 */
	inline bool StartLatentCoroutine(UObject* WorldContextObject, const FLatentActionInfo& LatentInfo, TTask<void>&& Task)
	{
		if (!WorldContextObject)
		{
			return false;
		}

		UWorld* World = WorldContextObject->GetWorld();
		if (!World)
		{
			return false;
		}

		FLatentActionManager& LatentManager = World->GetLatentActionManager();

		if (LatentManager.FindExistingAction<FAsyncFlowLatentAction>(LatentInfo.CallbackTarget, LatentInfo.UUID))
		{
			// Already running — do not start a duplicate
			return false;
		}

		LatentManager.AddNewAction(LatentInfo.CallbackTarget, LatentInfo.UUID, new FAsyncFlowLatentAction(LatentInfo, MoveTemp(Task)));

		return true;
	}

} // namespace AsyncFlow
