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

// AsyncFlowAction_PlayMontage.cpp — Play montage action implementation.
#include "Actions/AsyncFlowAction_PlayMontage.h"
#include "AsyncFlowAnimAwaiters.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"

UAsyncFlowAction_PlayMontage* UAsyncFlowAction_PlayMontage::AsyncPlayMontage(
	UObject* WorldContextObject,
	USkeletalMeshComponent* InMeshComponent,
	UAnimMontage* InMontage,
	float InPlayRate)
{
	UAsyncFlowAction_PlayMontage* Action = NewObject<UAsyncFlowAction_PlayMontage>();
	Action->MeshComponent = InMeshComponent;
	Action->Montage = InMontage;
	Action->PlayRate = InPlayRate;
	Action->RegisterWithGameInstance(WorldContextObject);
	return Action;
}

AsyncFlow::TTask<void> UAsyncFlowAction_PlayMontage::ExecuteAction(UObject* WorldContext)
{
	bHandledBroadcast = true;

	USkeletalMeshComponent* Mesh = MeshComponent.Get();
	if (!Mesh || !Montage)
	{
		if (ShouldBroadcastDelegates())
		{
			OnFailed.Broadcast();
		}
		co_return;
	}

	UAnimInstance* AnimInstance = Mesh->GetAnimInstance();
	if (!AnimInstance)
	{
		if (ShouldBroadcastDelegates())
		{
			OnFailed.Broadcast();
		}
		co_return;
	}

	const bool bCompleted = co_await AsyncFlow::PlayMontageAndWait(
		AnimInstance, Montage, PlayRate, NAME_None);

	if (!ShouldBroadcastDelegates())
	{
		co_return;
	}

	if (bCompleted)
	{
		OnCompleted.Broadcast();
	}
	else
	{
		OnInterrupted.Broadcast();
	}
}
