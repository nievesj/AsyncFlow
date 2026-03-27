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
//
// Shared types between UAsyncFlowGameplayAbility and user ability subclasses.
// EAbilitySuccessType maps coroutine outcomes to GAS EndAbility semantics.
// FAbilityParams bundles all GAS activation data into a single struct for
// convenient access from the coroutine body.
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
 *
 * Bundles GAS activation parameters into a single value type for the
 * coroutine body. Passed by value into ExecuteAbility() — the coroutine
 * frame owns its own copy.
 */
USTRUCT(BlueprintType)
struct ASYNCFLOWGAS_API FAbilityParams
{
	GENERATED_BODY()

	/** The ability spec handle that triggered activation. */
	UPROPERTY()
	FGameplayAbilitySpecHandle Handle;

	/** Actor info at the time of activation (owner, avatar, ASC). */
	UPROPERTY()
	FGameplayAbilityActorInfo ActorInfo;

	/** Activation info (prediction key, activation mode). */
	UPROPERTY()
	FGameplayAbilityActivationInfo ActivationInfo;

	/** Event data if the ability was activated by a gameplay event. */
	UPROPERTY()
	FGameplayEventData TriggerEventData;

	/** True if TriggerEventData was populated from an activation event. */
	bool bHasTriggerEventData = false;

	/** @return true if the ActorInfo has a valid OwnerActor. */
	bool HasValidActorInfo() const
	{
		return ActorInfo.OwnerActor.IsValid();
	}

	/** @return the avatar actor cast to ACharacter, or nullptr if not a character. */
	ACharacter* GetAvatarCharacter() const
	{
		return Cast<ACharacter>(ActorInfo.AvatarActor.Get());
	}

	/** @return the owning actor from ActorInfo. */
	AActor* GetOwningActor() const
	{
		return ActorInfo.OwnerActor.Get();
	}

	/** @return the AbilitySystemComponent from ActorInfo. */
	UAbilitySystemComponent* GetAbilitySystemComponent() const
	{
		return ActorInfo.AbilitySystemComponent.Get();
	}
};
