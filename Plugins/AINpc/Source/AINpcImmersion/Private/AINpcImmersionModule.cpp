#include "AINpcImmersionModule.h"
#include "AINpcImmersionLog.h"

#define LOCTEXT_NAMESPACE "FAINpcImmersionModule"

void FAINpcImmersionModule::StartupModule()
{
	UE_LOG(LogAINpcImmersion, Log, TEXT("AINpcImmersion module started"));
}

void FAINpcImmersionModule::ShutdownModule()
{
	UE_LOG(LogAINpcImmersion, Log, TEXT("AINpcImmersion module shutdown"));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FAINpcImmersionModule, AINpcImmersion)
