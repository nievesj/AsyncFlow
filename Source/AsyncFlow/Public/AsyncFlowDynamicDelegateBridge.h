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

// AsyncFlowDynamicDelegateBridge.h — UObject bridge for dynamic delegate awaiting
//
// Dynamic delegates (DECLARE_DYNAMIC_MULTICAST_DELEGATE_*) require UFUNCTION
// targets for binding. This UObject provides the UFUNCTION callbacks that
// resume a waiting coroutine when the dynamic delegate fires.
//
// Currently supports zero-arg dynamic multicast delegates.
// For typed dynamic delegates, use AsyncFlow::Chain() with manual binding.
#pragma once

#include "UObject/Object.h"
#include "Delegates/DelegateCombinations.h"

#include <coroutine>

#include "AsyncFlowDynamicDelegateBridge.generated.h"

/**
 * Minimal UObject that provides UFUNCTION callback targets for dynamic delegate
 * awaiting. Instances are created on-demand per co_await and destroyed after
 * the delegate fires or the awaiter is cancelled.
 *
 * Not intended for direct use — created internally by the dynamic delegate awaiters.
 */
UCLASS(Transient)
class ASYNCFLOW_API UAsyncFlowDelegateBridge : public UObject
{
	GENERATED_BODY()

public:
	/** UFUNCTION callback for zero-arg dynamic delegates. */
	UFUNCTION()
	void OnSimpleDelegateFired();

	/** The coroutine handle to resume when the delegate fires. */
	std::coroutine_handle<> Continuation;

	/** Weak alive flag — if expired or false, the coroutine frame is gone. */
	TWeakPtr<bool> AliveFlag;

	/** Set to true once the delegate has fired (prevents double-resume). */
	bool bFired = false;
};
