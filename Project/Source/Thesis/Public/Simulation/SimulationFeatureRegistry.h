#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Simulation/SimulationTypes.h"
#include "SimulationFeatureRegistry.generated.h"

/**
 * Central feature gate for all simulation modes.
 * Do not scatter feature booleans elsewhere — query this subsystem only.
 */
UCLASS()
class THESIS_API USimulationFeatureRegistry : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	UFUNCTION(BlueprintCallable, Category = "Simulation|Features")
	bool IsFeatureEnabled(ESimulationFeature Feature) const;

	UFUNCTION(BlueprintCallable, Category = "Simulation|Features")
	void SetFeatureEnabled(ESimulationFeature Feature, bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "Simulation|Scenario")
	ESimulationScenario GetCurrentScenario() const { return CurrentScenario; }

	UFUNCTION(BlueprintCallable, Category = "Simulation|Scenario")
	void SetCurrentScenario(ESimulationScenario Scenario) { CurrentScenario = Scenario; }

	UFUNCTION(BlueprintCallable, Category = "Simulation|Evacuation")
	const FEvacuationRoutingSettings& GetEvacuationSettings() const { return EvacuationSettings; }

	void SetEvacuationSettings(const FEvacuationRoutingSettings& Settings) { EvacuationSettings = Settings; }

	UFUNCTION(BlueprintCallable, Category = "Simulation|Daily Urban Life")
	const FDailyUrbanLifeSettings& GetDailyUrbanLifeSettings() const { return DailyUrbanLifeSettings; }

	void SetDailyUrbanLifeSettings(const FDailyUrbanLifeSettings& Settings) { DailyUrbanLifeSettings = Settings; }

	UFUNCTION(BlueprintCallable, Category = "Simulation|Infrastructure Planning")
	const FInfrastructurePlanningSettings& GetInfrastructurePlanningSettings() const { return InfrastructurePlanningSettings; }

	void SetInfrastructurePlanningSettings(const FInfrastructurePlanningSettings& Settings) { InfrastructurePlanningSettings = Settings; }

	UFUNCTION(BlueprintCallable, Category = "Simulation|Leadership")
	const FLeadershipSettings& GetLeadershipSettings() const { return LeadershipSettings; }

	void SetLeadershipSettings(const FLeadershipSettings& Settings) { LeadershipSettings = Settings; }

	UFUNCTION(BlueprintCallable, Category = "Simulation|CopsAndRobbers")
	const FCopsAndRobbersSettings& GetCopsAndRobbersSettings() const { return CopsAndRobbersSettings; }

	void SetCopsAndRobbersSettings(const FCopsAndRobbersSettings& Settings) { CopsAndRobbersSettings = Settings; }

	UFUNCTION(BlueprintCallable, Category = "Simulation|Validation")
	const FValidationAnalyticsSettings& GetValidationAnalyticsSettings() const { return ValidationAnalyticsSettings; }

	void SetValidationAnalyticsSettings(const FValidationAnalyticsSettings& Settings) { ValidationAnalyticsSettings = Settings; }

	UFUNCTION(BlueprintCallable, Category = "Simulation|Clock")
	const FSimulationClockSettings& GetSimulationClockSettings() const { return SimulationClockSettings; }

	void SetSimulationClockSettings(const FSimulationClockSettings& Settings) { SimulationClockSettings = Settings; }

	UFUNCTION(BlueprintCallable, Category = "Simulation|Mode")
	ESimulationMode GetSimulationMode() const { return SimulationMode; }

	void SetSimulationMode(ESimulationMode Mode) { SimulationMode = Mode; }

	UFUNCTION(BlueprintCallable, Category = "Simulation|Mode")
	bool IsFastSimulationMode() const { return SimulationMode == ESimulationMode::Fast; }

	UFUNCTION(BlueprintCallable, Category = "Simulation|Mode")
	bool IsAccurateTimeMode() const { return SimulationMode == ESimulationMode::AccurateTime; }

	/** Visual, Fast, or AccurateTime. */
	UFUNCTION(BlueprintCallable, Category = "Simulation|Mode")
	FString GetSimulationModeLabel() const;

	/**
	 * Clock scale for the active mode (sim seconds per real second).
	 * Visual uses <simulationClock>, Fast uses <fastSimulation>, AccurateTime uses <accurateTime>.
	 */
	UFUNCTION(BlueprintCallable, Category = "Simulation|Mode")
	float GetActiveClockSimulationSpeed() const;

	/**
	 * Engine-timer seconds for an authored duration.
	 * Visual: duration is real seconds (unchanged).
	 * Fast: duration is simulation seconds, so the timer is duration / Fast clock.
	 * AccurateTime: duration stays the Visual real-second number. The timer is scaled so the
	 * simulation clock spends the same time there as Visual (authored × Visual clock speed).
	 */
	UFUNCTION(BlueprintCallable, Category = "Simulation|Mode")
	float ScaleAuthoredDurationToRealSeconds(float AuthoredDuration) const;

	UFUNCTION(BlueprintCallable, Category = "Simulation|Fast")
	const FFastSimulationSettings& GetFastSimulationSettings() const { return FastSimulationSettings; }

	void SetFastSimulationSettings(const FFastSimulationSettings& Settings) { FastSimulationSettings = Settings; }

	UFUNCTION(BlueprintCallable, Category = "Simulation|AccurateTime")
	const FAccurateTimeSettings& GetAccurateTimeSettings() const { return AccurateTimeSettings; }

	void SetAccurateTimeSettings(const FAccurateTimeSettings& Settings) { AccurateTimeSettings = Settings; }

	/** True when the loaded map is a validation scenario (V01–V07 or name contains Validation). */
	UFUNCTION(BlueprintCallable, Category = "Simulation|Validation")
	bool IsValidationMap() const { return bValidationMapActive; }

	UFUNCTION(BlueprintCallable, Category = "Simulation|Config")
	void ReloadConfig();

protected:
	UPROPERTY()
	ESimulationScenario CurrentScenario = ESimulationScenario::None;

	UPROPERTY()
	TMap<ESimulationFeature, bool> FeatureStates;

	UPROPERTY()
	FEvacuationRoutingSettings EvacuationSettings;

	UPROPERTY()
	FDailyUrbanLifeSettings DailyUrbanLifeSettings;

	UPROPERTY()
	FInfrastructurePlanningSettings InfrastructurePlanningSettings;

	UPROPERTY()
	FLeadershipSettings LeadershipSettings;

	UPROPERTY()
	FCopsAndRobbersSettings CopsAndRobbersSettings;

	UPROPERTY()
	FValidationAnalyticsSettings ValidationAnalyticsSettings;

	UPROPERTY()
	FSimulationClockSettings SimulationClockSettings;

	UPROPERTY()
	ESimulationMode SimulationMode = ESimulationMode::Visual;

	UPROPERTY()
	FFastSimulationSettings FastSimulationSettings;

	UPROPERTY()
	FAccurateTimeSettings AccurateTimeSettings;

	bool bValidationMapActive = false;

	void ApplyDefaults();
	bool LoadConfigFromDisk();

	/**
	 * After config load: turn Daily Urban Life off on other use-case maps (MallA/MallB, etc.)
	 * and enable it only on Daily Urban Life maps. Runs before actor BeginPlay.
	 */
	void ApplyMapDrivenFeatureGates();
};
