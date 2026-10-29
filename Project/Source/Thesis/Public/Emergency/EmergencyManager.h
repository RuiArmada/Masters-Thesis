#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Simulation/SimulationTypes.h"
#include "EmergencyManager.generated.h"

class AIncidentManager;
class USimulationFeatureRegistry;
class USimulationAnalytics;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnEvacuationStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnEvacuationEnded);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEvacuationTimerUpdated, float, TimeRemainingSeconds);

/**
 * Orchestrates global emergency evacuation. Composes with AIncidentManager — does not replace it.
 */
UCLASS()
class THESIS_API AEmergencyManager : public AActor
{
	GENERATED_BODY()

public:
	AEmergencyManager();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** UI / Blueprint entry point — starts hybrid global evacuation. */
	UFUNCTION(BlueprintCallable, Category = "Emergency")
	void TriggerGlobalEvacuation();

	/** Ends evacuation via existing AIncidentManager::EndAllIncidents() cleanup path. */
	UFUNCTION(BlueprintCallable, Category = "Emergency")
	void EndEvacuation();

	UFUNCTION(BlueprintCallable, Category = "Emergency")
	bool IsEvacuationActive() const { return CurrentPhase == EEvacuationPhase::Evacuating; }

	UFUNCTION(BlueprintCallable, Category = "Emergency")
	EEvacuationPhase GetCurrentPhase() const { return CurrentPhase; }

	UFUNCTION(BlueprintCallable, Category = "Emergency")
	float GetEvacuationTimeRemaining() const { return TimerRemainingSeconds; }

	UFUNCTION(BlueprintCallable, Category = "Emergency")
	bool IsEvacuationTimerActive() const { return bEvacuationTimerActive; }

	UPROPERTY(BlueprintAssignable, Category = "Emergency|Events")
	FOnEvacuationStarted OnEvacuationStarted;

	UPROPERTY(BlueprintAssignable, Category = "Emergency|Events")
	FOnEvacuationEnded OnEvacuationEnded;

	UPROPERTY(BlueprintAssignable, Category = "Emergency|Events")
	FOnEvacuationTimerUpdated OnEvacuationTimerUpdated;

protected:
	UPROPERTY(EditAnywhere, Category = "Emergency")
	TObjectPtr<AIncidentManager> IncidentManagerRef;

	/** World location used for the global incident (defaults to this actor's location). */
	UPROPERTY(EditAnywhere, Category = "Emergency")
	FVector EvacuationIncidentLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, Category = "Emergency")
	EEvacuationPhase CurrentPhase = EEvacuationPhase::Idle;

	void ResolveIncidentManager();
	FVector GetEvacuationCenter() const;
	void EnsureAllAgentsEvacuate(const FVector& IncidentLocation);
	void StartEvacuationTimer();
	void ClearEvacuationTimer();
	void ProcessEvacuationCasualties();
	void OnEvacuationTimerExpired();

	FTimerHandle EvacuationTimerHandle;
	bool bEvacuationTimerActive = false;
	bool bTimerExpired = false;
	float TimerRemainingSeconds = 0.f;
	double EvacuationDeadlineWorldTime = 0.0;
};
