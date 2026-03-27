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

// AsyncFlowGenerator.h — TGenerator<T> for co_yield-based lazy iteration
//
// Synchronous pull-based generator. Each call to MoveNext() resumes the
// coroutine until the next co_yield, producing one value at a time.
// Unlike TTask, generators are not async — they run inline on the caller's
// thread and cannot use co_await (await_transform is deleted).
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

/**
 * Promise type for TGenerator<T>. Manages yielded values and frame lifetime.
 *
 * Yielded lvalues are stored by pointer (zero-copy). Yielded rvalues are
 * moved into StoredValue, and CurrentValue points into that TOptional.
 *
 * co_await is explicitly deleted — generators are synchronous only.
 *
 * @tparam T  The type of each yielded value.
 */
template <typename T>
struct TGeneratorPromise
{
	/** Holds the last yielded rvalue. Reset before each yield_value(T&&). */
	TOptional<T> StoredValue;

	/** Points to the current yielded value — either into StoredValue or directly at the lvalue. */
	T* CurrentValue = nullptr;

	/** Captured if the coroutine body throws. Rethrown in MoveNext(). */
	std::exception_ptr Exception;

	TGenerator<T> get_return_object();

	std::suspend_always initial_suspend() const noexcept { return {}; }
	std::suspend_always final_suspend() const noexcept { return {}; }

	/**
	 * Yield an lvalue reference. No copy — CurrentValue points directly at the caller's variable.
	 * The reference must remain valid until the next MoveNext() call.
	 */
	std::suspend_always yield_value(T& Value)
	{
		StoredValue.Reset();
		CurrentValue = &Value;
		return {};
	}

	/**
	 * Yield an rvalue. The value is moved into StoredValue and CurrentValue
	 * points into the TOptional.
	 */
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

	/** co_await is not supported inside generators — compile-time error. */
	template <typename AwaiterType>
	AwaiterType&& await_transform(AwaiterType&& Awaiter) = delete;
};

// ============================================================================
// TGenerator<T> — synchronous pull-based coroutine generator
// ============================================================================

/**
 * Lazy O(1)-memory iterator driven by co_yield. Move-only, single-owner.
 *
 * Each call to MoveNext() resumes the coroutine until the next co_yield
 * or until the coroutine body returns. Current() accesses the last yielded value.
 *
 * Supports range-based for via begin()/end(). The iterator calls MoveNext()
 * on construction and on each ++, so the first value is available at *begin().
 *
 * @tparam T  The type of each yielded value.
 *
 * Usage:
 *   TGenerator<int32> CountTo(int32 N)
 *   {
 *       for (int32 I = 0; I < N; ++I) { co_yield I; }
 *   }
 *   for (int32 Val : CountTo(10)) { ... }
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

	/**
	 * Advance to the next yielded value.
	 *
	 * @return false if the generator is exhausted (coroutine returned).
	 * @warning Rethrows any exception captured from the coroutine body.
	 */
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

	/**
	 * Access the current yielded value. Only valid after MoveNext() returns true.
	 * @warning Undefined behavior if called after MoveNext() returned false.
	 */
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

	/**
	 * Forward iterator for range-based for. Calls MoveNext() on construction
	 * and on each operator++. Compares done-state for termination.
	 */
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

