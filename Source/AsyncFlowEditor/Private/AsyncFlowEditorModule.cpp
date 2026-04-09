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

// AsyncFlowEditorModule.cpp — Module registration for AsyncFlowEditor.
#include "AsyncFlowEditorModule.h"
#include "AsyncFlowEditorTickDriver.h"
#include "AsyncFlowDebug.h"

#define LOCTEXT_NAMESPACE "FAsyncFlowEditorModule"

void FAsyncFlowEditorModule::StartupModule()
{
	TickDriver = MakeUnique<FAsyncFlowEditorTickDriver>();

	EditorListCmd = MakeUnique<FAutoConsoleCommand>(
		TEXT("AsyncFlow.EditorList"),
		TEXT("Dumps all active AsyncFlow coroutines (editor and runtime) to the output log."),
		FConsoleCommandDelegate::CreateLambda([]() {
			AsyncFlow::FAsyncFlowDebugger::Get().DumpToLog();
		}));
}

void FAsyncFlowEditorModule::ShutdownModule()
{
	EditorListCmd.Reset();
	TickDriver.Reset();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FAsyncFlowEditorModule, AsyncFlowEditor)
