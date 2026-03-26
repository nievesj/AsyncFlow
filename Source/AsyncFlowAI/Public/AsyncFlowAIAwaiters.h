// AsyncFlowAIAwaiters.h — AI awaiters (MoveTo, pathfinding)
#pragma once

#include "AsyncFlowTask.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "NavFilters/NavigationQueryFilter.h"

#include <coroutine>

namespace AsyncFlow
{

// ============================================================================
// AIMoveTo — wraps AAIController::MoveToLocation / MoveToActor
// ============================================================================

struct FAIMoveToAwaiter
{
	AAIController* Controller = nullptr;
	FVector GoalLocation;
	AActor* GoalActor = nullptr;
	float AcceptanceRadius = -1.0f;
	bool bMoveToActor = false;
	EPathFollowingResult::Type Result = EPathFollowingResult::Aborted;
	std::coroutine_handle<> Continuation;
	TSharedPtr<bool> AliveFlag = MakeShared<bool>(true);
	FDelegateHandle PathDelegateHandle;
	TWeakObjectPtr<UPathFollowingComponent> WeakPathComp;

	~FAIMoveToAwaiter() { *AliveFlag = false; }

	bool await_ready() const { return false; }

	void await_suspend(std::coroutine_handle<> Handle)
	{
		Continuation = Handle;

		if (!Controller)
		{
			Result = EPathFollowingResult::Invalid;
			Handle.resume();
			return;
		}

		EPathFollowingRequestResult::Type MoveResult;
		if (bMoveToActor && GoalActor)
		{
			MoveResult = Controller->MoveToActor(GoalActor, AcceptanceRadius, true, true, false, true);
		}
		else
		{
			MoveResult = Controller->MoveToLocation(GoalLocation, AcceptanceRadius, true, true, false, true);
		}

		if (MoveResult == EPathFollowingRequestResult::AlreadyAtGoal)
		{
			Result = EPathFollowingResult::Success;
			Handle.resume();
			return;
		}

		if (MoveResult == EPathFollowingRequestResult::Failed)
		{
			Result = EPathFollowingResult::Aborted;
			Handle.resume();
			return;
		}

		UPathFollowingComponent* PathComp = Controller->GetPathFollowingComponent();
		if (PathComp)
		{
			WeakPathComp = PathComp;
			const FAIRequestID CurrentRequestID = Controller->GetCurrentMoveRequestID();
			TWeakPtr<bool> WeakAlive = AliveFlag;

			PathDelegateHandle = PathComp->OnRequestFinished.AddLambda(
				[this, CurrentRequestID, WeakAlive](FAIRequestID InRequestID, const FPathFollowingResult& PathResult)
				{
					if (!WeakAlive.IsValid()) { return; }
					if (InRequestID != CurrentRequestID) { return; }

					// Unbind ourselves
					if (WeakPathComp.IsValid())
					{
						WeakPathComp->OnRequestFinished.Remove(PathDelegateHandle);
					}

					Result = PathResult.IsSuccess() ? EPathFollowingResult::Success : EPathFollowingResult::Aborted;
					if (Continuation && !Continuation.done())
					{
						Continuation.resume();
					}
				}
			);
		}
		else
		{
			Result = EPathFollowingResult::Aborted;
			Handle.resume();
		}
	}

	EPathFollowingResult::Type await_resume() const { return Result; }
};

/** AI move to a world location. Returns the path following result. */
[[nodiscard]] inline FAIMoveToAwaiter AIMoveTo(AAIController* Controller, const FVector& GoalLocation, float AcceptanceRadius = -1.0f)
{
	return FAIMoveToAwaiter{Controller, GoalLocation, nullptr, AcceptanceRadius, false};
}

/** AI move to an actor. Returns the path following result. */
[[nodiscard]] inline FAIMoveToAwaiter AIMoveTo(AAIController* Controller, AActor* GoalActor, float AcceptanceRadius = -1.0f)
{
	return FAIMoveToAwaiter{Controller, FVector::ZeroVector, GoalActor, AcceptanceRadius, true};
}

// ============================================================================
// FindPathAsync — async pathfinding
// ============================================================================

struct FFindPathAsyncAwaiter
{
	UObject* WorldContext = nullptr;
	FPathFindingQuery Query;
	ENavigationQueryResult::Type ResultType = ENavigationQueryResult::Error;
	FNavPathSharedPtr ResultPath;
	std::coroutine_handle<> Continuation;
	TSharedPtr<bool> AliveFlag = MakeShared<bool>(true);

	~FFindPathAsyncAwaiter() { *AliveFlag = false; }

	bool await_ready() const { return false; }

	void await_suspend(std::coroutine_handle<> Handle)
	{
		Continuation = Handle;

		if (!WorldContext)
		{
			Handle.resume();
			return;
		}

		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(WorldContext->GetWorld());
		if (!NavSys)
		{
			Handle.resume();
			return;
		}

		TWeakPtr<bool> WeakAlive = AliveFlag;
		NavSys->FindPathAsync(
			Query.NavAgentProperties,
			Query,
			FNavPathQueryDelegate::CreateLambda([this, WeakAlive](uint32 PathId, ENavigationQueryResult::Type InResultType, FNavPathSharedPtr InPath)
			{
				if (!WeakAlive.IsValid()) { return; }
				ResultType = InResultType;
				ResultPath = InPath;
				if (Continuation && !Continuation.done())
				{
					Continuation.resume();
				}
			}),
			EPathFindingMode::Regular
		);
	}

	TTuple<ENavigationQueryResult::Type, FNavPathSharedPtr> await_resume() const
	{
		return MakeTuple(ResultType, ResultPath);
	}
};

/** Asynchronous pathfinding. Returns {ResultType, Path}. */
[[nodiscard]] inline FFindPathAsyncAwaiter FindPathAsync(UObject* WorldContext, const FPathFindingQuery& Query)
{
	return FFindPathAsyncAwaiter{WorldContext, Query};
}

} // namespace AsyncFlow

