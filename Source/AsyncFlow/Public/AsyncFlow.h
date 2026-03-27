// AsyncFlow.h — Umbrella header for the AsyncFlow core module
//
// Includes the core coroutine type, macros, and lightweight awaiters.
// Engine-integration awaiters (collision, audio, HTTP, sequences, etc.) have
// dedicated headers and must be included explicitly to avoid pulling heavy
// engine dependencies into every translation unit.
#pragma once

// Core types
#include "AsyncFlowLogging.h"
#include "AsyncFlowTask.h"
#include "AsyncFlowMacros.h"
#include "AsyncFlowGenerator.h"

// Tick subsystem
#include "AsyncFlowTickSubsystem.h"

// Core awaiters (lightweight — no heavy engine deps)
#include "AsyncFlowAwaiters.h"
#include "AsyncFlowDelegateAwaiter.h"
#include "AsyncFlowSyncPrimitives.h"

// Latent UFUNCTION support
#include "AsyncFlowLatentAction.h"

// Debug and tracking
#include "AsyncFlowDebug.h"

// Engine integration awaiters (opt-in — include individually as needed):
//   #include "AsyncFlowThreadAwaiters.h"   // Background thread / multithreading
//   #include "AsyncFlowAssetAwaiters.h"
//   #include "AsyncFlowLevelAwaiters.h"
//   #include "AsyncFlowCollisionAwaiters.h"
//   #include "AsyncFlowAnimAwaiters.h"
//   #include "AsyncFlowAudioAwaiters.h"
//   #include "AsyncFlowHttpAwaiter.h"
//   #include "AsyncFlowSaveGameAwaiters.h"
//   #include "AsyncFlowSequenceAwaiter.h"
//   #include "AsyncFlowMiscAwaiters.h"

