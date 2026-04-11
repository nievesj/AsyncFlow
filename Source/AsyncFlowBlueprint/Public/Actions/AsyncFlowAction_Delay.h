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

// AsyncFlowAction_Delay.h — Blueprint async action that suspends for a duration.
#pragma once

#include "AsyncFlowAction.h"
#include "AsyncFlowAction_Delay.generated.h"

/**
 * Blueprint node that suspends execution for the given duration using
 * AsyncFlow's game-time-dilated Delay awaiter.
 */
UCLASS(meta = (DisplayName = "Async Flow Delay"))
class ASYNCFLOWBLUEPRINT_API UAsyncFlowAction_Delay : public UAsyncFlowAction
{
	GENERATED_BODY()

public:
	/** Suspends for Duration seconds (game-dilated time). */
	UFUNCTION(BlueprintCallable,
		meta = (BlueprintInternalUseOnly = "true",
			WorldContext = "WorldContextObject",
			DisplayName = "Async Flow Delay"),
		Category = "AsyncFlow")
	static UAsyncFlowAction_Delay* AsyncDelay(
		UObject* WorldContextObject,
		float Duration);

protected:
	virtual AsyncFlow::TTask<void> ExecuteAction(UObject* WorldContext) override;

private:
	float Duration = 1.0f;
};
