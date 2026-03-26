// AsyncFlowTask.h — Core coroutine type: AsyncFlow::TTask<T>
// NOTE: All coroutine resumption must happen on the game thread. Async
// callbacks (HTTP, asset loading, etc.) are dispatched by UE on the game
// thread. Do not resume coroutines from worker threads.
#pragma once

#include "HAL/Platform.h"
#include "Containers/Array.h"
#include "Templates/SharedPointer.h"
#include "Templates/Function.h"
#include "Misc/AssertionMacros.h"
#include "AsyncFlowLogging.h"

#include <coroutine>
#include <atomic>
#include <exception>

namespace AsyncFlow
{

// ============================================================================
// FAsyncFlowState — shared cancellation / contract state for a coroutine
// ============================================================================

struct ASYNCFLOW_API FAsyncFlowState
{
	std::atomic<bool> bCancelled{false};

	/** Contract predicates registered via CO_CONTRACT. Checked before each suspension. */
	TArray<TFunction<bool()>> ContractChecks;

	bool IsCancelled() const { return bCancelled.load(std::memory_order_acquire); }

	void Cancel() { bCancelled.store(true, std::memory_order_release); }

	/** Returns true if all contracts still hold. */
	bool AreContractsValid() const
	{
		for (const TFunction<bool()>& Check : ContractChecks)
		{
			if (!Check())
			{
				return false;
			}
		}
		return true;
	}
};

// ============================================================================
// Thread-local current-promise access (game-thread only)
// ============================================================================

namespace Private
{

ASYNCFLOW_API FAsyncFlowState* GetCurrentFlowState();
ASYNCFLOW_API void SetCurrentFlowState(FAsyncFlowState* State);

} // namespace Private

// ============================================================================
// Forward declaration
// ============================================================================

template <typename T = void>
class TTask;

// ============================================================================
// TTaskPromise<T> — promise_type for TTask<T>
// ============================================================================

namespace Private
{

/** Final-suspend awaiter: noop resume, just marks completion. */
struct FFinalAwaiter
{
	bool await_ready() const noexcept { return false; }

	void await_suspend(std::coroutine_handle<>) const noexcept
	{
		// Coroutine frame stays alive; TTask destructor will destroy it.
	}

	void await_resume() const noexcept {}
};

/** Contract-checking wrapper awaiter: wraps any inner awaiter and checks contracts + cancellation before suspending. */
template <typename InnerAwaiter>
struct TContractCheckAwaiter
{
	InnerAwaiter Inner;
	TSharedPtr<FAsyncFlowState> State;

	bool await_ready()
	{
		if (State)
		{
			if (State->IsCancelled())
			{
				return true; // Already cancelled, skip suspension
			}
			if (!State->AreContractsValid())
			{
				State->Cancel(); // Mark as cancelled so all subsequent co_awaits also skip
				return true;
			}
		}
		return Inner.await_ready();
	}

	template <typename HandleType>
	auto await_suspend(HandleType Handle)
	{
		return Inner.await_suspend(Handle);
	}

	auto await_resume()
	{
		return Inner.await_resume();
	}
};

} // namespace Private

template <typename T>
struct TTaskPromise
{
	using CoroutineHandle = std::coroutine_handle<TTaskPromise<T>>;

	TSharedPtr<FAsyncFlowState> FlowState = MakeShared<FAsyncFlowState>();
	TOptional<T> Result;
	std::exception_ptr Exception;
	std::atomic<bool> bCompleted{false};
	TFunction<void()> OnCompleted;

	TTask<T> get_return_object();

	std::suspend_always initial_suspend() const noexcept { return {}; }

	Private::FFinalAwaiter final_suspend() noexcept
	{
		bCompleted.store(true, std::memory_order_release);
		if (OnCompleted)
		{
			OnCompleted();
		}
		return {};
	}

	void return_value(T Value)
	{
		Result.Emplace(MoveTemp(Value));
	}

	void unhandled_exception()
	{
		Exception = std::current_exception();
		UE_LOG(LogAsyncFlow, Error, TEXT("Unhandled exception in AsyncFlow coroutine"));
	}

	/** await_transform: wraps every co_await to inject contract/cancellation checks. */
	template <typename AwaiterType>
	Private::TContractCheckAwaiter<AwaiterType> await_transform(AwaiterType Awaiter)
	{
		return Private::TContractCheckAwaiter<AwaiterType>{MoveTemp(Awaiter), FlowState};
	}

	// Allow co_await on std::suspend_always without wrapping
	std::suspend_always await_transform(std::suspend_always Awaiter)
	{
		return Awaiter;
	}
};

// Specialization for void
template <>
struct TTaskPromise<void>
{
	using CoroutineHandle = std::coroutine_handle<TTaskPromise<void>>;

	TSharedPtr<FAsyncFlowState> FlowState = MakeShared<FAsyncFlowState>();
	bool bHasReturned = false;
	std::exception_ptr Exception;
	std::atomic<bool> bCompleted{false};
	TFunction<void()> OnCompleted;

	TTask<void> get_return_object();

	std::suspend_always initial_suspend() const noexcept { return {}; }

	Private::FFinalAwaiter final_suspend() noexcept
	{
		bCompleted.store(true, std::memory_order_release);
		if (OnCompleted)
		{
			OnCompleted();
		}
		return {};
	}

	void return_void()
	{
		bHasReturned = true;
	}

	void unhandled_exception()
	{
		Exception = std::current_exception();
		UE_LOG(LogAsyncFlow, Error, TEXT("Unhandled exception in AsyncFlow coroutine"));
	}

	template <typename AwaiterType>
	Private::TContractCheckAwaiter<AwaiterType> await_transform(AwaiterType Awaiter)
	{
		return Private::TContractCheckAwaiter<AwaiterType>{MoveTemp(Awaiter), FlowState};
	}

	std::suspend_always await_transform(std::suspend_always Awaiter)
	{
		return Awaiter;
	}
};

// ============================================================================
// TTask<T> — the coroutine return type (non-void specialization)
// ============================================================================

template <typename T>
class TTask
{
public:
	using promise_type = TTaskPromise<T>;
	using CoroutineHandle = std::coroutine_handle<promise_type>;

	TTask() = default;

	explicit TTask(CoroutineHandle InHandle)
		: Handle(InHandle)
	{
	}

	~TTask()
	{
		if (Handle)
		{
			Handle.destroy();
			Handle = nullptr;
		}
	}

	// Move only
	TTask(TTask&& Other) noexcept
		: Handle(Other.Handle)
	{
		Other.Handle = nullptr;
	}

	TTask& operator=(TTask&& Other) noexcept
	{
		if (this != &Other)
		{
			if (Handle)
			{
				Handle.destroy();
			}
			Handle = Other.Handle;
			Other.Handle = nullptr;
		}
		return *this;
	}

	TTask(const TTask&) = delete;
	TTask& operator=(const TTask&) = delete;

	/** Resume the coroutine. Call once to start, subsequent calls continue from last suspension. */
	void Resume()
	{
		if (Handle && !Handle.done())
		{
			Private::SetCurrentFlowState(Handle.promise().FlowState.Get());
			Handle.resume();
			Private::SetCurrentFlowState(nullptr);
		}
	}

	/** Start the task (alias for Resume on first call). */
	void Start() { Resume(); }

	/** Cancel this coroutine. It will stop at the next co_await. */
	void Cancel()
	{
		if (Handle)
		{
			Handle.promise().FlowState->Cancel();
		}
	}

	bool IsCompleted() const
	{
		return Handle && Handle.promise().bCompleted.load(std::memory_order_acquire);
	}

	bool IsCancelled() const
	{
		return Handle && Handle.promise().FlowState->IsCancelled();
	}

	bool IsValid() const { return Handle != nullptr; }

	/** Get the result value. Only valid after completion. */
	const T& GetResult() const
	{
		check(IsCompleted());
		return Handle.promise().Result.GetValue();
	}

	/** Register a callback for when this task completes. */
	void OnComplete(TFunction<void()> Callback)
	{
		if (Handle)
		{
			check(!Handle.promise().OnCompleted);
			Handle.promise().OnCompleted = MoveTemp(Callback);
		}
	}

	/** Get the shared flow state (for contract registration, cancellation propagation). */
	TSharedPtr<FAsyncFlowState> GetFlowState() const
	{
		return Handle ? Handle.promise().FlowState : nullptr;
	}

	/** Get the raw coroutine handle (for awaiter integration). */
	CoroutineHandle GetHandle() const { return Handle; }

	// ========================================================================
	// Awaitable interface: allows co_await on TTask<T> from another coroutine
	// ========================================================================

	bool await_ready() const { return IsCompleted(); }

	void await_suspend(std::coroutine_handle<> Continuation)
	{
		Handle.promise().OnCompleted = [Continuation]() mutable
		{
			Continuation.resume();
		};
		if (!Handle.done())
		{
			Resume();
		}
	}

	T await_resume()
	{
		return Handle.promise().Result.GetValue();
	}

private:
	CoroutineHandle Handle = nullptr;
};

// ============================================================================
// TTask<void> — explicit specialization for void return type
// ============================================================================

template <>
class TTask<void>
{
public:
	using promise_type = TTaskPromise<void>;
	using CoroutineHandle = std::coroutine_handle<promise_type>;

	TTask() = default;

	explicit TTask(CoroutineHandle InHandle)
		: Handle(InHandle)
	{
	}

	~TTask()
	{
		if (Handle)
		{
			Handle.destroy();
			Handle = nullptr;
		}
	}

	TTask(TTask&& Other) noexcept
		: Handle(Other.Handle)
	{
		Other.Handle = nullptr;
	}

	TTask& operator=(TTask&& Other) noexcept
	{
		if (this != &Other)
		{
			if (Handle)
			{
				Handle.destroy();
			}
			Handle = Other.Handle;
			Other.Handle = nullptr;
		}
		return *this;
	}

	TTask(const TTask&) = delete;
	TTask& operator=(const TTask&) = delete;

	void Resume()
	{
		if (Handle && !Handle.done())
		{
			Private::SetCurrentFlowState(Handle.promise().FlowState.Get());
			Handle.resume();
			Private::SetCurrentFlowState(nullptr);
		}
	}

	void Start() { Resume(); }

	void Cancel()
	{
		if (Handle)
		{
			Handle.promise().FlowState->Cancel();
		}
	}

	bool IsCompleted() const
	{
		return Handle && Handle.promise().bCompleted.load(std::memory_order_acquire);
	}

	bool IsCancelled() const
	{
		return Handle && Handle.promise().FlowState->IsCancelled();
	}

	bool IsValid() const { return Handle != nullptr; }

	/** Register a callback for when this task completes. */
	void OnComplete(TFunction<void()> Callback)
	{
		if (Handle)
		{
			check(!Handle.promise().OnCompleted);
			Handle.promise().OnCompleted = MoveTemp(Callback);
		}
	}

	TSharedPtr<FAsyncFlowState> GetFlowState() const
	{
		return Handle ? Handle.promise().FlowState : nullptr;
	}

	CoroutineHandle GetHandle() const { return Handle; }

	// Awaitable interface
	bool await_ready() const { return IsCompleted(); }

	void await_suspend(std::coroutine_handle<> Continuation)
	{
		Handle.promise().OnCompleted = [Continuation]() mutable
		{
			Continuation.resume();
		};
		if (!Handle.done())
		{
			Resume();
		}
	}

	void await_resume() {}

private:
	CoroutineHandle Handle = nullptr;
};

// ============================================================================
// get_return_object implementations
// ============================================================================

template <typename T>
TTask<T> TTaskPromise<T>::get_return_object()
{
	return TTask<T>(std::coroutine_handle<TTaskPromise<T>>::from_promise(*this));
}

inline TTask<void> TTaskPromise<void>::get_return_object()
{
	return TTask<void>(std::coroutine_handle<TTaskPromise<void>>::from_promise(*this));
}

} // namespace AsyncFlow

