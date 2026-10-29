#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AI/AgentCrimeRoleComponent.h"
#include "CopsAndRobbersDemoManager.generated.h"

class ABoidsAB;

USTRUCT(BlueprintType)
struct FCrimeRoleAssignment
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crime")
	FName AgentId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crime")
	ECrimeRole Role = ECrimeRole::Civilian;
};

/**
 * Editor-driven demo wiring for Police / Robber / Civilian roles.
 * Only applies when enableCopsAndRobbers is true.
 */
UCLASS()
class THESIS_API ACopsAndRobbersDemoManager : public AActor
{
	GENERATED_BODY()

public:
	ACopsAndRobbersDemoManager();

	UPROPERTY(EditAnywhere, Category = "Crime|Demo")
	bool bAutoAssignOnBeginPlay = true;

	UPROPERTY(EditAnywhere, Category = "Crime|Demo")
	TArray<FCrimeRoleAssignment> RoleAssignments;

	UPROPERTY(EditAnywhere, Category = "Crime|Demo")
	bool bExportAnalyticsOnEndPlay = true;

	UFUNCTION(BlueprintCallable, Category = "Crime|Demo")
	void ApplyAssignments();

	UFUNCTION(BlueprintCallable, Category = "Crime|Demo")
	void ExportAnalytics();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	FTimerHandle InitDelayHandle;
	void DelayedInitialize();

	ABoidsAB* FindBoidById(FName AgentId) const;
};
