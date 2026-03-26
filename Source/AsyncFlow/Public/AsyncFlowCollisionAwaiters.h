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

} // namespace AsyncFlow

