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

// AsyncFlowEditorTask.h — Helper utilities for editor coroutines
//
// Convenience function for starting editor-context coroutines with optional
// debug registration. Uses the same TTask<T> type as the runtime module.
#pragma once

#include "AsyncFlowTask.h"
#include "AsyncFlowDebug.h"

namespace AsyncFlow
{

	/**
 * Start an editor-context coroutine task with optional debug tracking.
 *
 * Calls Task.Start() and, if DebugName is non-empty, registers the task
 * with FAsyncFlowDebugger for visibility in AsyncFlow.List / AsyncFlow.EditorList.
 *
 * @tparam T          The task's result type.
 * @param Task        The task to start. Must be valid (not yet started).
 * @param DebugName   Optional human-readable name for debug tracking.
 */
	template <typename T>
	void StartEditorTask(TTask<T>& Task, const FString& DebugName = TEXT(""))
	{
		if (!DebugName.IsEmpty())
		{
			Task.SetDebugName(DebugName);
			DebugRegisterTask(Task);
		}
		Task.Start();
	}

} // namespace AsyncFlow
