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

// AsyncFlowEditorTickDriver.h — Editor tick driver for coroutine scheduling
//
// FAsyncFlowEditorTickDriver is a FTickableEditorObject singleton that drives
// coroutine scheduling in pure-editor contexts (no game world required).
// It ticks every editor frame and processes four resume categories:
// real-time delays, tick counts, condition predicates, and per-tick updates.
//
// All scheduling and ticking happens on the editor main thread.
#pragma once

#include "Tickable.h"
#include "Containers/Array.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"

#include <coroutine>

namespace AsyncFlow::EditorPrivate
{

	struct FEditorDelayedResume
	{
		std::coroutine_handle<> Handle;
		double ResumeAtTime = 0.0;
		TSharedPtr<bool> bAlive;
	};

	struct FEditorTickResume
	{
		std::coroutine_handle<> Handle;
		int32 RemainingTicks = 0;
		TSharedPtr<bool> bAlive;
	};

	struct FEditorConditionResume
	{
		std::coroutine_handle<> Handle;
		TFunction<bool()> Predicate;
		TSharedPtr<bool> bAlive;
	};

	struct FEditorTickUpdate
	{
		std::coroutine_handle<> Handle;
		TFunction<bool(float DeltaTime)> UpdateFunc;
		TSharedPtr<bool> bAlive;
	};

} // namespace AsyncFlow::EditorPrivate

/**
 * FAsyncFlowEditorTickDriver
 *
 * Singleton FTickableEditorObject that drives coroutine scheduling in the
 * editor. Ticks every editor frame (even without PIE) and processes its
 * four internal arrays: delays -> ticks -> conditions -> tick-updates.
 *
 * Created by FAsyncFlowEditorModule::StartupModule() and destroyed on shutdown.
 * Use Get()/IsAvailable() to access the singleton.
 */
class ASYNCFLOWEDITOR_API FAsyncFlowEditorTickDriver : public FTickableEditorObject
{
public:
	FAsyncFlowEditorTickDriver();
	virtual ~FAsyncFlowEditorTickDriver() override;

	/** @return the singleton instance. Check IsAvailable() first or ensure the module is loaded. */
	static FAsyncFlowEditorTickDriver& Get();

	/** @return true if the tick driver singleton exists (module is loaded). */
	static bool IsAvailable();

	/**
	 * Schedule a coroutine to resume after a wall-clock delay.
	 *
	 * @param Handle   The suspended coroutine handle.
	 * @param Seconds  Delay in real seconds (FPlatformTime::Seconds).
	 * @param InAlive  Shared alive flag from the awaiter. Null means no tracking.
	 */
	void ScheduleDelay(std::coroutine_handle<> Handle, float Seconds, TSharedPtr<bool> InAlive = nullptr);

	/**
	 * Schedule a coroutine to resume after NumTicks editor frames.
	 *
	 * @param Handle    The suspended coroutine handle.
	 * @param NumTicks  Number of editor ticks to wait. Clamped to at least 1.
	 * @param InAlive   Shared alive flag from the awaiter.
	 */
	void ScheduleTicks(std::coroutine_handle<> Handle, int32 NumTicks, TSharedPtr<bool> InAlive = nullptr);

	/**
	 * Schedule a coroutine to resume when Predicate returns true. Checked once per editor tick.
	 *
	 * @param Handle     The suspended coroutine handle.
	 * @param Predicate  Callable returning bool. Must be editor-main-thread-safe.
	 * @param InAlive    Shared alive flag from the awaiter.
	 */
	void ScheduleCondition(std::coroutine_handle<> Handle, TFunction<bool()> Predicate, TSharedPtr<bool> InAlive = nullptr);

	/**
	 * Schedule a per-tick update function. Called each editor frame with DeltaTime.
	 * When UpdateFunc returns true, the entry is removed and Handle is resumed.
	 *
	 * @param Handle      The suspended coroutine handle.
	 * @param UpdateFunc  Per-tick callable. Return true when finished.
	 * @param InAlive     Shared alive flag from the awaiter.
	 */
	void ScheduleTickUpdate(std::coroutine_handle<> Handle, TFunction<bool(float)> UpdateFunc, TSharedPtr<bool> InAlive = nullptr);

	/**
	 * Remove all pending entries associated with the given coroutine handle.
	 *
	 * @param Handle  The coroutine handle to purge from all arrays.
	 */
	void CancelHandle(std::coroutine_handle<> Handle);

	// FTickableEditorObject interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override
	{
		return true;
	}

private:
	static FAsyncFlowEditorTickDriver* Instance;

	TArray<AsyncFlow::EditorPrivate::FEditorDelayedResume> DelayedResumes;
	TArray<AsyncFlow::EditorPrivate::FEditorTickResume> TickResumes;
	TArray<AsyncFlow::EditorPrivate::FEditorConditionResume> ConditionResumes;
	TArray<AsyncFlow::EditorPrivate::FEditorTickUpdate> TickUpdates;
};
