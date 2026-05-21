#include "Test/AINpcTestCharacter.h"
#include "Animation/AnimInstance.h"
#include "Components/AINpcComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const TCHAR* StandardMannequinMeshPath = TEXT("/Game/Mannequin/Character/Mesh/SK_Mannequin.SK_Mannequin");
	const TCHAR* StandardMannequinAnimBlueprintPath = TEXT("/Game/Mannequin/Animations/ThirdPerson_AnimBP");
}

AAINpcTestCharacter::AAINpcTestCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->InitCapsuleSize(42.0f, 96.0f);
	}

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->MaxWalkSpeed = VisualActionMoveSpeed;
		Movement->bOrientRotationToMovement = true;
		Movement->RotationRate = FRotator(0.0f, 360.0f, 0.0f);
	}

	USkeletalMeshComponent* const MeshComponent = GetMesh();
	if (MeshComponent)
	{
		MeshComponent->SetRelativeLocation(FVector(0.0f, 0.0f, -96.0f));
		MeshComponent->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MeshComponent->SetVisibility(true);
		MeshComponent->SetHiddenInGame(false);
	}

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> MannequinMeshFinder(StandardMannequinMeshPath);
	bVisualMeshLoaded = MannequinMeshFinder.Succeeded() && MeshComponent;
	if (bVisualMeshLoaded)
	{
		MeshComponent->SetSkeletalMesh(MannequinMeshFinder.Object);
		UE_LOG(LogTemp, Warning, TEXT("=== UE template Mannequin skeletal mesh loaded: %s ==="), StandardMannequinMeshPath);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("=== FAILED to load UE template Mannequin skeletal mesh: %s ==="), StandardMannequinMeshPath);
	}

	static ConstructorHelpers::FClassFinder<UAnimInstance> MannequinAnimFinder(StandardMannequinAnimBlueprintPath);
	bVisualAnimLoaded = MannequinAnimFinder.Succeeded() && MeshComponent;
	if (bVisualAnimLoaded)
	{
		MeshComponent->SetAnimInstanceClass(MannequinAnimFinder.Class);
		UE_LOG(LogTemp, Warning, TEXT("=== UE template Mannequin animation blueprint loaded: %s ==="), StandardMannequinAnimBlueprintPath);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("=== FAILED to load UE template Mannequin animation blueprint: %s ==="), StandardMannequinAnimBlueprintPath);
	}

	NpcComponent = CreateDefaultSubobject<UAINpcComponent>(TEXT("NpcComponent"));
	NpcComponent->bAutoCreateNpcController = false;
}

void AAINpcTestCharacter::BeginPlay()
{
	Super::BeginPlay();

	USkeletalMeshComponent* const MeshComponent = GetMesh();
	if (MeshComponent)
	{
		MeshComponent->SetVisibility(true);
		MeshComponent->SetHiddenInGame(false);
	}

	const bool bHasMesh = MeshComponent && MeshComponent->GetSkeletalMeshAsset();
	UE_LOG(LogTemp, Warning, TEXT("=== UE template Mannequin NPC Character ready. SkeletalMesh=%s AnimClass=%s ObserverControlled=false ==="),
		bHasMesh ? *MeshComponent->GetSkeletalMeshAsset()->GetName() : TEXT("MISSING"),
		MeshComponent && MeshComponent->GetAnimClass() ? *MeshComponent->GetAnimClass()->GetName() : TEXT("MISSING"));

	DrawDebugString(GetWorld(), GetActorLocation() + FVector(0, 0, 230),
		TEXT("UE TEMPLATE MANNEQUIN AINPC"), nullptr, FColor::White, 30.0f, true);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 30.0f, HasValidVisualMeshAndAnimation() ? FColor::Red : FColor::Yellow,
			FString::Printf(TEXT("UE template Mannequin NPC spawned at %s, SkeletalMesh=%s, Anim=%s, separate from observer pawn"),
				*GetActorLocation().ToString(),
				bHasMesh ? *MeshComponent->GetSkeletalMeshAsset()->GetName() : TEXT("MISSING"),
				MeshComponent && MeshComponent->GetAnimClass() ? *MeshComponent->GetAnimClass()->GetName() : TEXT("MISSING")));
	}
}

void AAINpcTestCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bVisualActionMoveActive && !bVisualActionTargetReached)
	{
		const FVector CurrentLocation = GetActorLocation();
		const FVector NextLocation = FMath::VInterpConstantTo(CurrentLocation, VisualActionTargetLocation, DeltaTime, VisualActionMoveSpeed);
		SetActorLocation(NextLocation);

		const FVector ToTarget = VisualActionTargetLocation - NextLocation;
		if (!ToTarget.IsNearlyZero())
		{
			SetActorRotation(FVector(ToTarget.X, ToTarget.Y, 0.0f).Rotation());
		}

		if (FVector::DistSquared2D(NextLocation, VisualActionTargetLocation) <= FMath::Square(VisualActionAcceptanceDistance))
		{
			bVisualActionMoveActive = false;
			bVisualActionTargetReached = true;
		}
	}

	const FColor SphereColor = bVisualActionTargetReached ? FColor::Green : (bVisualActionMoveActive ? FColor::Yellow : FColor::Red);
	DrawDebugSphere(GetWorld(), GetActorLocation() + FVector(0, 0, 220), 16.0f, 12, SphereColor, false, 0.0f);
	DrawDebugString(GetWorld(), GetActorLocation() + FVector(0, 0, 245), TEXT("UE TEMPLATE MANNEQUIN AINPC"), nullptr, FColor::White, 0.0f, true);
	if (!VisibleStateText.IsEmpty())
	{
		DrawDebugString(GetWorld(), GetActorLocation() + FVector(0, 0, 275), FString::Printf(TEXT("NPC State: %s"), *VisibleStateText), nullptr, FColor::Cyan, 0.0f, true);
	}
	if (!VisibleDelayMaskingText.IsEmpty())
	{
		DrawDebugString(GetWorld(), GetActorLocation() + FVector(0, 0, 305), FString::Printf(TEXT("Delay masking debug: %s"), *VisibleDelayMaskingText), nullptr, FColor::Orange, 0.0f, true);
	}
	if (!VisibleDialogueText.IsEmpty())
	{
		DrawDebugString(GetWorld(), GetActorLocation() + FVector(0, 0, 335), FString::Printf(TEXT("NPC says: %s"), *VisibleDialogueText.Left(140)), nullptr, FColor::Yellow, 0.0f, true);
	}

	if (bVisualActionMoveActive || bVisualActionTargetReached)
	{
		DrawDebugSphere(GetWorld(), VisualActionTargetLocation, 40.0f, 16, bVisualActionTargetReached ? FColor::Green : FColor::Yellow, false, 0.0f, 0, 2.0f);
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
}

void AAINpcTestCharacter::SetVisibleDialogueText(const FString& InText)
{
	VisibleDialogueText = InText;
}

void AAINpcTestCharacter::SetVisibleDelayMaskingText(const FString& InText)
{
	VisibleDelayMaskingText = InText;
}

void AAINpcTestCharacter::SetVisibleStateText(const FString& InText)
{
	VisibleStateText = InText;
}

bool AAINpcTestCharacter::HasReachedVisualActionTarget() const
{
	return bVisualActionTargetReached;
}

bool AAINpcTestCharacter::IsVisualActionMoveActive() const
{
	return bVisualActionMoveActive;
}

bool AAINpcTestCharacter::HasValidVisualMeshAndAnimation() const
{
	USkeletalMeshComponent* MeshComponent = GetMesh();
	return bVisualMeshLoaded && bVisualAnimLoaded && MeshComponent && MeshComponent->GetSkeletalMeshAsset() && MeshComponent->GetAnimClass();
}

float AAINpcTestCharacter::GetVisualActionTargetDistance() const
{
	return FVector::Dist(GetActorLocation(), VisualActionTargetLocation);
}

const FString& AAINpcTestCharacter::GetVisualActionTargetId() const
{
	return VisualActionTargetId;
}
