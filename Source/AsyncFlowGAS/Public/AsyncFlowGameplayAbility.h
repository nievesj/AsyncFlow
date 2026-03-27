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
#pragma once

#include "Abilities/GameplayAbility.h"
#include "AsyncFlowAbilityTypes.h"
#include "AsyncFlowTask.h"

#include "AsyncFlowGameplayAbility.generated.h"

/**
 * UAsyncFlowGameplayAbility
 * Base class for abilities that use C++20 coroutines for their execution flow.
 *
 * Subclasses override ExecuteAbility() which returns TTask<EAbilitySuccessType>.
 * The base handles launching the coroutine, mapping the result to EndAbility,
 * and propagating GAS cancellation to the coroutine's cancellation token.
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
	 * Override this in subclasses. This is your ability's main logic as a coroutine.
	 * Use co_await on AsyncFlow awaiters for async operations.
	 * Return EAbilitySuccessType to indicate outcome.
	 *
	 * Base implementation logs an error and returns Failed. Subclasses must override.
	 */
	virtual AsyncFlow::TTask<EAbilitySuccessType> ExecuteAbility(FAbilityParams Params);

private:
	/** The running coroutine task. */
	AsyncFlow::TTask<EAbilitySuccessType> ActiveTask;

	/** Drives the coroutine forward after the initial Start(). */
	void OnCoroutineCompleted();

	/** Cached params for EndAbility calls. */
	FGameplayAbilitySpecHandle CachedHandle;
	FGameplayAbilityActorInfo CachedActorInfo;
	FGameplayAbilityActivationInfo CachedActivationInfo;
};

