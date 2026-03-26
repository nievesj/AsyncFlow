// AsyncFlowDelegateAwaiter.h — Universal delegate-to-coroutine bridge
#pragma once

#include "AsyncFlowTask.h"
#include "Delegates/Delegate.h"
#include "Templates/Tuple.h"

#include <coroutine>

namespace AsyncFlow
{

// ============================================================================
// WaitForDelegate — binds to any UE multicast delegate, resumes on broadcast
// ============================================================================

/**
 * Awaiter for UE multicast delegates.
 * Binds a lambda, suspends the coroutine, and resumes with captured arguments
 * when the delegate fires. Auto-unbinds on resume.
 *
 * Usage:
 *   co_await AsyncFlow::WaitForDelegate(MyActor->OnDestroyed);
 *   auto [DestroyedActor] = co_await AsyncFlow::WaitForDelegate(MyActor->OnDestroyed);
 */

// Primary template for multicast delegates with parameters
template <typename DelegateType>
struct TWaitForDelegateAwaiter;

// Specialization for TMulticastDelegate<void(Args...)> (non-dynamic)
template <typename... Args>
struct TWaitForDelegateAwaiter<TMulticastDelegate<void(Args...)>>
{
	using DelegateType = TMulticastDelegate<void(Args...)>;
	using ResultType = TTuple<std::decay_t<Args>...>;

	DelegateType& Delegate;
	FDelegateHandle BoundHandle;
	TOptional<ResultType> CapturedArgs;
	std::coroutine_handle<> Continuation;
	TSharedPtr<bool> AliveFlag = MakeShared<bool>(true);

	explicit TWaitForDelegateAwaiter(DelegateType& InDelegate)
		: Delegate(InDelegate)
	{
	}

	~TWaitForDelegateAwaiter() { *AliveFlag = false; }

	bool await_ready() const { return false; }

	void await_suspend(std::coroutine_handle<> Handle)
	{
		Continuation = Handle;

		TWeakPtr<bool> WeakAlive = AliveFlag;
		BoundHandle = Delegate.AddLambda([this, WeakAlive](Args... InArgs)
		{
			if (!WeakAlive.IsValid()) { return; }
			CapturedArgs.Emplace(MakeTuple(InArgs...));
			Delegate.Remove(BoundHandle);
			if (Continuation && !Continuation.done())
			{
				Continuation.resume();
			}
		});
	}

	ResultType await_resume()
	{
		return CapturedArgs.GetValue();
	}
};

// Specialization for void delegates (no parameters)
template <>
struct TWaitForDelegateAwaiter<TMulticastDelegate<void()>>
{
	using DelegateType = TMulticastDelegate<void()>;

	DelegateType& Delegate;
	FDelegateHandle BoundHandle;
	std::coroutine_handle<> Continuation;
	TSharedPtr<bool> AliveFlag = MakeShared<bool>(true);

	explicit TWaitForDelegateAwaiter(DelegateType& InDelegate)
		: Delegate(InDelegate)
	{
	}

	~TWaitForDelegateAwaiter() { *AliveFlag = false; }

	bool await_ready() const { return false; }

	void await_suspend(std::coroutine_handle<> Handle)
	{
		Continuation = Handle;

		TWeakPtr<bool> WeakAlive = AliveFlag;
		BoundHandle = Delegate.AddLambda([this, WeakAlive]()
		{
			if (!WeakAlive.IsValid()) { return; }
			Delegate.Remove(BoundHandle);
			if (Continuation && !Continuation.done())
			{
				Continuation.resume();
			}
		});
	}

	void await_resume() {}
};

/** Wait for a multicast delegate to fire. Returns the delegate arguments as a tuple. */
template <typename... Args>
[[nodiscard]] TWaitForDelegateAwaiter<TMulticastDelegate<void(Args...)>> WaitForDelegate(TMulticastDelegate<void(Args...)>& Delegate)
{
	return TWaitForDelegateAwaiter<TMulticastDelegate<void(Args...)>>(Delegate);
}

// ============================================================================
// Simple callback awaiter for one-shot lambdas and non-delegate patterns
// ============================================================================

/**
 * FCallbackAwaiter
 * Generic awaiter for patterns where you need to provide a callback
 * to some async operation and resume the coroutine when it fires.
 */
template <typename T = void>
struct TCallbackAwaiter
{
	TOptional<T> Result;
	std::coroutine_handle<> Continuation;

	bool await_ready() const { return Result.IsSet(); }

	void await_suspend(std::coroutine_handle<> Handle)
	{
		Continuation = Handle;
	}

	T await_resume()
	{
		return MoveTemp(Result.GetValue());
	}

	/** Call this from the callback to resume the coroutine with a value. */
	void SetResult(T Value)
	{
		Result.Emplace(MoveTemp(Value));
		if (Continuation)
		{
			Continuation.resume();
		}
	}
};

template <>
struct TCallbackAwaiter<void>
{
	bool bReady = false;
	std::coroutine_handle<> Continuation;

	bool await_ready() const { return bReady; }

	void await_suspend(std::coroutine_handle<> Handle)
	{
		Continuation = Handle;
	}

	void await_resume() {}

	/** Call this from the callback to resume the coroutine. */
	void SetReady()
	{
		bReady = true;
		if (Continuation)
		{
			Continuation.resume();
		}
	}
};

} // namespace AsyncFlow

