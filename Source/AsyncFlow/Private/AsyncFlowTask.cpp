// AsyncFlowTask.cpp — Thread-local state management
#include "AsyncFlowTask.h"

namespace AsyncFlow::Private
{

static thread_local FAsyncFlowState* GCurrentFlowState = nullptr;

FAsyncFlowState* GetCurrentFlowState()
{
	return GCurrentFlowState;
}

void SetCurrentFlowState(FAsyncFlowState* State)
{
	GCurrentFlowState = State;
}

} // namespace AsyncFlow::Private

