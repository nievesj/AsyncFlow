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
//
// Wraps IHttpRequest::ProcessRequest as a co_awaitable. The coroutine
// suspends until the HTTP response arrives (or the connection fails).
// Returns {FHttpResponsePtr, bSuccess} as a TTuple.
//
// AliveFlag prevents resuming a dead frame if the awaiter is destroyed
// before the response arrives (e.g., coroutine cancellation).
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

/**
 * Awaiter that sends an HTTP request and waits for the response.
 * Binds OnProcessRequestComplete and resumes with {Response, bSuccess}.
 *
 * Destructor invalidates the alive flag — if the response arrives after
 * the awaiter is dead (e.g., coroutine was cancelled), the callback no-ops.
 */
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

/**
 * Send an HTTP request and co_await the response.
 *
 * @param Request  A configured FHttpRequestRef (verb, URL, headers, body).
 * @return         An awaiter — co_await yields TTuple<FHttpResponsePtr, bool>.
 *                 The bool is true if the connection succeeded.
 */
[[nodiscard]] inline FHttpRequestAwaiter ProcessHttpRequest(FHttpRequestRef Request)
{
	return FHttpRequestAwaiter(MoveTemp(Request));
}

} // namespace AsyncFlow

