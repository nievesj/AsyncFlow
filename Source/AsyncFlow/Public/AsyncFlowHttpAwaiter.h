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

