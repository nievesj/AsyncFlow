// AsyncFlowModule.h
#pragma once

#include "Modules/ModuleManager.h"

class FAsyncFlowModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

