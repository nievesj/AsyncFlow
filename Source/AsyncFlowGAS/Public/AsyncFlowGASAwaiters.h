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

// AsyncFlowGASAwaiters.h — GAS-specific awaiters wrapping ASC delegates
//
// Each awaiter binds to an AbilitySystemComponent delegate, captures the
// first matching event, unbinds, and resumes the coroutine. All awaiters
// clean up their delegate bindings on destruction (e.g., if the coroutine
// is cancelled mid-wait).
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

/**
 * Awaiter that listens for a gameplay event with a specific tag on the ASC.
 * Binds to GenericGameplayEventCallbacks, captures FGameplayEventData,
 * unbinds after the first matching event, and resumes the coroutine.
 *
 * Destructor cleans up the delegate binding if the awaiter is destroyed
 * before the event fires (e.g., coroutine cancellation).
 */
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

/**
 * Wait for a gameplay event with the specified tag.
 *
 * @param InASC     The AbilitySystemComponent to listen on.
 * @param EventTag  The gameplay tag to match against.
 * @return          An awaiter — co_await yields FGameplayEventData.
 */
[[nodiscard]] inline FWaitGameplayEventAwaiter WaitGameplayEvent(UAbilitySystemComponent* InASC, FGameplayTag EventTag)
{
	return FWaitGameplayEventAwaiter{InASC, EventTag};
}

// ============================================================================
// WaitGameplayTagAdded — waits until a gameplay tag is added to the ASC
// ============================================================================

/**
 * Awaiter that waits until a specific gameplay tag is added to the ASC.
 * If the tag is already present at the point of co_await, resumes immediately.
 * Binds to RegisterGameplayTagEvent with NewOrRemoved type.
 */
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

/**
 * Wait until a gameplay tag is present on the ASC.
 *
 * @param InASC  The AbilitySystemComponent to monitor.
 * @param Tag    The tag to wait for.
 * @return       An awaiter — use with co_await. Returns void.
 */
[[nodiscard]] inline FWaitGameplayTagAddedAwaiter WaitGameplayTagAdded(UAbilitySystemComponent* InASC, FGameplayTag Tag)
{
	return FWaitGameplayTagAddedAwaiter{InASC, Tag};
}

// ============================================================================
// WaitGameplayTagRemoved — waits until a gameplay tag is removed from the ASC
// ============================================================================

/**
 * Awaiter that waits until a specific gameplay tag is removed from the ASC.
 * If the tag is already absent at the point of co_await, resumes immediately.
 */
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

/**
 * Wait until a gameplay tag is removed from the ASC.
 *
 * @param InASC  The AbilitySystemComponent to monitor.
 * @param Tag    The tag to wait for removal.
 * @return       An awaiter — use with co_await. Returns void.
 */
[[nodiscard]] inline FWaitGameplayTagRemovedAwaiter WaitGameplayTagRemoved(UAbilitySystemComponent* InASC, FGameplayTag Tag)
{
	return FWaitGameplayTagRemovedAwaiter{InASC, Tag};
}

// ============================================================================
// WaitAttributeChange — waits for an attribute value to change
// ============================================================================

/**
 * Awaiter that waits for a GAS attribute to change value.
 * Binds to GetGameplayAttributeValueChangeDelegate on the ASC.
 * Resumes with the new value after the first change.
 */
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

/**
 * Wait for a GAS attribute to change. Returns the new value.
 *
 * @param InASC      The AbilitySystemComponent owning the attribute.
 * @param Attribute  The gameplay attribute to monitor.
 * @return           An awaiter — co_await yields float (the new value).
 */
[[nodiscard]] inline FWaitAttributeChangeAwaiter WaitAttributeChange(UAbilitySystemComponent* InASC, FGameplayAttribute Attribute)
{
	return FWaitAttributeChangeAwaiter{InASC, Attribute};
}

// ============================================================================
// WaitGameplayEffectRemoved — waits for an active gameplay effect to be removed
// ============================================================================

/**
 * Awaiter that waits for an active gameplay effect to be removed from the ASC.
 * Binds to OnGameplayEffectRemoved_InfoDelegate. If the effect is already
 * gone when the delegate pointer is requested, resumes immediately.
 */
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

/**
 * Wait for an active gameplay effect to be removed.
 *
 * @param InASC         The AbilitySystemComponent.
 * @param EffectHandle  The handle of the active effect to watch.
 * @return              An awaiter — use with co_await. Returns void.
 */
[[nodiscard]] inline FWaitGameplayEffectRemovedAwaiter WaitGameplayEffectRemoved(UAbilitySystemComponent* InASC, FActiveGameplayEffectHandle EffectHandle)
{
	return FWaitGameplayEffectRemovedAwaiter{InASC, EffectHandle};
}

} // namespace AsyncFlow

