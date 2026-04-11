# AI Module

**Header:** `#include "AsyncFlowAI.h"`
**Build.cs:** `PublicDependencyModuleNames.Add("AsyncFlowAI");`
**Requires:** AIModule, NavigationSystem

---

## AIMoveTo

Wraps `AAIController::MoveToLocation` and `AAIController::MoveToActor`. Returns `EPathFollowingResult::Type`.

### Move to Location

```cpp
EPathFollowingResult::Type Result = co_await AsyncFlow::AIMoveTo(
    AIController,
    FVector(1000.0f, 500.0f, 0.0f),
    50.0f  // AcceptanceRadius
);

if (Result == EPathFollowingResult::Success)
{
    // Arrived at destination
}
```

### Move to Actor

```cpp
EPathFollowingResult::Type Result = co_await AsyncFlow::AIMoveTo(
    AIController,
    TargetActor,
    100.0f  // AcceptanceRadius
);
```

### Return Values

| Value                           | Meaning                    |
|---------------------------------|----------------------------|
| `EPathFollowingResult::Success` | Reached the goal           |
| `EPathFollowingResult::Aborted` | Path failed or was aborted |
| `EPathFollowingResult::Invalid` | No controller provided     |

### Edge Cases

- If the AI is already at the goal, returns `Success` immediately.
- If pathfinding fails synchronously, returns `Aborted` immediately.
- If the `PathFollowingComponent` is missing, returns `Aborted` immediately.
- The awaiter listens to `OnRequestFinished` filtered by request ID, so concurrent move requests don't
  cross-contaminate.

---

## FindPathAsync

Asynchronous pathfinding via `UNavigationSystemV1::FindPathAsync`. Returns the query result and the computed path.

```cpp
FPathFindingQuery Query;
Query.StartLocation = GetActorLocation();
Query.EndLocation = TargetLocation;
Query.NavAgentProperties = AIController->GetNavAgentPropertiesRef();

auto Result = co_await AsyncFlow::FindPathAsync(this, Query);
ENavigationQueryResult::Type ResultType = Result.Get<0>();
FNavPathSharedPtr Path = Result.Get<1>();

if (ResultType == ENavigationQueryResult::Success && Path.IsValid())
{
    const TArray<FNavPathPoint>& Points = Path->GetPathPoints();
}
```

### Return Values

| Field        | Type                           | Description                                   |
|--------------|--------------------------------|-----------------------------------------------|
| `ResultType` | `ENavigationQueryResult::Type` | `Success`, `Fail`, or `Error`                 |
| `Path`       | `FNavPathSharedPtr`            | The computed navigation path (shared pointer) |

---

## SimpleMoveTo

Fire-and-forget move via `UNavigationSystemV1::SimpleMoveToLocation` / `SimpleMoveToActor`. Waits for the move to
complete.

Works with any `AController`, not just `AAIController`.

### Move to Location

```cpp
co_await AsyncFlow::SimpleMoveTo(Controller, FVector(500.0f, 0.0f, 0.0f));
```

### Move to Actor

```cpp
co_await AsyncFlow::SimpleMoveTo(Controller, TargetActor);
```

### How It Works

1. Calls `UNavigationSystemV1::SimpleMoveToLocation` or `SimpleMoveToActor`.
2. Finds the `UPathFollowingComponent` on the controller.
3. Listens to `OnRequestFinished` filtered by request ID.
4. Resumes the coroutine when the move finishes (success or failure).
5. If no `PathFollowingComponent` exists, resumes immediately.

---

## Patrol Example

```cpp
AsyncFlow::TTask<void> AMyAIController::PatrolRoute()
{
    TWeakObjectPtr<AMyAIController> WeakSelf = this;
    CO_CONTRACT([WeakSelf]() { return WeakSelf.IsValid(); });

    while (true)
    {
        for (const FVector& Waypoint : PatrolPoints)
        {
            EPathFollowingResult::Type Result = co_await AsyncFlow::AIMoveTo(this, Waypoint, 50.0f);

            if (Result != EPathFollowingResult::Success)
            {
                // Path blocked — wait and retry
                co_await AsyncFlow::Delay(2.0f);
                continue;
            }

            // Wait at waypoint
            co_await AsyncFlow::Delay(IdleTimeAtWaypoint);
        }
    }
}
```
