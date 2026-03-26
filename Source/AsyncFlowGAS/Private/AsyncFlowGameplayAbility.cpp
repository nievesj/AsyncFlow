// AsyncFlowGameplayAbility.cpp
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

	const EAbilitySuccessType Result = ActiveTask.GetResult();
	const bool bWasCancelled = (Result == EAbilitySuccessType::Canceled) || ActiveTask.IsCancelled();

	UE_LOG(LogAsyncFlow, Verbose, TEXT("AsyncFlowAbility [%s] completed with result: %d"),
		*GetName(), static_cast<int32>(Result));

	if (IsActive())
	{
		EndAbility(CachedHandle, &CachedActorInfo, CachedActivationInfo, true, bWasCancelled);
	}
}


