// AsyncFlowTickSubsystem.h — World subsystem that drives coroutine timing
#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Containers/Array.h"
#include "Templates/Function.h"

#include <coroutine>

#include "AsyncFlowTickSubsystem.generated.h"

namespace AsyncFlow::Private
{

/** Entry for a timed resume (game-time or real-time). */
struct FDelayedResume
{
	std::coroutine_handle<> Handle;
	double ResumeAtTime = 0.0;
	bool bUseRealTime = false;
};

/** Entry for a tick-count resume. */
struct FTickResume
{
	std::coroutine_handle<> Handle;
	int32 RemainingTicks = 0;
};

/** Entry for a condition-based resume. */
struct FConditionResume
{
	std::coroutine_handle<> Handle;
	TWeakObjectPtr<UObject> Context;
	TFunction<bool()> Predicate;
};

/**
 * Entry for a per-tick update that calls a function each frame.
 * Returns true when finished (entry is then removed and coroutine is resumed).
 */
struct FTickUpdate
{
	std::coroutine_handle<> Handle;
	TFunction<bool(float DeltaTime)> UpdateFunc;
};

} // namespace AsyncFlow::Private

/**
 * UAsyncFlowTickSubsystem
 * Per-world subsystem that ticks every frame to resume delayed, tick-based,
 * and condition-based coroutine awaiters.
 */
UCLASS()
class ASYNCFLOW_API UAsyncFlowTickSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// UTickableWorldSubsystem
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Schedule a coroutine to resume after a delay (game-dilated time). */
	void ScheduleDelay(std::coroutine_handle<> Handle, float Seconds);

	/** Schedule a coroutine to resume after a delay (real wall-clock time). */
	void ScheduleRealDelay(std::coroutine_handle<> Handle, float Seconds);

	/** Schedule a coroutine to resume after N ticks. */
	void ScheduleTicks(std::coroutine_handle<> Handle, int32 NumTicks);

	/** Schedule a coroutine to resume when a predicate returns true. Checks each tick. */
	void ScheduleCondition(std::coroutine_handle<> Handle, UObject* Context, TFunction<bool()> Predicate);

	/**
	 * Schedule a per-tick update function. Called each frame with DeltaTime.
	 * When UpdateFunc returns true, the entry is removed and the coroutine Handle is resumed.
	 */
	void ScheduleTickUpdate(std::coroutine_handle<> Handle, TFunction<bool(float)> UpdateFunc);

	/** Remove all pending resumes associated with the given coroutine handle. */
	void CancelHandle(std::coroutine_handle<> Handle);

private:
	TArray<AsyncFlow::Private::FDelayedResume> DelayedResumes;
	TArray<AsyncFlow::Private::FTickResume> TickResumes;
	TArray<AsyncFlow::Private::FConditionResume> ConditionResumes;
	TArray<AsyncFlow::Private::FTickUpdate> TickUpdates;
};


