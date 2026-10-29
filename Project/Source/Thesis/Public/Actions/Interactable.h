#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Interactable.generated.h"

class ABoidsAB;

UINTERFACE(MinimalAPI, Blueprintable)
class UInteractable : public UInterface
{
	GENERATED_BODY()
};

/** Minimal interaction contract for schedule InteractAction targets. */
class THESIS_API IInteractable
{
	GENERATED_BODY()

public:
	virtual bool CanInteract(ABoidsAB* Agent) const = 0;
	virtual bool Interact(ABoidsAB* Agent) = 0;

	/** Approach location for navigation (prefer an InteractionPoint, not the actor pivot). */
	virtual FVector GetInteractionLocation() const = 0;
};
