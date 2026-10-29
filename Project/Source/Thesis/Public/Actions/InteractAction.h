#pragma once

#include "CoreMinimal.h"
#include "Actions/AgentAction.h"
#include "InteractAction.generated.h"

class UGoToAction;
class IInteractable;

/**
 * GoTo an interactable target, then invoke Interact once on arrival.
 * Not mall-specific — any IInteractable actor tagged for schedule lookup.
 */
UCLASS()
class THESIS_API UInteractAction : public UAgentAction
{
	GENERATED_BODY()

public:
	void Configure(ABoidsAB* InAgent, AActor* InTargetActor);

	FString GetTargetDisplayName() const
	{
		return TargetActor.IsValid() ? TargetActor->GetName() : FString(TEXT("none"));
	}

protected:
	virtual void OnStart() override;
	virtual void OnTick(float DeltaSeconds) override;
	virtual void OnFailed(const FString& Reason) override;

	UPROPERTY()
	TWeakObjectPtr<AActor> TargetActor;

	UPROPERTY()
	TObjectPtr<UGoToAction> GoToSubAction;

	bool bInteractionAttempted = false;
};
