# Blueprint Module

**Header:** `#include "AsyncFlowAction.h"`
**Build.cs:** `PublicDependencyModuleNames.Add("AsyncFlowBlueprint");`

## Overview

The `AsyncFlowBlueprint` module exposes AsyncFlow coroutines to Blueprint via
`UCancellableAsyncAction` nodes. Each node appears in the Blueprint action menu
and produces output exec pins for completion, failure, or custom results.

No custom K2Nodes are required — the engine auto-generates Blueprint nodes from
`UFUNCTION(BlueprintCallable, meta=(BlueprintInternalUseOnly="true"))` factory
functions and `UPROPERTY(BlueprintAssignable)` delegates.

## Pre-built Nodes

### Async Flow Delay

Suspends execution for a given duration using game-dilated time.

```
Async Flow Delay
├─ Duration (float input)
├─ OnCompleted (exec output)
└─ OnFailed (exec output)
```

**C++ equivalent:** `co_await AsyncFlow::Delay(Duration, WorldContext);`

### Async Flow Load Asset

Asynchronously loads a soft object reference.

```
Async Flow Load Asset
├─ Asset (TSoftObjectPtr input)
├─ OnLoaded (exec output, UObject* pin)
└─ OnFailed (exec output)
```

**C++ equivalent:** `co_await AsyncFlow::AsyncLoadObject<UObject>(Asset);`

The `OnLoaded` pin carries the loaded `UObject*`. Cast it to your expected type
in Blueprint. `OnFailed` fires if the asset path is invalid or loading fails.

### Async Flow Play Montage

Plays an animation montage and waits for it to finish or be interrupted.

```
Async Flow Play Montage
├─ Mesh Component (USkeletalMeshComponent* input)
├─ Montage (UAnimMontage* input)
├─ Play Rate (float input, default 1.0)
├─ OnCompleted (exec output)
├─ OnInterrupted (exec output)
└─ OnFailed (exec output)
```

**C++ equivalent:** `co_await AsyncFlow::PlayMontageAndWait(AnimInstance, Montage, PlayRate);`

`OnCompleted` fires when the montage finishes naturally. `OnInterrupted` fires
if the montage is interrupted (e.g., by another montage). `OnFailed` fires if
the mesh component, montage, or anim instance is null.

## Creating Custom Blueprint Actions

Subclass `UAsyncFlowAction` to wrap any `AsyncFlow::TTask<void>` coroutine as a
Blueprint node.

### Step 1 — Declare the Action Class

```cpp
#pragma once

#include "AsyncFlowAction.h"
#include "MyAction.generated.h"

UCLASS(meta = (DisplayName = "My Custom Action"))
class MYMODULE_API UMyAction : public UAsyncFlowAction
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable,
        meta = (BlueprintInternalUseOnly = "true",
                WorldContext = "WorldContextObject",
                DisplayName = "My Custom Action"),
        Category = "MyCategory")
    static UMyAction* DoMyAction(
        UObject* WorldContextObject,
        float SomeParam);

protected:
    virtual AsyncFlow::TTask<void> ExecuteAction(UObject* WorldContext) override;

private:
    float SomeParam = 0.f;
};
```

### Step 2 — Implement the Factory and Coroutine

```cpp
#include "MyAction.h"
#include "AsyncFlowAwaiters.h"

UMyAction* UMyAction::DoMyAction(
    UObject* WorldContextObject,
    float InSomeParam)
{
    UMyAction* Action = NewObject<UMyAction>();
    Action->SomeParam = InSomeParam;
    Action->RegisterWithGameInstance(WorldContextObject);
    return Action;
}

AsyncFlow::TTask<void> UMyAction::ExecuteAction(UObject* WorldContext)
{
    co_await AsyncFlow::Delay(SomeParam, WorldContext);
    // OnCompleted is broadcast automatically by the base class.
}
```

### Key Points

- **Factory function** must be `static`, return the action pointer, and call
  `RegisterWithGameInstance()` for GC protection.
- **`ExecuteAction()`** is the coroutine entry point. Use any AsyncFlow awaiter
  inside it.
- **Automatic broadcast:** When `ExecuteAction()` finishes, the base class
  broadcasts `OnCompleted` automatically. You do not need to broadcast it
  yourself for simple actions.
- **Custom delegates:** If your action needs custom output pins (e.g., a loaded
  asset reference), declare additional `UPROPERTY(BlueprintAssignable)` delegates
  and broadcast them in `ExecuteAction()`. Set `bHandledBroadcast = true` to
  prevent the base class from also broadcasting `OnCompleted`.

### Custom Output Pins Example

```cpp
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FOnDataReady, FString, ResultData);

UCLASS(meta = (DisplayName = "Fetch Data"))
class UMyFetchAction : public UAsyncFlowAction
{
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintAssignable)
    FOnDataReady OnDataReady;

    // ... factory UFUNCTION ...

protected:
    virtual AsyncFlow::TTask<void> ExecuteAction(UObject* WorldContext) override
    {
        bHandledBroadcast = true;

        FString Result = co_await SomeFetchCoroutine();

        if (ShouldBroadcastDelegates())
        {
            OnDataReady.Broadcast(Result);
        }
    }
};
```

## UAsyncFlowAction Lifecycle

1. Blueprint calls the static factory → `NewObject<T>()` + `RegisterWithGameInstance()`
2. Engine calls `Activate()` → base starts `RunAction()` coroutine
3. `RunAction()` calls your `ExecuteAction()` and `co_await`s it
4. When `ExecuteAction()` completes → `OnCompleted.Broadcast()` (unless `bHandledBroadcast`)
5. User cancels (or owning object destroyed) → `Cancel()` → `ActiveTask.Cancel()`

### GC Protection

`RegisterWithGameInstance()` prevents the action from being garbage collected
while it is alive. The engine removes the reference when the action completes or
is cancelled. You do not need to manage the action's lifetime manually.

### Cancellation

The base `Cancel()` implementation calls `ActiveTask.Cancel()`, which propagates
cancellation through the AsyncFlow coroutine chain. Any `co_await` point will
throw cancellation and unwind the coroutine. You do not need to check for
cancellation manually in most cases.

## Module Dependencies

Your module's `Build.cs` must depend on both `AsyncFlow` (for awaiters) and
`AsyncFlowBlueprint` (for the base action class):

```csharp
PublicDependencyModuleNames.AddRange(new string[]
{
    "Core",
    "CoreUObject",
    "Engine",
    "AsyncFlow",
    "AsyncFlowBlueprint"
});
```
