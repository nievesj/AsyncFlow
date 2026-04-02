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

// AsyncFlowMacros.h — co_verifyf and CO_CONTRACT macros
#pragma once

#include "AsyncFlowTask.h"
#include "AsyncFlowLogging.h"

/**
 * co_verifyf(Expr, Fmt, ...)
 * For TTask<void> coroutines. If Expr is false, logs and co_returns immediately.
 *
 * Example:
 *   co_verifyf(Ptr != nullptr, TEXT("Ptr was null in %s"), *GetName());
 */
#define co_verifyf(Expr, Fmt, ...)                                                                           \
	do                                                                                                       \
	{                                                                                                        \
		if (UNLIKELY(!(Expr)))                                                                               \
		{                                                                                                    \
			UE_LOG(LogAsyncFlow, Warning, TEXT("co_verifyf FAILED: %s | " Fmt), TEXT(#Expr), ##__VA_ARGS__); \
			co_return;                                                                                       \
		}                                                                                                    \
	}                                                                                                        \
	while (false)

/**
 * co_verifyf_r(Expr, Fmt, ...)
 * For TTask<T> coroutines where T != void. If Expr is false, logs and co_returns T{}.
 * T must be default-constructible.
 *
 * Example:
 *   co_verifyf_r(Count > 0, TEXT("Count was zero")); // returns false, 0, nullptr, etc.
 */
#define co_verifyf_r(Expr, Fmt, ...)                                                                           \
	do                                                                                                         \
	{                                                                                                          \
		if (UNLIKELY(!(Expr)))                                                                                 \
		{                                                                                                      \
			UE_LOG(LogAsyncFlow, Warning, TEXT("co_verifyf_r FAILED: %s | " Fmt), TEXT(#Expr), ##__VA_ARGS__); \
			co_return {};                                                                                      \
		}                                                                                                      \
	}                                                                                                          \
	while (false)

/**
 * CO_CONTRACT(ConditionLambda)
 * Registers a contract that is checked before every co_await.
 * If the condition returns false, the coroutine is cancelled at the next suspension point.
 *
 * The lambda must return bool and capture what it needs (prefer TWeakObjectPtr for UObjects).
 *
 * Example:
 *   TWeakObjectPtr<AActor> WeakOwner = Owner;
 *   CO_CONTRACT([WeakOwner]() { return WeakOwner.IsValid(); });
 */
#define CO_CONTRACT(ConditionLambda)                                                                \
	do                                                                                              \
	{                                                                                               \
		AsyncFlow::FAsyncFlowState* FlowState_Contract = AsyncFlow::Private::GetCurrentFlowState(); \
		if (FlowState_Contract)                                                                     \
		{                                                                                           \
			FlowState_Contract->ContractChecks.Add(ConditionLambda);                                \
		}                                                                                           \
	}                                                                                               \
	while (false)
