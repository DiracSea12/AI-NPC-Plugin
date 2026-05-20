#include "Test/AINpcTestCharacter.h"
#include "Components/AINpcComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	void ConfigureHumanoidPart(
		UStaticMeshComponent& Part,
		USceneComponent& Parent,
		const FVector& RelativeLocation,
		const FVector& RelativeScale)
	{
		Part.SetupAttachment(&Parent);
		Part.SetRelativeLocation(RelativeLocation);
		Part.SetRelativeScale3D(RelativeScale);
		Part.SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part.SetVisibility(true);
		Part.SetHiddenInGame(false);
	}
}

AAINpcTestCharacter::AAINpcTestCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->InitCapsuleSize(42.0f, 96.0f);
	}

	TorsoMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HumanoidTorso"));
	HeadMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HumanoidHead"));
	LeftArmMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HumanoidLeftArm"));
	RightArmMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HumanoidRightArm"));
	LeftLegMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HumanoidLeftLeg"));
	RightLegMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HumanoidRightLeg"));

	ConfigureHumanoidPart(*TorsoMesh, *GetRootComponent(), FVector(0.0f, 0.0f, 95.0f), FVector(0.48f, 0.34f, 0.95f));
	ConfigureHumanoidPart(*HeadMesh, *GetRootComponent(), FVector(0.0f, 0.0f, 165.0f), FVector(0.32f, 0.32f, 0.32f));
	ConfigureHumanoidPart(*LeftArmMesh, *GetRootComponent(), FVector(0.0f, -48.0f, 90.0f), FVector(0.13f, 0.13f, 0.74f));
	ConfigureHumanoidPart(*RightArmMesh, *GetRootComponent(), FVector(0.0f, 48.0f, 90.0f), FVector(0.13f, 0.13f, 0.74f));
	ConfigureHumanoidPart(*LeftLegMesh, *GetRootComponent(), FVector(0.0f, -18.0f, 30.0f), FVector(0.16f, 0.16f, 0.62f));
	ConfigureHumanoidPart(*RightLegMesh, *GetRootComponent(), FVector(0.0f, 18.0f, 30.0f), FVector(0.16f, 0.16f, 0.62f));

	HumanoidBodyParts = {
		TorsoMesh,
		HeadMesh,
		LeftArmMesh,
		RightArmMesh,
		LeftLegMesh,
		RightLegMesh
	};

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere"));
	if (CylinderFinder.Succeeded())
	{
		TorsoMesh->SetStaticMesh(CylinderFinder.Object);
		LeftArmMesh->SetStaticMesh(CylinderFinder.Object);
		RightArmMesh->SetStaticMesh(CylinderFinder.Object);
		LeftLegMesh->SetStaticMesh(CylinderFinder.Object);
		RightLegMesh->SetStaticMesh(CylinderFinder.Object);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("=== FAILED to load Cylinder mesh for humanoid NPC body! ==="));
	}

	if (SphereFinder.Succeeded())
	{
		HeadMesh->SetStaticMesh(SphereFinder.Object);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("=== FAILED to load Sphere mesh for humanoid NPC head! ==="));
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatFinder(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial"));
	if (MatFinder.Succeeded())
	{
		for (UStaticMeshComponent* BodyPart : HumanoidBodyParts)
		{
			BodyPart->SetMaterial(0, MatFinder.Object.Get());
		}
		UE_LOG(LogTemp, Warning, TEXT("=== Humanoid NPC body meshes/material loaded successfully ==="));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("=== FAILED to load BasicShapeMaterial! ==="));
	}

	NpcComponent = CreateDefaultSubobject<UAINpcComponent>(TEXT("NpcComponent"));
	NpcComponent->bAutoCreateNpcController = false;
}

void AAINpcTestCharacter::BeginPlay()
{
	Super::BeginPlay();

	FDateTime Now = FDateTime::Now();
	UE_LOG(LogTemp, Warning, TEXT("=== AAINpcTestCharacter::BeginPlay START [%s] ==="),
		*Now.ToString(TEXT("%Y-%m-%d %H:%M:%S")));

	HumanoidMaterials.Reset();
	bool bHasMissingBodyPart = false;
	for (UStaticMeshComponent* BodyPart : HumanoidBodyParts)
	{
		if (!BodyPart)
		{
			bHasMissingBodyPart = true;
			continue;
		}

		BodyPart->SetVisibility(true);
		BodyPart->SetHiddenInGame(false);
		HumanoidMaterials.Add(BodyPart->CreateAndSetMaterialInstanceDynamic(0));
	}

	UpdateDebugMaterialColor(FLinearColor::Red);
	if (bHasMissingBodyPart)
	{
		UE_LOG(LogTemp, Error, TEXT("=== Humanoid NPC has missing body part components! ==="));
	}
	UE_LOG(LogTemp, Warning, TEXT("=== Humanoid NPC Character ready. BodyParts=%d ObserverControlled=false ==="),
		HumanoidBodyParts.Num());

	DrawDebugString(GetWorld(), GetActorLocation() + FVector(0, 0, 230),
		TEXT("HUMANOID AINPC TEST CHARACTER"), nullptr, FColor::White, 30.0f, true);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 30.0f, FColor::Red,
			FString::Printf(TEXT("Humanoid NPC Character spawned at %s, BodyParts=%d, separate from observer pawn"),
				*GetActorLocation().ToString(),
				HumanoidBodyParts.Num()));
	}
}

void AAINpcTestCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bVisualActionMoveActive && !bVisualActionTargetReached)
	{
		const FVector CurrentLocation = GetActorLocation();
		const FVector NextLocation = FMath::VInterpConstantTo(
			CurrentLocation,
			VisualActionTargetLocation,
			DeltaTime,
			VisualActionMoveSpeed);
		SetActorLocation(NextLocation);

		const FVector ToTarget = VisualActionTargetLocation - NextLocation;
		if (!ToTarget.IsNearlyZero())
		{
			SetActorRotation(ToTarget.Rotation());
		}

		if (FVector::DistSquared(NextLocation, VisualActionTargetLocation)
			<= FMath::Square(VisualActionAcceptanceDistance))
		{
			bVisualActionMoveActive = false;
			bVisualActionTargetReached = true;
			UpdateDebugMaterialColor(FLinearColor::Green);
		}
	}

	const FColor SphereColor = bVisualActionTargetReached ? FColor::Green : (bVisualActionMoveActive ? FColor::Yellow : FColor::Red);
	DrawDebugSphere(GetWorld(), GetActorLocation() + FVector(0, 0, 220), 16.0f, 12, SphereColor, false, 0.0f);
	DrawDebugString(GetWorld(), GetActorLocation() + FVector(0, 0, 245),
		TEXT("HUMANOID AINPC TEST NPC"), nullptr, FColor::White, 0.0f, true);

	if (bVisualActionMoveActive || bVisualActionTargetReached)
	{
		DrawDebugSphere(
			GetWorld(),
			VisualActionTargetLocation,
			40.0f,
			16,
			bVisualActionTargetReached ? FColor::Green : FColor::Yellow,
			false,
			0.0f,
			0,
			2.0f);
		DrawDebugLine(GetWorld(), GetActorLocation(), VisualActionTargetLocation, FColor::Yellow, false, 0.0f, 0, 2.0f);
	}
}

void AAINpcTestCharacter::BeginVisualActionMove(const FTransform& ClaimedSlotTransform, const FString& InTargetId)
{
	VisualActionTargetLocation = ClaimedSlotTransform.GetLocation();
	if (const UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		VisualActionTargetLocation.Z += Capsule->GetScaledCapsuleHalfHeight();
	}
	VisualActionTargetId = InTargetId;
	bVisualActionMoveActive = true;
	bVisualActionTargetReached = false;
	UpdateDebugMaterialColor(FLinearColor::Yellow);
}

bool AAINpcTestCharacter::HasReachedVisualActionTarget() const
{
	return bVisualActionTargetReached;
}

bool AAINpcTestCharacter::IsVisualActionMoveActive() const
{
	return bVisualActionMoveActive;
}

float AAINpcTestCharacter::GetVisualActionTargetDistance() const
{
	return FVector::Dist(GetActorLocation(), VisualActionTargetLocation);
}

const FString& AAINpcTestCharacter::GetVisualActionTargetId() const
{
	return VisualActionTargetId;
}

void AAINpcTestCharacter::UpdateDebugMaterialColor(const FLinearColor& NewColor)
{
	for (UMaterialInstanceDynamic* Material : HumanoidMaterials)
	{
		if (Material)
		{
			Material->SetVectorParameterValue(TEXT("Color"), NewColor);
		}
	}
}
