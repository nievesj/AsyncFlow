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
#define co_verifyf(Expr, Fmt, ...) \
	do \
	{ \
		if (UNLIKELY(!(Expr))) \
		{ \
			UE_LOG(LogAsyncFlow, Warning, TEXT("co_verifyf FAILED: %s | " Fmt), TEXT(#Expr), ##__VA_ARGS__); \
			co_return; \
		} \
	} while (false)

/**
 * co_verifyf_r(Expr, Fmt, ...)
 * For TTask<T> coroutines where T != void. If Expr is false, logs and co_returns T{}.
 * T must be default-constructible.
 *
 * Example:
 *   co_verifyf_r(Count > 0, TEXT("Count was zero")); // returns false, 0, nullptr, etc.
 */
#define co_verifyf_r(Expr, Fmt, ...) \
	do \
	{ \
		if (UNLIKELY(!(Expr))) \
		{ \
			UE_LOG(LogAsyncFlow, Warning, TEXT("co_verifyf_r FAILED: %s | " Fmt), TEXT(#Expr), ##__VA_ARGS__); \
			co_return {}; \
		} \
	} while (false)

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
#define CO_CONTRACT(ConditionLambda) \
	do \
	{ \
		AsyncFlow::FAsyncFlowState* FlowState_Contract = AsyncFlow::Private::GetCurrentFlowState(); \
		if (FlowState_Contract) \
		{ \
			FlowState_Contract->ContractChecks.Add(ConditionLambda); \
		} \
	} while (false)

