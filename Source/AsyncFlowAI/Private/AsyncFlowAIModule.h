// AsyncFlowAIModule.h
#pragma once

#include "Modules/ModuleManager.h"

class FAsyncFlowAIModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

