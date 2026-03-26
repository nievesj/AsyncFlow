// AsyncFlowTickSubsystem.cpp
#include "AsyncFlowTickSubsystem.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "AsyncFlowLogging.h"

void UAsyncFlowTickSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UAsyncFlowTickSubsystem::Deinitialize()
{
	// Clear all pending entries. TTask destructors will destroy the coroutine frames.
	DelayedResumes.Empty();
	TickResumes.Empty();
	ConditionResumes.Empty();
	TickUpdates.Empty();

	Super::Deinitialize();
}

void UAsyncFlowTickSubsystem::Tick(float DeltaTime)
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const double GameTime = World->GetTimeSeconds();
	const double RealTime = FPlatformTime::Seconds();

	// Process delayed resumes
	for (int32 Index = DelayedResumes.Num() - 1; Index >= 0; --Index)
	{
		AsyncFlow::Private::FDelayedResume& Entry = DelayedResumes[Index];
		const double CurrentTime = Entry.bUseRealTime ? RealTime : GameTime;

		if (CurrentTime >= Entry.ResumeAtTime)
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
		AsyncFlow::Private::FTickResume& Entry = TickResumes[Index];
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
		AsyncFlow::Private::FConditionResume& Entry = ConditionResumes[Index];

		// If context object is dead, remove the entry. TTask destructor handles cleanup.
		if (Entry.Context.IsValid() == false && Entry.Context.IsExplicitlyNull() == false)
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

	// Process per-tick updates (Timeline, MoveComponentTo, etc.)
	for (int32 Index = TickUpdates.Num() - 1; Index >= 0; --Index)
	{
		AsyncFlow::Private::FTickUpdate& Entry = TickUpdates[Index];

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

TStatId UAsyncFlowTickSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UAsyncFlowTickSubsystem, STATGROUP_Tickables);
}

void UAsyncFlowTickSubsystem::ScheduleDelay(std::coroutine_handle<> Handle, float Seconds)
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	AsyncFlow::Private::FDelayedResume Entry;
	Entry.Handle = Handle;
	Entry.ResumeAtTime = World->GetTimeSeconds() + static_cast<double>(Seconds);
	Entry.bUseRealTime = false;
	DelayedResumes.Add(MoveTemp(Entry));
}

void UAsyncFlowTickSubsystem::ScheduleRealDelay(std::coroutine_handle<> Handle, float Seconds)
{
	AsyncFlow::Private::FDelayedResume Entry;
	Entry.Handle = Handle;
	Entry.ResumeAtTime = FPlatformTime::Seconds() + static_cast<double>(Seconds);
	Entry.bUseRealTime = true;
	DelayedResumes.Add(MoveTemp(Entry));
}

void UAsyncFlowTickSubsystem::ScheduleTicks(std::coroutine_handle<> Handle, int32 NumTicks)
{
	AsyncFlow::Private::FTickResume Entry;
	Entry.Handle = Handle;
	Entry.RemainingTicks = FMath::Max(NumTicks, 1);
	TickResumes.Add(MoveTemp(Entry));
}

void UAsyncFlowTickSubsystem::ScheduleCondition(std::coroutine_handle<> Handle, UObject* Context, TFunction<bool()> Predicate)
{
	AsyncFlow::Private::FConditionResume Entry;
	Entry.Handle = Handle;
	Entry.Context = Context;
	Entry.Predicate = MoveTemp(Predicate);
	ConditionResumes.Add(MoveTemp(Entry));
}

void UAsyncFlowTickSubsystem::ScheduleTickUpdate(std::coroutine_handle<> Handle, TFunction<bool(float)> UpdateFunc)
{
	AsyncFlow::Private::FTickUpdate Entry;
	Entry.Handle = Handle;
	Entry.UpdateFunc = MoveTemp(UpdateFunc);
	TickUpdates.Add(MoveTemp(Entry));
}

void UAsyncFlowTickSubsystem::CancelHandle(std::coroutine_handle<> Handle)
{
	DelayedResumes.RemoveAll([Handle](const AsyncFlow::Private::FDelayedResume& E) { return E.Handle == Handle; });
	TickResumes.RemoveAll([Handle](const AsyncFlow::Private::FTickResume& E) { return E.Handle == Handle; });
	ConditionResumes.RemoveAll([Handle](const AsyncFlow::Private::FConditionResume& E) { return E.Handle == Handle; });
	TickUpdates.RemoveAll([Handle](const AsyncFlow::Private::FTickUpdate& E) { return E.Handle == Handle; });
}


