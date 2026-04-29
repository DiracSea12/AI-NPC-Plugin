#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FVerifierHostModule : public IModuleInterface {};

IMPLEMENT_PRIMARY_GAME_MODULE(FVerifierHostModule, VerifierHost, "VerifierHost");
