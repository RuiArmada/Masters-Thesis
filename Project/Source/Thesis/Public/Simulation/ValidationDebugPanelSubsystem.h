#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ValidationDebugPanelSubsystem.generated.h"

/**
 * Renders the Validation Statistics on-screen debug panel.
 * Collection lives in USimulationAnalytics; this subsystem only samples + draws.
 * Auto-activates when ESimulationFeature::ValidationStats is enabled (Validation/V0*_ maps).
 * Runtime toggle: thesis.ValidationDebugPanel 0|1
 */
UCLASS()
class THESIS_API UValidationDebugPanelSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return true; }
	virtual bool IsTickableInEditor() const override { return false; }

protected:
	float SampleAccumulator = 0.f;
	bool bSessionStarted = false;

	void EnsureSessionStarted();
	void DrawPanel() const;
	bool ShouldShowPanel() const;
};
