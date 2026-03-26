// AsyncFlowGASModule.h
#pragma once

#include "Modules/ModuleManager.h"

class FAsyncFlowGASModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

