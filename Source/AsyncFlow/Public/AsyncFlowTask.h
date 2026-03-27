// AsyncFlowTask.h — Core coroutine type: AsyncFlow::TTask<T>
#pragma once

#include "HAL/Platform.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/UnrealString.h"
#include "Templates/SharedPointer.h"
#include "Templates/Function.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "UObject/Object.h"
#include "Misc/AssertionMacros.h"
#include "AsyncFlowLogging.h"

#include <coroutine>
#include <atomic>
#include <exception>

namespace AsyncFlow
{

// ============================================================================
// FAsyncFlowState — shared cancellation / contract / guard state
// ============================================================================

struct ASYNCFLOW_API FAsyncFlowState
{
	std::atomic<bool> bCancelled{false};

	/** Depth counter for FCancellationGuard. When > 0, cancellation and contract checks are deferred. */
	std::atomic<int32> CancellationGuardDepth{0};

	/**
	 * Contract predicates registered via CO_CONTRACT. Checked before each suspension.
	 * NOT thread-safe — must only be mutated from the game thread before the task starts.
	 */
	TArray<TFunction<bool()>> ContractChecks;

	/** Callback invoked when Cancel() is called. */
	TFunction<void()> OnCancelledCallback;

	/** Debug name for tracking. */
	FString DebugName;

	bool IsCancelled() const { return bCancelled.load(std::memory_order_acquire); }

	bool IsGuarded() const { return CancellationGuardDepth.load(std::memory_order_acquire) > 0; }

	void Cancel()
	{
		const bool bWasCancelled = bCancelled.exchange(true, std::memory_order_acq_rel);
		if (!bWasCancelled && OnCancelledCallback)
		{
			OnCancelledCallback();
		}
	}

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

	/** Check cancellation and contracts, respecting guard depth. */
	bool ShouldCancel() const
	{
		if (IsGuarded())
		{
			return false;
		}
		if (IsCancelled())
		{
			return true;
		}
		if (!AreContractsValid())
		{
			return true;
		}
		return false;
	}
};

// ============================================================================
// FCancellationGuard — RAII guard that defers cancellation within a scope
// ============================================================================

struct ASYNCFLOW_API FCancellationGuard
{
	FCancellationGuard();
	~FCancellationGuard();

	FCancellationGuard(const FCancellationGuard&) = delete;
	FCancellationGuard& operator=(const FCancellationGuard&) = delete;

private:
	FAsyncFlowState* State = nullptr;
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
// Free functions for querying state from inside a coroutine body
// ============================================================================

/** Returns true if the currently executing coroutine has been cancelled. Game-thread only. */
inline bool IsCurrentCoroutineCanceled()
{
	FAsyncFlowState* State = Private::GetCurrentFlowState();
	return State && State->IsCancelled();
}

// ============================================================================
// FSelfCancellation — tag type; actual cancellation logic lives in
// TTaskPromise::await_transform(FSelfCancellation) so the coroutine
// stops immediately without executing any subsequent statements.
// ============================================================================

struct FSelfCancellation {};

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

/**
 * Awaiter for FSelfCancellation. Cancels the flow state, marks the promise
 * as completed, fires OnCompleted, and leaves the coroutine permanently
 * suspended. The TTask destructor will destroy the frame.
 */
struct FSelfCancelAwaiter
{
	TSharedPtr<FAsyncFlowState> FlowState;
	std::atomic<bool>& bCompleted;
	TFunction<void()>& OnCompleted;

	bool await_ready() const { return false; }

	void await_suspend(std::coroutine_handle<>) const
	{
		if (FlowState)
		{
			FlowState->Cancel();
		}
		bCompleted.store(true, std::memory_order_release);
		if (OnCompleted)
		{
			OnCompleted();
		}
		// Do NOT resume — coroutine stays suspended; TTask destructor cleans up.
	}

	void await_resume() const {}
};

/**
 * Final-suspend awaiter: marks completion and fires OnCompleted from
 * await_suspend rather than final_suspend. At this point the coroutine
 * is fully suspended, so the callback may legally call Handle.destroy()
 * (e.g. when the parent resumes and destroys the child TTask).
 */
struct FFinalAwaiter
{
	bool await_ready() const noexcept { return false; }

	template <typename PromiseType>
	void await_suspend(std::coroutine_handle<PromiseType> Handle) const noexcept
	{
		PromiseType& Promise = Handle.promise();
		Promise.bCompleted.store(true, std::memory_order_release);
		if (Promise.OnCompleted)
		{
			Promise.OnCompleted();
		}
		// Frame may be destroyed after OnCompleted fires — do NOT access Promise or Handle.
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
		if (State && State->ShouldCancel())
		{
			if (!State->IsCancelled())
			{
				State->Cancel();
			}
			return true; // Skip suspension
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
		return {};
	}

	void return_value(T Value)
	{
		Result.Emplace(MoveTemp(Value));
	}

	void unhandled_exception()
	{
		Exception = std::current_exception();
		UE_LOG(LogAsyncFlow, Error, TEXT("Unhandled exception in AsyncFlow coroutine [%s]"),
			*FlowState->DebugName);
	}

	/** await_transform: wraps every co_await to inject contract/cancellation checks.
	 *  Uses a forwarding reference so lvalue awaiters (e.g. non-copyable FAwaitableEvent)
	 *  are stored by reference, while rvalue awaiters are moved by value. */
	template <typename AwaiterType>
	Private::TContractCheckAwaiter<AwaiterType> await_transform(AwaiterType&& Awaiter)
	{
		return Private::TContractCheckAwaiter<AwaiterType>{std::forward<AwaiterType>(Awaiter), FlowState};
	}

	// FSelfCancellation: cancel + complete immediately, no subsequent code runs
	Private::FSelfCancelAwaiter await_transform(FSelfCancellation)
	{
		return Private::FSelfCancelAwaiter{FlowState, bCompleted, OnCompleted};
	}

	/**
	 * Bypass contract/cancellation wrapping for std::suspend_always. This is used by
	 * initial_suspend and internal machinery where checking contracts would be premature
	 * or cause infinite recursion (the coroutine hasn't started executing user code yet).
	 */
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
		return {};
	}

	void return_void()
	{
		bHasReturned = true;
	}

	void unhandled_exception()
	{
		Exception = std::current_exception();
		UE_LOG(LogAsyncFlow, Error, TEXT("Unhandled exception in AsyncFlow coroutine [%s]"),
			*FlowState->DebugName);
	}

	template <typename AwaiterType>
	Private::TContractCheckAwaiter<AwaiterType> await_transform(AwaiterType&& Awaiter)
	{
		return Private::TContractCheckAwaiter<AwaiterType>{std::forward<AwaiterType>(Awaiter), FlowState};
	}

	// FSelfCancellation: cancel + complete immediately, no subsequent code runs
	Private::FSelfCancelAwaiter await_transform(FSelfCancellation)
	{
		return Private::FSelfCancelAwaiter{FlowState, bCompleted, OnCompleted};
	}

	/** Bypass contract wrapping for internal suspend points. See TTaskPromise<T>::await_transform. */
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

	/**
	 * Resume the coroutine. Call once to start, subsequent calls continue from last suspension.
	 * Copies the handle to a local before resuming — if OnCompleted destroys this TTask,
	 * the local handle remains valid for the duration of the resume() call.
	 */
	void Resume()
	{
		if (Handle && !Handle.done())
		{
			// Copy to locals — synchronous completion may destroy this TTask
			CoroutineHandle LocalHandle = Handle;
			Private::SetCurrentFlowState(LocalHandle.promise().FlowState.Get());
			LocalHandle.resume();
			Private::SetCurrentFlowState(nullptr);
		}
	}

	/** Start the task (alias for Resume on first call). */
	void Start() { Resume(); }

	/** Cancel this coroutine. It will stop at the next co_await (unless guarded). */
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

	/** Returns true if the task completed without cancellation. */
	bool WasSuccessful() const
	{
		return IsCompleted() && !IsCancelled();
	}

	bool IsValid() const { return Handle != nullptr; }

	/** Get the result value. Only valid after completion. */
	const T& GetResult() const
	{
		check(IsCompleted());
		return Handle.promise().Result.GetValue();
	}

	/** Move the result out. Only valid after completion. */
	T MoveResult()
	{
		check(IsCompleted());
		return MoveTemp(Handle.promise().Result.GetValue());
	}

	/** Register a callback for when this task completes. */
	void OnComplete(TFunction<void()> Callback)
	{
		if (Handle)
		{
			ensureMsgf(!Handle.promise().OnCompleted, TEXT("OnComplete callback already registered for task [%s]"), *GetDebugName());
			Handle.promise().OnCompleted = MoveTemp(Callback);
		}
	}

	/** Register a weak-ref callback; only fires if the UObject is still valid. */
	void ContinueWithWeak(TWeakObjectPtr<UObject> WeakObj, TFunction<void()> Callback)
	{
		OnComplete([WeakObj, Cb = MoveTemp(Callback)]()
		{
			if (WeakObj.IsValid())
			{
				Cb();
			}
		});
	}

	/** Register a callback for when this task is cancelled. */
	void OnCancelled(TFunction<void()> Callback)
	{
		if (Handle)
		{
			Handle.promise().FlowState->OnCancelledCallback = MoveTemp(Callback);
		}
	}

	/** Set a debug name for tracking and diagnostics. */
	void SetDebugName(const FString& Name)
	{
		if (Handle)
		{
			Handle.promise().FlowState->DebugName = Name;
		}
	}

	/** Get the debug name. */
	FString GetDebugName() const
	{
		return Handle ? Handle.promise().FlowState->DebugName : FString();
	}

	/** Get the shared flow state (for contract registration, cancellation propagation). */
	TSharedPtr<FAsyncFlowState> GetFlowState() const
	{
		return Handle ? Handle.promise().FlowState : nullptr;
	}

	/** Get the raw coroutine handle (for awaiter integration). */
	CoroutineHandle GetHandle() const { return Handle; }

	/** Get a hash for use as container key. */
	friend uint32 GetTypeHash(const TTask& Task)
	{
		return ::GetTypeHash(reinterpret_cast<uintptr_t>(Task.Handle.address()));
	}

	// ========================================================================
	// Factory: create an already-completed task from a value
	// ========================================================================

	static TTask FromResult(T Value)
	{
		auto Coro = [](T Val) -> TTask<T>
		{
			co_return Val;
		};
		TTask<T> Task = Coro(MoveTemp(Value));
		Task.Start();
		return Task;
	}

	// ========================================================================
	// Awaitable interface: allows co_await on TTask<T> from another coroutine
	// ========================================================================

	bool await_ready() const { return IsCompleted(); }

	void await_suspend(std::coroutine_handle<> Continuation)
	{
		ensureMsgf(!Handle.promise().OnCompleted, TEXT("await_suspend: OnComplete callback already registered for task [%s]"), *GetDebugName());
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
		if (Handle.promise().Exception)
		{
			std::rethrow_exception(Handle.promise().Exception);
		}
		return MoveTemp(Handle.promise().Result.GetValue());
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
			CoroutineHandle LocalHandle = Handle;
			Private::SetCurrentFlowState(LocalHandle.promise().FlowState.Get());
			LocalHandle.resume();
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

	bool WasSuccessful() const
	{
		return IsCompleted() && !IsCancelled();
	}

	bool IsValid() const { return Handle != nullptr; }

	/** Register a callback for when this task completes. */
	void OnComplete(TFunction<void()> Callback)
	{
		if (Handle)
		{
			ensureMsgf(!Handle.promise().OnCompleted, TEXT("OnComplete callback already registered for task [%s]"), *GetDebugName());
			Handle.promise().OnCompleted = MoveTemp(Callback);
		}
	}

	/** Register a weak-ref callback; only fires if the UObject is still valid. */
	void ContinueWithWeak(TWeakObjectPtr<UObject> WeakObj, TFunction<void()> Callback)
	{
		OnComplete([WeakObj, Cb = MoveTemp(Callback)]()
		{
			if (WeakObj.IsValid())
			{
				Cb();
			}
		});
	}

	/** Register a callback for when this task is cancelled. */
	void OnCancelled(TFunction<void()> Callback)
	{
		if (Handle)
		{
			Handle.promise().FlowState->OnCancelledCallback = MoveTemp(Callback);
		}
	}

	void SetDebugName(const FString& Name)
	{
		if (Handle)
		{
			Handle.promise().FlowState->DebugName = Name;
		}
	}

	FString GetDebugName() const
	{
		return Handle ? Handle.promise().FlowState->DebugName : FString();
	}

	TSharedPtr<FAsyncFlowState> GetFlowState() const
	{
		return Handle ? Handle.promise().FlowState : nullptr;
	}

	CoroutineHandle GetHandle() const { return Handle; }

	friend uint32 GetTypeHash(const TTask& Task)
	{
		return ::GetTypeHash(reinterpret_cast<uintptr_t>(Task.Handle.address()));
	}

	/** Create an already-completed void task. */
	static TTask CompletedTask()
	{
		auto Coro = []() -> TTask<void>
		{
			co_return;
		};
		TTask<void> Task = Coro();
		Task.Start();
		return Task;
	}

	// Awaitable interface
	bool await_ready() const { return IsCompleted(); }

	void await_suspend(std::coroutine_handle<> Continuation)
	{
		ensureMsgf(!Handle.promise().OnCompleted, TEXT("await_suspend: OnComplete callback already registered for task [%s]"), *GetDebugName());
		Handle.promise().OnCompleted = [Continuation]() mutable
		{
			Continuation.resume();
		};
		if (!Handle.done())
		{
			Resume();
		}
	}

	void await_resume()
	{
		if (Handle.promise().Exception)
		{
			std::rethrow_exception(Handle.promise().Exception);
		}
	}

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

