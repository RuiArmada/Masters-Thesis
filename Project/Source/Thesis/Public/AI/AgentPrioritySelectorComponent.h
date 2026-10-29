#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AgentPrioritySelectorComponent.generated.h"

class ABoidsAB;

UENUM(BlueprintType)
enum class EAgentPriorityLayer : uint8
{
	PanicEmergency UMETA(DisplayName = "Panic / Emergency"),
	CriticalNeeds UMETA(DisplayName = "Critical Needs"),
	CrimeResponse UMETA(DisplayName = "Crime Response"),
	Leadership UMETA(DisplayName = "Leadership"),
	ScheduledTasks UMETA(DisplayName = "Scheduled Tasks"),
	IdleFlock UMETA(DisplayName = "Idle / Flock")
};

/**
 * Priority layer for panic, needs, crime, leadership, schedule, and flock behavior.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class THESIS_API UAgentPrioritySelectorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAgentPrioritySelectorComponent();

	/** Defer schedule commands while critical needs are unmet. */
	UPROPERTY(EditAnywhere, Category = "Priority")
	bool bDeferScheduleWhenCriticalNeeds = true;

	/** Issue StartMoveTo for critical food/home needs. */
	UPROPERTY(EditAnywhere, Category = "Priority")
	bool bApplyCriticalNeedMovement = true;

	/** Issue StartMoveTo for police pursuit / robber flee / civilian avoid. */
	UPROPERTY(EditAnywhere, Category = "Priority")
	bool bApplyCrimeMovement = true;

	/** Issue StartMoveTo for active followers when leadership is enabled. */
	UPROPERTY(EditAnywhere, Category = "Priority")
	bool bApplyLeadershipMovement = true;

	EAgentPriorityLayer EvaluateLayer(ABoidsAB* Boid) const;

	void TickDecisionLayer(float DeltaTime, ABoidsAB* Boid);

	/** Wrapper for ScheduleManager — skip task execution this tick when true. */
	bool ShouldDeferScheduleCommands(ABoidsAB* Boid) const;

	EAgentPriorityLayer GetLastEvaluatedLayer() const { return LastEvaluatedLayer; }
	const FString& GetLastEvaluationReason() const { return LastEvaluationReason; }

protected:
	mutable EAgentPriorityLayer LastEvaluatedLayer = EAgentPriorityLayer::IdleFlock;
	mutable FString LastEvaluationReason;

	float DebugLogThrottle = 0.f;

	/** DUL-only: mill near home after schedule ends so agents do not freeze in place. */
	float DailyLifeIdleMillTimer = 0.f;
	float DailyLifeIdleMillInterval = 7.f;

	void UpdateDebugLog(ABoidsAB* Boid) const;
	void ApplyLeadershipMovement(float DeltaTime, ABoidsAB* Boid);
	void ApplyCrimeMovement(float DeltaTime, ABoidsAB* Boid);
	void ApplyDailyLifeIdleMill(float DeltaTime, ABoidsAB* Boid);
};
