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

// AsyncFlowAction.cpp — Base async action implementation.
#include "AsyncFlowAction.h"

void UAsyncFlowAction::Activate()
{
	// Use whatever was pre-seeded by the factory. Fall back to GetWorld() only if
	// the factory didn't set it (e.g. legacy subclasses without a world context param).
	if (!CachedWorldContext.IsValid())
	{
		CachedWorldContext = GetWorld();
	}
	ActiveTask = RunAction();
	ActiveTask.Start();
}

void UAsyncFlowAction::Cancel()
{
	Super::Cancel();
	if (ActiveTask.IsValid())
	{
		ActiveTask.Cancel();
	}
}

AsyncFlow::TTask<void> UAsyncFlowAction::RunAction()
{
	co_await ExecuteAction(CachedWorldContext.Get());

	if (bHandledBroadcast || !ShouldBroadcastDelegates())
	{
		co_return;
	}
	OnCompleted.Broadcast();
}

AsyncFlow::TTask<void> UAsyncFlowAction::ExecuteAction(UObject* InWorldContext)
{
	co_return;
}
