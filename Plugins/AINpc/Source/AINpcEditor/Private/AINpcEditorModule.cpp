#include "Modules/ModuleManager.h"
#include "AINpcExampleAssetGenerator.h"
#include "Framework/Commands/Commands.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "LevelEditor.h"

class FAINpcEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
		TSharedPtr<FExtender> MenuExtender = MakeShareable(new FExtender());
		MenuExtender->AddMenuExtension("Help", EExtensionHook::After, nullptr,
			FMenuExtensionDelegate::CreateRaw(this, &FAINpcEditorModule::AddMenuEntry));
		LevelEditorModule.GetMenuExtensibilityManager()->AddExtender(MenuExtender);
	}

	virtual void ShutdownModule() override {}

private:
	void AddMenuEntry(FMenuBuilder& MenuBuilder)
	{
		MenuBuilder.AddMenuEntry(
			FText::FromString("Generate AI NPC Examples"),
			FText::FromString("Generate example Blueprint assets (BP_ExampleNpc, DA_ExamplePersona)"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(this, &FAINpcEditorModule::OnGenerateExamples))
		);
	}

	void OnGenerateExamples()
	{
		FString Message;
		bool bSuccess = UAINpcExampleAssetGenerator::GenerateExampleAssets(Message);

		FNotificationInfo Info(FText::FromString(Message));
		Info.ExpireDuration = 5.0f;
		Info.bUseLargeFont = false;
		Info.bUseSuccessFailIcons = true;

		if (bSuccess)
		{
			UE_LOG(LogTemp, Log, TEXT("AI NPC: %s"), *Message);
			FSlateNotificationManager::Get().AddNotification(Info)->SetCompletionState(SNotificationItem::CS_Success);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("AI NPC: %s"), *Message);
			FSlateNotificationManager::Get().AddNotification(Info)->SetCompletionState(SNotificationItem::CS_Fail);
		}
	}
};

IMPLEMENT_MODULE(FAINpcEditorModule, AINpcEditor)
