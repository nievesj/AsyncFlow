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

// AsyncFlowBudget.h — Tick time budget awaiter for AsyncFlow
//
// Usage:
//   FTickTimeBudget Budget(5.0);  // 5ms per frame
//   for (auto& Item : LargeArray) {
//       ProcessItem(Item);
//       co_await Budget;  // If budget exceeded, suspend until next tick
//   }

#pragma once

#include "HAL/PlatformTime.h"
#include "Async/Async.h"
#include "AsyncFlowTask.h"

namespace AsyncFlow
{

	class FTickTimeBudget
	{
	public:
		explicit FTickTimeBudget(double InBudgetMs)
			: BudgetMs(InBudgetMs)
			, FrameStartSeconds(FPlatformTime::Seconds())
		{
		}

		// Reset the timer (called automatically on resume after suspension)
		void Reset()
		{
			FrameStartSeconds = FPlatformTime::Seconds();
		}

		// Check if budget is exceeded
		bool IsExceeded() const
		{
			double ElapsedMs = (FPlatformTime::Seconds() - FrameStartSeconds) * 1000.0;
			return ElapsedMs >= BudgetMs;
		}

		double GetElapsedMs() const
		{
			return (FPlatformTime::Seconds() - FrameStartSeconds) * 1000.0;
		}

		double GetBudgetMs() const
		{
			return BudgetMs;
		}

		// Awaitable interface
		bool await_ready() const
		{
			return !IsExceeded(); // If budget not exceeded, don't suspend
		}

		void await_suspend(std::coroutine_handle<> Handle)
		{
			// Budget exceeded — schedule resume on next game thread tick
			AsyncTask(ENamedThreads::GameThread, [this, Handle]() mutable {
				Reset();
				Handle.resume();
			});
		}

		void await_resume()
		{
			// If we didn't suspend (await_ready returned true), nothing to do.
			// If we suspended and resumed, Reset() was already called in await_suspend.
		}

	private:
		double BudgetMs;
		double FrameStartSeconds;
	};

} // namespace AsyncFlow
