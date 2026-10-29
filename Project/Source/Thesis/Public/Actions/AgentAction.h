#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "AgentAction.generated.h"

class ABoidsAB;

UENUM(BlueprintType)
enum class EAgentActionState : uint8
{
	NotStarted UMETA(DisplayName = "Not Started"),
	Running    UMETA(DisplayName = "Running"),
	Completed  UMETA(DisplayName = "Completed"),
	Failed     UMETA(DisplayName = "Failed")
};

/**
 * Thin scheduled-behaviour wrapper. Physical movement/stations stay on ABoidsAB.
 * Owned by the agent (Outer) so GC keeps it alive while running.
 */
UCLASS(Abstract)
class THESIS_API UAgentAction : public UObject
{
	GENERATED_BODY()

public:
	EAgentActionState GetState() const { return State; }
	bool IsTerminal() const
	{
		return State == EAgentActionState::Completed || State == EAgentActionState::Failed;
	}

	virtual void Start();
	virtual void Tick(float DeltaSeconds);
	virtual void Cancel();

	ABoidsAB* GetAgent() const { return Agent.Get(); }

protected:
	void Complete();
	void Fail(const FString& Reason);

	virtual void OnStart() {}
	virtual void OnTick(float DeltaSeconds) {}
	virtual void OnCompleted() {}
	virtual void OnFailed(const FString& Reason) {}

	void SetAgent(ABoidsAB* InAgent);

	UPROPERTY()
	TWeakObjectPtr<ABoidsAB> Agent;

	UPROPERTY()
	EAgentActionState State = EAgentActionState::NotStarted;
};
