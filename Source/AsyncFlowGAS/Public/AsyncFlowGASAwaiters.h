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
	TWeakObjectPtr<UAbilitySystemComponent> ASC;
	FGameplayTag EventTag;
	FGameplayEventData ResultData;
	std::coroutine_handle<> Continuation;
	FDelegateHandle DelegateHandle;
	TSharedPtr<bool> AliveFlag = MakeShared<bool>(true);

	~FWaitGameplayEventAwaiter()
	{
		*AliveFlag = false;
		if (ASC.IsValid() && DelegateHandle.IsValid())
		{
			ASC->GenericGameplayEventCallbacks.FindOrAdd(EventTag).Remove(DelegateHandle);
		}
	}

	bool await_ready() const { return false; }

	void await_suspend(std::coroutine_handle<> Handle)
	{
		Continuation = Handle;

		if (!ASC.IsValid())
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
				if (ASC.IsValid())
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
[[nodiscard]] inline FWaitGameplayEventAwaiter WaitGameplayEvent(UAbilitySystemComponent* InASC, FGameplayTag EventTag)
{
	return FWaitGameplayEventAwaiter{InASC, EventTag};
}

// ============================================================================
// WaitGameplayTagAdded — waits until a gameplay tag is added to the ASC
// ============================================================================

struct FWaitGameplayTagAddedAwaiter
{
	TWeakObjectPtr<UAbilitySystemComponent> ASC;
	FGameplayTag Tag;
	std::coroutine_handle<> Continuation;
	FDelegateHandle DelegateHandle;
	TSharedPtr<bool> AliveFlag = MakeShared<bool>(true);

	~FWaitGameplayTagAddedAwaiter()
	{
		*AliveFlag = false;
		if (ASC.IsValid() && DelegateHandle.IsValid())
		{
			ASC->RegisterGameplayTagEvent(Tag, EGameplayTagEventType::NewOrRemoved).Remove(DelegateHandle);
		}
	}

	bool await_ready() const
	{
		return ASC.IsValid() && ASC->HasMatchingGameplayTag(Tag);
	}

	void await_suspend(std::coroutine_handle<> Handle)
	{
		Continuation = Handle;

		if (!ASC.IsValid())
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
					if (ASC.IsValid())
					{
						ASC->RegisterGameplayTagEvent(Tag, EGameplayTagEventType::NewOrRemoved).Remove(DelegateHandle);
					}
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
[[nodiscard]] inline FWaitGameplayTagAddedAwaiter WaitGameplayTagAdded(UAbilitySystemComponent* InASC, FGameplayTag Tag)
{
	return FWaitGameplayTagAddedAwaiter{InASC, Tag};
}

// ============================================================================
// WaitGameplayTagRemoved — waits until a gameplay tag is removed from the ASC
// ============================================================================

struct FWaitGameplayTagRemovedAwaiter
{
	TWeakObjectPtr<UAbilitySystemComponent> ASC;
	FGameplayTag Tag;
	std::coroutine_handle<> Continuation;
	FDelegateHandle DelegateHandle;
	TSharedPtr<bool> AliveFlag = MakeShared<bool>(true);

	~FWaitGameplayTagRemovedAwaiter()
	{
		*AliveFlag = false;
		if (ASC.IsValid() && DelegateHandle.IsValid())
		{
			ASC->RegisterGameplayTagEvent(Tag, EGameplayTagEventType::NewOrRemoved).Remove(DelegateHandle);
		}
	}

	bool await_ready() const
	{
		return ASC.IsValid() && !ASC->HasMatchingGameplayTag(Tag);
	}

	void await_suspend(std::coroutine_handle<> Handle)
	{
		Continuation = Handle;

		if (!ASC.IsValid())
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
					if (ASC.IsValid())
					{
						ASC->RegisterGameplayTagEvent(Tag, EGameplayTagEventType::NewOrRemoved).Remove(DelegateHandle);
					}
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
[[nodiscard]] inline FWaitGameplayTagRemovedAwaiter WaitGameplayTagRemoved(UAbilitySystemComponent* InASC, FGameplayTag Tag)
{
	return FWaitGameplayTagRemovedAwaiter{InASC, Tag};
}

// ============================================================================
// WaitAttributeChange — waits for an attribute value to change
// ============================================================================

struct FWaitAttributeChangeAwaiter
{
	TWeakObjectPtr<UAbilitySystemComponent> ASC;
	FGameplayAttribute Attribute;
	float NewValue = 0.0f;
	std::coroutine_handle<> Continuation;
	FDelegateHandle DelegateHandle;
	TSharedPtr<bool> AliveFlag = MakeShared<bool>(true);

	~FWaitAttributeChangeAwaiter()
	{
		*AliveFlag = false;
		if (ASC.IsValid() && DelegateHandle.IsValid())
		{
			ASC->GetGameplayAttributeValueChangeDelegate(Attribute).Remove(DelegateHandle);
		}
	}

	bool await_ready() const { return false; }

	void await_suspend(std::coroutine_handle<> Handle)
	{
		Continuation = Handle;

		if (!ASC.IsValid())
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
				if (ASC.IsValid())
				{
					ASC->GetGameplayAttributeValueChangeDelegate(Attribute).Remove(DelegateHandle);
				}
				if (Continuation && !Continuation.done())
				{
					Continuation.resume();
				}
			});
	}

	float await_resume() const { return NewValue; }
};

/** Wait for an attribute to change. Returns the new value. */
[[nodiscard]] inline FWaitAttributeChangeAwaiter WaitAttributeChange(UAbilitySystemComponent* InASC, FGameplayAttribute Attribute)
{
	return FWaitAttributeChangeAwaiter{InASC, Attribute};
}

// ============================================================================
// WaitGameplayEffectRemoved — waits for an active gameplay effect to be removed
// ============================================================================

struct FWaitGameplayEffectRemovedAwaiter
{
	TWeakObjectPtr<UAbilitySystemComponent> ASC;
	FActiveGameplayEffectHandle EffectHandle;
	std::coroutine_handle<> Continuation;
	FDelegateHandle DelegateHandle;
	TSharedPtr<bool> AliveFlag = MakeShared<bool>(true);

	~FWaitGameplayEffectRemovedAwaiter()
	{
		*AliveFlag = false;
		if (ASC.IsValid() && DelegateHandle.IsValid())
		{
			FOnActiveGameplayEffectRemoved_Info* Delegate = ASC->OnGameplayEffectRemoved_InfoDelegate(EffectHandle);
			if (Delegate)
			{
				Delegate->Remove(DelegateHandle);
			}
		}
	}

	bool await_ready() const { return false; }

	void await_suspend(std::coroutine_handle<> Handle)
	{
		Continuation = Handle;

		if (!ASC.IsValid())
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
[[nodiscard]] inline FWaitGameplayEffectRemovedAwaiter WaitGameplayEffectRemoved(UAbilitySystemComponent* InASC, FActiveGameplayEffectHandle EffectHandle)
{
	return FWaitGameplayEffectRemovedAwaiter{InASC, EffectHandle};
}

} // namespace AsyncFlow

