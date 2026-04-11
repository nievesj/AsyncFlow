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

// AsyncFlowAction.h — Base class for Blueprint-exposed AsyncFlow coroutine actions.
#pragma once

#include "AsyncFlowTask.h"
#include "Engine/CancellableAsyncAction.h"
#include "AsyncFlowAction.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FAsyncFlowOutputPin);

/**
 * Base class for all AsyncFlow Blueprint async actions.
 *
 * Subclasses override ExecuteAction() and return a TTask<void> coroutine that
 * performs the async work. The base Activate() → RunAction() pipeline
 * automatically broadcasts OnCompleted when ExecuteAction finishes, unless the
 * subclass sets bHandledBroadcast to true and broadcasts its own delegates.
 */
UCLASS(Abstract)
class ASYNCFLOWBLUEPRINT_API UAsyncFlowAction : public UCancellableAsyncAction
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable)
	FAsyncFlowOutputPin OnCompleted;

	UPROPERTY(BlueprintAssignable)
	FAsyncFlowOutputPin OnFailed;

	virtual void Activate() override;
	virtual void Cancel() override;

protected:
	/** Subclasses return their coroutine from this. Called by RunAction(). */
	virtual AsyncFlow::TTask<void> ExecuteAction(UObject* WorldContext);

	/**
	 * Set to true in ExecuteAction if the subclass broadcasts its own
	 * delegates. Prevents RunAction from also broadcasting OnCompleted.
	 */
	bool bHandledBroadcast = false;

	/**
	 * World context object pre-seeded by the factory function before Activate() is called.
	 * Subclass factories should assign this from their WorldContextObject parameter so that
	 * Activate() does not have to rely on GetWorld() (which returns null when the action was
	 * created via NewObject<> without a world-connected outer).
	 */
	TWeakObjectPtr<UObject> CachedWorldContext;

private:
	AsyncFlow::TTask<void> ActiveTask;

	/** Internal coroutine that wraps ExecuteAction and broadcasts delegates. */
	AsyncFlow::TTask<void> RunAction();
};
