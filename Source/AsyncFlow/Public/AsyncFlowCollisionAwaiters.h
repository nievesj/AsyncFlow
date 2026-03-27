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

// AsyncFlowCollisionAwaiters.h — Async collision query awaiters
#pragma once

#include "AsyncFlowTask.h"
#include "AsyncFlowTickSubsystem.h"
#include "Engine/World.h"
#include "Engine/EngineTypes.h"
#include "CollisionQueryParams.h"
#include "WorldCollision.h"

#include <coroutine>

namespace AsyncFlow
{

// ============================================================================
// AsyncLineTrace — wraps UWorld::AsyncLineTraceByChannel
// ============================================================================

struct FAsyncLineTraceAwaiter
{
	UWorld* World = nullptr;
	EAsyncTraceType TraceType;
	FVector Start;
	FVector End;
	ECollisionChannel Channel;
	FCollisionQueryParams Params;
	FTraceHandle TraceHandle;
	FTraceDatum ResultDatum;
	std::coroutine_handle<> Continuation;
	TSharedPtr<bool> AliveFlag = MakeShared<bool>(true);

	~FAsyncLineTraceAwaiter() { *AliveFlag = false; }

	bool await_ready() const { return false; }

	void await_suspend(std::coroutine_handle<> Handle)
	{
		Continuation = Handle;

		if (!World)
		{
			Handle.resume();
			return;
		}

		FTraceDelegate Delegate;
		TWeakPtr<bool> WeakAlive = AliveFlag;
		Delegate.BindLambda([this, WeakAlive](const FTraceHandle& InTraceHandle, FTraceDatum& InDatum)
		{
			if (!WeakAlive.IsValid()) { return; }
			ResultDatum = InDatum;
			if (Continuation && !Continuation.done())
			{
				Continuation.resume();
			}
		});

		TraceHandle = World->AsyncLineTraceByChannel(
			TraceType, Start, End, Channel, Params, FCollisionResponseParams::DefaultResponseParam, &Delegate
		);
	}

	FTraceDatum await_resume() const { return ResultDatum; }
};

/** Async line trace. Results arrive next frame. */
[[nodiscard]] inline FAsyncLineTraceAwaiter AsyncLineTrace(
	UWorld* World,
	EAsyncTraceType TraceType,
	const FVector& Start,
	const FVector& End,
	ECollisionChannel Channel,
	const FCollisionQueryParams& Params = FCollisionQueryParams::DefaultQueryParam)
{
	return FAsyncLineTraceAwaiter{World, TraceType, Start, End, Channel, Params};
}

// ============================================================================
// AsyncSweep — wraps UWorld::AsyncSweepByChannel
// ============================================================================

struct FAsyncSweepAwaiter
{
	UWorld* World = nullptr;
	EAsyncTraceType TraceType;
	FVector Start;
	FVector End;
	FQuat Rot;
	ECollisionChannel Channel;
	FCollisionShape Shape;
	FCollisionQueryParams Params;
	FTraceDatum ResultDatum;
	std::coroutine_handle<> Continuation;
	TSharedPtr<bool> AliveFlag = MakeShared<bool>(true);

	~FAsyncSweepAwaiter() { *AliveFlag = false; }

	bool await_ready() const { return false; }

	void await_suspend(std::coroutine_handle<> Handle)
	{
		Continuation = Handle;

		if (!World)
		{
			Handle.resume();
			return;
		}

		FTraceDelegate Delegate;
		TWeakPtr<bool> WeakAlive = AliveFlag;
		Delegate.BindLambda([this, WeakAlive](const FTraceHandle& InTraceHandle, FTraceDatum& InDatum)
		{
			if (!WeakAlive.IsValid()) { return; }
			ResultDatum = InDatum;
			if (Continuation && !Continuation.done())
			{
				Continuation.resume();
			}
		});

		World->AsyncSweepByChannel(
			TraceType, Start, End, Rot, Channel, Shape, Params,
			FCollisionResponseParams::DefaultResponseParam, &Delegate
		);
	}

	FTraceDatum await_resume() const { return ResultDatum; }
};

/** Async sweep trace. Results arrive next frame. */
[[nodiscard]] inline FAsyncSweepAwaiter AsyncSweep(
	UWorld* World,
	EAsyncTraceType TraceType,
	const FVector& Start,
	const FVector& End,
	const FQuat& Rot,
	ECollisionChannel Channel,
	const FCollisionShape& Shape,
	const FCollisionQueryParams& Params = FCollisionQueryParams::DefaultQueryParam)
{
	return FAsyncSweepAwaiter{World, TraceType, Start, End, Rot, Channel, Shape, Params};
}

// ============================================================================
// AsyncOverlap — wraps UWorld::AsyncOverlapByChannel
// ============================================================================

struct FAsyncOverlapAwaiter
{
	UWorld* World = nullptr;
	FVector Position;
	FQuat Rotation;
	ECollisionChannel Channel;
	FCollisionShape Shape;
	FCollisionQueryParams Params;
	FOverlapDatum ResultDatum;
	std::coroutine_handle<> Continuation;
	TSharedPtr<bool> AliveFlag = MakeShared<bool>(true);

	~FAsyncOverlapAwaiter() { *AliveFlag = false; }

	bool await_ready() const { return false; }

	void await_suspend(std::coroutine_handle<> Handle)
	{
		Continuation = Handle;

		if (!World)
		{
			Handle.resume();
			return;
		}

		FOverlapDelegate Delegate;
		TWeakPtr<bool> WeakAlive = AliveFlag;
		Delegate.BindLambda([this, WeakAlive](const FTraceHandle& InTraceHandle, FOverlapDatum& InDatum)
		{
			if (!WeakAlive.IsValid()) { return; }
			ResultDatum = InDatum;
			if (Continuation && !Continuation.done())
			{
				Continuation.resume();
			}
		});

		World->AsyncOverlapByChannel(
			Position, Rotation, Channel, Shape, Params,
			FCollisionResponseParams::DefaultResponseParam, &Delegate
		);
	}

	FOverlapDatum await_resume() const { return ResultDatum; }
};

/** Async overlap check. Results arrive next frame. */
[[nodiscard]] inline FAsyncOverlapAwaiter AsyncOverlap(
	UWorld* World,
	const FVector& Position,
	const FQuat& Rotation,
	ECollisionChannel Channel,
	const FCollisionShape& Shape,
	const FCollisionQueryParams& Params = FCollisionQueryParams::DefaultQueryParam)
{
	return FAsyncOverlapAwaiter{World, Position, Rotation, Channel, Shape, Params};
}

// ============================================================================
// ByObjectType variants — wraps UWorld::AsyncXxxByObjectType
// ============================================================================

struct FAsyncLineTraceByObjectAwaiter
{
	UWorld* World = nullptr;
	EAsyncTraceType TraceType;
	FVector Start;
	FVector End;
	FCollisionObjectQueryParams ObjectParams;
	FCollisionQueryParams Params;
	FTraceDatum ResultDatum;
	std::coroutine_handle<> Continuation;
	TSharedPtr<bool> AliveFlag = MakeShared<bool>(true);

	~FAsyncLineTraceByObjectAwaiter() { *AliveFlag = false; }

	bool await_ready() const { return false; }

	void await_suspend(std::coroutine_handle<> Handle)
	{
		Continuation = Handle;
		if (!World) { Handle.resume(); return; }

		FTraceDelegate Delegate;
		TWeakPtr<bool> WeakAlive = AliveFlag;
		Delegate.BindLambda([this, WeakAlive](const FTraceHandle&, FTraceDatum& InDatum)
		{
			if (!WeakAlive.IsValid()) { return; }
			ResultDatum = MoveTemp(InDatum);
			if (Continuation && !Continuation.done()) { Continuation.resume(); }
		});

		World->AsyncLineTraceByObjectType(TraceType, Start, End, ObjectParams, Params, &Delegate);
	}

	FTraceDatum await_resume() { return MoveTemp(ResultDatum); }
};

/** Async line trace filtered by object type. Results arrive next frame. */
[[nodiscard]] inline FAsyncLineTraceByObjectAwaiter AsyncLineTraceByObjectType(
	UWorld* World, EAsyncTraceType TraceType, const FVector& Start, const FVector& End,
	const FCollisionObjectQueryParams& ObjectParams,
	const FCollisionQueryParams& Params = FCollisionQueryParams::DefaultQueryParam)
{
	return FAsyncLineTraceByObjectAwaiter{World, TraceType, Start, End, ObjectParams, Params};
}

struct FAsyncSweepByObjectAwaiter
{
	UWorld* World = nullptr;
	EAsyncTraceType TraceType;
	FVector Start;
	FVector End;
	FQuat Rot;
	FCollisionObjectQueryParams ObjectParams;
	FCollisionShape Shape;
	FCollisionQueryParams Params;
	FTraceDatum ResultDatum;
	std::coroutine_handle<> Continuation;
	TSharedPtr<bool> AliveFlag = MakeShared<bool>(true);

	~FAsyncSweepByObjectAwaiter() { *AliveFlag = false; }
	bool await_ready() const { return false; }

	void await_suspend(std::coroutine_handle<> Handle)
	{
		Continuation = Handle;
		if (!World) { Handle.resume(); return; }

		FTraceDelegate Delegate;
		TWeakPtr<bool> WeakAlive = AliveFlag;
		Delegate.BindLambda([this, WeakAlive](const FTraceHandle&, FTraceDatum& InDatum)
		{
			if (!WeakAlive.IsValid()) { return; }
			ResultDatum = MoveTemp(InDatum);
			if (Continuation && !Continuation.done()) { Continuation.resume(); }
		});

		World->AsyncSweepByObjectType(TraceType, Start, End, Rot, ObjectParams, Shape, Params, &Delegate);
	}

	FTraceDatum await_resume() { return MoveTemp(ResultDatum); }
};

/** Async sweep filtered by object type. Results arrive next frame. */
[[nodiscard]] inline FAsyncSweepByObjectAwaiter AsyncSweepByObjectType(
	UWorld* World, EAsyncTraceType TraceType, const FVector& Start, const FVector& End,
	const FQuat& Rot, const FCollisionObjectQueryParams& ObjectParams, const FCollisionShape& Shape,
	const FCollisionQueryParams& Params = FCollisionQueryParams::DefaultQueryParam)
{
	return FAsyncSweepByObjectAwaiter{World, TraceType, Start, End, Rot, ObjectParams, Shape, Params};
}

struct FAsyncOverlapByObjectAwaiter
{
	UWorld* World = nullptr;
	FVector Position;
	FQuat Rotation;
	FCollisionObjectQueryParams ObjectParams;
	FCollisionShape Shape;
	FCollisionQueryParams Params;
	FOverlapDatum ResultDatum;
	std::coroutine_handle<> Continuation;
	TSharedPtr<bool> AliveFlag = MakeShared<bool>(true);

	~FAsyncOverlapByObjectAwaiter() { *AliveFlag = false; }
	bool await_ready() const { return false; }

	void await_suspend(std::coroutine_handle<> Handle)
	{
		Continuation = Handle;
		if (!World) { Handle.resume(); return; }

		FOverlapDelegate Delegate;
		TWeakPtr<bool> WeakAlive = AliveFlag;
		Delegate.BindLambda([this, WeakAlive](const FTraceHandle&, FOverlapDatum& InDatum)
		{
			if (!WeakAlive.IsValid()) { return; }
			ResultDatum = MoveTemp(InDatum);
			if (Continuation && !Continuation.done()) { Continuation.resume(); }
		});

		World->AsyncOverlapByObjectType(Position, Rotation, ObjectParams, Shape, Params, &Delegate);
	}

	FOverlapDatum await_resume() { return MoveTemp(ResultDatum); }
};

/** Async overlap filtered by object type. Results arrive next frame. */
[[nodiscard]] inline FAsyncOverlapByObjectAwaiter AsyncOverlapByObjectType(
	UWorld* World, const FVector& Position, const FQuat& Rotation,
	const FCollisionObjectQueryParams& ObjectParams, const FCollisionShape& Shape,
	const FCollisionQueryParams& Params = FCollisionQueryParams::DefaultQueryParam)
{
	return FAsyncOverlapByObjectAwaiter{World, Position, Rotation, ObjectParams, Shape, Params};
}

// ============================================================================
// ByProfile variants
// ============================================================================

struct FAsyncLineTraceByProfileAwaiter
{
	UWorld* World = nullptr;
	EAsyncTraceType TraceType;
	FVector Start;
	FVector End;
	FName ProfileName;
	FCollisionQueryParams Params;
	FTraceDatum ResultDatum;
	std::coroutine_handle<> Continuation;
	TSharedPtr<bool> AliveFlag = MakeShared<bool>(true);

	~FAsyncLineTraceByProfileAwaiter() { *AliveFlag = false; }
	bool await_ready() const { return false; }

	void await_suspend(std::coroutine_handle<> Handle)
	{
		Continuation = Handle;
		if (!World) { Handle.resume(); return; }

		FTraceDelegate Delegate;
		TWeakPtr<bool> WeakAlive = AliveFlag;
		Delegate.BindLambda([this, WeakAlive](const FTraceHandle&, FTraceDatum& InDatum)
		{
			if (!WeakAlive.IsValid()) { return; }
			ResultDatum = MoveTemp(InDatum);
			if (Continuation && !Continuation.done()) { Continuation.resume(); }
		});

		World->AsyncLineTraceByProfile(TraceType, Start, End, ProfileName, Params, &Delegate);
	}

	FTraceDatum await_resume() { return MoveTemp(ResultDatum); }
};

/** Async line trace filtered by collision profile name. Results arrive next frame. */
[[nodiscard]] inline FAsyncLineTraceByProfileAwaiter AsyncLineTraceByProfile(
	UWorld* World, EAsyncTraceType TraceType, const FVector& Start, const FVector& End,
	FName ProfileName, const FCollisionQueryParams& Params = FCollisionQueryParams::DefaultQueryParam)
{
	return FAsyncLineTraceByProfileAwaiter{World, TraceType, Start, End, ProfileName, Params};
}

struct FAsyncSweepByProfileAwaiter
{
	UWorld* World = nullptr;
	EAsyncTraceType TraceType;
	FVector Start;
	FVector End;
	FQuat Rot;
	FName ProfileName;
	FCollisionShape Shape;
	FCollisionQueryParams Params;
	FTraceDatum ResultDatum;
	std::coroutine_handle<> Continuation;
	TSharedPtr<bool> AliveFlag = MakeShared<bool>(true);

	~FAsyncSweepByProfileAwaiter() { *AliveFlag = false; }
	bool await_ready() const { return false; }

	void await_suspend(std::coroutine_handle<> Handle)
	{
		Continuation = Handle;
		if (!World) { Handle.resume(); return; }

		FTraceDelegate Delegate;
		TWeakPtr<bool> WeakAlive = AliveFlag;
		Delegate.BindLambda([this, WeakAlive](const FTraceHandle&, FTraceDatum& InDatum)
		{
			if (!WeakAlive.IsValid()) { return; }
			ResultDatum = MoveTemp(InDatum);
			if (Continuation && !Continuation.done()) { Continuation.resume(); }
		});

		World->AsyncSweepByProfile(TraceType, Start, End, Rot, ProfileName, Shape, Params, &Delegate);
	}

	FTraceDatum await_resume() { return MoveTemp(ResultDatum); }
};

/** Async sweep filtered by collision profile name. Results arrive next frame. */
[[nodiscard]] inline FAsyncSweepByProfileAwaiter AsyncSweepByProfile(
	UWorld* World, EAsyncTraceType TraceType, const FVector& Start, const FVector& End,
	const FQuat& Rot, FName ProfileName, const FCollisionShape& Shape,
	const FCollisionQueryParams& Params = FCollisionQueryParams::DefaultQueryParam)
{
	return FAsyncSweepByProfileAwaiter{World, TraceType, Start, End, Rot, ProfileName, Shape, Params};
}

} // namespace AsyncFlow

