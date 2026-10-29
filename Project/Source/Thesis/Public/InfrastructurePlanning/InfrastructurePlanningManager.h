#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InfrastructurePlanningManager.generated.h"

class ABoidsAB;
class AAgentStation;
class AInfrastructureVisualizationActor;

/** Shopping mall layout comparison bootstrap. */
UCLASS()
class THESIS_API AInfrastructurePlanningManager : public AActor
{
	GENERATED_BODY()

public:
	AInfrastructurePlanningManager();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditAnywhere, Category = "Infrastructure Planning")
	bool bAutoInitializeOnBeginPlay = true;

	FTimerHandle InitDelayHandle;
	FTimerHandle SamplingTimerHandle;
	FTimerHandle EndRunPollTimerHandle;

	/** Total sim minutes on the ScheduleManager clock when the run started. */
	double RunStartAccumulatedSimMinutes = 0.0;

	bool bRunFinalized = false;

	UPROPERTY()
	TObjectPtr<AInfrastructureVisualizationActor> VisualizationActor;

	UFUNCTION(BlueprintCallable, Category = "Infrastructure Planning")
	void InitializeInfrastructurePlanning();

	UFUNCTION(BlueprintCallable, Category = "Infrastructure Planning")
	void FinalizeInfrastructurePlanning();

	void CopyScheduleToSaved(const FString& RelativePath) const;
	void ConfigureScheduleManager() const;
	void BootstrapDailyLifeAgents();
	void WarnAboutUnscheduledAgents() const;
	void ApplyLayoutVariantSettings();
	void ValidateMapSetup() const;
	void StartSampling();
	void CheckSimDurationEnd();
	void ExportSessionResults() const;

	/**
	 * When AutoSelectFromMap is enabled, detect MallA/MallB from the world map name
	 * and set layoutVariant + scheduleFile + default service capacity.
	 */
	void ApplyMapDrivenInfrastructureSettings();

	UFUNCTION()
	void HandleSamplingTick();
};
