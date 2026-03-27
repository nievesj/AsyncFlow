// AsyncFlowGenerator.h — TGenerator<T> for co_yield-based lazy iteration
#pragma once

#include "HAL/Platform.h"
#include "Containers/Array.h"

#include <coroutine>
#include <exception>

namespace AsyncFlow
{

// Forward declaration
template <typename T>
class TGenerator;

// ============================================================================
// TGeneratorPromise<T> — promise type for TGenerator<T>
// ============================================================================

template <typename T>
struct TGeneratorPromise
{
	TOptional<T> StoredValue;
	T* CurrentValue = nullptr;
	std::exception_ptr Exception;

	TGenerator<T> get_return_object();

	std::suspend_always initial_suspend() const noexcept { return {}; }
	std::suspend_always final_suspend() const noexcept { return {}; }

	std::suspend_always yield_value(T& Value)
	{
		StoredValue.Reset();
		CurrentValue = &Value;
		return {};
	}

	std::suspend_always yield_value(T&& Value)
	{
		StoredValue.Emplace(MoveTemp(Value));
		CurrentValue = &StoredValue.GetValue();
		return {};
	}

	void return_void() {}

	void unhandled_exception()
	{
		Exception = std::current_exception();
	}

	// Generators do not use await_transform — co_await is not supported inside generators
	template <typename AwaiterType>
	AwaiterType&& await_transform(AwaiterType&& Awaiter) = delete;
};

// ============================================================================
// TGenerator<T> — synchronous pull-based coroutine generator
// ============================================================================

/**
 * Lazy O(1)-memory iterator driven by co_yield.
 * Each call to operator++ (or MoveNext) resumes the coroutine until the
 * next co_yield. Supports range-based for.
 *
 * Usage:
 *   TGenerator<int32> CountTo(int32 N)
 *   {
 *       for (int32 I = 0; I < N; ++I)
 *       {
 *           co_yield I;
 *       }
 *   }
 *
 *   for (int32 Val : CountTo(10))
 *   {
 *       UE_LOG(LogTemp, Log, TEXT("%d"), Val);
 *   }
 */
template <typename T>
class TGenerator
{
public:
	using promise_type = TGeneratorPromise<T>;
	using CoroutineHandle = std::coroutine_handle<promise_type>;

	TGenerator() = default;

	explicit TGenerator(CoroutineHandle InHandle)
		: Handle(InHandle)
	{
	}

	~TGenerator()
	{
		if (Handle)
		{
			Handle.destroy();
			Handle = nullptr;
		}
	}

	// Move only
	TGenerator(TGenerator&& Other) noexcept
		: Handle(Other.Handle)
	{
		Other.Handle = nullptr;
	}

	TGenerator& operator=(TGenerator&& Other) noexcept
	{
		if (this != &Other)
		{
			if (Handle) { Handle.destroy(); }
			Handle = Other.Handle;
			Other.Handle = nullptr;
		}
		return *this;
	}

	TGenerator(const TGenerator&) = delete;
	TGenerator& operator=(const TGenerator&) = delete;

	/** Advance to the next yielded value. Returns false if the generator is exhausted. */
	bool MoveNext()
	{
		if (!Handle || Handle.done())
		{
			return false;
		}
		Handle.resume();
		if (Handle.promise().Exception)
		{
			std::rethrow_exception(Handle.promise().Exception);
		}
		return !Handle.done();
	}

	/** Get the current yielded value. Only valid after MoveNext() returns true. */
	T& Current()
	{
		return *Handle.promise().CurrentValue;
	}

	const T& Current() const
	{
		return *Handle.promise().CurrentValue;
	}

	// ========================================================================
	// STL iterator support for range-based for
	// ========================================================================

	struct FIterator
	{
		TGenerator* Gen = nullptr;
		bool bDone = true;

		FIterator() = default;
		explicit FIterator(TGenerator* InGen) : Gen(InGen), bDone(false)
		{
			// Advance to the first value
			if (Gen && !Gen->MoveNext())
			{
				bDone = true;
			}
		}

		T& operator*() { return Gen->Current(); }
		const T& operator*() const { return Gen->Current(); }

		FIterator& operator++()
		{
			if (Gen && !Gen->MoveNext())
			{
				bDone = true;
			}
			return *this;
		}

		bool operator!=(const FIterator& Other) const
		{
			return bDone != Other.bDone;
		}

		bool operator==(const FIterator& Other) const
		{
			return bDone == Other.bDone;
		}
	};

	FIterator begin() { return FIterator(this); }
	FIterator end() { return FIterator(); }

private:
	CoroutineHandle Handle = nullptr;
};

// ============================================================================
// get_return_object implementation
// ============================================================================

template <typename T>
TGenerator<T> TGeneratorPromise<T>::get_return_object()
{
	return TGenerator<T>(std::coroutine_handle<TGeneratorPromise<T>>::from_promise(*this));
}

} // namespace AsyncFlow

