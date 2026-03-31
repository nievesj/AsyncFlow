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

// AsyncFlowTask.h — Core coroutine type: AsyncFlow::TTask<T>
//
// TTask<T> is a lazily-started, move-only coroutine handle that runs on the
// game thread. It supports cancellation, lifecycle contracts (CO_CONTRACT),
// completion callbacks, and co_await composition with other TTask instances.
//
// Thread safety: TTask and FAsyncFlowState are game-thread-only, except for
// the std::atomic fields on FAsyncFlowState which are touched from background
// threads only in the thread-awaiter paths.
//
// Ownership model: TTask owns its coroutine frame. When TTask is destroyed,
// the frame is destroyed with it. Move assignment destroys the previous frame.
// Copy is deleted — coroutine frames are single-owner resources.
//
// WARNING — Coroutine parameter lifetimes (Rule 21):
//   Coroutine functions copy/move their parameters into the coroutine frame
//   before the first suspension point. Reference and pointer parameters bind
//   to the CALLER's locals, which may be destroyed before the coroutine
//   resumes. This causes silent dangling references.
//
//   NEVER pass reference or raw-pointer parameters to a coroutine function:
//
//     // WRONG — FString& dangles after first co_await:
//     TTask<void> BadCoroutine(const FString& Name) { co_await ...; UE_LOG(..., *Name); }
//
//     // CORRECT — copy by value:
//     TTask<void> GoodCoroutine(FString Name) { co_await ...; UE_LOG(..., *Name); }
//
//   For large objects, use TSharedPtr or move semantics.
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

/**
 * Mutable state shared between a coroutine and its TTask handle.
 * Tracks cancellation, contract predicates, guard depth, and debug info.
 *
 * Allocated once per coroutine via TSharedPtr in TTaskPromise. Both the
 * TTask owner and any awaiters that need to propagate cancellation hold
 * shared references.
 *
 * Most fields are game-thread-only. The two atomics (bCancelled,
 * CancellationGuardDepth) are safe to read from background threads
 * but should only be written from the game thread under normal use.
 */
struct ASYNCFLOW_API FAsyncFlowState
{
	/** True once Cancel() has been called. Checked at every co_await boundary. */
	std::atomic<bool> bCancelled{false};

	/** Depth counter for FCancellationGuard. When > 0, cancellation and contract checks are deferred. */
	std::atomic<int32> CancellationGuardDepth{0};

	/**
	 * Contract predicates registered via CO_CONTRACT. Checked before each suspension.
	 * If any predicate returns false, the coroutine cancels at the next co_await.
	 *
	 * NOT thread-safe — must only be mutated from the game thread before the task starts.
	 */
	TArray<TFunction<bool()>> ContractChecks;

	/** Fires exactly once when Cancel() is first called. */
	TFunction<void()> OnCancelledCallback;

	/** Human-readable label for logging and FAsyncFlowDebugger tracking. */
	FString DebugName;

	/** @return true if Cancel() has been called. */
	bool IsCancelled() const { return bCancelled.load(std::memory_order_acquire); }

	/** @return true if inside an FCancellationGuard scope. */
	bool IsGuarded() const { return CancellationGuardDepth.load(std::memory_order_acquire) > 0; }

	/**
	 * Mark this coroutine as cancelled and fire OnCancelledCallback (once).
	 * Idempotent — calling Cancel() multiple times has no additional effect.
	 */
	void Cancel()
	{
		const bool bWasCancelled = bCancelled.exchange(true, std::memory_order_acq_rel);
		if (!bWasCancelled && OnCancelledCallback)
		{
			OnCancelledCallback();
		}
	}

	/** @return true if every registered contract predicate still returns true. */
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

	/**
	 * Composite check used at co_await boundaries.
	 * @return true if the coroutine should stop (cancelled or contract violation),
	 *         false if execution should continue. Respects guard depth.
	 */
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

/**
 * While an FCancellationGuard is alive, the enclosing coroutine's
 * co_await boundaries skip cancellation and contract checks.
 * Nests correctly — each guard increments CancellationGuardDepth.
 *
 * Grab the guard at the top of a critical section (e.g., multi-step
 * rollback logic) where partial execution would leave bad state.
 *
 * Game-thread-only. Non-copyable.
 */
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

/**
 * @return the FAsyncFlowState* for the coroutine currently executing on
 *         this thread, or nullptr if no coroutine is running.
 * Game-thread-only. Used by CO_CONTRACT and FCancellationGuard.
 */
ASYNCFLOW_API FAsyncFlowState* GetCurrentFlowState();

/** Set the active flow state. Called by TTask::Resume() around Handle.resume(). */
ASYNCFLOW_API void SetCurrentFlowState(FAsyncFlowState* State);

} // namespace Private

// ============================================================================
// Free functions for querying state from inside a coroutine body
// ============================================================================

/** @return true if the currently executing coroutine has been cancelled. Game-thread only. */
inline bool IsCurrentCoroutineCanceled()
{
	FAsyncFlowState* State = Private::GetCurrentFlowState();
	return State && State->IsCancelled();
}

// ============================================================================
// FSelfCancellation — tag type for co_await self-cancellation
// ============================================================================

/**
 * co_await FSelfCancellation{} inside a coroutine body to cancel
 * and stop immediately. No statements after the co_await execute.
 * The coroutine frame stays suspended; TTask's destructor cleans up.
 */
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
 * Awaiter produced by co_await FSelfCancellation{}.
 * Cancels the flow state, marks the promise as completed, fires OnCompleted,
 * and leaves the coroutine permanently suspended.
 *
 * The coroutine frame is cleaned up when the owning TTask is destroyed.
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
 * Returned by final_suspend(). Marks the promise as completed and fires
 * OnCompleted from await_suspend (not from the coroutine body).
 *
 * At this point the coroutine is fully suspended, so the callback may
 * safely destroy the TTask (and with it the coroutine frame).
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

/**
 * Wraps every awaiter passed through co_await to inject contract and
 * cancellation checks before suspension.
 *
 * If ShouldCancel() is true at the co_await boundary, the inner awaiter
 * is skipped entirely — await_ready returns true and no suspension occurs.
 *
 * @tparam InnerAwaiter  The original awaiter type being wrapped.
 */
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

/**
 * Promise type for TTask<T> (non-void). Created automatically by the compiler.
 *
 * Key behaviors:
 * - initial_suspend: always suspends (lazy start).
 * - final_suspend: fires OnCompleted via FFinalAwaiter.
 * - await_transform: wraps every co_await in TContractCheckAwaiter for contract/cancel checks.
 * - Stores the co_return value in Result (TOptional<T>).
 *
 * @tparam T  The value type returned by co_return.
 */
template <typename T>
struct TTaskPromise
{
	using CoroutineHandle = std::coroutine_handle<TTaskPromise<T>>;

	/** Shared state for cancellation, contracts, and debug info. */
	TSharedPtr<FAsyncFlowState> FlowState = MakeShared<FAsyncFlowState>();

	/** The co_return value. Set inside return_value(). */
	TOptional<T> Result;

	/** Captured if the coroutine body throws. Rethrown in await_resume(). */
	std::exception_ptr Exception;

	/** True once the coroutine reaches final_suspend or self-cancels. */
	std::atomic<bool> bCompleted{false};

	/** Single callback fired when the coroutine finishes (success, cancel, or exception). */
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

	/**
	 * Wraps every co_await expression to inject contract/cancellation checks.
	 * Uses a forwarding reference so lvalue awaiters (e.g. non-copyable FAwaitableEvent)
	 * are stored by reference, while rvalue awaiters are moved by value.
	 *
	 * @param Awaiter  The awaiter expression from the co_await.
	 * @return         A TContractCheckAwaiter wrapping the original awaiter.
	 */
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
	 * Bypass contract/cancellation wrapping for std::suspend_always.
	 * Used by initial_suspend and internal machinery where checking
	 * contracts would be premature or cause infinite recursion.
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

/**
 * Lazily-started, move-only coroutine handle.
 *
 * The coroutine does not execute until Start() (or Resume()) is called.
 * Destroying a TTask destroys the coroutine frame — cancel first if you
 * need cleanup logic to run.
 *
 * Awaitable: another coroutine can co_await a TTask<T> to get its result.
 *
 * Thread safety: all methods are game-thread-only. The co_await interface
 * is also game-thread-only (both coroutines must share the same thread).
 *
 * @tparam T  The value type produced by co_return. Use void for no result.
 */
template <typename T>
class TTask
{
public:
	using promise_type = TTaskPromise<T>;
	using CoroutineHandle = std::coroutine_handle<promise_type>;

	TTask() = default;

	/** Construct from a raw coroutine handle. Called by get_return_object(). */
	explicit TTask(CoroutineHandle InHandle)
		: Handle(InHandle)
	{
	}

	/** Destroys the coroutine frame if this TTask still owns one. */
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
	 * Resume the coroutine from its current suspension point.
	 * On first call this starts execution; subsequent calls continue from the last co_await.
	 *
	 * Copies the handle to a local before calling resume() — if the coroutine
	 * completes synchronously and its OnCompleted callback destroys this TTask,
	 * the local handle keeps the frame alive for the duration of resume().
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

	/** Alias for Resume(). Reads better at the call site for initial launch. */
	void Start() { Resume(); }

	/**
	 * Request cancellation. The coroutine stops at the next co_await
	 * unless an FCancellationGuard is active.
	 */
	void Cancel()
	{
		if (Handle)
		{
			Handle.promise().FlowState->Cancel();
		}
	}

	/** @return true if the coroutine has reached final_suspend or self-cancelled. */
	bool IsCompleted() const
	{
		return Handle && Handle.promise().bCompleted.load(std::memory_order_acquire);
	}

	/** @return true if Cancel() was called on this task's flow state. */
	bool IsCancelled() const
	{
		return Handle && Handle.promise().FlowState->IsCancelled();
	}

	/** @return true if the task completed without cancellation. */
	bool WasSuccessful() const
	{
		return IsCompleted() && !IsCancelled();
	}

	/** @return true if this TTask owns a coroutine frame. */
	bool IsValid() const { return Handle != nullptr; }

	/**
	 * Access the co_return value. Only valid after IsCompleted() returns true.
	 * @warning Asserts if called before completion.
	 */
	const T& GetResult() const
	{
		check(IsCompleted());
		return Handle.promise().Result.GetValue();
	}

	/**
	 * Move the co_return value out. Only valid after IsCompleted() returns true.
	 * @warning Asserts if called before completion. Leaves Result in a moved-from state.
	 */
	T MoveResult()
	{
		check(IsCompleted());
		return MoveTemp(Handle.promise().Result.GetValue());
	}

	/**
	 * Register a one-shot callback that fires when the coroutine finishes.
	 * Only one callback can be registered — registering a second asserts.
	 *
	 * @param Callback  Invoked on the game thread when the coroutine completes.
	 */
	void OnComplete(TFunction<void()> Callback)
	{
		if (Handle)
		{
			ensureMsgf(!Handle.promise().OnCompleted, TEXT("OnComplete callback already registered for task [%s]"), *GetDebugName());
			Handle.promise().OnCompleted = MoveTemp(Callback);
		}
	}

	/**
	 * Register a completion callback guarded by a weak UObject reference.
	 * The callback only fires if WeakObj is still valid at completion time.
	 *
	 * @param WeakObj   Weak reference to the UObject whose lifetime gates the callback.
	 * @param Callback  Invoked only if WeakObj is valid.
	 */
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

	/**
	 * Register a callback that fires when Cancel() is called.
	 *
	 * @param Callback  Invoked on the game thread at cancellation time.
	 */
	void OnCancelled(TFunction<void()> Callback)
	{
		if (Handle)
		{
			Handle.promise().FlowState->OnCancelledCallback = MoveTemp(Callback);
		}
	}

	/**
	 * Assign a human-readable name for logging and FAsyncFlowDebugger tracking.
	 *
	 * @param Name  Debug label (e.g., "AttackSequence" or "LoadLevel_Forest").
	 */
	void SetDebugName(const FString& Name)
	{
		if (Handle)
		{
			Handle.promise().FlowState->DebugName = Name;
		}
	}

	/** @return the debug name, or an empty string if none was set. */
	FString GetDebugName() const
	{
		return Handle ? Handle.promise().FlowState->DebugName : FString();
	}

	/**
	 * @return the shared flow state. Use for external contract registration
	 *         or cancellation propagation (e.g., Race() cancels loser tasks).
	 */
	TSharedPtr<FAsyncFlowState> GetFlowState() const
	{
		return Handle ? Handle.promise().FlowState : nullptr;
	}

	/** @return the raw std::coroutine_handle for low-level awaiter integration. */
	CoroutineHandle GetHandle() const { return Handle; }

	/** Get a hash for use as container key. */
	friend uint32 GetTypeHash(const TTask& Task)
	{
		return ::GetTypeHash(reinterpret_cast<uintptr_t>(Task.Handle.address()));
	}

	// ========================================================================
	// Factory: create an already-completed task from a value
	// ========================================================================

	/**
	 * Create a TTask that is already completed with the given value.
	 * Useful for returning cached results from a function that normally
	 * returns a coroutine.
	 *
	 * @param Value  The result value.
	 * @return       A started-and-completed TTask holding Value.
	 */
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

	/** True if the task already completed — skips suspension entirely. */
	bool await_ready() const { return IsCompleted(); }

	/**
	 * Hook the parent coroutine's continuation so it resumes when this task finishes.
	 * If the task hasn't started yet, Start() is called here.
	 */
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

	/**
	 * Called when the parent coroutine resumes after co_await.
	 * Rethrows any captured exception, then returns the result by move.
	 */
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

	/**
	 * Resume the coroutine from its current suspension point.
	 * On first call this starts execution; subsequent calls continue from the last co_await.
	 *
	 * Copies the handle to a local before calling resume() — if the coroutine
	 * completes synchronously and its OnCompleted callback destroys this TTask,
	 * the local handle keeps the frame alive for the duration of resume().
	 */
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

	/** Alias for Resume(). Reads better at the call site for initial launch. */
	void Start() { Resume(); }

	/**
	 * Request cancellation. The coroutine stops at the next co_await
	 * unless an FCancellationGuard is active.
	 */
	void Cancel()
	{
		if (Handle)
		{
			Handle.promise().FlowState->Cancel();
		}
	}

	/** @return true if the coroutine has reached final_suspend or self-cancelled. */
	bool IsCompleted() const
	{
		return Handle && Handle.promise().bCompleted.load(std::memory_order_acquire);
	}

	/** @return true if Cancel() was called on this task's flow state. */
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

