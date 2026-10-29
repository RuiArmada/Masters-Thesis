#include "FlockController.h"

#pragma region UE_Basics

AFlockController::AFlockController()
{
	PrimaryActorTick.bCanEverTick = false;

	Billboard = CreateDefaultSubobject<UBillboardComponent>(TEXT("FlockController Billboard"));
	RootComponent = Billboard;

	MaxSpeed = 150.0f;
	MinSpeed = 100.0f;

	AlignmentWeight = 200.0f;
	SeparationWeight = 2000.0f;
	CohesionWeight = 5.0f;
	AvoidanceWeight = 10000.0f;

	SeparationFOV = -1.0f;
	AlignmentFOV = 0.5f;
	CohesionFOV = -0.5f;

	NumSonar = 100;
	SonarRange = 300.0f;

	BuildCollisionSonars();
}

void AFlockController::BeginPlay()
{
	Super::BeginPlay();
}

#pragma endregion

#pragma region Flock

void AFlockController::AddBoidToTheFlock(ABoidsAB *Boid)
{
	if (Boid)
	{
		BoidInFlock.AddUnique(Boid);
	}

	UE_LOG(LogTemp, Log, TEXT("Boid added to the flock"));
}

void AFlockController::RemoveBoidFromTheFlock(ABoidsAB *Boid)
{
	if (Boid)
	{
		BoidInFlock.Remove(Boid);
	}

	UE_LOG(LogTemp, Log, TEXT("Boid removed from the flock"));
}

#pragma endregion

#pragma region Movement

void AFlockController::SetMaxSpeed(float newSpeed)
{
	if (newSpeed < 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("Max speed cannot be negative, value ignored"));
		return;
	}

	MaxSpeed = newSpeed;

	if (MinSpeed > MaxSpeed)
	{
		SetMinSpeed(MaxSpeed);
	}
}

void AFlockController::SetMinSpeed(float newSpeed)
{
	if (newSpeed < 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("Min speed cannot be negative, value ignored"));
		return;
	}

	MinSpeed = newSpeed;

	if (MaxSpeed < MinSpeed)
	{
		SetMaxSpeed(MinSpeed);
	}
}

#pragma endregion

#pragma region Steering

void AFlockController::SetAlignmentWeight(float newWeight)
{
	AlignmentWeight = newWeight;
}

void AFlockController::SetSeparationWeight(float newWeight)
{
	SeparationWeight = newWeight;
}

void AFlockController::SetCohesionWeight(float newWeight)
{
	CohesionWeight = newWeight;
}

void AFlockController::SetAvoidanceWeight(float newWeight)
{
	AvoidanceWeight = newWeight;
}

#pragma endregion

#pragma region Perception

void AFlockController::SetSeparationFOV(float newFOV)
{
	if (newFOV < -1.0f || newFOV > 1.0f)
	{
		UE_LOG(LogTemp, Warning, TEXT("Separation FOV must be between -1 and 1, value ignored"));
		return;
	}

	SeparationFOV = newFOV;
}

void AFlockController::SetAlignmentFOV(float newFOV)
{
	if (newFOV < -1.0f || newFOV > 1.0f)
	{
		UE_LOG(LogTemp, Warning, TEXT("Alignment FOV must be between -1 and 1, value ignored"));
		return;
	}

	AlignmentFOV = newFOV;
}

void AFlockController::SetCohesionFOV(float newFOV)
{
	if (newFOV < -1.0f || newFOV > 1.0f)
	{
		UE_LOG(LogTemp, Warning, TEXT("Cohesion FOV must be between -1 and 1, value ignored"));
		return;
	}

	CohesionFOV = newFOV;
}

#pragma endregion

#pragma region Collision

void AFlockController::SetNumSonar(int32 newNumSonar)
{
	if (newNumSonar < 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("Number of sonar cannot be negative, value ignored"));
		return;
	}

	NumSonar = newNumSonar;
	BuildCollisionSonars();
}

void AFlockController::SetSonarRange(float newRange)
{
	if (newRange < 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("Sonar range cannot be negative, value ignored"));
		return;
	}

	SonarRange = newRange;
	BuildCollisionSonars();
}

void AFlockController::BuildCollisionSonars()
{
	SonarDirections.Empty();

	float theta;
	float phi;
	FVector SonarDir;

	for (int i = 0; i < NumSonar; ++i)
	{
		theta = 2.0f * UKismetMathLibrary::GetPI() * GoldenRatio * i;
		phi = FMath::Acos(1.0f - (2.0f * float(i) / NumSonar));

		SonarDir.X = FMath::Cos(theta) * FMath::Sin(phi);
		SonarDir.Y = FMath::Sin(theta) * FMath::Sin(phi);
		SonarDir.Z = FMath::Cos(phi);

		SonarDirections.Emplace(SonarDir);
	}
}

#pragma endregion
