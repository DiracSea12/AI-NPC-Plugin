#include "Test/AINpcTestCharacter.h"
#include "Components/AINpcComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"

AAINpcTestCharacter::AAINpcTestCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	PlaceholderMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderMesh"));
	PlaceholderMesh->SetupAttachment(GetRootComponent());
	PlaceholderMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 100.0f));
	PlaceholderMesh->SetRelativeScale3D(FVector(5.0f, 5.0f, 10.0f));
	PlaceholderMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(
		TEXT("/Engine/BasicShapes/Cube"));
	if (CubeFinder.Succeeded())
	{
		PlaceholderMesh->SetStaticMesh(CubeFinder.Object);
		UE_LOG(LogTemp, Warning, TEXT("=== Cube mesh loaded successfully ==="));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("=== FAILED to load Cube mesh! ==="));
	}

	// Use basic unlit material - no lighting needed
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatFinder(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial"));
	if (MatFinder.Succeeded())
	{
		PlaceholderMesh->SetMaterial(0, MatFinder.Object.Get());
		UE_LOG(LogTemp, Warning, TEXT("=== BasicShapeMaterial loaded successfully ==="));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("=== FAILED to load BasicShapeMaterial! ==="));
	}

	AutoPossessPlayer = EAutoReceiveInput::Player0;

	NpcComponent = CreateDefaultSubobject<UAINpcComponent>(TEXT("NpcComponent"));
	NpcComponent->bAutoCreateNpcController = false;
}

void AAINpcTestCharacter::BeginPlay()
{
	Super::BeginPlay();

	FDateTime Now = FDateTime::Now();
	UE_LOG(LogTemp, Warning, TEXT("=== AAINpcTestCharacter::BeginPlay START [%s] ==="),
		*Now.ToString(TEXT("%Y-%m-%d %H:%M:%S")));

	// Force mesh visible
	if (PlaceholderMesh)
	{
		PlaceholderMesh->SetVisibility(true);
		PlaceholderMesh->SetHiddenInGame(false);
		PlaceholderMaterial = PlaceholderMesh->CreateAndSetMaterialInstanceDynamic(0);
		UpdateDebugMaterialColor(FLinearColor::Red);
		UE_LOG(LogTemp, Warning, TEXT("=== Mesh exists: %s, Visible: %d ==="),
			PlaceholderMesh->GetStaticMesh() ? TEXT("YES") : TEXT("NO"),
			PlaceholderMesh->IsVisible());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("=== PlaceholderMesh is NULL! ==="));
	}

	// Giant debug sphere — impossible to miss
	DrawDebugSphere(GetWorld(), GetActorLocation(), 100.0f, 32, FColor::Red, true, 30.0f);
	DrawDebugSphere(GetWorld(), GetActorLocation() + FVector(0, 0, 200), 60.0f, 32, FColor::Yellow, true, 30.0f);
	DrawDebugString(GetWorld(), GetActorLocation() + FVector(0, 0, 300),
		TEXT("AINPC TEST CHARACTER"), nullptr, FColor::White, 30.0f, true);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 30.0f, FColor::Red,
			FString::Printf(TEXT("NPC SPAWNED at %s — Mesh: %s"),
				*GetActorLocation().ToString(),
				PlaceholderMesh->GetStaticMesh() ? TEXT("YES") : TEXT("MISSING")));
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

	// Keep drawing debug sphere every frame so it never disappears
	const FColor SphereColor = bVisualActionTargetReached ? FColor::Green : (bVisualActionMoveActive ? FColor::Yellow : FColor::Red);
	DrawDebugSphere(GetWorld(), GetActorLocation(), 100.0f, 32, SphereColor, false, 0.0f);
	DrawDebugString(GetWorld(), GetActorLocation() + FVector(0, 0, 300),
		TEXT("AINPC TEST NPC"), nullptr, FColor::White, 0.0f, true);

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
	if (!PlaceholderMaterial)
	{
		return;
	}

	PlaceholderMaterial->SetVectorParameterValue(TEXT("Color"), NewColor);
}
