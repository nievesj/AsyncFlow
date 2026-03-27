# Engine Awaiters

**Header:** Include individually as needed — not part of the umbrella `AsyncFlow.h` header.

These awaiters wrap common UE engine systems. Each has a dedicated header to avoid pulling heavy engine dependencies into every translation unit.

---

## Asset Loading

**Header:** `#include "AsyncFlowAssetAwaiters.h"`

### AsyncLoadObject

Load a single soft object pointer asynchronously. Returns the hard pointer.

```cpp
TSoftObjectPtr<UTexture2D> SoftTexture = ...;
UTexture2D* Texture = co_await AsyncFlow::AsyncLoadObject(SoftTexture);
```

### AsyncLoadClass

Load a soft class pointer asynchronously. Returns `UClass*`.

```cpp
TSoftClassPtr<AActor> SoftClass = ...;
UClass* Class = co_await AsyncFlow::AsyncLoadClass(SoftClass);
```

### AsyncLoadPrimaryAsset

Load via `UAssetManager` by `FPrimaryAssetId`. Returns `UObject*`.

```cpp
UObject* Asset = co_await AsyncFlow::AsyncLoadPrimaryAsset(
    FPrimaryAssetId("ItemData:Sword_01")
);
```

Bundles:

```cpp
UObject* Asset = co_await AsyncFlow::AsyncLoadPrimaryAsset(
    FPrimaryAssetId("ItemData:Sword_01"),
    { TEXT("UI"), TEXT("Gameplay") }
);
```

### AsyncLoadObjects (Batch)

Load multiple soft object pointers in a single request.

```cpp
TArray<TSoftObjectPtr<UTexture2D>> SoftTextures = ...;
TArray<UTexture2D*> Textures = co_await AsyncFlow::AsyncLoadObjects(SoftTextures);
```

### AsyncLoadClasses (Batch)

```cpp
TArray<TSoftClassPtr<AActor>> SoftClasses = ...;
TArray<UClass*> Classes = co_await AsyncFlow::AsyncLoadClasses(SoftClasses);
```

### AsyncLoadPrimaryAssets (Batch)

```cpp
TArray<FPrimaryAssetId> Ids = { ... };
TArray<UObject*> Assets = co_await AsyncFlow::AsyncLoadPrimaryAssets(Ids);
```

### AsyncLoadPackage

Load a package asynchronously by path. Returns `UPackage*`.

```cpp
UPackage* Pkg = co_await AsyncFlow::AsyncLoadPackage(TEXT("/Game/Maps/Arena"));
```

---

## Collision Queries

**Header:** `#include "AsyncFlowCollisionAwaiters.h"`

All collision awaiters wrap `UWorld::AsyncXxx` functions. Results arrive next frame.

### AsyncLineTrace

```cpp
FTraceDatum Result = co_await AsyncFlow::AsyncLineTrace(
    GetWorld(),
    EAsyncTraceType::Single,
    Start, End,
    ECC_Visibility
);

if (Result.OutHits.Num() > 0)
{
    FHitResult& Hit = Result.OutHits[0];
}
```

### AsyncSweep

```cpp
FTraceDatum Result = co_await AsyncFlow::AsyncSweep(
    GetWorld(),
    EAsyncTraceType::Single,
    Start, End,
    FQuat::Identity,
    ECC_Pawn,
    FCollisionShape::MakeSphere(50.0f)
);
```

### AsyncOverlap

```cpp
FOverlapDatum Result = co_await AsyncFlow::AsyncOverlap(
    GetWorld(),
    Position,
    FQuat::Identity,
    ECC_WorldDynamic,
    FCollisionShape::MakeBox(FVector(100.0f))
);
```

### ByObjectType Variants

Filter traces by object type instead of collision channel:

```cpp
FTraceDatum Result = co_await AsyncFlow::AsyncLineTraceByObjectType(
    GetWorld(), EAsyncTraceType::Multi, Start, End,
    FCollisionObjectQueryParams(ECC_Pawn)
);

FTraceDatum SweepResult = co_await AsyncFlow::AsyncSweepByObjectType(
    GetWorld(), EAsyncTraceType::Single, Start, End,
    FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldStatic),
    FCollisionShape::MakeCapsule(34.0f, 88.0f)
);

FOverlapDatum OverlapResult = co_await AsyncFlow::AsyncOverlapByObjectType(
    GetWorld(), Position, FQuat::Identity,
    FCollisionObjectQueryParams(ECC_Pawn),
    FCollisionShape::MakeSphere(200.0f)
);
```

### ByProfile Variants

Filter by collision profile name:

```cpp
FTraceDatum Result = co_await AsyncFlow::AsyncLineTraceByProfile(
    GetWorld(), EAsyncTraceType::Single, Start, End,
    TEXT("BlockAllDynamic")
);

FTraceDatum SweepResult = co_await AsyncFlow::AsyncSweepByProfile(
    GetWorld(), EAsyncTraceType::Single, Start, End,
    FQuat::Identity, TEXT("Projectile"),
    FCollisionShape::MakeSphere(10.0f)
);
```

---

## Level Streaming

**Header:** `#include "AsyncFlowLevelAwaiters.h"`

### LoadStreamLevel

Load a streaming level. Polls until loaded (and optionally visible). Returns true on success.

```cpp
bool bSuccess = co_await AsyncFlow::LoadStreamLevel(this, TEXT("Arena_01"));
```

Skip visibility wait:

```cpp
bool bSuccess = co_await AsyncFlow::LoadStreamLevel(this, TEXT("Arena_01"), false);
```

### UnloadStreamLevel

Unload a streaming level. Polls until fully unloaded.

```cpp
bool bSuccess = co_await AsyncFlow::UnloadStreamLevel(this, TEXT("Arena_01"));
```

---

## Animation

**Header:** `#include "AsyncFlowAnimAwaiters.h"`

### PlayMontageAndWait

Play a montage and wait for it to end. Returns `true` if the montage completed normally, `false` if interrupted.

```cpp
bool bFinished = co_await AsyncFlow::PlayMontageAndWait(
    AnimInstance, AttackMontage, 1.0f
);

if (!bFinished)
{
    // Montage was interrupted
}
```

With a start section:

```cpp
bool bFinished = co_await AsyncFlow::PlayMontageAndWait(
    AnimInstance, AttackMontage, 1.0f, TEXT("WindUp")
);
```

### WaitForMontageBlendOut

Wait for a montage's blend-out to start.

```cpp
co_await AsyncFlow::WaitForMontageBlendOut(AnimInstance, AttackMontage);
```

### NextNotify

Wait for a montage to finish playing. (Current implementation polls for montage completion. See source comments for dynamic delegate relay pattern for true per-notify detection.)

```cpp
co_await AsyncFlow::NextNotify(AnimInstance, AttackMontage, TEXT("HitWindow"));
```

### WaitForMontageNotifyEnd

Same polling-based wait. Falls back to montage completion detection.

```cpp
co_await AsyncFlow::WaitForMontageNotifyEnd(AnimInstance, AttackMontage, TEXT("HitWindow"));
```

---

## Audio

**Header:** `#include "AsyncFlowAudioAwaiters.h"`

### PlaySoundAndWait

Start playing an audio component and wait until it finishes.

```cpp
co_await AsyncFlow::PlaySoundAndWait(AudioComponent);
```

### WaitForAudioFinished

Wait for an already-playing audio component to finish.

```cpp
co_await AsyncFlow::WaitForAudioFinished(AudioComponent);
```

---

## HTTP

**Header:** `#include "AsyncFlowHttpAwaiter.h"`

### ProcessHttpRequest

Send an HTTP request and wait for the response. Returns `{FHttpResponsePtr, bool bSuccess}`.

```cpp
FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
Request->SetURL(TEXT("https://api.example.com/data"));
Request->SetVerb(TEXT("GET"));

auto [Response, bSuccess] = co_await AsyncFlow::ProcessHttpRequest(Request);

if (bSuccess && Response.IsValid())
{
    FString Body = Response->GetContentAsString();
}
```

---

## Level Sequences

**Header:** `#include "AsyncFlowSequenceAwaiter.h"`

### PlaySequenceAndWait

Play a `UMovieSceneSequencePlayer` and wait for it to finish.

```cpp
co_await AsyncFlow::PlaySequenceAndWait(SequencePlayer);
```

---

## Save Games

**Header:** `#include "AsyncFlowSaveGameAwaiters.h"`

### AsyncSaveGame

Asynchronously save a game object to a slot. Returns true on success.

```cpp
bool bSaved = co_await AsyncFlow::AsyncSaveGame(SaveGameObject, TEXT("Slot1"));
```

### AsyncLoadGame

Asynchronously load a save game from a slot. Returns `nullptr` on failure.

```cpp
USaveGame* Save = co_await AsyncFlow::AsyncLoadGame(TEXT("Slot1"));
if (Save)
{
    UMySaveGame* MySave = Cast<UMySaveGame>(Save);
}
```

---

## Misc Awaiters

**Header:** `#include "AsyncFlowMiscAwaiters.h"`

### Timeline

Per-tick interpolation from one value to another over a duration. Calls the update callback each frame with the interpolated value.

```cpp
co_await AsyncFlow::Timeline(this, 0.0f, 1.0f, 0.5f, [this](float Alpha)
{
    DynamicMaterial->SetScalarParameterValue(TEXT("Opacity"), Alpha);
});
```

### RealTimeline / UnpausedTimeline

Same as Timeline but uses wall-clock time instead of game time. Runs during pause.

```cpp
co_await AsyncFlow::RealTimeline(this, 0.0f, 1.0f, 0.3f, [this](float Alpha)
{
    SetWidgetOpacity(Alpha);
});
```

### AudioTimeline

Uses real time as a proxy for audio time.

```cpp
co_await AsyncFlow::AudioTimeline(this, 0.0f, 1.0f, 1.0f, Callback);
```

### MoveComponentTo

Smoothly move a scene component to a target location and rotation over a duration with optional easing.

```cpp
co_await AsyncFlow::MoveComponentTo(
    RootComponent,
    FVector(100.0f, 200.0f, 0.0f),
    FRotator(0.0f, 90.0f, 0.0f),
    1.5f,   // Duration
    true,   // EaseIn
    true    // EaseOut
);
```

### WaitForNiagaraComplete

Wait for a Niagara particle system to finish. Only available if Niagara is enabled.

```cpp
#include "AsyncFlowMiscAwaiters.h"

co_await AsyncFlow::WaitForNiagaraComplete(NiagaraComponent);
```

### PlayWidgetAnimationAndWait

Play a UMG widget animation and wait for it to finish. Only available if UMG is enabled.

```cpp
co_await AsyncFlow::PlayWidgetAnimationAndWait(MyWidget, FadeInAnimation);
```

