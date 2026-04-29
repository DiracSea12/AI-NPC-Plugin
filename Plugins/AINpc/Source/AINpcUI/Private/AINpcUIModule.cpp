#include "Modules/ModuleManager.h"
#include "AINpcUILog.h"

DEFINE_LOG_CATEGORY(LogAINpcUI);

class FAINpcUIModule : public FDefaultModuleImpl
{
public:
	virtual void StartupModule() override
	{
		UE_LOG(LogAINpcUI, Log, TEXT("AINpcUI Dialogue system ready"));
	}
};

IMPLEMENT_MODULE(FAINpcUIModule, AINpcUI)
