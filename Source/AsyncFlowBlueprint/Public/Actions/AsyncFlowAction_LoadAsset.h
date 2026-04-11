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

// AsyncFlowAction_LoadAsset.h — Blueprint async action for asset loading.
#pragma once

#include "AsyncFlowAction.h"
#include "UObject/SoftObjectPtr.h"
#include "AsyncFlowAction_LoadAsset.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAsyncFlowLoadAssetOutput, UObject*, LoadedAsset);

/**
 * Blueprint node that asynchronously loads a soft-referenced asset using
 * AsyncFlow's streaming awaiter. Broadcasts OnLoaded with the result on
 * success, or OnFailed if the load fails.
 */
UCLASS(meta = (DisplayName = "Async Flow Load Asset"))
class ASYNCFLOWBLUEPRINT_API UAsyncFlowAction_LoadAsset : public UAsyncFlowAction
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable)
	FAsyncFlowLoadAssetOutput OnLoaded;

	/** Asynchronously loads the given soft object reference. */
	UFUNCTION(BlueprintCallable,
		meta = (BlueprintInternalUseOnly = "true",
			WorldContext = "WorldContextObject",
			DisplayName = "Async Flow Load Asset"),
		Category = "AsyncFlow")
	static UAsyncFlowAction_LoadAsset* AsyncLoadAsset(
		UObject* WorldContextObject,
		TSoftObjectPtr<UObject> Asset);

protected:
	virtual AsyncFlow::TTask<void> ExecuteAction(UObject* WorldContext) override;

private:
	TSoftObjectPtr<UObject> Asset;
};
