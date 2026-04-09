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

// AsyncFlowEditorAwaiters.h — Editor-specific coroutine awaiters
//
// All awaiters in this file are editor-main-thread-only and do NOT require
// a world context (UObject*). They schedule through FAsyncFlowEditorTickDriver,
// which ticks every editor frame regardless of PIE state.
//
// Use these from editor tools, custom asset editors, editor utilities, and
// standalone editor windows where no UWorld is available.
#pragma once

#include "AsyncFlowEditorTickDriver.h"
#include "AsyncFlowAwaiters.h" // For Private::FAwaiterAliveFlag
#include "AsyncFlowLogging.h"

#include <coroutine>

namespace AsyncFlow
{

	// ============================================================================
	// EditorDelay — suspends for N real-time seconds in editor context
	// ============================================================================

	/**
 * Awaiter struct for editor wall-clock delays. Uses FPlatformTime::Seconds().
 * Resumes immediately if Seconds <= 0 or the editor tick driver is unavailable.
 */
	struct FEditorDelayAwaiter
	{
		float Seconds = 0.0f;
		Private::FAwaiterAliveFlag AliveFlag;

		FEditorDelayAwaiter() = default;
		FEditorDelayAwaiter(FEditorDelayAwaiter&&) noexcept = default;
		FEditorDelayAwaiter& operator=(FEditorDelayAwaiter&&) noexcept = default;
		FEditorDelayAwaiter(const FEditorDelayAwaiter&) = delete;
		FEditorDelayAwaiter& operator=(const FEditorDelayAwaiter&) = delete;

		bool await_ready() const
		{
			return Seconds <= 0.0f;
		}

		void await_suspend(std::coroutine_handle<> Handle) const
		{
			if (!FAsyncFlowEditorTickDriver::IsAvailable())
			{
				UE_LOG(LogAsyncFlow, Warning, TEXT("EditorDelay: tick driver not available — resuming immediately"));
				Handle.resume();
				return;
			}
			FAsyncFlowEditorTickDriver::Get().ScheduleDelay(Handle, Seconds, AliveFlag.Get());
		}

		void await_resume() const
		{
		}
	};

	/**
 * Suspend for Seconds of real wall-clock time in editor context.
 * No UWorld or world context required.
 *
 * @param Seconds  Duration in real seconds. Values <= 0 resume immediately.
 * @return         An awaiter — use with co_await.
 */
	[[nodiscard]] inline FEditorDelayAwaiter EditorDelay(float Seconds)
	{
		FEditorDelayAwaiter Aw;
		Aw.Seconds = Seconds;
		return Aw;
	}

	// ============================================================================
	// EditorNextTick — suspends until the next editor frame
	// ============================================================================

	/**
 * Awaiter struct that yields for exactly one editor tick.
 * Always suspends (await_ready returns false).
 */
	struct FEditorNextTickAwaiter
	{
		Private::FAwaiterAliveFlag AliveFlag;

		FEditorNextTickAwaiter() = default;
		FEditorNextTickAwaiter(FEditorNextTickAwaiter&&) noexcept = default;
		FEditorNextTickAwaiter& operator=(FEditorNextTickAwaiter&&) noexcept = default;
		FEditorNextTickAwaiter(const FEditorNextTickAwaiter&) = delete;
		FEditorNextTickAwaiter& operator=(const FEditorNextTickAwaiter&) = delete;

		bool await_ready() const
		{
			return false;
		}

		void await_suspend(std::coroutine_handle<> Handle) const
		{
			if (!FAsyncFlowEditorTickDriver::IsAvailable())
			{
				UE_LOG(LogAsyncFlow, Warning, TEXT("EditorNextTick: tick driver not available — resuming immediately"));
				Handle.resume();
				return;
			}
			FAsyncFlowEditorTickDriver::Get().ScheduleTicks(Handle, 1, AliveFlag.Get());
		}

		void await_resume() const
		{
		}
	};

	/**
 * Suspend until the next editor tick.
 * No UWorld or world context required.
 *
 * @return  An awaiter — use with co_await.
 */
	[[nodiscard]] inline FEditorNextTickAwaiter EditorNextTick()
	{
		return FEditorNextTickAwaiter{};
	}

	// ============================================================================
	// EditorTicks — suspends for N editor frames
	// ============================================================================

	/**
 * Awaiter struct that yields for a specified number of editor ticks.
 * Resumes immediately if NumTicks <= 0.
 */
	struct FEditorTicksAwaiter
	{
		int32 NumTicks = 0;
		Private::FAwaiterAliveFlag AliveFlag;

		FEditorTicksAwaiter() = default;
		FEditorTicksAwaiter(FEditorTicksAwaiter&&) noexcept = default;
		FEditorTicksAwaiter& operator=(FEditorTicksAwaiter&&) noexcept = default;
		FEditorTicksAwaiter(const FEditorTicksAwaiter&) = delete;
		FEditorTicksAwaiter& operator=(const FEditorTicksAwaiter&) = delete;

		bool await_ready() const
		{
			return NumTicks <= 0;
		}

		void await_suspend(std::coroutine_handle<> Handle) const
		{
			if (!FAsyncFlowEditorTickDriver::IsAvailable())
			{
				UE_LOG(LogAsyncFlow, Warning, TEXT("EditorTicks: tick driver not available — resuming immediately"));
				Handle.resume();
				return;
			}
			FAsyncFlowEditorTickDriver::Get().ScheduleTicks(Handle, NumTicks, AliveFlag.Get());
		}

		void await_resume() const
		{
		}
	};

	/**
 * Suspend for InNumTicks editor ticks.
 * No UWorld or world context required.
 *
 * @param InNumTicks  Number of editor ticks to wait. Clamped to at least 1 in the driver.
 * @return            An awaiter — use with co_await.
 */
	[[nodiscard]] inline FEditorTicksAwaiter EditorTicks(int32 InNumTicks)
	{
		FEditorTicksAwaiter Aw;
		Aw.NumTicks = InNumTicks;
		return Aw;
	}

	// ============================================================================
	// EditorWaitForCondition — polls predicate each editor tick
	// ============================================================================

	/**
 * Awaiter struct that checks a predicate every editor tick and resumes
 * the coroutine when the predicate returns true. If the predicate is
 * already true at the point of co_await, no suspension occurs.
 */
	struct FEditorConditionAwaiter
	{
		TFunction<bool()> Predicate;
		Private::FAwaiterAliveFlag AliveFlag;

		FEditorConditionAwaiter() = default;
		FEditorConditionAwaiter(FEditorConditionAwaiter&&) noexcept = default;
		FEditorConditionAwaiter& operator=(FEditorConditionAwaiter&&) noexcept = default;
		FEditorConditionAwaiter(const FEditorConditionAwaiter&) = delete;
		FEditorConditionAwaiter& operator=(const FEditorConditionAwaiter&) = delete;

		bool await_ready() const
		{
			return Predicate && Predicate();
		}

		void await_suspend(std::coroutine_handle<> Handle) const
		{
			if (!FAsyncFlowEditorTickDriver::IsAvailable())
			{
				UE_LOG(LogAsyncFlow, Warning, TEXT("EditorWaitForCondition: tick driver not available — resuming immediately"));
				Handle.resume();
				return;
			}
			FAsyncFlowEditorTickDriver::Get().ScheduleCondition(Handle, Predicate, AliveFlag.Get());
		}

		void await_resume() const
		{
		}
	};

	/**
 * Suspend until InPredicate returns true. Evaluated once per editor tick.
 * No UWorld or world context required.
 *
 * @param InPredicate  Callable returning bool. Must be safe to call from the editor main thread.
 * @return             An awaiter — use with co_await.
 */
	[[nodiscard]] inline FEditorConditionAwaiter EditorWaitForCondition(TFunction<bool()> InPredicate)
	{
		FEditorConditionAwaiter Aw;
		Aw.Predicate = MoveTemp(InPredicate);
		return Aw;
	}

	// ============================================================================
	// EditorTickUpdate — per-tick update function in editor context
	// ============================================================================

	/**
 * Awaiter struct that calls an update function every editor tick.
 * When the function returns true, the coroutine is resumed.
 * Useful for progressive editor operations (e.g., multi-frame processing).
 */
	struct FEditorTickUpdateAwaiter
	{
		TFunction<bool(float DeltaTime)> UpdateFunc;
		Private::FAwaiterAliveFlag AliveFlag;

		FEditorTickUpdateAwaiter() = default;
		FEditorTickUpdateAwaiter(FEditorTickUpdateAwaiter&&) noexcept = default;
		FEditorTickUpdateAwaiter& operator=(FEditorTickUpdateAwaiter&&) noexcept = default;
		FEditorTickUpdateAwaiter(const FEditorTickUpdateAwaiter&) = delete;
		FEditorTickUpdateAwaiter& operator=(const FEditorTickUpdateAwaiter&) = delete;

		bool await_ready() const
		{
			return false;
		}

		void await_suspend(std::coroutine_handle<> Handle) const
		{
			if (!FAsyncFlowEditorTickDriver::IsAvailable())
			{
				UE_LOG(LogAsyncFlow, Warning, TEXT("EditorTickUpdate: tick driver not available — resuming immediately"));
				Handle.resume();
				return;
			}
			FAsyncFlowEditorTickDriver::Get().ScheduleTickUpdate(Handle, UpdateFunc, AliveFlag.Get());
		}

		void await_resume() const
		{
		}
	};

	/**
 * Schedule a per-tick update in editor context. Called each editor frame
 * with DeltaTime. When InUpdateFunc returns true, the coroutine resumes.
 * No UWorld or world context required.
 *
 * @param InUpdateFunc  Per-tick callable. Return true when finished.
 * @return              An awaiter — use with co_await.
 */
	[[nodiscard]] inline FEditorTickUpdateAwaiter EditorTickUpdate(TFunction<bool(float)> InUpdateFunc)
	{
		FEditorTickUpdateAwaiter Aw;
		Aw.UpdateFunc = MoveTemp(InUpdateFunc);
		return Aw;
	}

} // namespace AsyncFlow
