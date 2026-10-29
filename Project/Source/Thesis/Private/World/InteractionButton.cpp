#include "World/InteractionButton.h"
#include "World/MallEntranceGate.h"
#include "BoidsAB.h"
#include "Components/SceneComponent.h"

AInteractionButton::AInteractionButton()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	InteractionPoint = CreateDefaultSubobject<USceneComponent>(TEXT("InteractionPoint"));
	InteractionPoint->SetupAttachment(SceneRoot);
	InteractionPoint->SetRelativeLocation(FVector(0.f, 0.f, 0.f));
}

bool AInteractionButton::CanInteract(ABoidsAB* Agent) const
{
	if (!Agent)
	{
		return false;
	}

	if (bSingleUse && bHasInteracted)
	{
		return false;
	}

	if (bOpenEntranceOnInteract && !IsValid(LinkedEntrance))
	{
		return false;
	}

	return true;
}

bool AInteractionButton::Interact(ABoidsAB* Agent)
{
	if (!CanInteract(Agent))
	{
		return false;
	}

	bHasInteracted = true;

	if (bOpenEntranceOnInteract && IsValid(LinkedEntrance))
	{
		LinkedEntrance->Open();
		UE_LOG(LogTemp, Log,
			TEXT("[InteractAction] %s interacted with %s → [Gate] %s OPEN"),
			*Agent->AgentId.ToString(),
			*GetName(),
			*LinkedEntrance->GetGateDebugLabel());
	}

	return true;
}

FVector AInteractionButton::GetInteractionLocation() const
{
	if (InteractionPoint)
	{
		return InteractionPoint->GetComponentLocation();
	}
	return GetActorLocation();
}
