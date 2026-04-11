# GAS Module

**Header:** `#include "AsyncFlowGAS.h"`
**Build.cs:** `PublicDependencyModuleNames.Add("AsyncFlowGAS");`
**Requires:** GameplayAbilities plugin enabled.

---

## UAsyncFlowGameplayAbility

Base class for abilities that use coroutines for their execution flow. Subclass this instead of `UGameplayAbility`.

Override `ExecuteAbility()` — it returns `TTask<EAbilitySuccessType>` and is your ability's main logic.

The class is `Abstract` — you must subclass it. Defaults: `LocalPredicted` net execution policy and
`InstancedPerActor` instancing (suitable for single-player). Override the constructor to change these.

> **Note:** The base `ExecuteAbility()` logs an error and `co_return`s `Failed`. Always override it in subclasses.

```cpp
UCLASS()
class UGA_FireSlash : public UAsyncFlowGameplayAbility
{
    GENERATED_BODY()

protected:
    virtual AsyncFlow::TTask<EAbilitySuccessType> ExecuteAbility(FAbilityParams Params) override;

    UPROPERTY(EditDefaultsOnly)
    TObjectPtr<UAnimMontage> SlashMontage;

    UPROPERTY(EditDefaultsOnly)
    float CooldownSeconds = 1.0f;
};
```

```cpp
AsyncFlow::TTask<EAbilitySuccessType> UGA_FireSlash::ExecuteAbility(FAbilityParams Params)
{
    ACharacter* Avatar = Params.GetAvatarCharacter();
    co_verifyf_r(Avatar != nullptr, TEXT("No avatar character"));

    UAnimInstance* AnimInstance = Avatar->GetMesh()->GetAnimInstance();
    co_verifyf_r(AnimInstance != nullptr, TEXT("No anim instance"));

    bool bFinished = co_await AsyncFlow::PlayMontageAndWait(AnimInstance, SlashMontage);
    if (!bFinished)
    {
        co_return EAbilitySuccessType::Canceled;
    }

    co_await AsyncFlow::Delay(CooldownSeconds);
    co_return EAbilitySuccessType::Finished;
}
```

### How It Works

1. GAS calls `ActivateAbility()`.
2. The base class calls `ExecuteAbility()`, captures the returned `TTask`, and starts it.
3. Since `TTask` is now copyable and self-sustaining after `Start()`, the base class can hold a copy for state queries
   while the coroutine runs independently.
4. When the coroutine completes, the base class reads the `EAbilitySuccessType` and calls `EndAbility()` with the
   appropriate `bWasCancelled` flag.
5. If GAS cancels the ability externally (e.g., stun, death), the base class calls `Cancel()` on the task. The
   coroutine stops at the next `co_await` boundary where the cancellation flag is detected.

### EAbilitySuccessType

```cpp
UENUM(BlueprintType)
enum class EAbilitySuccessType : uint8
{
    Finished,   // Completed normally
    Failed,     // Failed (ability ends, not cancelled)
    Canceled    // Cancelled by external system or self
};
```

### FAbilityParams

Wraps GAS activation data for convenient access within the coroutine.

| Method                        | Returns                    |
|-------------------------------|----------------------------|
| `GetAvatarCharacter()`        | `ACharacter*` (or nullptr) |
| `GetOwningActor()`            | `AActor*`                  |
| `GetAbilitySystemComponent()` | `UAbilitySystemComponent*` |
| `HasValidActorInfo()`         | `bool`                     |

Fields:

| Field                  | Type                             |
|------------------------|----------------------------------|
| `Handle`               | `FGameplayAbilitySpecHandle`     |
| `ActorInfo`            | `FGameplayAbilityActorInfo`      |
| `ActivationInfo`       | `FGameplayAbilityActivationInfo` |
| `TriggerEventData`     | `FGameplayEventData`             |
| `bHasTriggerEventData` | `bool`                           |

---

## GAS Awaiters

**Header:** `#include "AsyncFlowGASAwaiters.h"` (included by `AsyncFlowGAS.h`)

### WaitGameplayEvent

`FWaitGameplayEventAwaiter WaitGameplayEvent(UAbilitySystemComponent* InASC, FGameplayTag EventTag)`

Wait for a gameplay event with a matching tag on an AbilitySystemComponent. `co_await` yields `FGameplayEventData`.

```cpp
UAbilitySystemComponent* ASC = Params.GetAbilitySystemComponent();

FGameplayEventData EventData = co_await AsyncFlow::WaitGameplayEvent(
    ASC,
    FGameplayTag::RequestGameplayTag(TEXT("Event.Attack.Hit"))
);

AActor* Target = EventData.Target.Get();
```

Automatically unregisters from the delegate after the first fire.

### WaitGameplayTagAdded

`FWaitGameplayTagAddedAwaiter WaitGameplayTagAdded(UAbilitySystemComponent* InASC, FGameplayTag Tag)`

Wait until a gameplay tag is added to the ASC. `co_await` yields `void`.

```cpp
co_await AsyncFlow::WaitGameplayTagAdded(
    ASC,
    FGameplayTag::RequestGameplayTag(TEXT("State.Stunned"))
);
// Tag was just added
```

If the tag is already present, resumes immediately.

### WaitGameplayTagRemoved

`FWaitGameplayTagRemovedAwaiter WaitGameplayTagRemoved(UAbilitySystemComponent* InASC, FGameplayTag Tag)`

Wait until a gameplay tag is removed from the ASC. `co_await` yields `void`.

```cpp
co_await AsyncFlow::WaitGameplayTagRemoved(
    ASC,
    FGameplayTag::RequestGameplayTag(TEXT("State.Stunned"))
);
// Stun wore off
```

If the tag is already absent, resumes immediately.

### WaitAttributeChange

`FWaitAttributeChangeAwaiter WaitAttributeChange(UAbilitySystemComponent* InASC, FGameplayAttribute Attribute)`

Wait for an attribute value to change. `co_await` yields `float` (the new value).

```cpp
float NewHealth = co_await AsyncFlow::WaitAttributeChange(
    ASC,
    UMyAttributeSet::GetHealthAttribute()
);
```

Fires on the first change and automatically unregisters.

### WaitGameplayEffectRemoved

`FWaitGameplayEffectRemovedAwaiter WaitGameplayEffectRemoved(UAbilitySystemComponent* InASC, FActiveGameplayEffectHandle EffectHandle)`

Wait for an active gameplay effect to be removed. `co_await` yields `void`.

```cpp
FActiveGameplayEffectHandle EffectHandle = ASC->ApplyGameplayEffectToSelf(...);
co_await AsyncFlow::WaitGameplayEffectRemoved(ASC, EffectHandle);
// Effect expired or was removed
```

If the effect was already removed by the time `await_suspend` runs, resumes immediately.

---

## Full Ability Example

```cpp
AsyncFlow::TTask<EAbilitySuccessType> UGA_HealingAura::ExecuteAbility(FAbilityParams Params)
{
    UAbilitySystemComponent* ASC = Params.GetAbilitySystemComponent();
    co_verifyf_r(ASC != nullptr, TEXT("No ASC"));

    ACharacter* Avatar = Params.GetAvatarCharacter();
    co_verifyf_r(Avatar != nullptr, TEXT("No avatar"));

    // Contract: cancel if avatar dies
    TWeakObjectPtr<ACharacter> WeakAvatar = Avatar;
    CO_CONTRACT([WeakAvatar]() { return WeakAvatar.IsValid(); });

    // Apply the aura effect
    FActiveGameplayEffectHandle AuraHandle = ASC->ApplyGameplayEffectToSelf(
        AuraEffect->GetDefaultObject<UGameplayEffect>(),
        1.0f, ASC->MakeEffectContext()
    );

    // Play the aura VFX montage
    UAnimInstance* AnimInstance = Avatar->GetMesh()->GetAnimInstance();
    co_await AsyncFlow::PlayMontageAndWait(AnimInstance, AuraMontage);

    // Wait for the effect to expire
    co_await AsyncFlow::WaitGameplayEffectRemoved(ASC, AuraHandle);

    co_return EAbilitySuccessType::Finished;
}
```
