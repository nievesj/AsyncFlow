// AsyncFlowDebug.cpp — FAsyncFlowDebugger implementation
#include "AsyncFlowDebug.h"

namespace AsyncFlow
{

FAsyncFlowDebugger& FAsyncFlowDebugger::Get()
{
	static FAsyncFlowDebugger Instance;
	return Instance;
}

void FAsyncFlowDebugger::Register(uint64 Id, const FString& DebugName)
{
	FScopeLock Lock(&CriticalSection);
	FCoroutineDebugInfo Info;
	Info.DebugName = DebugName;
	Info.CreationTime = FPlatformTime::Seconds();
	ActiveCoroutines.Add(Id, MoveTemp(Info));
}

void FAsyncFlowDebugger::Unregister(uint64 Id)
{
	FScopeLock Lock(&CriticalSection);
	ActiveCoroutines.Remove(Id);
}

TMap<uint64, FCoroutineDebugInfo> FAsyncFlowDebugger::GetActiveCoroutines() const
{
	FScopeLock Lock(&CriticalSection);
	return ActiveCoroutines;
}

int32 FAsyncFlowDebugger::GetActiveCount() const
{
	FScopeLock Lock(&CriticalSection);
	return ActiveCoroutines.Num();
}

void FAsyncFlowDebugger::DumpToLog() const
{
	FScopeLock Lock(&CriticalSection);
	const double Now = FPlatformTime::Seconds();

	UE_LOG(LogAsyncFlow, Log, TEXT("=== AsyncFlow Active Coroutines (%d) ==="), ActiveCoroutines.Num());
	for (const TPair<uint64, FCoroutineDebugInfo>& Pair : ActiveCoroutines)
	{
		const double Age = Now - Pair.Value.CreationTime;
		UE_LOG(LogAsyncFlow, Log, TEXT("  [0x%llX] %s — age: %.2fs"),
			Pair.Key,
			*Pair.Value.DebugName,
			Age);
	}
	UE_LOG(LogAsyncFlow, Log, TEXT("========================================"));
}

} // namespace AsyncFlow

