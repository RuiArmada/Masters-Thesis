#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "StressTestSubsystem.generated.h"

/**
 * Opt-in scaling test, active only when the game is launched with -StressAgents=N.
 *
 * On the first tick (after the level's agents exist), clones existing agents (same Blueprint class) until the level holds N agents,
 * then records frame, game-thread, render-thread and GPU times over a fixed window and writes
 * Saved/Analytics/Stress_<Map>_<N>.csv. Optional: -StressWarmup=<s> (default 15), -StressSeconds=<s> (default 60),
 * -StressQuit (exit when the window ends).
 */
UCLASS()
class THESIS_API UStressTestSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return bActive && !bFinished; }

private:
	void SpawnClones(UWorld& InWorld);
	void FinishAndExport();

	bool bActive = false;
	bool bFinished = false;
	bool bQuitWhenDone = false;
	bool bClonesSpawned = false;
	int32 TargetAgents = 0;
	int32 SpawnedAgents = 0;
	float WarmupSeconds = 15.f;
	float MeasureSeconds = 60.f;
	double StartRealTime = 0.0;

	TArray<float> FrameMs;
	TArray<float> GameThreadMs;
	TArray<float> RenderThreadMs;
	TArray<float> GpuMs;
	uint64 PeakUsedPhysicalBytes = 0;
};
