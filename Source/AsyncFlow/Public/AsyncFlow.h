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
