// AsyncFlowAIAwaiters.h — AI awaiters (MoveTo, pathfinding)
#pragma once

#include "AsyncFlowTask.h"
#include "AIController.h"
#include "GameFramework/Controller.h"
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

	~FAIMoveToAwaiter()
	{
		*AliveFlag = false;
		if (WeakPathComp.IsValid() && PathDelegateHandle.IsValid())
		{
			WeakPathComp->OnRequestFinished.Remove(PathDelegateHandle);
		}
	}

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
			MoveResult = Controller->MoveToActor(GoalActor, AcceptanceRadius, true, true, true);
		}
		else
		{
			MoveResult = Controller->MoveToLocation(GoalLocation, AcceptanceRadius, true, true, true);
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

// ============================================================================
// SimpleMoveTo — fire-and-forget move via UNavigationSystemV1
// ============================================================================

struct FSimpleMoveToAwaiter
{
	AController* Controller = nullptr;
	FVector GoalLocation;
	AActor* GoalActor = nullptr;
	bool bMoveToActor = false;
	std::coroutine_handle<> Continuation;
	TSharedPtr<bool> AliveFlag = MakeShared<bool>(true);
	FDelegateHandle PathDelegateHandle;
	TWeakObjectPtr<UPathFollowingComponent> WeakPathComp;

	~FSimpleMoveToAwaiter()
	{
		*AliveFlag = false;
		if (WeakPathComp.IsValid() && PathDelegateHandle.IsValid())
		{
			WeakPathComp->OnRequestFinished.Remove(PathDelegateHandle);
		}
	}

	bool await_ready() const { return false; }

	void await_suspend(std::coroutine_handle<> Handle)
	{
		Continuation = Handle;

		if (!Controller)
		{
			Handle.resume();
			return;
		}

		if (bMoveToActor && GoalActor)
		{
			UNavigationSystemV1::SimpleMoveToActor(Controller, GoalActor);
		}
		else
		{
			UNavigationSystemV1::SimpleMoveToLocation(Controller, GoalLocation);
		}

		// Listen for move completion via PathFollowingComponent, filtered by request ID
		UPathFollowingComponent* PathComp = Controller->FindComponentByClass<UPathFollowingComponent>();
		if (PathComp)
		{
			WeakPathComp = PathComp;
			const FAIRequestID CurrentRequestID = PathComp->GetCurrentRequestId();
			TWeakPtr<bool> WeakAlive = AliveFlag;

			PathDelegateHandle = PathComp->OnRequestFinished.AddLambda(
				[this, CurrentRequestID, WeakAlive](FAIRequestID InRequestID, const FPathFollowingResult&)
				{
					if (!WeakAlive.IsValid()) { return; }
					if (InRequestID != CurrentRequestID) { return; }
					if (WeakPathComp.IsValid())
					{
						WeakPathComp->OnRequestFinished.Remove(PathDelegateHandle);
					}
					if (Continuation && !Continuation.done())
					{
						Continuation.resume();
					}
				}
			);
		}
		else
		{
			Handle.resume();
		}
	}

	void await_resume() const {}
};

/** Simple move to a location. No result — fire and forget with completion. */
[[nodiscard]] inline FSimpleMoveToAwaiter SimpleMoveTo(AController* Controller, const FVector& GoalLocation)
{
	return FSimpleMoveToAwaiter{Controller, GoalLocation, nullptr, false};
}

/** Simple move to an actor. */
[[nodiscard]] inline FSimpleMoveToAwaiter SimpleMoveTo(AController* Controller, AActor* GoalActor)
{
	return FSimpleMoveToAwaiter{Controller, FVector::ZeroVector, GoalActor, true};
}

} // namespace AsyncFlow

