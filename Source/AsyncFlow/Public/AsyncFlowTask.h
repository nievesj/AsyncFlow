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
// TTask<T> is a lazily-started, copyable, discardable coroutine handle.
// Supports cancellation, lifecycle contracts (CO_CONTRACT), completion
// callbacks, and co_await composition with other TTask instances.
//
// Dual execution mode:
//   - Async mode (default): Start() resumes the coroutine manually.
//   - Latent mode (auto-detected): when the coroutine function signature
//     includes a FLatentActionInfo parameter, TTaskPromise detects it via
//     constructor overload resolution and registers a FAsyncFlowLatentAction
//     with the engine's latent action manager on Start().
//
// Ownership model: TTask holds a TSharedPtr to a FCoroutineControlBlock
// which owns the coroutine frame. Copies share the control block.
// After Start(), the coroutine is self-sustaining (fire-and-forget) —
// all TTask copies can be destroyed without cancelling the coroutine.
// Before Start(), destroying the last TTask copy destroys the frame.
//
// Thread safety: TTask and FAsyncFlowState are game-thread-only, except for
// the std::atomic fields on FAsyncFlowState which are touched from background
// threads only in the thread-awaiter paths.
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
#include "Async/Future.h"
#include "Delegates/Delegate.h"
#include "Delegates/DelegateCombinations.h"
#include "UObject/ScriptDelegates.h"
#include "AsyncFlowLogging.h"
#include "LatentActions.h"

class UWorld;

#include <coroutine>
#include <atomic>
#include <exception>
#include <concepts>

#if __has_include("Tasks/Task.h")
	#define ASYNCFLOW_HAS_UE_TASKS 1
#else
	#define ASYNCFLOW_HAS_UE_TASKS 0
#endif

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
		std::atomic<bool> bCancelled{ false };

		/** Depth counter for FCancellationGuard. When > 0, cancellation and contract checks are deferred. */
		std::atomic<int32> CancellationGuardDepth{ 0 };

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
		bool IsCancelled() const
		{
			return bCancelled.load(std::memory_order_acquire);
		}

		/** @return true if inside an FCancellationGuard scope. */
		bool IsGuarded() const
		{
			return CancellationGuardDepth.load(std::memory_order_acquire) > 0;
		}

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

		/** Register a contract predicate. Checked before each suspension point. */
		void AddContract(TFunction<bool()> Predicate)
		{
			ContractChecks.Add(MoveTemp(Predicate));
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

	// Forward declaration for latent fast-path support
	class FAsyncFlowLatentAction;

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

		/** @return the UObject* world context set for the currently executing coroutine, or nullptr. */
		ASYNCFLOW_API UObject* GetCurrentWorldContext();

		/** Set the active world context. Called by TTask::Resume() for latent-mode coroutines. */
		ASYNCFLOW_API void SetCurrentWorldContext(UObject* Ctx);

		/**
		 * Resolve a UWorld from an optional context object. Falls back to the thread-local
		 * world context, then to GEngine->GetCurrentPlayWorld().
		 */
		ASYNCFLOW_API UWorld* ResolveWorld(UObject* OptionalContext = nullptr);

		/** @return the FAsyncFlowLatentAction driving the current coroutine, or nullptr. */
		ASYNCFLOW_API FAsyncFlowLatentAction* GetCurrentLatentAction();

		/** Set the active latent action for latent fast-path detection. */
		ASYNCFLOW_API void SetCurrentLatentAction(FAsyncFlowLatentAction* Action);

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
	struct FSelfCancellation
	{
	};

	// ============================================================================
	// FFinishNowIfCanceled — tag type for early-exit check
	// ============================================================================

	/**
 * co_await FFinishNowIfCanceled{} to check cancellation status.
 * Goes through await_transform -> TContractCheckAwaiter which checks
 * ShouldCancel() first. If cancelled, the coroutine stops at this point.
 * If not cancelled, execution continues immediately (never truly suspends).
 */
	struct FFinishNowIfCanceled
	{
		bool await_ready() const
		{
			return true;
		} // Never suspends
		void await_suspend(std::coroutine_handle<>) const
		{
		}
		void await_resume() const
		{
		}
	};

	// ============================================================================
	// CancelableAwaiter concept
	// ============================================================================

	/**
	 * An awaiter satisfies CancelableAwaiter if it exposes a CancelAwaiter()
	 * method. When the TTask is cancelled while suspended on such an awaiter,
	 * the cancel machinery calls CancelAwaiter() for expedited wake-up instead
	 * of waiting until the next co_await boundary.
	 */
	template <typename T>
	concept CancelableAwaiter = requires(T& a) {
		{ a.CancelAwaiter() } -> std::same_as<void>;
	};

	/**
	 * An awaiter satisfies CleanupableAwaiter if it exposes a CleanupAwaiter()
	 * method. CleanupAwaiter() must remove the awaiter from whatever queue it is
	 * in without resuming the coroutine handle. Used by TContractCheckAwaiter for
	 * resource-acquisition awaiters (FAwaitableEvent, FAwaitableSemaphore) so that
	 * cancellation permanently terminates the frame instead of resuming it with a
	 * "successful" acquire.
	 */
	template <typename T>
	concept CleanupableAwaiter = requires(T& a) {
		{ a.CleanupAwaiter() } -> std::same_as<void>;
	};

	// ============================================================================
	// Forward declarations
	// ============================================================================

	template <typename T = void>
	class TTask;

	template <typename T>
	struct TTaskPromise;

	template <typename T>
	struct FCoroutineControlBlock;

	// ============================================================================
	// FCoroutineControlBlock<T> — shared control block owning the coroutine frame
	// ============================================================================

	/**
	 * Shared control block that owns the coroutine frame and its associated state.
	 *
	 * Every TTask<T> holds a TSharedPtr to one of these. Copies of a TTask share
	 * the same control block, giving copyable, reference-counted ownership.
	 *
	 * After Start(), the block holds a SelfRef (self-shared-pointer) so the
	 * coroutine stays alive even if all external TTask handles are destroyed
	 * (fire-and-forget). SelfRef is cleared at final_suspend or self-cancellation.
	 *
	 * In latent mode, the block also stores the FLatentActionInfo and world
	 * context needed by FAsyncFlowLatentAction.
	 *
	 * @tparam T  The co_return value type. void specialization omits the Result field.
	 */
	template <typename T>
	struct FCoroutineControlBlock
	{
		std::coroutine_handle<TTaskPromise<T>> Handle;
		TSharedPtr<FAsyncFlowState> FlowState;
		TOptional<T> Result;
		std::exception_ptr Exception;
		std::atomic<bool> bCompleted{ false };
		std::atomic<bool> bStarted{ false };
		TFunction<void()> OnCompleted;

		// Self-reference for fire-and-forget. Set by Start(), cleared at final_suspend.
		TSharedPtr<FCoroutineControlBlock<T>> SelfRef;

		// Type-erased cancel function for the currently suspended awaiter.
		// Set by TContractCheckAwaiter if inner awaiter satisfies CancelableAwaiter.
		TFunction<void()> CancelCurrentAwaiterFunc;

		// Latent mode data
		bool bLatentMode = false;
		TWeakObjectPtr<UObject> LatentWorldContext;
		FLatentActionInfo StoredLatentInfo;

		~FCoroutineControlBlock()
		{
			if (Handle)
			{
				Handle.destroy();
				Handle = nullptr;
			}
		}
	};

	template <>
	struct FCoroutineControlBlock<void>
	{
		std::coroutine_handle<TTaskPromise<void>> Handle;
		TSharedPtr<FAsyncFlowState> FlowState;
		bool bHasReturned = false;
		std::exception_ptr Exception;
		std::atomic<bool> bCompleted{ false };
		std::atomic<bool> bStarted{ false };
		TFunction<void()> OnCompleted;
		TSharedPtr<FCoroutineControlBlock<void>> SelfRef;
		TFunction<void()> CancelCurrentAwaiterFunc;
		bool bLatentMode = false;
		TWeakObjectPtr<UObject> LatentWorldContext;
		FLatentActionInfo StoredLatentInfo;

		~FCoroutineControlBlock()
		{
			if (Handle)
			{
				Handle.destroy();
				Handle = nullptr;
			}
		}
	};

	// ============================================================================
	// Latent action registration (defined in AsyncFlowTask.cpp)
	// ============================================================================

	namespace Private
	{

		/**
		 * Register a FAsyncFlowLatentAction with the world's latent action manager.
		 * Only for TTask<void> in latent mode. Defined in AsyncFlowTask.cpp.
		 */
		ASYNCFLOW_API void RegisterLatentAction(UObject* WorldContext, const FLatentActionInfo& LatentInfo, TSharedPtr<FCoroutineControlBlock<void>> CB);

	} // namespace Private

	// ============================================================================
	// Forward declarations for implicit awaiting (definitions in their respective headers)
	// ============================================================================

	template <typename T>
	struct FFutureAwaiter;
	template <typename DelegateType>
	struct TWaitForDelegateAwaiter;
	template <typename... Args>
	struct TWaitForUnicastDelegateAwaiter;

	// Concept for dynamic multicast delegates (all inherit TMulticastScriptDelegate<>)
	template <typename T>
	concept DynamicMulticastDelegate = std::is_base_of_v<TMulticastScriptDelegate<>, std::remove_cvref_t<T>>;

	// Forward declaration — actual template in AsyncFlowDelegateAwaiter.h
	template <DynamicMulticastDelegate DelegateType>
	struct TWaitForDynamicDelegateAwaiter;

#if ASYNCFLOW_HAS_UE_TASKS
	template <typename T>
	struct FUETaskAwaiter;
#endif

	// ============================================================================
	// Private awaiter types
	// ============================================================================

	namespace Private
	{

		/**
 * Awaiter produced by co_await FSelfCancellation{}.
 * Cancels the flow state, marks the control block as completed, fires OnCompleted,
 * and leaves the coroutine permanently suspended.
 *
 * Clears SelfRef to allow frame destruction if fire-and-forget.
 *
 * @tparam T  The result type of the owning TTask.
 */
		template <typename T>
		struct FSelfCancelAwaiter
		{
			FCoroutineControlBlock<T>* CBPtr;

			bool await_ready() const
			{
				return false;
			}

			void await_suspend(std::coroutine_handle<>) const
			{
				// Copy everything needed to locals before potentially destroying the frame
				auto* Local = CBPtr;
				Local->FlowState->Cancel();
				Local->bCompleted.store(true, std::memory_order_release);
				auto Callback = MoveTemp(Local->OnCompleted);
				Local->SelfRef.Reset(); // May trigger frame destruction
				// Local/CBPtr may now be dangling — only use stack-local Callback
				if (Callback)
				{
					Callback();
				}
			}

			void await_resume() const
			{
			}
		};

		/**
 * Returned by final_suspend(). Marks the control block as completed and fires
 * OnCompleted from await_suspend (not from the coroutine body).
 *
 * At this point the coroutine is fully suspended, so the callback may
 * safely trigger frame destruction by clearing SelfRef.
 */
		struct FFinalAwaiter
		{
			bool await_ready() const noexcept
			{
				return false;
			}

			template <typename PromiseType>
			void await_suspend(std::coroutine_handle<PromiseType> Handle) const noexcept
			{
				PromiseType& Promise = Handle.promise();
				auto* CBPtr = Promise.CB;
				CBPtr->bCompleted.store(true, std::memory_order_release);
				auto Callback = MoveTemp(CBPtr->OnCompleted);
				CBPtr->SelfRef.Reset(); // May destroy control block + frame
				// CBPtr/Handle/Promise may be dangling — only use local Callback
				if (Callback)
				{
					Callback();
				}
			}

			void await_resume() const noexcept
			{
			}
		};

		/**
 * Wraps every awaiter passed through co_await to inject contract and
 * cancellation checks before suspension.
 *
 * If ShouldCancel() is true at the co_await boundary, the inner awaiter
 * is skipped entirely — await_ready returns true and no suspension occurs.
 *
 * Integrates with CancelableAwaiter concept: if the inner awaiter supports
 * CancelAwaiter(), the cancel function is registered on the control block
 * for expedited cancellation.
 *
 * @tparam InnerAwaiter  The original awaiter type being wrapped.
 */
		template <typename InnerAwaiter>
		struct TContractCheckAwaiter
		{
			InnerAwaiter Inner;
			TSharedPtr<FAsyncFlowState> State;

			/**
			 * Alive flag tied to this co_await site. Set to false in the destructor,
			 * which fires when the coroutine frame is destroyed while suspended here.
			 * Awaitables that support await_suspend_alive() use this to skip resuming
			 * a dangling frame.
			 */
			TSharedPtr<bool> Alive = MakeShared<bool>(true);

			/** Pointer to the control block's CancelCurrentAwaiterFunc slot. */
			TFunction<void()>* CancelSlot = nullptr;

			/**
			 * Type-erased termination callback — called in await_suspend when
			 * ShouldCancel() fires. Marks the CB as completed, clears SelfRef
			 * (triggering frame destruction once all external TTask refs drop),
			 * and fires OnCompleted. Mirrors FSelfCancelAwaiter::await_suspend.
			 */
			TFunction<void()> TerminateFunc;

			TContractCheckAwaiter(InnerAwaiter InInner, TSharedPtr<FAsyncFlowState> InState)
				: Inner(Forward<InnerAwaiter>(InInner))
				, State(MoveTemp(InState))
			{
			}

			TContractCheckAwaiter(TContractCheckAwaiter&&) noexcept = default;
			TContractCheckAwaiter& operator=(TContractCheckAwaiter&&) noexcept = default;
			TContractCheckAwaiter(const TContractCheckAwaiter&) = delete;
			TContractCheckAwaiter& operator=(const TContractCheckAwaiter&) = delete;

			~TContractCheckAwaiter()
			{
				*Alive = false;
				if (CancelSlot)
				{
					*CancelSlot = nullptr;
				}
			}

			bool await_ready()
			{
				if (State && State->ShouldCancel())
				{
					if (!State->IsCancelled())
					{
						State->Cancel();
					}
					// Return false to force await_suspend, where we can properly
					// terminate the coroutine (leaving it permanently suspended).
					return false;
				}
				return Inner.await_ready();
			}

			template <typename HandleType>
			bool await_suspend(HandleType Handle)
			{
				// Terminate the coroutine if cancelled: fire the completion
				// callback and leave this frame permanently suspended.
				// The coroutine will not execute any code after this co_await.
				if (State && State->ShouldCancel())
				{
					// Clear cancel slot before TerminateFunc potentially destroys CB.
					if (CancelSlot)
					{
						*CancelSlot = nullptr;
						CancelSlot = nullptr;
					}
					if (TerminateFunc)
					{
						TerminateFunc(); // May destroy the coroutine frame
					}
					return true; // Stay suspended — do NOT access 'this' after TerminateFunc
				}

				// Register cancel function.
				// CleanupableAwaiter (resource awaiters like semaphore/event): remove from queue
				// without resuming, then terminate the frame via TerminateFunc. This prevents
				// code after co_await from running as if the resource was successfully acquired.
				// CancelableAwaiter (timing awaiters): resume the coroutine, let the next co_await
				// detect cancellation.
				if constexpr (CleanupableAwaiter<std::remove_reference_t<InnerAwaiter>>)
				{
					if (CancelSlot && TerminateFunc)
					{
						auto* InnerPtr = &Inner;
						auto TermFn = TerminateFunc;
						*CancelSlot = [InnerPtr, TermFn]() mutable {
							InnerPtr->CleanupAwaiter();
							TermFn();
						};
					}
				}
				else if constexpr (CancelableAwaiter<std::remove_reference_t<InnerAwaiter>>)
				{
					if (CancelSlot)
					{
						auto* InnerPtr = &Inner;
						*CancelSlot = [InnerPtr]() { InnerPtr->CancelAwaiter(); };
					}
				}

				// Prefer the alive-flag-aware overload when the inner awaitable supports it.
				// This prevents Signal()/Release() from touching a destroyed coroutine frame.
				// Propagate the bool return: false = immediate resume (resource acquired inline),
				// true = coroutine stays suspended until inner schedules a resume.
				if constexpr (requires { Inner.await_suspend_alive(Handle, TWeakPtr<bool>{}); })
				{
					return Inner.await_suspend_alive(Handle, TWeakPtr<bool>(Alive));
				}
				else
				{
					Inner.await_suspend(Handle);
					return true; // void inner always suspends; it is responsible for scheduling resume
				}
			}

			auto await_resume()
			{
				if (CancelSlot)
				{
					*CancelSlot = nullptr;
				}
				return Inner.await_resume();
			}
		};

		/**
		 * Checks whether a type T has a member operator co_await().
		 * Used by await_transform to resolve operator co_await before wrapping,
		 * so that awaitable types can return per-co_await awaiter objects that
		 * support CancelableAwaiter.
		 */
		template <typename T>
		concept HasMemberCoAwait = requires(T& t) { t.operator co_await(); };

	} // namespace Private

	// ============================================================================
	// TTaskPromise<T> — promise_type for TTask<T>
	// ============================================================================

	/**
 * Promise type for TTask<T> (non-void). Created automatically by the compiler.
 *
 * Key behaviors:
 * - initial_suspend: always suspends (lazy start).
 * - final_suspend: fires OnCompleted via FFinalAwaiter.
 * - await_transform: wraps every co_await in TContractCheckAwaiter for contract/cancel checks.
 * - Stores the co_return value in the control block's Result (TOptional<T>).
 * - Latent mode detection: if the coroutine function signature includes a
 *   FLatentActionInfo parameter (with or without a UObject* world context),
 *   the compiler selects a constructor overload that captures the latent info
 *   and world context. The task then auto-registers with the engine's latent
 *   action manager on Start(). No explicit helper call is needed.
 *
 * @tparam T  The value type returned by co_return.
 */
	template <typename T>
	struct TTaskPromise
	{
		using CoroutineHandle = std::coroutine_handle<TTaskPromise<T>>;

		/** Shared state for cancellation, contracts, and debug info. */
		TSharedPtr<FAsyncFlowState> FlowState = MakeShared<FAsyncFlowState>();

		/** Raw pointer to the control block (set in get_return_object). */
		FCoroutineControlBlock<T>* CB = nullptr;

		/** Default constructor — async mode (no FLatentActionInfo detected). */
		TTaskPromise() = default;

		/** Latent mode: free function with (UObject*, FLatentActionInfo). */
		TTaskPromise(UObject* WorldContext, FLatentActionInfo LatentInfo)
			: FlowState(MakeShared<FAsyncFlowState>())
		{
			InitLatentMode(WorldContext, LatentInfo);
		}

		/** Latent mode: member function (this, FLatentActionInfo). */
		template <typename ClassType>
		TTaskPromise(ClassType& Self, FLatentActionInfo LatentInfo)
			: FlowState(MakeShared<FAsyncFlowState>())
		{
			InitLatentMode(Cast<UObject>(&Self), LatentInfo);
		}

		/** Latent mode: free function with (UObject*, FLatentActionInfo, extra params...). */
		template <typename... Extra>
		TTaskPromise(UObject* WorldContext, FLatentActionInfo LatentInfo, Extra&&...)
			: FlowState(MakeShared<FAsyncFlowState>())
		{
			InitLatentMode(WorldContext, LatentInfo);
		}

		/** Latent mode: member function (this, FLatentActionInfo, extra params...). */
		template <typename ClassType, typename... Extra>
		TTaskPromise(ClassType& Self, FLatentActionInfo LatentInfo, Extra&&...)
			: FlowState(MakeShared<FAsyncFlowState>())
		{
			InitLatentMode(Cast<UObject>(&Self), LatentInfo);
		}

		TTask<T> get_return_object();

		std::suspend_always initial_suspend() const noexcept
		{
			return {};
		}

		Private::FFinalAwaiter final_suspend() noexcept
		{
			return {};
		}

		void return_value(T Value)
		{
			if (CB)
			{
				CB->Result.Emplace(MoveTemp(Value));
			}
		}

		void unhandled_exception()
		{
			if (CB)
			{
				CB->Exception = std::current_exception();
			}
			UE_LOG(LogAsyncFlow, Error, TEXT("Unhandled exception in AsyncFlow coroutine [%s]"), *FlowState->DebugName);
		}

		/**
	 * Wraps every co_await expression to inject contract/cancellation checks.
	 * Uses a forwarding reference so lvalue awaiters (e.g. non-copyable FAwaitableEvent)
	 * are stored by reference, while rvalue awaiters are moved by value.
	 *
	 * If the awaitable defines operator co_await(), the returned awaiter object is
	 * used as the inner awaiter. This enables types like FAwaitableEvent to provide
	 * per-co_await FAwaiter objects that track the handle and support CancelableAwaiter.
	 *
	 * @param Awaiter  The awaiter expression from the co_await.
	 * @return         A TContractCheckAwaiter wrapping the original (or resolved) awaiter.
	 */
		template <typename AwaiterType>
		auto await_transform(AwaiterType&& Awaiter)
		{
			if constexpr (Private::HasMemberCoAwait<std::remove_reference_t<AwaiterType>>)
			{
				auto InnerAwaiter = std::forward<AwaiterType>(Awaiter).operator co_await();
				Private::TContractCheckAwaiter<decltype(InnerAwaiter)> Wrapper{
					std::move(InnerAwaiter), FlowState
				};
				SetupContractAwaiter(Wrapper);
				return Wrapper;
			}
			else
			{
				Private::TContractCheckAwaiter<AwaiterType> Wrapper{ std::forward<AwaiterType>(Awaiter), FlowState };
				SetupContractAwaiter(Wrapper);
				return Wrapper;
			}
		}

		// FSelfCancellation: cancel + complete immediately, no subsequent code runs
		Private::FSelfCancelAwaiter<T> await_transform(FSelfCancellation)
		{
			return Private::FSelfCancelAwaiter<T>{ CB };
		}

		// FFinishNowIfCanceled: goes through contract check (cancellation detected there)
		Private::TContractCheckAwaiter<FFinishNowIfCanceled> await_transform(FFinishNowIfCanceled)
		{
			Private::TContractCheckAwaiter<FFinishNowIfCanceled> Wrapper{ FFinishNowIfCanceled{}, FlowState };
			SetupContractAwaiter(Wrapper);
			return Wrapper;
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

		// === Implicit awaiting overloads ===
		// These forward-declare the awaiter types. The actual definitions are in
		// AsyncFlowThreadAwaiters.h and AsyncFlowDelegateAwaiter.h respectively.
		// The user must include those headers for implicit awaiting to work.

		// TFuture<U> — co_await a future directly
		template <typename U>
		Private::TContractCheckAwaiter<FFutureAwaiter<U>> await_transform(TFuture<U>&& Future)
		{
			Private::TContractCheckAwaiter<FFutureAwaiter<U>> Wrapper{
				FFutureAwaiter<U>(MoveTemp(Future)), FlowState
			};
			SetupContractAwaiter(Wrapper);
			return Wrapper;
		}

		// TMulticastDelegate<void(Args...)> — co_await a multicast delegate directly
		template <typename... Args>
		Private::TContractCheckAwaiter<TWaitForDelegateAwaiter<TMulticastDelegate<void(Args...)>>> await_transform(TMulticastDelegate<void(Args...)>& Delegate)
		{
			Private::TContractCheckAwaiter<TWaitForDelegateAwaiter<TMulticastDelegate<void(Args...)>>> Wrapper{
				TWaitForDelegateAwaiter<TMulticastDelegate<void(Args...)>>(Delegate), FlowState
			};
			SetupContractAwaiter(Wrapper);
			return Wrapper;
		}

		// TDelegate<void(Args...)> — co_await a unicast delegate directly
		template <typename... Args>
		Private::TContractCheckAwaiter<TWaitForUnicastDelegateAwaiter<Args...>> await_transform(TDelegate<void(Args...)>& Delegate)
		{
			Private::TContractCheckAwaiter<TWaitForUnicastDelegateAwaiter<Args...>> Wrapper{
				TWaitForUnicastDelegateAwaiter<Args...>(Delegate), FlowState
			};
			SetupContractAwaiter(Wrapper);
			return Wrapper;
		}

		// Dynamic multicast delegates — co_await any DECLARE_DYNAMIC_MULTICAST_DELEGATE type
		template <DynamicMulticastDelegate DT>
		Private::TContractCheckAwaiter<TWaitForDynamicDelegateAwaiter<DT>> await_transform(DT& Delegate)
		{
			Private::TContractCheckAwaiter<TWaitForDynamicDelegateAwaiter<DT>> Wrapper{
				TWaitForDynamicDelegateAwaiter<DT>(Delegate), FlowState
			};
			SetupContractAwaiter(Wrapper);
			return Wrapper;
		}

#if ASYNCFLOW_HAS_UE_TASKS
		// UE::Tasks::TTask<U> — co_await a UE task directly
		template <typename U>
		Private::TContractCheckAwaiter<FUETaskAwaiter<U>> await_transform(UE::Tasks::TTask<U>&& Task)
		{
			Private::TContractCheckAwaiter<FUETaskAwaiter<U>> Wrapper{
				FUETaskAwaiter<U>(MoveTemp(Task)), FlowState
			};
			SetupContractAwaiter(Wrapper);
			return Wrapper;
		}
#endif

	private:
		/**
		 * Wires up both CancelSlot and TerminateFunc on a TContractCheckAwaiter
		 * so that CO_CONTRACT violations permanently stop the coroutine.
		 * Called from every await_transform overload that produces a wrapper.
		 */
		void SetupContractAwaiter(auto& Wrapper)
		{
			if (CB)
			{
				Wrapper.CancelSlot = &CB->CancelCurrentAwaiterFunc;
				auto* CBPtr = CB;
				Wrapper.TerminateFunc = [CBPtr]() {
					CBPtr->FlowState->Cancel();
					CBPtr->bCompleted.store(true, std::memory_order_release);
					auto Callback = MoveTemp(CBPtr->OnCompleted);
					CBPtr->SelfRef.Reset(); // Destroys frame when no external TTask refs remain
					if (Callback)
					{
						Callback();
					}
				};
			}
		}

		void InitLatentMode(UObject* WorldContext, FLatentActionInfo LatentInfo)
		{
			bLatentMode = true;
			LatentWorldContext = WorldContext;
			StoredLatentInfo = LatentInfo;
			if (UObject* Target = LatentInfo.CallbackTarget.Get())
			{
				FlowState->DebugName = FString::Printf(TEXT("Latent_%s_%d"), *Target->GetName(), LatentInfo.UUID);
			}
			else
			{
				FlowState->DebugName = FString::Printf(TEXT("Latent_%d"), LatentInfo.UUID);
			}
			FlowState->AddContract([WeakCtx = TWeakObjectPtr<UObject>(WorldContext)]() {
				return WeakCtx.IsValid();
			});
		}

		bool bLatentMode = false;
		TWeakObjectPtr<UObject> LatentWorldContext;
		FLatentActionInfo StoredLatentInfo;

		// get_return_object() needs access to transfer latent data to the control block
		friend TTask<T>;
	};

	template <>
	struct TTaskPromise<void>
	{
		using CoroutineHandle = std::coroutine_handle<TTaskPromise<void>>;

		TSharedPtr<FAsyncFlowState> FlowState = MakeShared<FAsyncFlowState>();
		FCoroutineControlBlock<void>* CB = nullptr;

		/** Default constructor — async mode (no FLatentActionInfo detected). */
		TTaskPromise() = default;

		/** Latent mode: free function with (UObject*, FLatentActionInfo). */
		TTaskPromise(UObject* WorldContext, FLatentActionInfo LatentInfo)
			: FlowState(MakeShared<FAsyncFlowState>())
		{
			InitLatentMode(WorldContext, LatentInfo);
		}

		/** Latent mode: member function (this, FLatentActionInfo). */
		template <typename ClassType>
		TTaskPromise(ClassType& Self, FLatentActionInfo LatentInfo)
			: FlowState(MakeShared<FAsyncFlowState>())
		{
			InitLatentMode(Cast<UObject>(&Self), LatentInfo);
		}

		/** Latent mode: free function with (UObject*, FLatentActionInfo, extra params...). */
		template <typename... Extra>
		TTaskPromise(UObject* WorldContext, FLatentActionInfo LatentInfo, Extra&&...)
			: FlowState(MakeShared<FAsyncFlowState>())
		{
			InitLatentMode(WorldContext, LatentInfo);
		}

		/** Latent mode: member function (this, FLatentActionInfo, extra params...). */
		template <typename ClassType, typename... Extra>
		TTaskPromise(ClassType& Self, FLatentActionInfo LatentInfo, Extra&&...)
			: FlowState(MakeShared<FAsyncFlowState>())
		{
			InitLatentMode(Cast<UObject>(&Self), LatentInfo);
		}

		TTask<void> get_return_object();

		std::suspend_always initial_suspend() const noexcept
		{
			return {};
		}

		Private::FFinalAwaiter final_suspend() noexcept
		{
			return {};
		}

		void return_void()
		{
			if (CB)
			{
				CB->bHasReturned = true;
			}
		}

		void unhandled_exception()
		{
			if (CB)
			{
				CB->Exception = std::current_exception();
			}
			UE_LOG(LogAsyncFlow, Error, TEXT("Unhandled exception in AsyncFlow coroutine [%s]"), *FlowState->DebugName);
		}

		template <typename AwaiterType>
		auto await_transform(AwaiterType&& Awaiter)
		{
			if constexpr (Private::HasMemberCoAwait<std::remove_reference_t<AwaiterType>>)
			{
				auto InnerAwaiter = std::forward<AwaiterType>(Awaiter).operator co_await();
				Private::TContractCheckAwaiter<decltype(InnerAwaiter)> Wrapper{
					std::move(InnerAwaiter), FlowState
				};
				SetupContractAwaiter(Wrapper);
				return Wrapper;
			}
			else
			{
				Private::TContractCheckAwaiter<AwaiterType> Wrapper{ std::forward<AwaiterType>(Awaiter), FlowState };
				SetupContractAwaiter(Wrapper);
				return Wrapper;
			}
		}

		// FSelfCancellation: cancel + complete immediately, no subsequent code runs
		Private::FSelfCancelAwaiter<void> await_transform(FSelfCancellation)
		{
			return Private::FSelfCancelAwaiter<void>{ CB };
		}

		// FFinishNowIfCanceled: goes through contract check (cancellation detected there)
		Private::TContractCheckAwaiter<FFinishNowIfCanceled> await_transform(FFinishNowIfCanceled)
		{
			Private::TContractCheckAwaiter<FFinishNowIfCanceled> Wrapper{ FFinishNowIfCanceled{}, FlowState };
			SetupContractAwaiter(Wrapper);
			return Wrapper;
		}

		/** Bypass contract wrapping for internal suspend points. See TTaskPromise<T>::await_transform. */
		std::suspend_always await_transform(std::suspend_always Awaiter)
		{
			return Awaiter;
		}

		// === Implicit awaiting overloads ===

		template <typename U>
		Private::TContractCheckAwaiter<FFutureAwaiter<U>> await_transform(TFuture<U>&& Future)
		{
			Private::TContractCheckAwaiter<FFutureAwaiter<U>> Wrapper{
				FFutureAwaiter<U>(MoveTemp(Future)), FlowState
			};
			SetupContractAwaiter(Wrapper);
			return Wrapper;
		}

		template <typename... Args>
		Private::TContractCheckAwaiter<TWaitForDelegateAwaiter<TMulticastDelegate<void(Args...)>>> await_transform(TMulticastDelegate<void(Args...)>& Delegate)
		{
			Private::TContractCheckAwaiter<TWaitForDelegateAwaiter<TMulticastDelegate<void(Args...)>>> Wrapper{
				TWaitForDelegateAwaiter<TMulticastDelegate<void(Args...)>>(Delegate), FlowState
			};
			SetupContractAwaiter(Wrapper);
			return Wrapper;
		}

		template <typename... Args>
		Private::TContractCheckAwaiter<TWaitForUnicastDelegateAwaiter<Args...>> await_transform(TDelegate<void(Args...)>& Delegate)
		{
			Private::TContractCheckAwaiter<TWaitForUnicastDelegateAwaiter<Args...>> Wrapper{
				TWaitForUnicastDelegateAwaiter<Args...>(Delegate), FlowState
			};
			SetupContractAwaiter(Wrapper);
			return Wrapper;
		}

		// Dynamic multicast delegates — co_await any DECLARE_DYNAMIC_MULTICAST_DELEGATE type
		template <DynamicMulticastDelegate DT>
		Private::TContractCheckAwaiter<TWaitForDynamicDelegateAwaiter<DT>> await_transform(DT& Delegate)
		{
			Private::TContractCheckAwaiter<TWaitForDynamicDelegateAwaiter<DT>> Wrapper{
				TWaitForDynamicDelegateAwaiter<DT>(Delegate), FlowState
			};
			SetupContractAwaiter(Wrapper);
			return Wrapper;
		}

#if ASYNCFLOW_HAS_UE_TASKS
		template <typename U>
		Private::TContractCheckAwaiter<FUETaskAwaiter<U>> await_transform(UE::Tasks::TTask<U>&& Task)
		{
			Private::TContractCheckAwaiter<FUETaskAwaiter<U>> Wrapper{
				FUETaskAwaiter<U>(MoveTemp(Task)), FlowState
			};
			SetupContractAwaiter(Wrapper);
			return Wrapper;
		}
#endif

	private:
		/** See TTaskPromise<T>::SetupContractAwaiter. */
		void SetupContractAwaiter(auto& Wrapper)
		{
			if (CB)
			{
				Wrapper.CancelSlot = &CB->CancelCurrentAwaiterFunc;
				auto* CBPtr = CB;
				Wrapper.TerminateFunc = [CBPtr]() {
					CBPtr->FlowState->Cancel();
					CBPtr->bCompleted.store(true, std::memory_order_release);
					auto Callback = MoveTemp(CBPtr->OnCompleted);
					CBPtr->SelfRef.Reset();
					if (Callback)
					{
						Callback();
					}
				};
			}
		}

		void InitLatentMode(UObject* WorldContext, FLatentActionInfo LatentInfo)
		{
			bLatentMode = true;
			LatentWorldContext = WorldContext;
			StoredLatentInfo = LatentInfo;
			if (UObject* Target = LatentInfo.CallbackTarget.Get())
			{
				FlowState->DebugName = FString::Printf(TEXT("Latent_%s_%d"), *Target->GetName(), LatentInfo.UUID);
			}
			else
			{
				FlowState->DebugName = FString::Printf(TEXT("Latent_%d"), LatentInfo.UUID);
			}
			FlowState->AddContract([WeakCtx = TWeakObjectPtr<UObject>(WorldContext)]() {
				return WeakCtx.IsValid();
			});
		}

		bool bLatentMode = false;
		TWeakObjectPtr<UObject> LatentWorldContext;
		FLatentActionInfo StoredLatentInfo;

		// get_return_object() needs access to transfer latent data to the control block
		friend TTask<void>;
	};

	// ============================================================================
	// TTask<T> — the coroutine return type (non-void specialization)
	// ============================================================================

	/**
 * Lazily-started, copyable, discardable coroutine handle with shared ownership.
 *
 * Internally holds a TSharedPtr to a FCoroutineControlBlock that owns the
 * coroutine frame. Copies of TTask share the same control block, so the
 * type is freely copyable and assignable.
 *
 * The coroutine does not execute until Start() is called (only once).
 * After Start(), the coroutine is self-sustaining — all TTask copies
 * can be destroyed without cancelling the coroutine (fire-and-forget /
 * discardable). Before Start(), destroying the last TTask copy destroys
 * the frame.
 *
 * Dual execution mode:
 *   - Async (default): the coroutine runs via explicit Start() / Resume().
 *   - Latent (auto-detected): when the promise detects a FLatentActionInfo
 *     parameter, Start() also registers with the engine's latent action
 *     manager so the Blueprint output pin fires on completion.
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

		explicit TTask(TSharedPtr<FCoroutineControlBlock<T>> InCB)
			: CB(MoveTemp(InCB))
		{
		}

		// Copyable
		TTask(const TTask& Other) = default;
		TTask& operator=(const TTask& Other) = default;

		// Moveable
		TTask(TTask&& Other) noexcept = default;
		TTask& operator=(TTask&& Other) noexcept = default;

		~TTask() = default; // Shared ref released automatically

		/**
	 * Start execution of the coroutine. Only the first call has effect.
	 * Sets the self-reference for fire-and-forget lifetime.
	 */
		void Start()
		{
			if (CB && !CB->bStarted.exchange(true, std::memory_order_acq_rel))
			{
				CB->SelfRef = CB; // Fire-and-forget: coroutine stays alive
				Resume();
			}
		}

		/**
	 * Start execution without making the coroutine self-sustaining.
	 * The caller manages coroutine lifetime via its TTask handle(s).
	 * Use this in WhenAll/WhenAny/Race where the outer frame owns the tasks.
	 */
		void StartManaged()
		{
			if (CB && !CB->bStarted.exchange(true, std::memory_order_acq_rel))
			{
				// No SelfRef — lifetime tied to external TTask handles
				Resume();
			}
		}

		/**
	 * Resume the coroutine from its current suspension point.
	 * On first call via Start(), this begins execution.
	 *
	 * Copies the handle to a local before calling resume() — if the coroutine
	 * completes synchronously and its OnCompleted callback destroys this TTask,
	 * the local handle keeps the frame alive for the duration of resume().
	 */
		void Resume()
		{
			if (CB && CB->Handle && !CB->Handle.done())
			{
				auto LocalHandle = CB->Handle;
				Private::SetCurrentFlowState(CB->FlowState.Get());
				if (CB->bLatentMode && CB->LatentWorldContext.IsValid())
				{
					Private::SetCurrentWorldContext(CB->LatentWorldContext.Get());
				}
				LocalHandle.resume();
				Private::SetCurrentWorldContext(nullptr);
				Private::SetCurrentFlowState(nullptr);
			}
		}

		/**
	 * Request cancellation. Calls CancelAwaiter on the current awaiter
	 * for expedited cancellation if supported.
	 */
		void Cancel()
		{
			if (CB)
			{
				CB->FlowState->Cancel();
				if (CB->CancelCurrentAwaiterFunc)
				{
					auto CancelFunc = MoveTemp(CB->CancelCurrentAwaiterFunc);
					CB->CancelCurrentAwaiterFunc = nullptr;
					CancelFunc();
				}
			}
		}

		/** @return true if the coroutine has reached final_suspend or self-cancelled. */
		bool IsCompleted() const
		{
			return CB && CB->bCompleted.load(std::memory_order_acquire);
		}

		/** @return true if Cancel() was called on this task's flow state. */
		bool IsCancelled() const
		{
			return CB && CB->FlowState && CB->FlowState->IsCancelled();
		}

		/** @return true if the task completed without cancellation. */
		bool WasSuccessful() const
		{
			return IsCompleted() && !IsCancelled();
		}

		/** @return true if this TTask holds a valid control block. */
		bool IsValid() const
		{
			return CB != nullptr;
		}

		/** @return true if Start() has been called. */
		bool IsStarted() const
		{
			return CB && CB->bStarted.load(std::memory_order_acquire);
		}

		/**
	 * Access the co_return value. Only valid after IsCompleted() returns true.
	 * @warning Asserts if called before completion.
	 */
		const T& GetResult() const
		{
			check(IsCompleted());
			return CB->Result.GetValue();
		}

		/**
	 * Move the co_return value out. Only valid after IsCompleted() returns true.
	 * @warning Asserts if called before completion. Leaves Result in a moved-from state.
	 */
		T MoveResult()
		{
			check(IsCompleted());
			return MoveTemp(CB->Result.GetValue());
		}

		/**
	 * Register a one-shot callback that fires when the coroutine finishes.
	 * Only one callback can be registered — registering a second asserts.
	 *
	 * @param Callback  Invoked on the game thread when the coroutine completes.
	 */
		void OnComplete(TFunction<void()> Callback)
		{
			if (CB)
			{
				ensureMsgf(!CB->OnCompleted, TEXT("OnComplete callback already registered for task [%s]"), *GetDebugName());
				CB->OnCompleted = MoveTemp(Callback);
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
			OnComplete([WeakObj, Cb = MoveTemp(Callback)]() {
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
			if (CB && CB->FlowState)
			{
				CB->FlowState->OnCancelledCallback = MoveTemp(Callback);
			}
		}

		/**
	 * Assign a human-readable name for logging and FAsyncFlowDebugger tracking.
	 *
	 * @param Name  Debug label (e.g., "AttackSequence" or "LoadLevel_Forest").
	 */
		void SetDebugName(const FString& Name)
		{
			if (CB && CB->FlowState)
			{
				CB->FlowState->DebugName = Name;
			}
		}

		/** @return the debug name, or an empty string if none was set. */
		FString GetDebugName() const
		{
			return (CB && CB->FlowState) ? CB->FlowState->DebugName : FString();
		}

		/**
	 * @return the shared flow state. Use for external contract registration
	 *         or cancellation propagation (e.g., Race() cancels loser tasks).
	 */
		TSharedPtr<FAsyncFlowState> GetFlowState() const
		{
			return CB ? CB->FlowState : nullptr;
		}

		/** @return the raw std::coroutine_handle for low-level awaiter integration. */
		CoroutineHandle GetHandle() const
		{
			return CB ? CB->Handle : CoroutineHandle{};
		}

		/** Get a hash for use as container key. */
		friend uint32 GetTypeHash(const TTask& Task)
		{
			return Task.CB ? ::GetTypeHash(reinterpret_cast<uintptr_t>(Task.CB->Handle.address())) : 0;
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
			auto Coro = [](T Val) -> TTask<T> {
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
		bool await_ready() const
		{
			return IsCompleted();
		}

		/**
	 * Hook the parent coroutine's continuation so it resumes when this task finishes.
	 * If the task hasn't started yet, Start() is called here.
	 */
		void await_suspend(std::coroutine_handle<> Continuation)
		{
			if (CB)
			{
				ensureMsgf(!CB->OnCompleted, TEXT("await_suspend: OnComplete callback already registered for task [%s]"), *GetDebugName());
				CB->OnCompleted = [Continuation]() mutable { Continuation.resume(); };
				if (!IsStarted())
				{
					Start();
				}
			}
		}

		/**
	 * Called when the parent coroutine resumes after co_await.
	 * Rethrows any captured exception, then returns the result by move.
	 */
		T await_resume()
		{
			if (CB && CB->Exception)
			{
				std::rethrow_exception(CB->Exception);
			}
			return CB ? MoveTemp(CB->Result.GetValue()) : T{};
		}

	private:
		TSharedPtr<FCoroutineControlBlock<T>> CB;
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

		explicit TTask(TSharedPtr<FCoroutineControlBlock<void>> InCB)
			: CB(MoveTemp(InCB))
		{
		}

		// Copyable
		TTask(const TTask& Other) = default;
		TTask& operator=(const TTask& Other) = default;

		// Moveable
		TTask(TTask&& Other) noexcept = default;
		TTask& operator=(TTask&& Other) noexcept = default;

		~TTask() = default; // Shared ref released automatically

		/**
	 * Start execution of the coroutine. Only the first call has effect.
	 * Sets the self-reference for fire-and-forget lifetime.
	 * In latent mode, also registers with the engine's latent action manager.
	 */
		void Start()
		{
			if (CB && !CB->bStarted.exchange(true, std::memory_order_acq_rel))
			{
				CB->SelfRef = CB; // Fire-and-forget: coroutine stays alive
				if (CB->bLatentMode)
				{
					if (UObject* Ctx = CB->LatentWorldContext.Get())
					{
						Private::RegisterLatentAction(Ctx, CB->StoredLatentInfo, CB);
					}
				}
				Resume();
			}
		}

		/**
	 * Start execution without making the coroutine self-sustaining.
	 * The caller manages coroutine lifetime via its TTask handle(s).
	 * Use this in WhenAll/WhenAny/Race where the outer frame owns the tasks.
	 */
		void StartManaged()
		{
			if (CB && !CB->bStarted.exchange(true, std::memory_order_acq_rel))
			{
				// No SelfRef — lifetime tied to external TTask handles
				if (CB->bLatentMode)
				{
					if (UObject* Ctx = CB->LatentWorldContext.Get())
					{
						Private::RegisterLatentAction(Ctx, CB->StoredLatentInfo, CB);
					}
				}
				Resume();
			}
		}

		/**
	 * Resume the coroutine from its current suspension point.
	 * On first call via Start(), this begins execution.
	 *
	 * Copies the handle to a local before calling resume() — if the coroutine
	 * completes synchronously and its OnCompleted callback destroys this TTask,
	 * the local handle keeps the frame alive for the duration of resume().
	 */
		void Resume()
		{
			if (CB && CB->Handle && !CB->Handle.done())
			{
				auto LocalHandle = CB->Handle;
				Private::SetCurrentFlowState(CB->FlowState.Get());
				if (CB->bLatentMode && CB->LatentWorldContext.IsValid())
				{
					Private::SetCurrentWorldContext(CB->LatentWorldContext.Get());
				}
				LocalHandle.resume();
				Private::SetCurrentWorldContext(nullptr);
				Private::SetCurrentFlowState(nullptr);
			}
		}

		/**
	 * Request cancellation. Calls CancelAwaiter on the current awaiter
	 * for expedited cancellation if supported.
	 */
		void Cancel()
		{
			if (CB)
			{
				CB->FlowState->Cancel();
				if (CB->CancelCurrentAwaiterFunc)
				{
					auto CancelFunc = MoveTemp(CB->CancelCurrentAwaiterFunc);
					CB->CancelCurrentAwaiterFunc = nullptr;
					CancelFunc();
				}
			}
		}

		/** @return true if the coroutine has reached final_suspend or self-cancelled. */
		bool IsCompleted() const
		{
			return CB && CB->bCompleted.load(std::memory_order_acquire);
		}

		/** @return true if Cancel() was called on this task's flow state. */
		bool IsCancelled() const
		{
			return CB && CB->FlowState && CB->FlowState->IsCancelled();
		}

		/** Returns true if the task completed without cancellation. */
		bool WasSuccessful() const
		{
			return IsCompleted() && !IsCancelled();
		}

		/** @return true if this TTask holds a valid control block. */
		bool IsValid() const
		{
			return CB != nullptr;
		}

		/** @return true if Start() has been called. */
		bool IsStarted() const
		{
			return CB && CB->bStarted.load(std::memory_order_acquire);
		}

		/** Register a callback for when this task completes. */
		void OnComplete(TFunction<void()> Callback)
		{
			if (CB)
			{
				ensureMsgf(!CB->OnCompleted, TEXT("OnComplete callback already registered for task [%s]"), *GetDebugName());
				CB->OnCompleted = MoveTemp(Callback);
			}
		}

		/** Register a weak-ref callback; only fires if the UObject is still valid. */
		void ContinueWithWeak(TWeakObjectPtr<UObject> WeakObj, TFunction<void()> Callback)
		{
			OnComplete([WeakObj, Cb = MoveTemp(Callback)]() {
				if (WeakObj.IsValid())
				{
					Cb();
				}
			});
		}

		/** Register a callback for when this task is cancelled. */
		void OnCancelled(TFunction<void()> Callback)
		{
			if (CB && CB->FlowState)
			{
				CB->FlowState->OnCancelledCallback = MoveTemp(Callback);
			}
		}

		void SetDebugName(const FString& Name)
		{
			if (CB && CB->FlowState)
			{
				CB->FlowState->DebugName = Name;
			}
		}

		FString GetDebugName() const
		{
			return (CB && CB->FlowState) ? CB->FlowState->DebugName : FString();
		}

		/** Get the shared flow state (for contract registration, cancellation propagation). */
		TSharedPtr<FAsyncFlowState> GetFlowState() const
		{
			return CB ? CB->FlowState : nullptr;
		}

		/** Get the raw coroutine handle (for awaiter integration). */
		CoroutineHandle GetHandle() const
		{
			return CB ? CB->Handle : CoroutineHandle{};
		}

		/** Get a hash for use as container key. */
		friend uint32 GetTypeHash(const TTask& Task)
		{
			return Task.CB ? ::GetTypeHash(reinterpret_cast<uintptr_t>(Task.CB->Handle.address())) : 0;
		}

		/** Create an already-completed void task. */
		static TTask CompletedTask()
		{
			auto Coro = []() -> TTask<void> {
				co_return;
			};
			TTask<void> Task = Coro();
			Task.Start();
			return Task;
		}

		// Awaitable interface
		bool await_ready() const
		{
			return IsCompleted();
		}

		void await_suspend(std::coroutine_handle<> Continuation)
		{
			if (CB)
			{
				ensureMsgf(!CB->OnCompleted, TEXT("await_suspend: OnComplete callback already registered for task [%s]"), *GetDebugName());
				CB->OnCompleted = [Continuation]() mutable { Continuation.resume(); };
				if (!IsStarted())
				{
					Start();
				}
			}
		}

		void await_resume()
		{
			if (CB && CB->Exception)
			{
				std::rethrow_exception(CB->Exception);
			}
		}

	private:
		TSharedPtr<FCoroutineControlBlock<void>> CB;
	};

	// ============================================================================
	// get_return_object implementations
	// ============================================================================

	template <typename T>
	TTask<T> TTaskPromise<T>::get_return_object()
	{
		TSharedPtr<FCoroutineControlBlock<T>> CBShared(MakeShared<FCoroutineControlBlock<T>>());
		CBShared->Handle = std::coroutine_handle<TTaskPromise<T>>::from_promise(*this);
		CBShared->FlowState = FlowState;
		if (bLatentMode)
		{
			CBShared->bLatentMode = true;
			CBShared->LatentWorldContext = LatentWorldContext;
			CBShared->StoredLatentInfo = StoredLatentInfo;
		}
		CB = CBShared.Get(); // Raw pointer for promise access
		return TTask<T>(MoveTemp(CBShared));
	}

	inline TTask<void> TTaskPromise<void>::get_return_object()
	{
		TSharedPtr<FCoroutineControlBlock<void>> CBShared(MakeShared<FCoroutineControlBlock<void>>());
		CBShared->Handle = std::coroutine_handle<TTaskPromise<void>>::from_promise(*this);
		CBShared->FlowState = FlowState;
		if (bLatentMode)
		{
			CBShared->bLatentMode = true;
			CBShared->LatentWorldContext = LatentWorldContext;
			CBShared->StoredLatentInfo = StoredLatentInfo;
		}
		CB = CBShared.Get(); // Raw pointer for promise access
		return TTask<void>(MoveTemp(CBShared));
	}

} // namespace AsyncFlow
