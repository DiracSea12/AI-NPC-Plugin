#include "Test/AINpcTestSmartObjectActor.h"

#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SmartObjectComponent.h"
#include "SmartObjectDefinition.h"

AAINpcTestSmartObjectActor::AAINpcTestSmartObjectActor()
{
	PrimaryActorTick.bCanEverTick = true;

	SmartObjectComponent = CreateDefaultSubobject<USmartObjectComponent>(TEXT("SmartObjectComponent"));
	RootComponent = SmartObjectComponent;

	ObjectMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ObjectMesh"));
	ObjectMesh->SetupAttachment(RootComponent);
	ObjectMesh->SetRelativeScale3D(FVector(1.2f, 1.2f, 2.6f));
	ObjectMesh->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);

	SlotMarkerMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SlotMarkerMesh"));
	SlotMarkerMesh->SetupAttachment(RootComponent);
	SlotMarkerMesh->SetRelativeLocation(SlotOffset + FVector(0.0f, 0.0f, 40.0f));
	SlotMarkerMesh->SetRelativeScale3D(FVector(0.4f, 0.4f, 0.8f));
	SlotMarkerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder"));
	if (CylinderFinder.Succeeded())
	{
		ObjectMesh->SetStaticMesh(CylinderFinder.Object);
		SlotMarkerMesh->SetStaticMesh(CylinderFinder.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial"));
	if (MaterialFinder.Succeeded())
	{
		ObjectMesh->SetMaterial(0, MaterialFinder.Object);
		SlotMarkerMesh->SetMaterial(0, MaterialFinder.Object);
	}
}

void AAINpcTestSmartObjectActor::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	InitializeRuntimeDefinition();
}

void AAINpcTestSmartObjectActor::BeginPlay()
{
	Super::BeginPlay();

	ObjectMeshMaterial = ObjectMesh ? ObjectMesh->CreateAndSetMaterialInstanceDynamic(0) : nullptr;
	SlotMarkerMaterial = SlotMarkerMesh ? SlotMarkerMesh->CreateAndSetMaterialInstanceDynamic(0) : nullptr;
	UpdateMaterialColors();
}

void AAINpcTestSmartObjectActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	DrawDebugSphere(GetWorld(), GetSlotWorldLocation(), 45.0f, 16, bInteractionActive ? ActiveColor : IdleColor, false, 0.0f, 0, 2.0f);
	DrawDebugLine(GetWorld(), GetActorLocation(), GetSlotWorldLocation(), bInteractionActive ? ActiveColor : IdleColor, false, 0.0f, 0, 3.0f);
}

FVector AAINpcTestSmartObjectActor::GetSlotWorldLocation() const
{
	return GetActorTransform().TransformPosition(SlotOffset);
}

void AAINpcTestSmartObjectActor::SetInteractionState(const bool bInInteractionActive)
{
	if (bInteractionActive == bInInteractionActive)
	{
		return;
	}

	bInteractionActive = bInInteractionActive;
	UpdateMaterialColors();

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			-1,
			6.0f,
			bInteractionActive ? FColor::Orange : FColor::Blue,
			bInteractionActive
				? TEXT("SmartObject claimed/used by visual harness.")
				: TEXT("SmartObject released by visual harness."));
	}
}

void AAINpcTestSmartObjectActor::InitializeRuntimeDefinition()
{
	if (RuntimeDefinition || !SmartObjectComponent)
	{
		return;
	}

	RuntimeDefinition = NewObject<USmartObjectDefinition>(this, TEXT("RuntimeSmartObjectDefinition"));
	UAINpcTestSmartObjectBehaviorDefinition* BehaviorDefinition =
		NewObject<UAINpcTestSmartObjectBehaviorDefinition>(RuntimeDefinition, TEXT("RuntimeBehaviorDefinition"));

	FSmartObjectSlotDefinition& Slot = RuntimeDefinition->DebugAddSlot();
	Slot.Offset = FVector3f(SlotOffset);
	Slot.BehaviorDefinitions.Add(BehaviorDefinition);

	SmartObjectComponent->SetDefinition(RuntimeDefinition);
}

void AAINpcTestSmartObjectActor::UpdateMaterialColors()
{
	const FLinearColor Color = bInteractionActive ? FLinearColor(ActiveColor) : FLinearColor(IdleColor);

	if (ObjectMeshMaterial)
	{
		ObjectMeshMaterial->SetVectorParameterValue(TEXT("Color"), Color);
	}

	if (SlotMarkerMaterial)
	{
		SlotMarkerMaterial->SetVectorParameterValue(TEXT("Color"), Color);
	}
}
