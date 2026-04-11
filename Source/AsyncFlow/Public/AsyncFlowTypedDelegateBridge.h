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

// AsyncFlowTypedDelegateBridge.h — UObject bridge for typed dynamic delegate awaiting
//
// Dynamic delegates (DECLARE_DYNAMIC_MULTICAST_DELEGATE_*Param) require UFUNCTION
// targets. This bridge provides a zero-param UFUNCTION placeholder and overrides
// ProcessEvent to capture the raw parameter bytes from the delegate's broadcast
// frame. The awaiter template then extracts the typed values at compile-time-known
// offsets.
//
// This approach allows a SINGLE bridge class to handle ANY dynamic delegate
// signature without requiring per-type bridge classes or CustomThunk tricks.
#pragma once

#include "UObject/Object.h"

#include <coroutine>

#include "AsyncFlowTypedDelegateBridge.generated.h"

/**
 * Generic UObject bridge for typed dynamic delegate awaiting.
 *
 * Unlike UAsyncFlowDelegateBridge (zero-arg only), this bridge captures the
 * raw parameter bytes from any dynamic delegate broadcast via ProcessEvent
 * interception. The awaiter template reads the captured bytes back into a
 * TTuple using compile-time offset calculations.
 *
 * Lifecycle:
 *   1. Awaiter creates bridge, sets ParamsSize, binds via FScriptDelegate
 *   2. Delegate fires → ProcessEvent intercepts → copies raw Parms → resumes coroutine
 *   3. Awaiter reads CapturedParams in await_resume() → returns typed result
 *   4. Cleanup unbinds and removes the bridge from the root set
 *
 * Not intended for direct use — created internally by TWaitForTypedDynamicDelegateAwaiter.
 */
UCLASS(Transient)
class ASYNCFLOW_API UAsyncFlowTypedDelegateBridge : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Placeholder UFUNCTION — exists so FindFunction(DelegateFunctionName) succeeds.
	 * Actual parameter capture happens in the ProcessEvent override. This fallback
	 * handles the degenerate case where ProcessEvent is bypassed.
	 */
	UFUNCTION()
	void OnDelegateFired();

	/**
	 * Intercepts delegate broadcast calls to capture the raw parameter bytes
	 * before they are dispatched to the (zero-param) OnDelegateFired UFUNCTION.
	 */
	virtual void ProcessEvent(UFunction* Function, void* Parms) override;

	/** The coroutine handle to resume when the delegate fires. */
	std::coroutine_handle<> Continuation;

	/** Weak alive flag — if expired or false, the coroutine frame is gone. */
	TWeakPtr<bool> AliveFlag;

	/** Set to true once the delegate has fired (prevents double-resume). */
	bool bFired = false;

	/** Size in bytes of the delegate's parameter struct. Set before binding. */
	int32 ParamsSize = 0;

	/** Buffer holding the raw parameter bytes captured from ProcessEvent. */
	TArray<uint8> CapturedParams;

	/** The name of the bound UFUNCTION — used to identify our calls in ProcessEvent. */
	static inline const FName DelegateFunctionName{ TEXT("OnDelegateFired") };
};
