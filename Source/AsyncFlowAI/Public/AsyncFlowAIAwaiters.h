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

// AsyncFlowAIAwaiters.h — AI awaiters (MoveTo, pathfinding)
//
// Wraps AAIController movement requests and UNavigationSystemV1 pathfinding
// as co_awaitable operations. Each awaiter binds to the PathFollowingComponent's
// OnRequestFinished delegate, filtered by request ID, and cleans up on destruction.
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

	/**
 * Awaiter that issues an AI move request and waits for it to finish.
 * Binds to UPathFollowingComponent::OnRequestFinished, filtered by
 * the FAIRequestID obtained at the time of the move request.
 *
 * If the controller has no PathFollowingComponent or the move request
 * fails immediately, the awaiter resumes synchronously with Aborted/Invalid.
 *
 * Destructor unbinds the delegate if the coroutine is cancelled mid-move.
 */
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

		bool await_ready() const
		{
			return false;
		}

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
					[this, CurrentRequestID, WeakAlive](FAIRequestID InRequestID, const FPathFollowingResult& PathResult) {
						if (!WeakAlive.IsValid())
						{
							return;
						}
						if (InRequestID != CurrentRequestID)
						{
							return;
						}

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
					});
			}
			else
			{
				Result = EPathFollowingResult::Aborted;
				Handle.resume();
			}
		}

		EPathFollowingResult::Type await_resume() const
		{
			return Result;
		}
	};

	/**
 * Issue an AI move-to-location request and wait for completion.
 *
 * @param Controller        The AI controller to command.
 * @param GoalLocation      World-space destination.
 * @param AcceptanceRadius  Distance from goal at which the move succeeds. -1 uses default.
 * @return                  An awaiter — co_await yields EPathFollowingResult::Type.
 */
	[[nodiscard]] inline FAIMoveToAwaiter AIMoveTo(AAIController* Controller, const FVector& GoalLocation, float AcceptanceRadius = -1.0f)
	{
		return FAIMoveToAwaiter{ Controller, GoalLocation, nullptr, AcceptanceRadius, false };
	}

	/**
 * Issue an AI move-to-actor request and wait for completion.
 *
 * @param Controller        The AI controller to command.
 * @param GoalActor         The target actor to move toward.
 * @param AcceptanceRadius  Distance from goal at which the move succeeds. -1 uses default.
 * @return                  An awaiter — co_await yields EPathFollowingResult::Type.
 */
	[[nodiscard]] inline FAIMoveToAwaiter AIMoveTo(AAIController* Controller, AActor* GoalActor, float AcceptanceRadius = -1.0f)
	{
		return FAIMoveToAwaiter{ Controller, FVector::ZeroVector, GoalActor, AcceptanceRadius, true };
	}

	// ============================================================================
	// FindPathAsync — async pathfinding
	// ============================================================================

	/**
 * Awaiter that performs asynchronous pathfinding via UNavigationSystemV1.
 * Resumes with the query result type and the computed path (if successful).
 */
	struct FFindPathAsyncAwaiter
	{
		UObject* WorldContext = nullptr;
		FPathFindingQuery Query;
		ENavigationQueryResult::Type ResultType = ENavigationQueryResult::Error;
		FNavPathSharedPtr ResultPath;
		std::coroutine_handle<> Continuation;
		TSharedPtr<bool> AliveFlag = MakeShared<bool>(true);

		~FFindPathAsyncAwaiter()
		{
			*AliveFlag = false;
		}

		bool await_ready() const
		{
			return false;
		}

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
				FNavPathQueryDelegate::CreateLambda([this, WeakAlive](uint32 PathId, ENavigationQueryResult::Type InResultType, FNavPathSharedPtr InPath) {
					if (!WeakAlive.IsValid())
					{
						return;
					}
					ResultType = InResultType;
					ResultPath = InPath;
					if (Continuation && !Continuation.done())
					{
						Continuation.resume();
					}
				}),
				EPathFindingMode::Regular);
		}

		TTuple<ENavigationQueryResult::Type, FNavPathSharedPtr> await_resume() const
		{
			return MakeTuple(ResultType, ResultPath);
		}
	};

	/**
 * Perform asynchronous pathfinding.
 *
 * @param WorldContext  Any UObject with a valid GetWorld().
 * @param Query         Pre-configured pathfinding query.
 * @return              An awaiter — co_await yields TTuple<ENavigationQueryResult::Type, FNavPathSharedPtr>.
 */
	[[nodiscard]] inline FFindPathAsyncAwaiter FindPathAsync(UObject* WorldContext, const FPathFindingQuery& Query)
	{
		return FFindPathAsyncAwaiter{ WorldContext, Query };
	}

	// ============================================================================
	// SimpleMoveTo — fire-and-forget move via UNavigationSystemV1
	// ============================================================================

	/**
 * Awaiter that wraps UNavigationSystemV1::SimpleMoveToLocation/Actor.
 * Unlike AIMoveTo, this does not require an AAIController — any AController works.
 * Listens on UPathFollowingComponent::OnRequestFinished for completion.
 *
 * If the controller has no PathFollowingComponent, resumes immediately.
 */
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

		bool await_ready() const
		{
			return false;
		}

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
					[this, CurrentRequestID, WeakAlive](FAIRequestID InRequestID, const FPathFollowingResult&) {
						if (!WeakAlive.IsValid())
						{
							return;
						}
						if (InRequestID != CurrentRequestID)
						{
							return;
						}
						if (WeakPathComp.IsValid())
						{
							WeakPathComp->OnRequestFinished.Remove(PathDelegateHandle);
						}
						if (Continuation && !Continuation.done())
						{
							Continuation.resume();
						}
					});
			}
			else
			{
				Handle.resume();
			}
		}

		void await_resume() const
		{
		}
	};

	/**
 * Simple move to a world location. No result type — just waits for completion.
 *
 * @param Controller    Any AController (not limited to AAIController).
 * @param GoalLocation  World-space destination.
 * @return              An awaiter — use with co_await. Returns void.
 */
	[[nodiscard]] inline FSimpleMoveToAwaiter SimpleMoveTo(AController* Controller, const FVector& GoalLocation)
	{
		return FSimpleMoveToAwaiter{ Controller, GoalLocation, nullptr, false };
	}

	/**
 * Simple move to an actor.
 *
 * @param Controller  Any AController.
 * @param GoalActor   The target actor.
 * @return            An awaiter — use with co_await. Returns void.
 */
	[[nodiscard]] inline FSimpleMoveToAwaiter SimpleMoveTo(AController* Controller, AActor* GoalActor)
	{
		return FSimpleMoveToAwaiter{ Controller, FVector::ZeroVector, GoalActor, true };
	}

} // namespace AsyncFlow
