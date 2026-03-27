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

// AsyncFlowHttpAwaiter.h — HTTP request awaiter
#pragma once

#include "AsyncFlowTask.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Templates/Tuple.h"

#include <coroutine>

namespace AsyncFlow
{

// ============================================================================
// ProcessHttpRequest — sends an HTTP request and waits for the response
// ============================================================================

struct FHttpRequestAwaiter
{
	FHttpRequestRef Request;
	FHttpResponsePtr Response;
	bool bSuccess = false;
	std::coroutine_handle<> Continuation;
	TSharedPtr<bool> AliveFlag = MakeShared<bool>(true);

	explicit FHttpRequestAwaiter(FHttpRequestRef InRequest)
		: Request(MoveTemp(InRequest))
	{
	}

	~FHttpRequestAwaiter() { *AliveFlag = false; }

	bool await_ready() const { return false; }

	void await_suspend(std::coroutine_handle<> Handle)
	{
		Continuation = Handle;

		TWeakPtr<bool> WeakAlive = AliveFlag;
		Request->OnProcessRequestComplete().BindLambda(
			[this, WeakAlive](FHttpRequestPtr InRequest, FHttpResponsePtr InResponse, bool bConnectedSuccessfully)
			{
				if (!WeakAlive.IsValid()) { return; }
				Response = InResponse;
				bSuccess = bConnectedSuccessfully;
				if (Continuation && !Continuation.done())
				{
					Continuation.resume();
				}
			}
		);

		Request->ProcessRequest();
	}

	TTuple<FHttpResponsePtr, bool> await_resume()
	{
		return MakeTuple(MoveTemp(Response), bSuccess);
	}
};

/** Send an HTTP request and co_await the response. Returns {ResponsePtr, bSuccess}. */
[[nodiscard]] inline FHttpRequestAwaiter ProcessHttpRequest(FHttpRequestRef Request)
{
	return FHttpRequestAwaiter(MoveTemp(Request));
}

} // namespace AsyncFlow

