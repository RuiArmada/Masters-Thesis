#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/BillboardComponent.h"
#include "Kismet/KismetMathLibrary.h"
#include "FlockController.generated.h"

class ABoidsAB;
class UBillboardComponent;

UCLASS()
class THESIS_API AFlockController : public AActor
{
	GENERATED_BODY()

public:
	AFlockController();

protected:
	virtual void BeginPlay() override;

#pragma region Components

protected:
	UPROPERTY(VisibleAnywhere, Category = "Flock Controller | Components")
	UBillboardComponent *Billboard;

#pragma endregion

#pragma region Flock

protected:
	TArray<ABoidsAB *> BoidInFlock;

public:
	void AddBoidToTheFlock(ABoidsAB *Boid);
	void RemoveBoidFromTheFlock(ABoidsAB *Boid);

#pragma endregion

#pragma region Movement

protected:
	UPROPERTY(EditAnywhere, Category = "Flock Controller | Movement", meta = (ClampMin = "0.0", ClampMax = "1000.0"))
	float MaxSpeed;

	UPROPERTY(EditAnywhere, Category = "Flock Controller | Movement", meta = (ClampMin = "0.0", ClampMax = "1000.0"))
	float MinSpeed;

public:
	UFUNCTION(BlueprintCallable, Category = "Flock Controller | Movement")
	inline float GetMaxSpeed() const { return MaxSpeed; }

	UFUNCTION(BlueprintCallable, Category = "Flock Controller | Movement")
	inline float GetMinSpeed() const { return MinSpeed; }

	UFUNCTION(BlueprintCallable, Category = "Flock Controller | Movement")
	void SetMaxSpeed(float NewMaxSpeed);

	UFUNCTION(BlueprintCallable, Category = "Flock Controller | Movement")
	void SetMinSpeed(float NewMinSpeed);

#pragma endregion

#pragma region Steering

protected:
	UPROPERTY(EditAnywhere, Category = "Flock Controller | Steering")
	float AlignmentWeight;

	UPROPERTY(EditAnywhere, Category = "Flock Controller | Steering")
	float SeparationWeight;

	UPROPERTY(EditAnywhere, Category = "Flock Controller | Steering")
	float CohesionWeight;

	UPROPERTY(EditAnywhere, Category = "Flock Controller | Steering")
	float AvoidanceWeight;

	/** Blend flock forces into scheduled MoveTo / path follow. Off = nav-only MoveTo. */
	UPROPERTY(EditAnywhere, Category = "Flock Controller | Steering|Move Blend")
	bool bEnableSteeringWhileMoving = true;

	/** How strongly A* / waypoint progress dominates (0 = flock only, 1 = nav only). */
	UPROPERTY(EditAnywhere, Category = "Flock Controller | Steering|Move Blend", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float NavProgressWeight = 0.75f;

	/** Strength of normalized flock steering while moving. */
	UPROPERTY(EditAnywhere, Category = "Flock Controller | Steering|Move Blend", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MovingSteeringWeight = 0.35f;

	/** Legacy damp; kept for editor compatibility (path blend is lateral-only). */
	UPROPERTY(EditAnywhere, Category = "Flock Controller | Steering|Move Blend", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AntiProgressDamp = 0.65f;

	/** Max heading deviation from path progress while MoveTo blending (degrees). */
	UPROPERTY(EditAnywhere, Category = "Flock Controller | Steering|Move Blend", meta = (ClampMin = "1.0", ClampMax = "35.0"))
	float MaxSteerDeviationDegrees = 18.f;

	/** Snap back onto NavMesh if farther than this (cm). */
	UPROPERTY(EditAnywhere, Category = "Flock Controller | Steering|Move Blend", meta = (ClampMin = "20.0"))
	float NavMeshSnapDistance = 50.f;

	/** Minimum seconds between NavMesh snaps. */
	UPROPERTY(EditAnywhere, Category = "Flock Controller | Steering|Move Blend", meta = (ClampMin = "0.25"))
	float NavMeshSnapCooldownSeconds = 1.0f;

	/** Look-ahead along the current path segment (cm). */
	UPROPERTY(EditAnywhere, Category = "Flock Controller | Steering|Move Blend", meta = (ClampMin = "40.0"))
	float PathLookAheadDistance = 140.f;

	/** Forward/side wall probe distance (cm). */
	UPROPERTY(EditAnywhere, Category = "Flock Controller | Steering|Move Blend", meta = (ClampMin = "40.0"))
	float WallProbeDistance = 110.f;

	/** Preferred minimum distance from side walls while path following (cm). */
	UPROPERTY(EditAnywhere, Category = "Flock Controller | Steering|Move Blend", meta = (ClampMin = "30.0"))
	float WallClearanceDistance = 95.f;

	/** Bias strength away from a too-close side wall (0 = off, 1 = strong). */
	UPROPERTY(EditAnywhere, Category = "Flock Controller | Steering|Move Blend", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WallClearanceStrength = 0.45f;

public:
	UFUNCTION(BlueprintCallable, Category = "Flock Controller | Steering")
	inline float GetAlignmentWeight() const { return AlignmentWeight; }

	UFUNCTION(BlueprintCallable, Category = "Flock Controller | Steering")
	inline float GetSeparationWeight() const { return SeparationWeight; }

	UFUNCTION(BlueprintCallable, Category = "Flock Controller | Steering")
	inline float GetCohesionWeight() const { return CohesionWeight; }

	UFUNCTION(BlueprintCallable, Category = "Flock Controller | Steering")
	inline float GetAvoidanceWeight() const { return AvoidanceWeight; }

	UFUNCTION(BlueprintCallable, Category = "Flock Controller | Steering|Move Blend")
	inline bool IsSteeringWhileMovingEnabled() const { return bEnableSteeringWhileMoving; }

	UFUNCTION(BlueprintCallable, Category = "Flock Controller | Steering|Move Blend")
	inline float GetMovingSteeringWeight() const { return MovingSteeringWeight; }

	UFUNCTION(BlueprintCallable, Category = "Flock Controller | Steering|Move Blend")
	inline float GetMaxSteerDeviationDegrees() const { return MaxSteerDeviationDegrees; }

	UFUNCTION(BlueprintCallable, Category = "Flock Controller | Steering|Move Blend")
	inline float GetNavMeshSnapDistance() const { return NavMeshSnapDistance; }

	UFUNCTION(BlueprintCallable, Category = "Flock Controller | Steering|Move Blend")
	inline float GetNavMeshSnapCooldownSeconds() const { return NavMeshSnapCooldownSeconds; }

	UFUNCTION(BlueprintCallable, Category = "Flock Controller | Steering|Move Blend")
	inline float GetPathLookAheadDistance() const { return PathLookAheadDistance; }

	UFUNCTION(BlueprintCallable, Category = "Flock Controller | Steering|Move Blend")
	inline float GetWallProbeDistance() const { return WallProbeDistance; }

	UFUNCTION(BlueprintCallable, Category = "Flock Controller | Steering|Move Blend")
	inline float GetWallClearanceDistance() const { return WallClearanceDistance; }

	UFUNCTION(BlueprintCallable, Category = "Flock Controller | Steering|Move Blend")
	inline float GetWallClearanceStrength() const { return WallClearanceStrength; }

	UFUNCTION(BlueprintCallable, Category = "Flock Controller | Steering")
	void SetAlignmentWeight(float NewAlignmentWeight);

	UFUNCTION(BlueprintCallable, Category = "Flock Controller | Steering")
	void SetSeparationWeight(float NewSeparationWeight);

	UFUNCTION(BlueprintCallable, Category = "Flock Controller | Steering")
	void SetCohesionWeight(float NewCohesionWeight);

	UFUNCTION(BlueprintCallable, Category = "Flock Controller | Steering")
	void SetAvoidanceWeight(float NewAvoidanceWeight);

#pragma endregion

#pragma region Perception

protected:
	UPROPERTY(EditAnywhere, Category = "Flock Controller | Perception", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float SeparationFOV;

	UPROPERTY(EditAnywhere, Category = "Flock Controller | Perception", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float AlignmentFOV;

	UPROPERTY(EditAnywhere, Category = "Flock Controller | Perception", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float CohesionFOV;

public:
	UFUNCTION(BlueprintCallable, Category = "Flock Controller | Perception")
	inline float GetSeparationFOV() const { return SeparationFOV; }

	UFUNCTION(BlueprintCallable, Category = "Flock Controller | Perception")
	inline float GetAlignmentFOV() const { return AlignmentFOV; }

	UFUNCTION(BlueprintCallable, Category = "Flock Controller | Perception")
	inline float GetCohesionFOV() const { return CohesionFOV; }

	UFUNCTION(BlueprintCallable, Category = "Flock Controller | Perception")
	void SetSeparationFOV(float NewSeparationFOV);

	UFUNCTION(BlueprintCallable, Category = "Flock Controller | Perception")
	void SetAlignmentFOV(float NewAlignmentFOV);

	UFUNCTION(BlueprintCallable, Category = "Flock Controller | Perception")
	void SetCohesionFOV(float NewCohesionFOV);

#pragma endregion

#pragma region Collision

protected:
	UPROPERTY(EditAnywhere, Category = "Flock Controller | Collision", meta = (ClampMin = "0", ClampMax = "1000"))
	int32 NumSonar;

	// Evenly distribute sonar directions on a sphere.
	const float GoldenRatio = (1.0 + FMath::Sqrt(5.0)) / 2.0;

	UPROPERTY(EditAnywhere, Category = "Flock Controller | Collision", meta = (ClampMin = "0.0"))
	float SonarRange;

	TArray<FVector> SonarDirections;

	void BuildCollisionSonars();

public:
	UFUNCTION(BlueprintCallable, Category = "Flock Controller | Collision")
	inline int32 GetNumSonar() const { return NumSonar; }

	UFUNCTION(BlueprintCallable, Category = "Flock Controller | Collision")
	inline float GetSonarRange() const { return SonarRange; }

	UFUNCTION(BlueprintCallable, Category = "Flock Controller | Collision")
	inline TArray<FVector> GetSonarDir() { return SonarDirections; }

	UFUNCTION(BlueprintCallable, Category = "Flock Controller | Collision")
	void SetNumSonar(int32 NewNumSonar);

	UFUNCTION(BlueprintCallable, Category = "Flock Controller | Collision")
	void SetSonarRange(float NewSonarRange);

#pragma endregion

#pragma region Rules_Behaviors

public:

	float ArrivalThreshold = 300.0f;

	UPROPERTY(EditAnywhere, Category = "Flocking Settings")
    float GoToWeight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flock")
    float Acceleration = 5.0f;

	UFUNCTION(BlueprintCallable, Category = "Agent-based Boids | Flock Controller")
    inline float GetGoToWeight() const { return GoToWeight; }

#pragma endregion
};
