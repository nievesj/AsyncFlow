// AsyncFlowDebug.h — Coroutine tracking and debugging utilities
#pragma once

#include "AsyncFlowTask.h"
#include "HAL/Platform.h"
#include "Containers/Map.h"
#include "Containers/UnrealString.h"
#include "HAL/CriticalSection.h"
#include "HAL/PlatformTime.h"
#include "Misc/OutputDeviceRedirector.h"

namespace AsyncFlow
{

// ============================================================================
// FCoroutineDebugInfo — per-coroutine tracking data
// ============================================================================

struct FCoroutineDebugInfo
{
	FString DebugName;
	double CreationTime = 0.0;
	bool bCompleted = false;
	bool bCancelled = false;
};

// ============================================================================
// FAsyncFlowDebugger — global coroutine tracker (opt-in)
// ============================================================================

/**
 * Singleton that tracks all active coroutines for debugging.
 * Use Register/Unregister from promise constructors/final_suspend,
 * or manually via TTask::SetDebugName + Register.
 *
 * Console command: AsyncFlow.List — dumps all active coroutines.
 */
class ASYNCFLOW_API FAsyncFlowDebugger
{
public:
	static FAsyncFlowDebugger& Get();

	/** Register a coroutine for tracking. */
	void Register(uint64 Id, const FString& DebugName);

	/** Mark a coroutine as completed and remove from tracking. */
	void Unregister(uint64 Id);

	/** Get all active coroutines (snapshot). */
	TMap<uint64, FCoroutineDebugInfo> GetActiveCoroutines() const;

	/** Get the count of currently active tracked coroutines. */
	int32 GetActiveCount() const;

	/** Dump all active coroutines to the log. */
	void DumpToLog() const;

private:
	FAsyncFlowDebugger() = default;
	mutable FCriticalSection CriticalSection;
	TMap<uint64, FCoroutineDebugInfo> ActiveCoroutines;
};

// ============================================================================
// Helper macros for opt-in tracking
// ============================================================================

/**
 * Register a TTask for debug tracking. Call after SetDebugName().
 * The Id is derived from the coroutine handle address.
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

