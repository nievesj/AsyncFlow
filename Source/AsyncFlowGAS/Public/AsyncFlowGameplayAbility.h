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

// AsyncFlowGameplayAbility.h — Base coroutine-driven gameplay ability
//
// Subclass this instead of UGameplayAbility to write ability logic as a
// single coroutine. Override ExecuteAbility() and use co_await on any
// AsyncFlow awaiter. The base class maps the coroutine's EAbilitySuccessType
// return value to EndAbility, and propagates GAS CancelAbility into the
// coroutine's cancellation token.
#pragma once

#include "Abilities/GameplayAbility.h"
#include "AsyncFlowAbilityTypes.h"
#include "AsyncFlowTask.h"

#include "AsyncFlowGameplayAbility.generated.h"

/**
 * UAsyncFlowGameplayAbility
 *
 * Abstract base for abilities whose execution flow is a C++20 coroutine.
 * Override ExecuteAbility() to define the ability's behavior as straight-line
 * async code.
 *
 * Lifecycle mapping:
 * - GAS ActivateAbility → packs FAbilityParams, creates the coroutine, calls Start().
 * - GAS CancelAbility → calls TTask::Cancel(). The coroutine stops at the next co_await.
 * - Coroutine co_return → OnCoroutineCompleted() calls EndAbility with the appropriate bWasCancelled flag.
 *
 * Defaults: LocalPredicted, InstancedPerActor (suitable for single-player).
 * Override the constructor for different policies.
 */
UCLASS(Abstract)
class ASYNCFLOWGAS_API UAsyncFlowGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UAsyncFlowGameplayAbility();

protected:
	// UGameplayAbility interface
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void CancelAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateCancelAbility) override;

	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

	/**
	 * Override in subclasses. This is the ability's main logic as a coroutine.
	 *
	 * @param Params  GAS activation data (handle, actor info, trigger event).
	 * @return        EAbilitySuccessType indicating the outcome.
	 *
	 * @note  Base implementation logs an error and co_returns Failed.
	 */
	virtual AsyncFlow::TTask<EAbilitySuccessType> ExecuteAbility(FAbilityParams Params);

private:
	/** The running coroutine task. Destroyed when the ability ends. */
	AsyncFlow::TTask<EAbilitySuccessType> ActiveTask;

	/** Reads the coroutine result and calls EndAbility accordingly. */
	void OnCoroutineCompleted();

	/** Cached from ActivateAbility for use in EndAbility calls. */
	FGameplayAbilitySpecHandle CachedHandle;
	FGameplayAbilityActorInfo CachedActorInfo;
	FGameplayAbilityActivationInfo CachedActivationInfo;
};
