#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PedestrianInteractionComponent.generated.h"

class ABoidsAB;

/**
 * Casual pedestrian greetings and brief pauses.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class THESIS_API UPedestrianInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPedestrianInteractionComponent();

	void TickInteractions(float DeltaTime, ABoidsAB* OwnerBoid);

	/** Brief pause for schedule trigger or casual encounter. */
	UFUNCTION(BlueprintCallable, Category = "Daily Life|Interaction")
	void BeginBriefPause(float DurationSeconds);

	UFUNCTION(BlueprintCallable, Category = "Daily Life|Interaction")
	bool IsPausedForInteraction() const { return bPausedForInteraction; }

	UFUNCTION(BlueprintCallable, Category = "Daily Life|Interaction")
	void TriggerCasualGreet(ABoidsAB* Other);

	/** Ends an in-progress brief pause (e.g. before a scheduled commute). */
	UFUNCTION(BlueprintCallable, Category = "Daily Life|Interaction")
	void CancelBriefPause();

protected:
	bool bPausedForInteraction = false;
	float PauseRemainingSeconds = 0.f;
	float GreetCooldownSeconds = 12.f;
	float PartnerGreetCooldownSeconds = 30.f;
	float LastGreetWorldTime = -1000.f;

	TMap<TWeakObjectPtr<ABoidsAB>, float> LastGreetByPartner;

	/** When true, resume schedule path following after a social pause (commute was active). */
	bool bResumeMovementAfterPause = false;

	void EndBriefPause();
};
