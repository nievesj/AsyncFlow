# AsyncFlow

**Version 1.0** · Unreal Engine 5.4+ · C++20 · MIT License

C++20 coroutine framework for Unreal Engine. Write async gameplay logic as straight-line code using `co_await` instead of callback chains, latent actions, or state machines.

```cpp
AsyncFlow::TTask<void> AMyActor::RunAttackSequence()
{
    co_await AsyncFlow::Delay(this, 0.5f);

    bool bFinished = co_await AsyncFlow::PlayMontageAndWait(AnimInstance, AttackMontage);
    co_verifyf(bFinished, TEXT("Montage interrupted"));

    co_await AsyncFlow::Delay(this, CooldownSeconds);
}
```

## What This Is

Inspired by [UniTask](https://github.com/Cysharp/UniTask), AsyncFlow is an attempt to solve the same problem in Unreal: callback hell. The moment you start chaining latent actions, timer delegates, and `OnMontageEnded` bindings to express a single ability or cutscene, the logic becomes impossible to follow. AsyncFlow replaces all of that with straight-line coroutine code.

AsyncFlow gives you `TTask<T>` — a lazily-started, move-only coroutine handle that runs on the game thread. It ships with ~50 awaiters that cover the common async patterns in UE: delays, ticks, delegates, asset loading, collision queries, animation montages, audio playback, level streaming, HTTP requests, GAS events, AI navigation, and more.

Three modules, zero boilerplate:

| Module | What it covers | Dependencies |
|--------|---------------|--------------|
| **AsyncFlow** | Core types + all engine awaiters | Core, Engine, HTTP, Niagara*, UMG*, MovieScene*, LevelSequence* |
| **AsyncFlowGAS** | Coroutine-driven abilities + GAS awaiters | GameplayAbilities, GameplayTags, GameplayTasks |
| **AsyncFlowAI** | AI MoveTo, pathfinding | AIModule, NavigationSystem |

\* Optional. Guarded by `__has_include`. Remove the `PrivateDependency` in `AsyncFlow.Build.cs` if you don't use them.

## Dependencies

- Unreal Engine 5.4+
- C++20 coroutine support (enabled by default in UE 5.4+)
- **GameplayAbilities** plugin (optional — only needed for `AsyncFlowGAS`)

## How to Integrate

Drop the `AsyncFlow` folder into your project's `Plugins/` directory. Add the modules you need to your `.Build.cs`:

```csharp
PublicDependencyModuleNames.Add("AsyncFlow");

// Optional:
PublicDependencyModuleNames.Add("AsyncFlowGAS");
PublicDependencyModuleNames.Add("AsyncFlowAI");
```

## Quick Start

**1. Include the umbrella header:**

```cpp
#include "AsyncFlow.h"
```

**2. Write a coroutine that returns `TTask<void>` or `TTask<T>`:**

```cpp
AsyncFlow::TTask<void> UMyComponent::FadeInAndActivate()
{
    // Wait 1 second (game time, respects dilation)
    co_await AsyncFlow::Delay(this, 1.0f);

    // Interpolate opacity from 0 to 1 over 0.5 seconds
    co_await AsyncFlow::Timeline(this, 0.0f, 1.0f, 0.5f, [this](float Alpha)
    {
        SetOpacity(Alpha);
    });

    Activate();
}
```

**3. Start the coroutine:**

```cpp
void UMyComponent::BeginPlay()
{
    Super::BeginPlay();
    Task = FadeInAndActivate();
    Task.Start();
}
```

**4. Cancel when done (or let the destructor handle it):**

```cpp
void UMyComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    Task.Cancel();
    Super::EndPlay(Reason);
}
```

## Documentation

Full API reference and usage guides:

- [Core Module — TTask, Macros, Awaiters](docs/CoreModule.md)
- [Engine Awaiters — Assets, Collision, Levels, Animation, Audio, HTTP, Sequences](docs/EngineAwaiters.md)
- [Threading — Background Work, Futures, UE::Tasks, Thread Migration](docs/Threading.md)
- [Synchronization Primitives — Events, Semaphores](docs/SyncPrimitives.md)
- [GAS Module — Coroutine Abilities, GAS Awaiters](docs/GASModule.md)
- [AI Module — MoveTo, Pathfinding](docs/AIModule.md)
- [Debugging — Coroutine Tracking, Console Commands](docs/Debugging.md)

## License

MIT — see [LICENSE](LICENSE).
