// AsyncFlowTask.cpp — Thread-local state management and FCancellationGuard
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

namespace AsyncFlow
{

FCancellationGuard::FCancellationGuard()
{
	State = Private::GetCurrentFlowState();
	if (State)
	{
		State->CancellationGuardDepth.fetch_add(1, std::memory_order_acq_rel);
	}
}

FCancellationGuard::~FCancellationGuard()
{
	if (State)
	{
		State->CancellationGuardDepth.fetch_sub(1, std::memory_order_acq_rel);
	}
}

} // namespace AsyncFlow
