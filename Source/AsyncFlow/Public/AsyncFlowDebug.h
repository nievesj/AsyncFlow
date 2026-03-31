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

// AsyncFlowDebug.h — Coroutine tracking and debugging utilities
//
// Opt-in debug infrastructure. FAsyncFlowDebugger is a process-wide singleton
// that tracks active coroutines by ID. Register/Unregister from your coroutine
// setup/teardown code, or use the DebugRegisterTask/DebugUnregisterTask helpers.
//
// Console command "AsyncFlow.List" dumps all active coroutines to the log.
#pragma once

#include "AsyncFlowTask.h"
#include "HAL/Platform.h"
#include "Containers/Map.h"
#include "Containers/UnrealString.h"
#include "HAL/CriticalSection.h"
#include "HAL/PlatformTime.h"
#include "Misc/OutputDeviceRedirector.h"

#include <atomic>

namespace AsyncFlow
{

// ============================================================================
// FCoroutineDebugInfo — per-coroutine tracking data
// ============================================================================

/** Snapshot of a single tracked coroutine's state. */
struct FCoroutineDebugInfo
{
	/** Human-readable label set via TTask::SetDebugName(). */
	FString DebugName;

	/** Wall-clock time (FPlatformTime::Seconds) when Register() was called. */
	double CreationTime = 0.0;

	/** True after the coroutine reaches final_suspend. */
	bool bCompleted = false;

	/** True if Cancel() was called on the coroutine's flow state. */
	bool bCancelled = false;
};

// ============================================================================
// FAsyncFlowDebugger — global coroutine tracker (opt-in)
// ============================================================================

/**
 * Process-wide singleton that tracks active coroutines for debugging.
 *
 * Thread-safe: all methods lock an internal FCriticalSection.
 * ID is typically the coroutine handle address cast to uint64.
 *
 * Console command: "AsyncFlow.List" calls DumpToLog().
 */
class ASYNCFLOW_API FAsyncFlowDebugger
{
public:
	/** @return the singleton instance. Created on first call. */
	static FAsyncFlowDebugger& Get();

	/**
	 * Register a coroutine for tracking.
	 *
	 * @param Id         Unique identifier (typically handle address).
	 * @param DebugName  Human-readable label for log output.
	 */
	void Register(uint64 Id, const FString& DebugName);

	/**
	 * Remove a coroutine from tracking.
	 *
	 * @param Id  The same ID passed to Register().
	 */
	void Unregister(uint64 Id);

	/** @return a snapshot copy of all currently tracked coroutines. Thread-safe. */
	TMap<uint64, FCoroutineDebugInfo> GetActiveCoroutines() const;

	/** @return number of currently tracked coroutines. Lock-free. */
	int32 GetActiveCount() const;

	/** Log all active coroutines with their names and ages. */
	void DumpToLog() const;

private:
	FAsyncFlowDebugger() = default;
	mutable FCriticalSection CriticalSection;
	TMap<uint64, FCoroutineDebugInfo> ActiveCoroutines;
	std::atomic<int32> ActiveCount{0};
};

// ============================================================================
// Helper functions for opt-in tracking
// ============================================================================

/**
 * Register a TTask for debug tracking. Call after SetDebugName().
 * Uses the coroutine handle address as the tracking ID.
 *
 * @tparam T     The task's result type.
 * @param Task   The task to register. Must be valid (IsValid() == true).
 */
template <typename T>
void DebugRegisterTask(TTask<T>& Task)
{
	if (Task.IsValid())
	{
		const uint64 Id = reinterpret_cast<uint64>(Task.GetHandle().address());
		FAsyncFlowDebugger::Get().Register(Id, Task.GetDebugName());
	}
}

/**
 * Unregister a TTask from debug tracking.
 *
 * @tparam T     The task's result type.
 * @param Task   The task to unregister. Must be valid.
 */
template <typename T>
void DebugUnregisterTask(TTask<T>& Task)
{
	if (Task.IsValid())
	{
		const uint64 Id = reinterpret_cast<uint64>(Task.GetHandle().address());
		FAsyncFlowDebugger::Get().Unregister(Id);
	}
}

} // namespace AsyncFlow

