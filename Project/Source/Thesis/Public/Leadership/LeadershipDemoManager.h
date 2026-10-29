#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LeadershipDemoManager.generated.h"

class ABoidsAB;

USTRUCT(BlueprintType)
struct FLeadershipGroupAssignment
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Leadership")
	FName LeaderAgentId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Leadership")
	TArray<FName> FollowerAgentIds;

	/** Optional label for logs (Family, Dog, Police, etc.). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Leadership")
	FString GroupLabel;
};

/**
 * Editor-driven demo wiring for leader/follower groups.
 * Only applies assignments when enableLeadership is true.
 */
UCLASS()
class THESIS_API ALeadershipDemoManager : public AActor
{
	GENERATED_BODY()

public:
	ALeadershipDemoManager();

	UPROPERTY(EditAnywhere, Category = "Leadership|Demo")
	bool bAutoAssignOnBeginPlay = true;

	UPROPERTY(EditAnywhere, Category = "Leadership|Demo")
	TArray<FLeadershipGroupAssignment> Groups;

	UFUNCTION(BlueprintCallable, Category = "Leadership|Demo")
	void ApplyAssignments();

protected:
	virtual void BeginPlay() override;

	FTimerHandle InitDelayHandle;
	void DelayedInitialize();

	ABoidsAB* FindBoidById(FName AgentId) const;
};
