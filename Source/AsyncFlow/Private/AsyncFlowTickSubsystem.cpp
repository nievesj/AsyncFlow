// AsyncFlowTickSubsystem.cpp
#include "AsyncFlowTickSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "AsyncFlowLogging.h"

void UAsyncFlowTickSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UAsyncFlowTickSubsystem::Deinitialize()
{
	DelayedResumes.Empty();
	ActorDilatedResumes.Empty();
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
	const double UnpausedTime = World->GetUnpausedTimeSeconds();
	const double AudioTime = World->GetAudioTimeSeconds();

	// Process delayed resumes (all time sources)
	for (int32 Index = DelayedResumes.Num() - 1; Index >= 0; --Index)
	{
		AsyncFlow::Private::FDelayedResume& Entry = DelayedResumes[Index];

		// Awaiter was destroyed mid-suspension — discard without touching the dead handle.
		if (Entry.bAlive && !*Entry.bAlive)
		{
			DelayedResumes.RemoveAtSwap(Index);
			continue;
		}

		double CurrentTime = 0.0;
		switch (Entry.TimeSource)
		{
		case AsyncFlow::Private::EDelayTimeSource::GameTime:    CurrentTime = GameTime; break;
		case AsyncFlow::Private::EDelayTimeSource::RealTime:    CurrentTime = RealTime; break;
		case AsyncFlow::Private::EDelayTimeSource::UnpausedTime: CurrentTime = UnpausedTime; break;
		case AsyncFlow::Private::EDelayTimeSource::AudioTime:   CurrentTime = AudioTime; break;
		}

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

	// Process actor-dilated resumes
	for (int32 Index = ActorDilatedResumes.Num() - 1; Index >= 0; --Index)
	{
		AsyncFlow::Private::FActorDilatedResume& Entry = ActorDilatedResumes[Index];

		if (Entry.bAlive && !*Entry.bAlive)
		{
			ActorDilatedResumes.RemoveAtSwap(Index);
			continue;
		}

		if (!Entry.Actor.IsValid())
		{
			ActorDilatedResumes.RemoveAtSwap(Index);
			continue;
		}

		const float ActorDilation = Entry.Actor->CustomTimeDilation;
		Entry.RemainingSeconds -= DeltaTime * ActorDilation;

		if (Entry.RemainingSeconds <= 0.0f)
		{
			std::coroutine_handle<> Handle = Entry.Handle;
			ActorDilatedResumes.RemoveAtSwap(Index);
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
		AsyncFlow::Private::FConditionResume& Entry = ConditionResumes[Index];

		if (Entry.bAlive && !*Entry.bAlive)
		{
			ConditionResumes.RemoveAtSwap(Index);
			continue;
		}

		// Detect GC'd context: the weak pointer was set (not explicitly null) but is no longer valid
		const bool bContextWasDestroyed = !Entry.Context.IsValid() && !Entry.Context.IsExplicitlyNull();
		if (bContextWasDestroyed)
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

TStatId UAsyncFlowTickSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UAsyncFlowTickSubsystem, STATGROUP_Tickables);
}

void UAsyncFlowTickSubsystem::ScheduleDelay(std::coroutine_handle<> Handle, float Seconds, TSharedPtr<bool> InAlive)
{
	const UWorld* World = GetWorld();
	if (!World) { return; }

	AsyncFlow::Private::FDelayedResume Entry;
	Entry.Handle = Handle;
	Entry.ResumeAtTime = World->GetTimeSeconds() + static_cast<double>(Seconds);
	Entry.TimeSource = AsyncFlow::Private::EDelayTimeSource::GameTime;
	Entry.bAlive = MoveTemp(InAlive);
	DelayedResumes.Add(MoveTemp(Entry));
}

void UAsyncFlowTickSubsystem::ScheduleRealDelay(std::coroutine_handle<> Handle, float Seconds, TSharedPtr<bool> InAlive)
{
	AsyncFlow::Private::FDelayedResume Entry;
	Entry.Handle = Handle;
	Entry.ResumeAtTime = FPlatformTime::Seconds() + static_cast<double>(Seconds);
	Entry.TimeSource = AsyncFlow::Private::EDelayTimeSource::RealTime;
	Entry.bAlive = MoveTemp(InAlive);
	DelayedResumes.Add(MoveTemp(Entry));
}

void UAsyncFlowTickSubsystem::ScheduleUnpausedDelay(std::coroutine_handle<> Handle, float Seconds, TSharedPtr<bool> InAlive)
{
	const UWorld* World = GetWorld();
	if (!World) { return; }

	AsyncFlow::Private::FDelayedResume Entry;
	Entry.Handle = Handle;
	Entry.ResumeAtTime = World->GetUnpausedTimeSeconds() + static_cast<double>(Seconds);
	Entry.TimeSource = AsyncFlow::Private::EDelayTimeSource::UnpausedTime;
	Entry.bAlive = MoveTemp(InAlive);
	DelayedResumes.Add(MoveTemp(Entry));
}

void UAsyncFlowTickSubsystem::ScheduleAudioDelay(std::coroutine_handle<> Handle, float Seconds, TSharedPtr<bool> InAlive)
{
	const UWorld* World = GetWorld();
	if (!World) { return; }

	AsyncFlow::Private::FDelayedResume Entry;
	Entry.Handle = Handle;
	Entry.ResumeAtTime = World->GetAudioTimeSeconds() + static_cast<double>(Seconds);
	Entry.TimeSource = AsyncFlow::Private::EDelayTimeSource::AudioTime;
	Entry.bAlive = MoveTemp(InAlive);
	DelayedResumes.Add(MoveTemp(Entry));
}

void UAsyncFlowTickSubsystem::ScheduleActorDilatedDelay(std::coroutine_handle<> Handle, AActor* Actor, float Seconds, TSharedPtr<bool> InAlive)
{
	AsyncFlow::Private::FActorDilatedResume Entry;
	Entry.Handle = Handle;
	Entry.Actor = Actor;
	Entry.RemainingSeconds = Seconds;
	Entry.bAlive = MoveTemp(InAlive);
	ActorDilatedResumes.Add(MoveTemp(Entry));
}

void UAsyncFlowTickSubsystem::ScheduleTicks(std::coroutine_handle<> Handle, int32 NumTicks, TSharedPtr<bool> InAlive)
{
	AsyncFlow::Private::FTickResume Entry;
	Entry.Handle = Handle;
	Entry.RemainingTicks = FMath::Max(NumTicks, 1);
	Entry.bAlive = MoveTemp(InAlive);
	TickResumes.Add(MoveTemp(Entry));
}

void UAsyncFlowTickSubsystem::ScheduleCondition(std::coroutine_handle<> Handle, UObject* Context, TFunction<bool()> Predicate, TSharedPtr<bool> InAlive)
{
	AsyncFlow::Private::FConditionResume Entry;
	Entry.Handle = Handle;
	Entry.Context = Context;
	Entry.Predicate = MoveTemp(Predicate);
	Entry.bAlive = MoveTemp(InAlive);
	ConditionResumes.Add(MoveTemp(Entry));
}

void UAsyncFlowTickSubsystem::ScheduleTickUpdate(std::coroutine_handle<> Handle, TFunction<bool(float)> UpdateFunc, TSharedPtr<bool> InAlive)
{
	AsyncFlow::Private::FTickUpdate Entry;
	Entry.Handle = Handle;
	Entry.UpdateFunc = MoveTemp(UpdateFunc);
	Entry.bAlive = MoveTemp(InAlive);
	TickUpdates.Add(MoveTemp(Entry));
}

void UAsyncFlowTickSubsystem::CancelHandle(std::coroutine_handle<> Handle)
{
	DelayedResumes.RemoveAll([Handle](const AsyncFlow::Private::FDelayedResume& E) { return E.Handle == Handle; });
	ActorDilatedResumes.RemoveAll([Handle](const AsyncFlow::Private::FActorDilatedResume& E) { return E.Handle == Handle; });
	TickResumes.RemoveAll([Handle](const AsyncFlow::Private::FTickResume& E) { return E.Handle == Handle; });
	ConditionResumes.RemoveAll([Handle](const AsyncFlow::Private::FConditionResume& E) { return E.Handle == Handle; });
	TickUpdates.RemoveAll([Handle](const AsyncFlow::Private::FTickUpdate& E) { return E.Handle == Handle; });
}

