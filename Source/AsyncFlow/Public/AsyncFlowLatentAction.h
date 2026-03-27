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
 * Usage in a UFUNCTION:
 *   UFUNCTION(BlueprintCallable, meta=(Latent, LatentInfo="LatentInfo"))
 *   void MyLatentFunc(UObject* WorldContextObject, FLatentActionInfo LatentInfo)
 *   {
 *       AsyncFlow::StartLatentCoroutine(WorldContextObject, LatentInfo, MyCoroutine());
 *   }
 */
class FAsyncFlowLatentAction : public FPendingLatentAction
{
public:
	FAsyncFlowLatentAction(const FLatentActionInfo& InLatentInfo, TTask<void>&& InTask)
		: Task(MoveTemp(InTask))
		, ExecutionFunction(InLatentInfo.ExecutionFunction)
		, OutputLink(InLatentInfo.Linkage)
		, CallbackTarget(InLatentInfo.CallbackTarget)
	{
		Task.OnComplete([this]()
		{
			bTaskCompleted = true;
		});
		Task.Start();
	}

	virtual void UpdateOperation(FLatentResponse& Response) override
	{
		if (bTaskCompleted || Task.IsCancelled())
		{
			Response.FinishAndTriggerIf(true, ExecutionFunction, OutputLink, CallbackTarget.Get());
		}
	}

	virtual ~FAsyncFlowLatentAction() override
	{
		// Cancel the coroutine if the latent action is destroyed (owner died, etc.)
		if (Task.IsValid() && !Task.IsCompleted())
		{
			Task.Cancel();
		}
	}

#if WITH_EDITOR
	virtual FString GetDescription() const override
	{
		return FString::Printf(TEXT("AsyncFlow Latent [%s]"), *Task.GetDebugName());
	}
#endif

private:
	TTask<void> Task;
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

	LatentManager.AddNewAction(LatentInfo.CallbackTarget, LatentInfo.UUID,
		new FAsyncFlowLatentAction(LatentInfo, MoveTemp(Task)));

	return true;
}

} // namespace AsyncFlow

