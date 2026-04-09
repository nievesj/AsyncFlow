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

// AsyncFlowEditorTickDriver.cpp — Editor tick driver implementation.
//
// All four resume arrays (delayed, tick-count, condition, tick-update) are
// iterated back-to-front with RemoveAtSwap so that removal is O(1) and does
// not invalidate indices of not-yet-visited elements.
//
// Each entry's bAlive flag is checked before resuming. When an awaiter is
// destroyed mid-suspension (e.g., coroutine frame teardown), it sets
// *bAlive = false. The driver detects this and skips the resume.
#include "AsyncFlowEditorTickDriver.h"
#include "HAL/PlatformTime.h"

FAsyncFlowEditorTickDriver* FAsyncFlowEditorTickDriver::Instance = nullptr;

FAsyncFlowEditorTickDriver::FAsyncFlowEditorTickDriver()
{
	Instance = this;
}

FAsyncFlowEditorTickDriver::~FAsyncFlowEditorTickDriver()
{
	DelayedResumes.Empty();
	TickResumes.Empty();
	ConditionResumes.Empty();
	TickUpdates.Empty();

	if (Instance == this)
	{
		Instance = nullptr;
	}
}

FAsyncFlowEditorTickDriver& FAsyncFlowEditorTickDriver::Get()
{
	check(Instance);
	return *Instance;
}

bool FAsyncFlowEditorTickDriver::IsAvailable()
{
	return Instance != nullptr;
}

void FAsyncFlowEditorTickDriver::Tick(float DeltaTime)
{
	const double RealTime = FPlatformTime::Seconds();

	// Process delayed resumes (wall-clock time only)
	for (int32 Index = DelayedResumes.Num() - 1; Index >= 0; --Index)
	{
		AsyncFlow::EditorPrivate::FEditorDelayedResume& Entry = DelayedResumes[Index];

		if (Entry.bAlive && !*Entry.bAlive)
		{
			DelayedResumes.RemoveAtSwap(Index);
			continue;
		}

		if (RealTime >= Entry.ResumeAtTime)
		{
			std::coroutine_handle<> Handle = Entry.Handle;
			DelayedResumes.RemoveAtSwap(Index);
			if (Handle && !Handle.done())
			{
				Handle.resume();
			}
		}
	}

	// Process tick-count resumes
	for (int32 Index = TickResumes.Num() - 1; Index >= 0; --Index)
	{
		AsyncFlow::EditorPrivate::FEditorTickResume& Entry = TickResumes[Index];

		if (Entry.bAlive && !*Entry.bAlive)
		{
			TickResumes.RemoveAtSwap(Index);
			continue;
		}

		--Entry.RemainingTicks;

		if (Entry.RemainingTicks <= 0)
		{
			std::coroutine_handle<> Handle = Entry.Handle;
			TickResumes.RemoveAtSwap(Index);
			if (Handle && !Handle.done())
			{
				Handle.resume();
			}
		}
	}

	// Process condition resumes
	for (int32 Index = ConditionResumes.Num() - 1; Index >= 0; --Index)
	{
		AsyncFlow::EditorPrivate::FEditorConditionResume& Entry = ConditionResumes[Index];

		if (Entry.bAlive && !*Entry.bAlive)
		{
			ConditionResumes.RemoveAtSwap(Index);
			continue;
		}

		if (Entry.Predicate && Entry.Predicate())
		{
			std::coroutine_handle<> Handle = Entry.Handle;
			ConditionResumes.RemoveAtSwap(Index);
			if (Handle && !Handle.done())
			{
				Handle.resume();
			}
		}
	}

	// Process per-tick updates
	for (int32 Index = TickUpdates.Num() - 1; Index >= 0; --Index)
	{
		AsyncFlow::EditorPrivate::FEditorTickUpdate& Entry = TickUpdates[Index];

		if (Entry.bAlive && !*Entry.bAlive)
		{
			TickUpdates.RemoveAtSwap(Index);
			continue;
		}

		const bool bFinished = Entry.UpdateFunc && Entry.UpdateFunc(DeltaTime);
		if (bFinished)
		{
			std::coroutine_handle<> Handle = Entry.Handle;
			TickUpdates.RemoveAtSwap(Index);
			if (Handle && !Handle.done())
			{
				Handle.resume();
			}
		}
	}
}

TStatId FAsyncFlowEditorTickDriver::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FAsyncFlowEditorTickDriver, STATGROUP_Tickables);
}

void FAsyncFlowEditorTickDriver::ScheduleDelay(std::coroutine_handle<> Handle, float Seconds, TSharedPtr<bool> InAlive)
{
	AsyncFlow::EditorPrivate::FEditorDelayedResume Entry;
	Entry.Handle = Handle;
	Entry.ResumeAtTime = FPlatformTime::Seconds() + static_cast<double>(Seconds);
	Entry.bAlive = MoveTemp(InAlive);
	DelayedResumes.Add(MoveTemp(Entry));
}

void FAsyncFlowEditorTickDriver::ScheduleTicks(std::coroutine_handle<> Handle, int32 NumTicks, TSharedPtr<bool> InAlive)
{
	AsyncFlow::EditorPrivate::FEditorTickResume Entry;
	Entry.Handle = Handle;
	Entry.RemainingTicks = FMath::Max(NumTicks, 1);
	Entry.bAlive = MoveTemp(InAlive);
	TickResumes.Add(MoveTemp(Entry));
}

void FAsyncFlowEditorTickDriver::ScheduleCondition(std::coroutine_handle<> Handle, TFunction<bool()> Predicate, TSharedPtr<bool> InAlive)
{
	AsyncFlow::EditorPrivate::FEditorConditionResume Entry;
	Entry.Handle = Handle;
	Entry.Predicate = MoveTemp(Predicate);
	Entry.bAlive = MoveTemp(InAlive);
	ConditionResumes.Add(MoveTemp(Entry));
}

void FAsyncFlowEditorTickDriver::ScheduleTickUpdate(std::coroutine_handle<> Handle, TFunction<bool(float)> UpdateFunc, TSharedPtr<bool> InAlive)
{
	AsyncFlow::EditorPrivate::FEditorTickUpdate Entry;
	Entry.Handle = Handle;
	Entry.UpdateFunc = MoveTemp(UpdateFunc);
	Entry.bAlive = MoveTemp(InAlive);
	TickUpdates.Add(MoveTemp(Entry));
}

void FAsyncFlowEditorTickDriver::CancelHandle(std::coroutine_handle<> Handle)
{
	DelayedResumes.RemoveAll([Handle](const AsyncFlow::EditorPrivate::FEditorDelayedResume& E) { return E.Handle == Handle; });
	TickResumes.RemoveAll([Handle](const AsyncFlow::EditorPrivate::FEditorTickResume& E) { return E.Handle == Handle; });
	ConditionResumes.RemoveAll([Handle](const AsyncFlow::EditorPrivate::FEditorConditionResume& E) { return E.Handle == Handle; });
	TickUpdates.RemoveAll([Handle](const AsyncFlow::EditorPrivate::FEditorTickUpdate& E) { return E.Handle == Handle; });
}
