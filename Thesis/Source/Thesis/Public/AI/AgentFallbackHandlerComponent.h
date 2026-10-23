#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AgentFallbackHandlerComponent.generated.h"

class ABoidsAB;

UENUM()
enum class EAgentFallbackFailureKind : uint8
{
	Pathfinding,
	ScheduleTargetMissing,
	ScheduleAreaMissing,
	AreaClosed,
	StationUnavailable,
	StuckWhileMoving,
	StationQueueTimeout
};

UENUM()
enum class EAgentFallbackResolution : uint8
{
	None,
	Retry,
	AlternateDestination,
	Wait,
	SkipTask,
	Idle,
	GoHome
};

/**
 * Recovery policy for path and schedule failures.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class THESIS_API UAgentFallbackHandlerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAgentFallbackHandlerComponent();

	UPROPERTY(EditAnywhere, Category = "Fallback")
	int32 MaxPathRetries = 3;

	UPROPERTY(EditAnywhere, Category = "Fallback")
	float StuckDistanceThreshold = 15.f;

	UPROPERTY(EditAnywhere, Category = "Fallback")
	float StuckTimeSeconds = 6.f;

	UPROPERTY(EditAnywhere, Category = "Fallback")
	float StationQueueTimeoutSeconds = 50.f;

	UPROPERTY(EditAnywhere, Category = "Fallback")
	float WaitDurationSeconds = 2.f;

	/** Cooldown before PrioritySelector may re-issue need-driven movement after path exhaustion. */
	UPROPERTY(EditAnywhere, Category = "Fallback")
	float NeedMovementSuppressSeconds = 18.f;

	void OnMovementCommandIssued(ABoidsAB* Boid, const FVector& Target);
	void OnPathResult(ABoidsAB* Boid, bool bSuccess, const FVector& AttemptedTarget);
	void NotifyScheduleFailure(ABoidsAB* Boid, EAgentFallbackFailureKind Kind, const FString& Context);
	void OnEnteredStationQueue(ABoidsAB* Boid);
	void OnLeftStationQueue(ABoidsAB* Boid);
	void TickFallback(float DeltaTime, ABoidsAB* Boid);

/** After need-driven path exhaustion, pause re-issuing food/home moves. */
	bool ShouldSuppressNeedMovement() const;

	EAgentFallbackResolution GetLastResolution() const { return LastResolution; }
	const FString& GetLastResolutionNote() const { return LastResolutionNote; }

protected:
	int32 PathFailCount = 0;
	float StuckTimer = 0.f;
	FVector LastStuckSampleLocation = FVector::ZeroVector;
	bool bHasStuckSample = false;
	float QueueWaitTimer = 0.f;
	bool bInStationQueue = false;
	float WaitCountdown = 0.f;
	bool bInWaitState = false;
	FVector LastIssuedTarget = FVector::ZeroVector;
	float NeedMovementSuppressUntil = -1000.f;
	mutable float LastRetryLogTime = -1000.f;
	mutable float LastUc3StuckRetryTime = -1000.f;

	EAgentFallbackResolution LastResolution = EAgentFallbackResolution::None;
	FString LastResolutionNote;

	void ResetTracking();
	void ApplyResolution(ABoidsAB* Boid, EAgentFallbackResolution Res, const FString& Note);
	void LogResolution(ABoidsAB* Boid, EAgentFallbackFailureKind Kind, EAgentFallbackResolution Res, const FString& Note) const;
};
