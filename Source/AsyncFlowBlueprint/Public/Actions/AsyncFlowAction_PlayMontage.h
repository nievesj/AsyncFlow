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

// AsyncFlowAction_PlayMontage.h — Blueprint async action for montage playback.
#pragma once

#include "AsyncFlowAction.h"
#include "AsyncFlowAction_PlayMontage.generated.h"

class UAnimMontage;
class USkeletalMeshComponent;

/**
 * Blueprint node that plays an animation montage and suspends until it
 * completes or is interrupted. Uses AsyncFlow's PlayMontageAndWait awaiter.
 */
UCLASS(meta = (DisplayName = "Async Flow Play Montage"))
class ASYNCFLOWBLUEPRINT_API UAsyncFlowAction_PlayMontage : public UAsyncFlowAction
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable)
	FAsyncFlowOutputPin OnInterrupted;

	/** Plays the montage on the given mesh and suspends until completion. */
	UFUNCTION(BlueprintCallable,
		meta = (BlueprintInternalUseOnly = "true",
			WorldContext = "WorldContextObject",
			DisplayName = "Async Flow Play Montage"),
		Category = "AsyncFlow")
	static UAsyncFlowAction_PlayMontage* AsyncPlayMontage(
		UObject* WorldContextObject,
		USkeletalMeshComponent* MeshComponent,
		UAnimMontage* Montage,
		float PlayRate = 1.0f);

protected:
	virtual AsyncFlow::TTask<void> ExecuteAction(UObject* WorldContext) override;

private:
	TWeakObjectPtr<USkeletalMeshComponent> MeshComponent;
	TObjectPtr<UAnimMontage> Montage;
	float PlayRate = 1.0f;
};
