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

// AsyncFlowTickSubsystem.h — World subsystem that drives coroutine timing
#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Containers/Array.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"
#include "UObject/WeakObjectPtrTemplates.h"

#include <coroutine>

#include "AsyncFlowTickSubsystem.generated.h"

class AActor;

namespace AsyncFlow::Private
{

/** Time source enum for delay entries. */
enum class EDelayTimeSource : uint8
{
	GameTime,
	RealTime,
	UnpausedTime,
	AudioTime
};

/**
 * Entry for a timed resume using various time sources.
 * bAlive is shared with the awaiter; when the awaiter is destroyed mid-suspension
 * it sets *bAlive = false and the subsystem skips the resume on the next tick.
 */
struct FDelayedResume
{
	std::coroutine_handle<> Handle;
	double ResumeAtTime = 0.0;
	EDelayTimeSource TimeSource = EDelayTimeSource::GameTime;
	TSharedPtr<bool> bAlive;
};

/** Entry for actor-dilated delay. */
struct FActorDilatedResume
{
	std::coroutine_handle<> Handle;
	TWeakObjectPtr<AActor> Actor;
	float RemainingSeconds = 0.0f;
	TSharedPtr<bool> bAlive;
};

/** Entry for a tick-count resume. */
struct FTickResume
{
	std::coroutine_handle<> Handle;
	int32 RemainingTicks = 0;
	TSharedPtr<bool> bAlive;
};

/** Entry for a condition-based resume. */
struct FConditionResume
{
	std::coroutine_handle<> Handle;
	TWeakObjectPtr<UObject> Context;
	TFunction<bool()> Predicate;
	TSharedPtr<bool> bAlive;
};

/**
 * Entry for a per-tick update that calls a function each frame.
 * Returns true when finished (entry is then removed and coroutine is resumed).
 */
struct FTickUpdate
{
	std::coroutine_handle<> Handle;
	TFunction<bool(float DeltaTime)> UpdateFunc;
	TSharedPtr<bool> bAlive;
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
	void ScheduleDelay(std::coroutine_handle<> Handle, float Seconds, TSharedPtr<bool> InAlive = nullptr);

	/** Schedule a coroutine to resume after a delay (real wall-clock time). */
	void ScheduleRealDelay(std::coroutine_handle<> Handle, float Seconds, TSharedPtr<bool> InAlive = nullptr);

	/** Schedule a coroutine to resume after a delay (unpaused time). */
	void ScheduleUnpausedDelay(std::coroutine_handle<> Handle, float Seconds, TSharedPtr<bool> InAlive = nullptr);

	/** Schedule a coroutine to resume after a delay (audio time). */
	void ScheduleAudioDelay(std::coroutine_handle<> Handle, float Seconds, TSharedPtr<bool> InAlive = nullptr);

	/** Schedule a coroutine to resume after Seconds scaled by an actor's CustomTimeDilation. */
	void ScheduleActorDilatedDelay(std::coroutine_handle<> Handle, AActor* Actor, float Seconds, TSharedPtr<bool> InAlive = nullptr);

	/** Schedule a coroutine to resume after N ticks. */
	void ScheduleTicks(std::coroutine_handle<> Handle, int32 NumTicks, TSharedPtr<bool> InAlive = nullptr);

	/** Schedule a coroutine to resume when a predicate returns true. Checks each tick. */
	void ScheduleCondition(std::coroutine_handle<> Handle, UObject* Context, TFunction<bool()> Predicate, TSharedPtr<bool> InAlive = nullptr);

	/**
	 * Schedule a per-tick update function. Called each frame with DeltaTime.
	 * When UpdateFunc returns true, the entry is removed and the coroutine Handle is resumed.
	 */
	void ScheduleTickUpdate(std::coroutine_handle<> Handle, TFunction<bool(float)> UpdateFunc, TSharedPtr<bool> InAlive = nullptr);

	/** Remove all pending resumes associated with the given coroutine handle. */
	void CancelHandle(std::coroutine_handle<> Handle);

private:
	TArray<AsyncFlow::Private::FDelayedResume> DelayedResumes;
	TArray<AsyncFlow::Private::FActorDilatedResume> ActorDilatedResumes;
	TArray<AsyncFlow::Private::FTickResume> TickResumes;
	TArray<AsyncFlow::Private::FConditionResume> ConditionResumes;
	TArray<AsyncFlow::Private::FTickUpdate> TickUpdates;
};

