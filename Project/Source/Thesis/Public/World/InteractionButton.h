#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Actions/Interactable.h"
#include "InteractionButton.generated.h"

class AMallEntranceGate;
class USceneComponent;

/**
 * Schedule Interact target. Tag the actor (e.g. EntranceButton2) for XML lookup.
 * On Interact, opens the linked MallEntranceGate exactly once by default.
 */
UCLASS()
class THESIS_API AInteractionButton : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	AInteractionButton();

	// IInteractable
	virtual bool CanInteract(ABoidsAB* Agent) const override;
	virtual bool Interact(ABoidsAB* Agent) override;
	virtual FVector GetInteractionLocation() const override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interaction")
	TObjectPtr<USceneComponent> SceneRoot;

	/** Approach point for MoveTo / GoTo (prefer NavMesh-reachable floor height). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interaction")
	TObjectPtr<USceneComponent> InteractionPoint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	TObjectPtr<AMallEntranceGate> LinkedEntrance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	bool bOpenEntranceOnInteract = true;

	/** When true, Interact succeeds only once (prevents spam Open). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	bool bSingleUse = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interaction")
	bool bHasInteracted = false;
};
