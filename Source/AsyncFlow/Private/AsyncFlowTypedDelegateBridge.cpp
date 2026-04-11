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

#include "AsyncFlowTypedDelegateBridge.h"
#include "AsyncFlowLogging.h"

void UAsyncFlowTypedDelegateBridge::OnDelegateFired()
{
	// Fallback path for zero-param delegates (shouldn't normally reach here
	// for typed delegates — ProcessEvent intercepts first). Handle gracefully.
	if (bFired)
	{
		return;
	}
	bFired = true;

	TSharedPtr<bool> Alive = AliveFlag.Pin();
	if (Alive && *Alive && Continuation && !Continuation.done())
	{
		Continuation.resume();
	}
}

void UAsyncFlowTypedDelegateBridge::ProcessEvent(UFunction* Function, void* Parms)
{
	// Intercept calls to our bound function to capture delegate parameters.
	// The delegate system calls ProcessEvent with our zero-param UFunction but
	// passes the full parameter struct from the broadcast as Parms.
	if (Function && Function->GetFName() == DelegateFunctionName && !bFired)
	{
		bFired = true;

		// Capture the raw parameter bytes
		if (ParamsSize > 0 && Parms)
		{
			CapturedParams.SetNumUninitialized(ParamsSize);
			FMemory::Memcpy(CapturedParams.GetData(), Parms, ParamsSize);
		}

		// Resume the coroutine if still alive
		TSharedPtr<bool> Alive = AliveFlag.Pin();
		if (Alive && *Alive && Continuation && !Continuation.done())
		{
			Continuation.resume();
		}
		return;
	}

	// Pass through all other ProcessEvent calls
	Super::ProcessEvent(Function, Parms);
}
