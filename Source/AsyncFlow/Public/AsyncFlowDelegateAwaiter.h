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

// AsyncFlowDelegateAwaiter.h — Universal delegate-to-coroutine bridge
//
// Bridges UE delegate systems (multicast, unicast, dynamic) to co_await.
// Each awaiter binds to the delegate on suspend, captures the broadcast
// arguments, unbinds, and resumes the coroutine. One-shot by design —
// the awaiter only captures the first broadcast after suspension.
#pragma once

#include "AsyncFlowTask.h"
#include "AsyncFlowDynamicDelegateBridge.h"
#include "AsyncFlowTypedDelegateBridge.h"
#include "Delegates/Delegate.h"
#include "Delegates/DelegateCombinations.h"
#include "Templates/Tuple.h"

#include <coroutine>

namespace AsyncFlow
{

	// ============================================================================
	// WaitForDelegate — binds to any UE multicast delegate, resumes on broadcast
	// ============================================================================

	/**
	 * Primary template for multicast delegates with parameters.
	 * Specializations below handle TMulticastDelegate<void(Args...)>
	 * and the void (no-args) case separately.
	 */
	template <typename DelegateType>
	struct TWaitForDelegateAwaiter;

	/**
	 * Awaiter for TMulticastDelegate<void(Args...)>.
	 * Captures broadcast arguments as a TTuple and unbinds after the first broadcast.
	 *
	 * @tparam Args  Parameter types of the multicast delegate signature.
	 */
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

		~TWaitForDelegateAwaiter()
		{
			*AliveFlag = false;
		}

		bool await_ready() const
		{
			return false;
		}

		void await_suspend(std::coroutine_handle<> Handle)
		{
			Continuation = Handle;

			TWeakPtr<bool> WeakAlive = AliveFlag;
			BoundHandle = Delegate.AddLambda([this, WeakAlive](Args... InArgs) {
				if (!WeakAlive.IsValid())
				{
					return;
				}
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

		~TWaitForDelegateAwaiter()
		{
			*AliveFlag = false;
		}

		bool await_ready() const
		{
			return false;
		}

		void await_suspend(std::coroutine_handle<> Handle)
		{
			Continuation = Handle;

			TWeakPtr<bool> WeakAlive = AliveFlag;
			BoundHandle = Delegate.AddLambda([this, WeakAlive]() {
				if (!WeakAlive.IsValid())
				{
					return;
				}
				Delegate.Remove(BoundHandle);
				if (Continuation && !Continuation.done())
				{
					Continuation.resume();
				}
			});
		}

		void await_resume()
		{
		}
	};

	/**
	 * Wait for a multicast delegate to fire once.
	 * Arguments are captured and returned as a TTuple.
	 *
	 * @param Delegate  Reference to the multicast delegate to listen on.
	 * @return          An awaiter — co_await yields TTuple<decay_t<Args>...>.
	 */
	template <typename... Args>
	[[nodiscard]] TWaitForDelegateAwaiter<TMulticastDelegate<void(Args...)>> WaitForDelegate(TMulticastDelegate<void(Args...)>& Delegate)
	{
		return TWaitForDelegateAwaiter<TMulticastDelegate<void(Args...)>>(Delegate);
	}

	// ============================================================================
	// WaitForUnicastDelegate — binds to a unicast TDelegate, resumes on invoke
	// ============================================================================

	/**
	 * Awaiter for TDelegate<void(Args...)>. Binds a lambda, captures the
	 * invocation arguments as a TTuple, unbinds, and resumes the coroutine.
	 *
	 * @tparam Args  Parameter types of the unicast delegate signature.
	 *
	 * @warning Overwrites any existing binding on the delegate. The previous
	 *          binding is lost.
	 */
	template <typename... Args>
	struct TWaitForUnicastDelegateAwaiter
	{
		using DelegateType = TDelegate<void(Args...)>;
		using ResultType = TTuple<std::decay_t<Args>...>;

		DelegateType& Delegate;
		TOptional<ResultType> CapturedArgs;
		std::coroutine_handle<> Continuation;
		TSharedPtr<bool> AliveFlag = MakeShared<bool>(true);

		explicit TWaitForUnicastDelegateAwaiter(DelegateType& InDelegate)
			: Delegate(InDelegate)
		{
		}

		~TWaitForUnicastDelegateAwaiter()
		{
			*AliveFlag = false;
		}

		bool await_ready() const
		{
			return false;
		}

		void await_suspend(std::coroutine_handle<> Handle)
		{
			Continuation = Handle;

			TWeakPtr<bool> WeakAlive = AliveFlag;
			Delegate.BindLambda([this, WeakAlive](Args... InArgs) {
				if (!WeakAlive.IsValid())
				{
					return;
				}
				CapturedArgs.Emplace(MakeTuple(InArgs...));
				Delegate.Unbind();
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

	// Void specialization for unicast
	template <>
	struct TWaitForUnicastDelegateAwaiter<>
	{
		using DelegateType = TDelegate<void()>;

		DelegateType& Delegate;
		std::coroutine_handle<> Continuation;
		TSharedPtr<bool> AliveFlag = MakeShared<bool>(true);

		explicit TWaitForUnicastDelegateAwaiter(DelegateType& InDelegate)
			: Delegate(InDelegate)
		{
		}

		~TWaitForUnicastDelegateAwaiter()
		{
			*AliveFlag = false;
		}

		bool await_ready() const
		{
			return false;
		}

		void await_suspend(std::coroutine_handle<> Handle)
		{
			Continuation = Handle;

			TWeakPtr<bool> WeakAlive = AliveFlag;
			Delegate.BindLambda([this, WeakAlive]() {
				if (!WeakAlive.IsValid())
				{
					return;
				}
				Delegate.Unbind();
				if (Continuation && !Continuation.done())
				{
					Continuation.resume();
				}
			});
		}

		void await_resume()
		{
		}
	};

	/**
	 * Wait for a unicast delegate to fire once.
	 *
	 * @param Delegate  Reference to the unicast delegate to bind.
	 * @return          An awaiter — co_await yields TTuple<decay_t<Args>...>, or void for no-args.
	 */
	template <typename... Args>
	[[nodiscard]] TWaitForUnicastDelegateAwaiter<Args...> WaitForDelegate(TDelegate<void(Args...)>& Delegate)
	{
		return TWaitForUnicastDelegateAwaiter<Args...>(Delegate);
	}

	// ============================================================================
	// Simple callback awaiter for one-shot lambdas and non-delegate patterns
	// ============================================================================

	/**
	 * Manual callback awaiter. Create one, co_await it, then call SetResult()
	 * (or SetReady() for void) from your callback to resume the coroutine.
	 *
	 * Unlike the delegate awaiters, this does not bind to any UE delegate
	 * automatically — you wire it up yourself. Useful for third-party APIs.
	 *
	 * @tparam T  The result type. Use void for no result.
	 */
	template <typename T = void>
	struct TCallbackAwaiter
	{
		TOptional<T> Result;
		std::coroutine_handle<> Continuation;

		bool await_ready() const
		{
			return Result.IsSet();
		}

		void await_suspend(std::coroutine_handle<> Handle)
		{
			Continuation = Handle;
		}

		T await_resume()
		{
			return MoveTemp(Result.GetValue());
		}

		/**
		 * Call from your callback to resume the coroutine with a value.
		 *
		 * @param Value  The result to deliver to the co_await expression.
		 */
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

		bool await_ready() const
		{
			return bReady;
		}

		void await_suspend(std::coroutine_handle<> Handle)
		{
			Continuation = Handle;
		}

		void await_resume()
		{
		}

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

	// ============================================================================
	// Chain — universal wrapper for callback-based functions
	// ============================================================================

	/**
	 * Wraps any function that accepts a completion callback as its last argument.
	 * The SetupFunc receives a TFunction<void(T)> that it must call when the
	 * async operation finishes. The coroutine suspends until that callback fires.
	 *
	 * @tparam T  The callback parameter type. Use void for no result.
	 *
	 * Usage:
	 *   auto Result = co_await AsyncFlow::Chain<int32>([](TFunction<void(int32)> Callback)
	 *   {
	 *       SomeAsyncAPI(MoveTemp(Callback));
	 *   });
	 */
	template <typename T>
	struct TChainAwaiter
	{
		TFunction<void(TFunction<void(T)>)> SetupFunc;
		TOptional<T> Result;
		std::coroutine_handle<> Continuation;

		bool await_ready() const
		{
			return false;
		}

		void await_suspend(std::coroutine_handle<> Handle)
		{
			Continuation = Handle;
			SetupFunc([this](T Value) {
				Result.Emplace(MoveTemp(Value));
				if (Continuation && !Continuation.done())
				{
					Continuation.resume();
				}
			});
		}

		T await_resume()
		{
			return MoveTemp(Result.GetValue());
		}
	};

	template <>
	struct TChainAwaiter<void>
	{
		TFunction<void(TFunction<void()>)> SetupFunc;
		std::coroutine_handle<> Continuation;

		bool await_ready() const
		{
			return false;
		}

		void await_suspend(std::coroutine_handle<> Handle)
		{
			Continuation = Handle;
			SetupFunc([this]() {
				if (Continuation && !Continuation.done())
				{
					Continuation.resume();
				}
			});
		}

		void await_resume()
		{
		}
	};

	/**
	 * Wrap a callback-based async function as a co_awaitable.
	 *
	 * @tparam T          The callback parameter type (void for no result).
	 * @param SetupFunc   A callable that receives TFunction<void(T)> and must invoke it on completion.
	 * @return            An awaiter — co_await yields T (or void).
	 */
	template <typename T = void>
	[[nodiscard]] TChainAwaiter<T> Chain(TFunction<void(TFunction<void(T)>)> SetupFunc)
	{
		return TChainAwaiter<T>{ MoveTemp(SetupFunc) };
	}

	[[nodiscard]] inline TChainAwaiter<void> Chain(TFunction<void(TFunction<void()>)> SetupFunc)
	{
		return TChainAwaiter<void>{ MoveTemp(SetupFunc) };
	}

	// ============================================================================
	// WaitForDynamicDelegate — dynamic multicast delegate awaiting
	// ============================================================================

	/**
	 * Awaiter for zero-arg dynamic multicast delegates.
	 *
	 * Works with any DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMyDelegate) type.
	 * Uses the DynamicMulticastDelegate concept defined in AsyncFlowTask.h.
	 * Creates a transient UAsyncFlowDelegateBridge UObject, binds its UFUNCTION
	 * to the delegate via FScriptDelegate, and resumes the coroutine when the
	 * delegate broadcasts. One-shot: unbinds after the first broadcast.
	 *
	 * For typed dynamic delegates (one or more parameters), use AsyncFlow::Chain()
	 * with manual delegate binding instead.
	 *
	 * @tparam DelegateType  Any type satisfying DynamicMulticastDelegate concept.
	 */
	template <DynamicMulticastDelegate DelegateType>
	struct TWaitForDynamicDelegateAwaiter
	{
		DelegateType& Delegate;
		TSharedPtr<bool> AliveFlag = MakeShared<bool>(true);
		std::coroutine_handle<> Continuation;
		TObjectPtr<UAsyncFlowDelegateBridge> Bridge;

		explicit TWaitForDynamicDelegateAwaiter(DelegateType& InDelegate)
			: Delegate(InDelegate)
		{
		}

		~TWaitForDynamicDelegateAwaiter()
		{
			*AliveFlag = false;
			Cleanup();
		}

		TWaitForDynamicDelegateAwaiter(TWaitForDynamicDelegateAwaiter&&) noexcept = default;
		TWaitForDynamicDelegateAwaiter& operator=(TWaitForDynamicDelegateAwaiter&&) = delete;
		TWaitForDynamicDelegateAwaiter(const TWaitForDynamicDelegateAwaiter&) = delete;
		TWaitForDynamicDelegateAwaiter& operator=(const TWaitForDynamicDelegateAwaiter&) = delete;

		bool await_ready() const
		{
			return false;
		}

		void await_suspend(std::coroutine_handle<> Handle)
		{
			Continuation = Handle;

			Bridge = NewObject<UAsyncFlowDelegateBridge>();
			Bridge->AddToRoot();
			Bridge->Continuation = Handle;
			Bridge->AliveFlag = AliveFlag;

			FScriptDelegate Binding;
			Binding.BindUFunction(Bridge.Get(), FName(TEXT("OnSimpleDelegateFired")));
			Delegate.Add(Binding);
		}

		void await_resume()
		{
			Cleanup();
		}

		void CancelAwaiter()
		{
			Cleanup();
			if (Continuation && !Continuation.done())
			{
				auto H = Continuation;
				Continuation = nullptr;
				H.resume();
			}
		}

	private:
		void Cleanup()
		{
			if (Bridge)
			{
				FScriptDelegate Binding;
				Binding.BindUFunction(Bridge.Get(), FName(TEXT("OnSimpleDelegateFired")));
				Delegate.Remove(Binding);

				Bridge->RemoveFromRoot();
				Bridge->Continuation = nullptr;
				Bridge = nullptr;
			}
		}
	};

	/**
	 * Wait for a dynamic multicast delegate to fire once (zero-arg).
	 *
	 * Works with any DECLARE_DYNAMIC_MULTICAST_DELEGATE type.
	 * For delegates with parameters, the parameters are ignored — only the
	 * "fired" event is captured. Use AsyncFlow::Chain() if you need args.
	 *
	 * @param Delegate  Reference to the dynamic multicast delegate.
	 * @return          An awaiter — co_await yields void.
	 *
	 * Example:
	 *   co_await AsyncFlow::WaitForDynamicDelegate(MyActor->OnSomeEvent);
	 */
	template <DynamicMulticastDelegate DelegateType>
	[[nodiscard]] TWaitForDynamicDelegateAwaiter<DelegateType> WaitForDynamicDelegate(DelegateType& Delegate)
	{
		return TWaitForDynamicDelegateAwaiter<DelegateType>(Delegate);
	}

	// ============================================================================
	// Typed Dynamic Delegate Awaiting — captures broadcast parameters
	// ============================================================================

	namespace Private
	{
		/**
		 * Compute the byte offset of each parameter in a flat struct following
		 * platform-standard C++ alignment rules. These match UHT-generated parameter
		 * structs for POD/pointer types (float, int32, bool, UObject*, FVector, etc.).
		 *
		 * For complex UE types (FString, TArray), the struct layout still matches
		 * because UHT uses the compiler's natural alignment. A raw Memcpy captures
		 * the bytes, and reinterpret_cast reads them back within the same call frame
		 * (the original Parms buffer is still alive on the Broadcast() caller's stack).
		 */
		template <typename... Ts>
		struct TDelegateParamLayout
		{
			static constexpr size_t Count = sizeof...(Ts);

			static void ComputeOffsets(size_t* OutOffsets)
			{
				size_t Off = 0;
				size_t I = 0;
				((Off = (Off + alignof(Ts) - 1) & ~(alignof(Ts) - 1),
					 OutOffsets[I++] = Off,
					 Off += sizeof(Ts)),
					...);
			}

			static constexpr size_t TotalSize()
			{
				size_t Size = 0;
				((Size = (Size + alignof(Ts) - 1) & ~(alignof(Ts) - 1),
					 Size += sizeof(Ts)),
					...);
				return Size;
			}
		};

		template <typename T>
		T ReadParamAtOffset(const uint8* Data, size_t Offset)
		{
			return *reinterpret_cast<const T*>(Data + Offset);
		}

		template <typename... Ts, size_t... Is>
		TTuple<Ts...> ExtractDelegateParamsImpl(const uint8* Data, const size_t* Offsets, std::index_sequence<Is...>)
		{
			return MakeTuple(ReadParamAtOffset<Ts>(Data, Offsets[Is])...);
		}

		template <typename... Ts>
		TTuple<Ts...> ExtractDelegateParams(const TArray<uint8>& Buffer)
		{
			size_t Offsets[sizeof...(Ts)];
			TDelegateParamLayout<Ts...>::ComputeOffsets(Offsets);
			return ExtractDelegateParamsImpl<Ts...>(Buffer.GetData(), Offsets, std::index_sequence_for<Ts...>{});
		}
	} // namespace Private

	/**
	 * Awaiter for typed dynamic multicast delegates (1+ parameters).
	 *
	 * Uses UAsyncFlowTypedDelegateBridge which overrides ProcessEvent to capture
	 * the raw parameter bytes from the delegate's broadcast frame. Parameters are
	 * extracted at compile-time-known offsets in await_resume().
	 *
	 * Supports CancelableAwaiter concept for expedited cancellation via CO_CONTRACT.
	 *
	 * @tparam DelegateType  Any type satisfying DynamicMulticastDelegate concept.
	 * @tparam ParamTypes    The delegate's parameter types, in declaration order.
	 */
	template <DynamicMulticastDelegate DelegateType, typename... ParamTypes>
	struct TWaitForTypedDynamicDelegateAwaiter
	{
		static_assert(sizeof...(ParamTypes) > 0,
			"Use TWaitForDynamicDelegateAwaiter for zero-arg delegates.");

		using ResultType = std::conditional_t<
			sizeof...(ParamTypes) == 1,
			std::tuple_element_t<0, std::tuple<std::decay_t<ParamTypes>...>>,
			TTuple<std::decay_t<ParamTypes>...>>;

		DelegateType& Delegate;
		TSharedPtr<bool> AliveFlag = MakeShared<bool>(true);
		std::coroutine_handle<> Continuation;
		TObjectPtr<UAsyncFlowTypedDelegateBridge> Bridge;

		explicit TWaitForTypedDynamicDelegateAwaiter(DelegateType& InDelegate)
			: Delegate(InDelegate)
		{
		}

		~TWaitForTypedDynamicDelegateAwaiter()
		{
			*AliveFlag = false;
			Cleanup();
		}

		TWaitForTypedDynamicDelegateAwaiter(TWaitForTypedDynamicDelegateAwaiter&&) noexcept = default;
		TWaitForTypedDynamicDelegateAwaiter& operator=(TWaitForTypedDynamicDelegateAwaiter&&) = delete;
		TWaitForTypedDynamicDelegateAwaiter(const TWaitForTypedDynamicDelegateAwaiter&) = delete;
		TWaitForTypedDynamicDelegateAwaiter& operator=(const TWaitForTypedDynamicDelegateAwaiter&) = delete;

		bool await_ready() const
		{
			return false;
		}

		void await_suspend(std::coroutine_handle<> Handle)
		{
			Continuation = Handle;

			Bridge = NewObject<UAsyncFlowTypedDelegateBridge>();
			Bridge->AddToRoot();
			Bridge->Continuation = Handle;
			Bridge->AliveFlag = AliveFlag;
			Bridge->ParamsSize = static_cast<int32>(
				Private::TDelegateParamLayout<std::decay_t<ParamTypes>...>::TotalSize());

			FScriptDelegate Binding;
			Binding.BindUFunction(Bridge.Get(), UAsyncFlowTypedDelegateBridge::DelegateFunctionName);
			Delegate.Add(Binding);
		}

		ResultType await_resume()
		{
			ResultType Result;
			if constexpr (sizeof...(ParamTypes) == 1)
			{
				using T = std::tuple_element_t<0, std::tuple<std::decay_t<ParamTypes>...>>;
				Result = Private::ReadParamAtOffset<T>(Bridge->CapturedParams.GetData(), 0);
			}
			else
			{
				Result = Private::ExtractDelegateParams<std::decay_t<ParamTypes>...>(
					Bridge->CapturedParams);
			}
			Cleanup();
			return Result;
		}

		void CancelAwaiter()
		{
			Cleanup();
			if (Continuation && !Continuation.done())
			{
				auto H = Continuation;
				Continuation = nullptr;
				H.resume();
			}
		}

	private:
		void Cleanup()
		{
			if (Bridge)
			{
				FScriptDelegate Binding;
				Binding.BindUFunction(Bridge.Get(),
					UAsyncFlowTypedDelegateBridge::DelegateFunctionName);
				Delegate.Remove(Binding);

				Bridge->RemoveFromRoot();
				Bridge->Continuation = nullptr;
				Bridge = nullptr;
			}
		}
	};

	/**
	 * Wait for a typed dynamic multicast delegate to fire once, capturing its parameters.
	 *
	 * Uses ProcessEvent interception to generically capture parameters from any
	 * dynamic delegate signature — no per-type bridge classes needed.
	 *
	 * @tparam ParamTypes  The delegate's parameter types, in order.
	 *                     Must match the DECLARE_DYNAMIC_MULTICAST_DELEGATE_*Param declaration.
	 * @param Delegate     Reference to the dynamic multicast delegate.
	 * @return             An awaiter — co_await yields:
	 *                     - T directly for single-param delegates
	 *                     - TTuple<T1, T2, ...> for multi-param delegates
	 *
	 * Example (single param):
	 *   // Given: DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDamage, float, Damage);
	 *   float Damage = co_await AsyncFlow::WaitForDynamicDelegate<float>(MyActor->OnDamage);
	 *
	 * Example (multi param):
	 *   // Given: DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnHit, AActor*, Actor, float, Damage);
	 *   auto [Actor, Damage] = co_await AsyncFlow::WaitForDynamicDelegate<AActor*, float>(MyActor->OnHit);
	 */
	template <typename... ParamTypes, DynamicMulticastDelegate DelegateType>
		requires(sizeof...(ParamTypes) > 0)
	[[nodiscard]] auto WaitForDynamicDelegate(DelegateType& Delegate)
	{
		return TWaitForTypedDynamicDelegateAwaiter<DelegateType, ParamTypes...>(Delegate);
	}

} // namespace AsyncFlow
