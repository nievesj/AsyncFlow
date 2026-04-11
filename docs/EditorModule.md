# AsyncFlowEditor Module

## Overview

**AsyncFlowEditor** is an editor-only module that extends AsyncFlow's coroutine
framework to pure-editor contexts. It provides a tick driver and awaiters that
work **without a game world**, enabling coroutines in custom editor tools,
asset editors, utility widgets, and standalone editor windows.

## Architecture

```
AsyncFlowEditor (Type: Editor)
├── FAsyncFlowEditorTickDriver   — FTickableEditorObject singleton
├── AsyncFlowEditorAwaiters.h    — EditorDelay / EditorNextTick / EditorTicks / ...
├── AsyncFlowEditorTask.h        — StartEditorTask<T>()
└── AsyncFlowEditor.h            — Umbrella include
```

The runtime module (`AsyncFlow`) uses `UAsyncFlowTickSubsystem`, a
`UTickableWorldSubsystem` that drives timing awaiters (delays, conditions, tick counts).
The editor module replaces this with `FAsyncFlowEditorTickDriver`, a `FTickableEditorObject`
that is ticked by the editor's own frame loop — no world needed.

> **v2 note:** `TTask` itself has **zero tick dependency** — only timing awaiters need a tick source.
> Thread awaiters, sync primitives, and delegate awaiters work without any subsystem in both
> runtime and editor contexts.

Both systems are independent and can run simultaneously during PIE.

## Quick Start

Add `AsyncFlowEditor` to your editor module's `Build.cs`:

```csharp
if (Target.Type == TargetType.Editor)
{
    PublicDependencyModuleNames.Add("AsyncFlowEditor");
}
```

Include the umbrella header:

```cpp
#include "AsyncFlowEditor.h"
```

Write a coroutine:

```cpp
AsyncFlow::TTask<void> MyEditorTool::DoAsyncWork()
{
    UE_LOG(LogTemp, Log, TEXT("Starting editor work..."));
    co_await AsyncFlow::EditorDelay(1.0f);
    UE_LOG(LogTemp, Log, TEXT("1 second later (real time)"));
    co_await AsyncFlow::EditorNextTick();
    UE_LOG(LogTemp, Log, TEXT("Next editor frame"));
    co_return;
}
```

Start it:

```cpp
AsyncFlow::TTask<void> MyTask = DoAsyncWork();
AsyncFlow::StartEditorTask(MyTask, TEXT("MyEditorTool::DoAsyncWork"));
// MyTask is a passive observer after Start — fire-and-forget is safe.
```

Since `TTask` is now copyable and discardable, you can store a copy for status queries
while the coroutine runs independently:

```cpp
AsyncFlow::TTask<void> MyTask = DoAsyncWork();
AsyncFlow::StartEditorTask(MyTask, TEXT("MyEditorTool::DoAsyncWork"));

// Later — check if it finished (the coroutine keeps running even if MyTask goes out of scope)
if (MyTask.IsCompleted()) { ... }
```

## API Reference

### Tick Driver

`FAsyncFlowEditorTickDriver` is a singleton `FTickableEditorObject` created
automatically when the module loads. Access it via:

- `FAsyncFlowEditorTickDriver::Get()` — returns the singleton (asserts if unavailable)
- `FAsyncFlowEditorTickDriver::IsAvailable()` — returns true if the module is loaded

You typically don't interact with the tick driver directly — the awaiters
handle scheduling internally.

### Editor Awaiters

All awaiters are in the `AsyncFlow` namespace. None require a `UObject*`
world context.

#### `EditorDelay(float Seconds)`

Suspends for `Seconds` of real wall-clock time (uses `FPlatformTime::Seconds()`).
No time dilation, no pause awareness.

```cpp
co_await AsyncFlow::EditorDelay(2.0f);  // wait 2 real seconds
```

#### `EditorNextTick()`

Suspends until the next editor frame. Always suspends.

```cpp
co_await AsyncFlow::EditorNextTick();
```

#### `EditorTicks(int32 NumTicks)`

Suspends for `NumTicks` editor frames. Resumes immediately if `NumTicks <= 0`.

```cpp
co_await AsyncFlow::EditorTicks(10);  // wait 10 editor frames
```

#### `EditorWaitForCondition(TFunction<bool()> Predicate)`

Polls `Predicate` once per editor tick. Resumes when it returns `true`.
If the predicate is already true at the `co_await` point, no suspension occurs.

```cpp
co_await AsyncFlow::EditorWaitForCondition([&]() {
    return ImportProgress >= 1.0f;
});
```

#### `EditorTickUpdate(TFunction<bool(float DeltaTime)> UpdateFunc)`

Calls `UpdateFunc` each editor frame with `DeltaTime`. When it returns `true`,
the coroutine resumes. Useful for progressive editor operations.

```cpp
co_await AsyncFlow::EditorTickUpdate([&](float DT) -> bool {
    ProcessedItems += FMath::CeilToInt(DT * ItemsPerSecond);
    return ProcessedItems >= TotalItems;
});
```

### Helper: `StartEditorTask`

```cpp
template <typename T>
void AsyncFlow::StartEditorTask(TTask<T>& Task, const FString& DebugName = TEXT(""))
```

Starts a task and optionally registers it with `FAsyncFlowDebugger` for
visibility in `AsyncFlow.List` / `AsyncFlow.EditorList`.

### Console Command

`AsyncFlow.EditorList` — dumps all active coroutines (editor and runtime)
to the output log. Registered by the editor module on startup.

## Thread Safety

All editor awaiters and the tick driver operate on the editor main thread.
This is the same thread as the game thread in Unreal Engine. Do not call
scheduling functions from background threads.

> **Note:** Sync primitives (`FAwaitableEvent`, `FAwaitableSemaphore`, `FAutoResetEvent`)
> are fully thread-safe and work in editor coroutines without any special setup. They
> do not depend on any tick source.

## PIE Interaction

During PIE, both tick systems run independently:

- **Game coroutines** (using `Delay`, `NextTick`, etc.) → `UAsyncFlowTickSubsystem`
- **Editor coroutines** (using `EditorDelay`, `EditorNextTick`, etc.) → `FAsyncFlowEditorTickDriver`

Mixing is safe: an editor coroutine can `co_await` runtime awaiters if it
has a world context, and vice versa. But for pure-editor tools, stick to
the `Editor*` awaiters to avoid world-dependency issues.
