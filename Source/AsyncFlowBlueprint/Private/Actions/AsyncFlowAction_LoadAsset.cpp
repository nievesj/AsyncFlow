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

// AsyncFlowAction_LoadAsset.cpp — Load asset action implementation.
#include "Actions/AsyncFlowAction_LoadAsset.h"
#include "AsyncFlowAssetAwaiters.h"

UAsyncFlowAction_LoadAsset* UAsyncFlowAction_LoadAsset::AsyncLoadAsset(
	UObject* WorldContextObject,
	TSoftObjectPtr<UObject> InAsset)
{
	UAsyncFlowAction_LoadAsset* Action = NewObject<UAsyncFlowAction_LoadAsset>();
	Action->Asset = InAsset;
	Action->RegisterWithGameInstance(WorldContextObject);
	return Action;
}

AsyncFlow::TTask<void> UAsyncFlowAction_LoadAsset::ExecuteAction(UObject* WorldContext)
{
	bHandledBroadcast = true;

	UObject* Loaded = co_await AsyncFlow::AsyncLoadObject<UObject>(Asset);

	if (!ShouldBroadcastDelegates())
	{
		co_return;
	}

	if (Loaded)
	{
		OnLoaded.Broadcast(Loaded);
	}
	else
	{
		OnFailed.Broadcast();
	}
}
