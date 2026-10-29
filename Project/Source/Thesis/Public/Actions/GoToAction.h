#pragma once

#include "CoreMinimal.h"
#include "Actions/AgentAction.h"
#include "GoToAction.generated.h"

/**
 * Thin wrapper around existing NotifyScheduleMoveTo / StartMoveTo / path follow.
 * Does not implement its own movement.
 */
UCLASS()
class THESIS_API UGoToAction : public UAgentAction
{
	GENERATED_BODY()

public:
	void Configure(ABoidsAB* InAgent, const FVector& InTarget, const FString& InDestinationTag);

	const FString& GetDestinationTag() const { return DestinationTag; }

protected:
	virtual void OnStart() override;
	virtual void OnTick(float DeltaSeconds) override;
	virtual void OnFailed(const FString& Reason) override;

	UPROPERTY()
	FVector TargetLocation = FVector::ZeroVector;

	UPROPERTY()
	FString DestinationTag;

	UPROPERTY()
	float ArrivalRadius2D = 180.f;

	UPROPERTY()
	float MaxWaitSeconds = 180.f;

	float ElapsedSeconds = 0.f;
	bool bMoveIssued = false;
};
