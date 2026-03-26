// AsyncFlowAbilityTypes.h — GAS-specific types for AsyncFlow
#pragma once

#include "Abilities/GameplayAbilityTypes.h"
#include "GameFramework/Character.h"

#include "AsyncFlowAbilityTypes.generated.h"

/** Outcome of an ability executed through AsyncFlow. */
UENUM(BlueprintType)
enum class EAbilitySuccessType : uint8
{
	Finished,
	Failed,
	Canceled
};

/**
 * FAbilityParams
 * Wraps GAS activation parameters for convenient access within coroutine-based abilities.
 */
USTRUCT(BlueprintType)
struct ASYNCFLOWGAS_API FAbilityParams
{
	GENERATED_BODY()

	UPROPERTY()
	FGameplayAbilitySpecHandle Handle;

	UPROPERTY()
	FGameplayAbilityActorInfo ActorInfo;

	UPROPERTY()
	FGameplayAbilityActivationInfo ActivationInfo;

	UPROPERTY()
	FGameplayEventData TriggerEventData;

	/** Whether TriggerEventData was populated from an activation event. */
	bool bHasTriggerEventData = false;

	/** Check if this params struct has valid actor info. */
	bool HasValidActorInfo() const
	{
		return ActorInfo.OwnerActor.IsValid();
	}

	/** Get the avatar actor as ACharacter, or nullptr. */
	ACharacter* GetAvatarCharacter() const
	{
		return Cast<ACharacter>(ActorInfo.AvatarActor.Get());
	}

	/** Get the owning actor. */
	AActor* GetOwningActor() const
	{
		return ActorInfo.OwnerActor.Get();
	}

	/** Get the AbilitySystemComponent from actor info. */
	UAbilitySystemComponent* GetAbilitySystemComponent() const
	{
		return ActorInfo.AbilitySystemComponent.Get();
	}
};

