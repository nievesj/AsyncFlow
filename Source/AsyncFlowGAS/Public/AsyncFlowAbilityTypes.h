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

