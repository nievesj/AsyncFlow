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
//
// UAsyncFlowTickSubsystem ticks every frame to process five resume categories:
// timed delays (game, real, unpaused, audio), actor-dilated delays, tick counts,
// condition predicates, and per-tick update functions. All arrays are iterated
// back-to-front with RemoveAtSwap for O(1) removal.
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

	/**
 * Which clock drives a timed delay entry.
 * The subsystem reads the corresponding UWorld time source each tick.
 */
	enum class EDelayTimeSource : uint8
	{
		GameTime,	  ///< UWorld::GetTimeSeconds() — respects pause and global dilation.
		RealTime,	  ///< FPlatformTime::Seconds() — wall clock, independent of game state.
		UnpausedTime, ///< UWorld::GetUnpausedTimeSeconds() — ticks during pause, respects dilation.
		AudioTime	  ///< UWorld::GetAudioTimeSeconds() — audio engine clock.
	};

	/**
 * Entry for a timed resume. Holds the coroutine handle, the absolute
 * time at which to resume, and the clock source.
 *
 * bAlive is shared with the awaiter's FAwaiterAliveFlag. When the awaiter
 * is destroyed mid-suspension, *bAlive becomes false and the subsystem
 * discards this entry on the next tick instead of resuming a dead frame.
 */
	struct FDelayedResume
	{
		std::coroutine_handle<> Handle;
		double ResumeAtTime = 0.0;
		EDelayTimeSource TimeSource = EDelayTimeSource::GameTime;
		TSharedPtr<bool> bAlive;
	};

	/**
 * Entry for actor-dilated delays. RemainingSeconds is decremented each
 * tick by DeltaTime * Actor->CustomTimeDilation. If the actor is GC'd
 * (TWeakObjectPtr becomes invalid), the entry is discarded.
 */
	struct FActorDilatedResume
	{
		std::coroutine_handle<> Handle;
		TWeakObjectPtr<AActor> Actor;
		float RemainingSeconds = 0.0f;
		TSharedPtr<bool> bAlive;
	};

	/**
 * Entry for tick-count resumes. RemainingTicks is decremented by 1
 * each frame. When it hits 0, the coroutine is resumed.
 */
	struct FTickResume
	{
		std::coroutine_handle<> Handle;
		int32 RemainingTicks = 0;
		TSharedPtr<bool> bAlive;
	};

	/**
 * Entry for condition-based resumes. The Predicate is called each tick;
 * when it returns true, the coroutine is resumed and the entry is removed.
 *
 * Context is held via TWeakObjectPtr. If the context object is GC'd,
 * the entry is silently discarded.
 */
	struct FConditionResume
	{
		std::coroutine_handle<> Handle;
		TWeakObjectPtr<UObject> Context;
		TFunction<bool()> Predicate;
		TSharedPtr<bool> bAlive;
	};

	/**
 * Entry for per-tick updates (Timeline, MoveComponentTo, etc.).
 * UpdateFunc is called each frame with DeltaTime. When it returns true,
 * the entry is removed and the coroutine Handle is resumed.
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
 *
 * Per-world tickable subsystem that drives all time-based, tick-based,
 * and condition-based coroutine awaiters. Ticks every frame and processes
 * its five internal arrays in order: delays → actor-dilated → ticks →
 * conditions → tick-updates.
 *
 * Created automatically by the engine for each UWorld. No manual setup needed.
 */
UCLASS()
class ASYNCFLOW_API UAsyncFlowTickSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/**
	 * Schedule a coroutine to resume after a delay using game-dilated time.
	 *
	 * @param Handle   The suspended coroutine handle.
	 * @param Seconds  Delay in game-dilated seconds.
	 * @param InAlive  Shared alive flag from the awaiter. Null means no tracking.
	 */
	void ScheduleDelay(std::coroutine_handle<> Handle, float Seconds, TSharedPtr<bool> InAlive = nullptr);

	/**
	 * Schedule a coroutine to resume after a delay using real wall-clock time.
	 *
	 * @param Handle   The suspended coroutine handle.
	 * @param Seconds  Delay in real seconds (FPlatformTime::Seconds).
	 * @param InAlive  Shared alive flag from the awaiter.
	 */
	void ScheduleRealDelay(std::coroutine_handle<> Handle, float Seconds, TSharedPtr<bool> InAlive = nullptr);

	/**
	 * Schedule a coroutine to resume after a delay using unpaused time.
	 *
	 * @param Handle   The suspended coroutine handle.
	 * @param Seconds  Delay in unpaused seconds.
	 * @param InAlive  Shared alive flag from the awaiter.
	 */
	void ScheduleUnpausedDelay(std::coroutine_handle<> Handle, float Seconds, TSharedPtr<bool> InAlive = nullptr);

	/**
	 * Schedule a coroutine to resume after a delay using audio time.
	 *
	 * @param Handle   The suspended coroutine handle.
	 * @param Seconds  Delay in audio-clock seconds.
	 * @param InAlive  Shared alive flag from the awaiter.
	 */
	void ScheduleAudioDelay(std::coroutine_handle<> Handle, float Seconds, TSharedPtr<bool> InAlive = nullptr);

	/**
	 * Schedule a coroutine to resume after Seconds scaled by an actor's CustomTimeDilation.
	 *
	 * @param Handle   The suspended coroutine handle.
	 * @param Actor    The actor whose dilation scales the delay.
	 * @param Seconds  Base delay before dilation.
	 * @param InAlive  Shared alive flag from the awaiter.
	 */
	void ScheduleActorDilatedDelay(std::coroutine_handle<> Handle, AActor* Actor, float Seconds, TSharedPtr<bool> InAlive = nullptr);

	/**
	 * Schedule a coroutine to resume after NumTicks frames.
	 *
	 * @param Handle    The suspended coroutine handle.
	 * @param NumTicks  Number of ticks to wait. Clamped to at least 1.
	 * @param InAlive   Shared alive flag from the awaiter.
	 */
	void ScheduleTicks(std::coroutine_handle<> Handle, int32 NumTicks, TSharedPtr<bool> InAlive = nullptr);

	/**
	 * Schedule a coroutine to resume when Predicate returns true. Checked once per tick.
	 *
	 * @param Handle     The suspended coroutine handle.
	 * @param Context    UObject for lifetime tracking (entry is discarded if GC'd).
	 * @param Predicate  Callable returning bool. Must be game-thread-safe.
	 * @param InAlive    Shared alive flag from the awaiter.
	 */
	void ScheduleCondition(std::coroutine_handle<> Handle, UObject* Context, TFunction<bool()> Predicate, TSharedPtr<bool> InAlive = nullptr);

	/**
	 * Schedule a per-tick update function. Called each frame with DeltaTime.
	 * When UpdateFunc returns true, the entry is removed and Handle is resumed.
	 *
	 * @param Handle      The suspended coroutine handle.
	 * @param UpdateFunc  Per-tick callable. Return true when finished.
	 * @param InAlive     Shared alive flag from the awaiter.
	 */
	void ScheduleTickUpdate(std::coroutine_handle<> Handle, TFunction<bool(float)> UpdateFunc, TSharedPtr<bool> InAlive = nullptr);

	/**
	 * Remove all pending entries associated with the given coroutine handle.
	 * Called when a task is cancelled and you want to stop any pending resumes.
	 *
	 * @param Handle  The coroutine handle to purge from all arrays.
	 */
	void CancelHandle(std::coroutine_handle<> Handle);

private:
	TArray<AsyncFlow::Private::FDelayedResume> DelayedResumes;
	TArray<AsyncFlow::Private::FActorDilatedResume> ActorDilatedResumes;
	TArray<AsyncFlow::Private::FTickResume> TickResumes;
	TArray<AsyncFlow::Private::FConditionResume> ConditionResumes;
	TArray<AsyncFlow::Private::FTickUpdate> TickUpdates;
};
