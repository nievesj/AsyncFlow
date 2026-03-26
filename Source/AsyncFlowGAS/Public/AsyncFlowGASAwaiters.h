// AsyncFlowGASAwaiters.h — GAS-specific awaiters wrapping ASC delegates
#pragma once

#include "AsyncFlowTask.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayTagContainer.h"

#include <coroutine>

namespace AsyncFlow
{

// ============================================================================
// WaitGameplayEvent — waits for a gameplay event with a matching tag
// ============================================================================

struct FWaitGameplayEventAwaiter
{
	UAbilitySystemComponent* ASC = nullptr;
	FGameplayTag EventTag;
	FGameplayEventData ResultData;
	std::coroutine_handle<> Continuation;
	FDelegateHandle DelegateHandle;
	TSharedPtr<bool> AliveFlag = MakeShared<bool>(true);

	~FWaitGameplayEventAwaiter() { *AliveFlag = false; }

	bool await_ready() const { return false; }

	void await_suspend(std::coroutine_handle<> Handle)
	{
		Continuation = Handle;

		if (!ASC)
		{
			Handle.resume();
			return;
		}

		TWeakPtr<bool> WeakAlive = AliveFlag;
		DelegateHandle = ASC->GenericGameplayEventCallbacks.FindOrAdd(EventTag).AddLambda(
			[this, WeakAlive](const FGameplayEventData* Payload)
			{
				if (!WeakAlive.IsValid()) { return; }
				if (Payload)
				{
					ResultData = *Payload;
				}
				if (ASC)
				{
					ASC->GenericGameplayEventCallbacks.FindOrAdd(EventTag).Remove(DelegateHandle);
				}
				if (Continuation && !Continuation.done())
				{
					Continuation.resume();
				}
			}
		);
	}

	FGameplayEventData await_resume() const { return ResultData; }
};

/** Wait for a gameplay event with the specified tag. Returns the event data. */
[[nodiscard]] inline FWaitGameplayEventAwaiter WaitGameplayEvent(UAbilitySystemComponent* ASC, FGameplayTag EventTag)
{
	return FWaitGameplayEventAwaiter{ASC, EventTag};
}

// ============================================================================
// WaitGameplayTagAdded — waits until a gameplay tag is added to the ASC
// ============================================================================

struct FWaitGameplayTagAddedAwaiter
{
	UAbilitySystemComponent* ASC = nullptr;
	FGameplayTag Tag;
	std::coroutine_handle<> Continuation;
	FDelegateHandle DelegateHandle;
	TSharedPtr<bool> AliveFlag = MakeShared<bool>(true);

	~FWaitGameplayTagAddedAwaiter() { *AliveFlag = false; }

	bool await_ready() const
	{
		return ASC && ASC->HasMatchingGameplayTag(Tag);
	}

	void await_suspend(std::coroutine_handle<> Handle)
	{
		Continuation = Handle;

		if (!ASC)
		{
			Handle.resume();
			return;
		}

		if (ASC->HasMatchingGameplayTag(Tag))
		{
			Handle.resume();
			return;
		}

		TWeakPtr<bool> WeakAlive = AliveFlag;
		DelegateHandle = ASC->RegisterGameplayTagEvent(Tag, EGameplayTagEventType::NewOrRemoved)
			.AddLambda([this, WeakAlive](const FGameplayTag InTag, int32 NewCount)
			{
				if (!WeakAlive.IsValid()) { return; }
				if (NewCount > 0)
				{
					ASC->RegisterGameplayTagEvent(Tag, EGameplayTagEventType::NewOrRemoved).Remove(DelegateHandle);
					if (Continuation && !Continuation.done())
					{
						Continuation.resume();
					}
				}
			});
	}

	void await_resume() const {}
};

/** Wait until a gameplay tag is added to the ASC. */
[[nodiscard]] inline FWaitGameplayTagAddedAwaiter WaitGameplayTagAdded(UAbilitySystemComponent* ASC, FGameplayTag Tag)
{
	return FWaitGameplayTagAddedAwaiter{ASC, Tag};
}

// ============================================================================
// WaitGameplayTagRemoved — waits until a gameplay tag is removed from the ASC
// ============================================================================

struct FWaitGameplayTagRemovedAwaiter
{
	UAbilitySystemComponent* ASC = nullptr;
	FGameplayTag Tag;
	std::coroutine_handle<> Continuation;
	FDelegateHandle DelegateHandle;
	TSharedPtr<bool> AliveFlag = MakeShared<bool>(true);

	~FWaitGameplayTagRemovedAwaiter() { *AliveFlag = false; }

	bool await_ready() const
	{
		return ASC && !ASC->HasMatchingGameplayTag(Tag);
	}

	void await_suspend(std::coroutine_handle<> Handle)
	{
		Continuation = Handle;

		if (!ASC)
		{
			Handle.resume();
			return;
		}

		if (!ASC->HasMatchingGameplayTag(Tag))
		{
			Handle.resume();
			return;
		}

		TWeakPtr<bool> WeakAlive = AliveFlag;
		DelegateHandle = ASC->RegisterGameplayTagEvent(Tag, EGameplayTagEventType::NewOrRemoved)
			.AddLambda([this, WeakAlive](const FGameplayTag InTag, int32 NewCount)
			{
				if (!WeakAlive.IsValid()) { return; }
				if (NewCount == 0)
				{
					ASC->RegisterGameplayTagEvent(Tag, EGameplayTagEventType::NewOrRemoved).Remove(DelegateHandle);
					if (Continuation && !Continuation.done())
					{
						Continuation.resume();
					}
				}
			});
	}

	void await_resume() const {}
};

/** Wait until a gameplay tag is removed from the ASC. */
[[nodiscard]] inline FWaitGameplayTagRemovedAwaiter WaitGameplayTagRemoved(UAbilitySystemComponent* ASC, FGameplayTag Tag)
{
	return FWaitGameplayTagRemovedAwaiter{ASC, Tag};
}

// ============================================================================
// WaitAttributeChange — waits for an attribute value to change
// ============================================================================

struct FWaitAttributeChangeAwaiter
{
	UAbilitySystemComponent* ASC = nullptr;
	FGameplayAttribute Attribute;
	float NewValue = 0.0f;
	std::coroutine_handle<> Continuation;
	FDelegateHandle DelegateHandle;
	TSharedPtr<bool> AliveFlag = MakeShared<bool>(true);

	~FWaitAttributeChangeAwaiter() { *AliveFlag = false; }

	bool await_ready() const { return false; }

	void await_suspend(std::coroutine_handle<> Handle)
	{
		Continuation = Handle;

		if (!ASC)
		{
			Handle.resume();
			return;
		}

		TWeakPtr<bool> WeakAlive = AliveFlag;
		DelegateHandle = ASC->GetGameplayAttributeValueChangeDelegate(Attribute)
			.AddLambda([this, WeakAlive](const FOnAttributeChangeData& Data)
			{
				if (!WeakAlive.IsValid()) { return; }
				NewValue = Data.NewValue;
				ASC->GetGameplayAttributeValueChangeDelegate(Attribute).Remove(DelegateHandle);
				if (Continuation && !Continuation.done())
				{
					Continuation.resume();
				}
			});
	}

	float await_resume() const { return NewValue; }
};

/** Wait for an attribute to change. Returns the new value. */
[[nodiscard]] inline FWaitAttributeChangeAwaiter WaitAttributeChange(UAbilitySystemComponent* ASC, FGameplayAttribute Attribute)
{
	return FWaitAttributeChangeAwaiter{ASC, Attribute};
}

// ============================================================================
// WaitGameplayEffectRemoved — waits for an active gameplay effect to be removed
// ============================================================================

struct FWaitGameplayEffectRemovedAwaiter
{
	UAbilitySystemComponent* ASC = nullptr;
	FActiveGameplayEffectHandle EffectHandle;
	std::coroutine_handle<> Continuation;
	FDelegateHandle DelegateHandle;
	TSharedPtr<bool> AliveFlag = MakeShared<bool>(true);

	~FWaitGameplayEffectRemovedAwaiter() { *AliveFlag = false; }

	bool await_ready() const { return false; }

	void await_suspend(std::coroutine_handle<> Handle)
	{
		Continuation = Handle;

		if (!ASC)
		{
			Handle.resume();
			return;
		}

		FOnActiveGameplayEffectRemoved_Info* Delegate = ASC->OnGameplayEffectRemoved_InfoDelegate(EffectHandle);
		if (Delegate)
		{
			TWeakPtr<bool> WeakAlive = AliveFlag;
			DelegateHandle = Delegate->AddLambda([this, WeakAlive](const FGameplayEffectRemovalInfo& RemovalInfo)
			{
				if (!WeakAlive.IsValid()) { return; }
				if (Continuation && !Continuation.done())
				{
					Continuation.resume();
				}
			});
		}
		else
		{
			// Effect already removed
			Handle.resume();
		}
	}

	void await_resume() const {}
};

/** Wait for an active gameplay effect to be removed. */
[[nodiscard]] inline FWaitGameplayEffectRemovedAwaiter WaitGameplayEffectRemoved(UAbilitySystemComponent* ASC, FActiveGameplayEffectHandle EffectHandle)
{
	return FWaitGameplayEffectRemovedAwaiter{ASC, EffectHandle};
}

} // namespace AsyncFlow

