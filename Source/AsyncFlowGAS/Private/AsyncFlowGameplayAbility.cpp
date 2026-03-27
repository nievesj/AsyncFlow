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

// AsyncFlowGameplayAbility.cpp — UAsyncFlowGameplayAbility implementation.
//
// Activation packs GAS parameters into FAbilityParams, launches ExecuteAbility()
// as a coroutine, and maps the TTask result to EndAbility on completion.
// CancelAbility() propagates the GAS cancel signal into the coroutine's flow
// state; the coroutine stops at the next co_await boundary.
#include "AsyncFlowGameplayAbility.h"
#include "AsyncFlowAbilityTypes.h"
#include "AsyncFlowLogging.h"
#include "AbilitySystemComponent.h"

UAsyncFlowGameplayAbility::UAsyncFlowGameplayAbility()
{
	// Single-player default: local predicted, no replication needed
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

void UAsyncFlowGameplayAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// Cache for EndAbility
	CachedHandle = Handle;
	if (ActorInfo)
	{
		CachedActorInfo = *ActorInfo;
	}
	CachedActivationInfo = ActivationInfo;

	// Pack params
	FAbilityParams Params;
	Params.Handle = Handle;
	if (ActorInfo)
	{
		Params.ActorInfo = *ActorInfo;
	}
	Params.ActivationInfo = ActivationInfo;
	if (TriggerEventData)
	{
		Params.TriggerEventData = *TriggerEventData;
		Params.bHasTriggerEventData = true;
	}

	// Launch the coroutine
	ActiveTask = ExecuteAbility(MoveTemp(Params));

	// Register completion callback
	ActiveTask.OnComplete([this]()
	{
		OnCoroutineCompleted();
	});

	// Start execution — will run until the first co_await
	ActiveTask.Start();
}

void UAsyncFlowGameplayAbility::CancelAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateCancelAbility)
{
	// Signal the coroutine to cancel at next suspension point
	if (ActiveTask.IsValid() && !ActiveTask.IsCompleted())
	{
		ActiveTask.Cancel();
	}

	Super::CancelAbility(Handle, ActorInfo, ActivationInfo, bReplicateCancelAbility);
}

void UAsyncFlowGameplayAbility::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	// Cancel coroutine if still running
	if (ActiveTask.IsValid() && !ActiveTask.IsCompleted())
	{
		ActiveTask.Cancel();
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

AsyncFlow::TTask<EAbilitySuccessType> UAsyncFlowGameplayAbility::ExecuteAbility(FAbilityParams Params)
{
	UE_LOG(LogAsyncFlow, Error, TEXT("ExecuteAbility not overridden in %s — subclasses must implement this."), *GetClass()->GetName());
	co_return EAbilitySuccessType::Failed;
}

void UAsyncFlowGameplayAbility::OnCoroutineCompleted()
{
	if (!ActiveTask.IsValid())
	{
		return;
	}

	// Self-cancellation (FSelfCancellation) sets bCancelled but does not set Result.
	// Guard against accessing an empty TOptional.
	if (ActiveTask.IsCancelled())
	{
		UE_LOG(LogAsyncFlow, Verbose, TEXT("AsyncFlowAbility [%s] was cancelled"), *GetName());
		if (IsActive())
		{
			EndAbility(CachedHandle, &CachedActorInfo, CachedActivationInfo, true, true);
		}
		return;
	}

	const EAbilitySuccessType Result = ActiveTask.GetResult();
	const bool bWasCancelled = (Result == EAbilitySuccessType::Canceled);

	UE_LOG(LogAsyncFlow, Verbose, TEXT("AsyncFlowAbility [%s] completed with result: %d"),
		*GetName(), static_cast<int32>(Result));

	if (IsActive())
	{
		EndAbility(CachedHandle, &CachedActorInfo, CachedActivationInfo, true, bWasCancelled);
	}
}


