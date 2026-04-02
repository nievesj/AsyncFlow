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
		ActiveCount.fetch_add(1, std::memory_order_relaxed);
	}

	void FAsyncFlowDebugger::Unregister(uint64 Id)
	{
		FScopeLock Lock(&CriticalSection);
		if (ActiveCoroutines.Remove(Id) > 0)
		{
			ActiveCount.fetch_sub(1, std::memory_order_relaxed);
		}
	}

	TMap<uint64, FCoroutineDebugInfo> FAsyncFlowDebugger::GetActiveCoroutines() const
	{
		FScopeLock Lock(&CriticalSection);
		return ActiveCoroutines;
	}

	int32 FAsyncFlowDebugger::GetActiveCount() const
	{
		return ActiveCount.load(std::memory_order_relaxed);
	}

	void FAsyncFlowDebugger::DumpToLog() const
	{
		FScopeLock Lock(&CriticalSection);
		const double Now = FPlatformTime::Seconds();

		UE_LOG(LogAsyncFlow, Log, TEXT("=== AsyncFlow Active Coroutines (%d) ==="), ActiveCoroutines.Num());
		for (const TPair<uint64, FCoroutineDebugInfo>& Pair : ActiveCoroutines)
		{
			const double Age = Now - Pair.Value.CreationTime;
			UE_LOG(LogAsyncFlow, Log, TEXT("  [0x%llX] %s — age: %.2fs"), Pair.Key, *Pair.Value.DebugName, Age);
		}
		UE_LOG(LogAsyncFlow, Log, TEXT("========================================"));
	}

} // namespace AsyncFlow
