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

// AsyncFlowTask.cpp — Thread-local state management and FCancellationGuard implementation.
//
// GCurrentFlowState is a thread-local pointer set to the active promise during
// Resume(). It lets CO_CONTRACT and co_verifyf access the coroutine's flow state
// without passing it explicitly through every call frame.
//
// FCancellationGuard reads GCurrentFlowState on construction to find the
// enclosing coroutine, then increments/decrements CancellationGuardDepth
// atomically. The atomic ops use acq_rel ordering because background awaiters
// may read bCancelled from a different thread.
#include "AsyncFlowTask.h"

namespace AsyncFlow::Private
{

static thread_local FAsyncFlowState* GCurrentFlowState = nullptr;

FAsyncFlowState* GetCurrentFlowState()
{
	return GCurrentFlowState;
}

void SetCurrentFlowState(FAsyncFlowState* State)
{
	GCurrentFlowState = State;
}

} // namespace AsyncFlow::Private

namespace AsyncFlow
{

FCancellationGuard::FCancellationGuard()
{
	State = Private::GetCurrentFlowState();
	if (State)
	{
		State->CancellationGuardDepth.fetch_add(1, std::memory_order_acq_rel);
	}
}

FCancellationGuard::~FCancellationGuard()
{
	if (State)
	{
		State->CancellationGuardDepth.fetch_sub(1, std::memory_order_acq_rel);
	}
}

} // namespace AsyncFlow
