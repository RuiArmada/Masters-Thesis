#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Simulation/SimulationTypes.h"
#include "CongestionAnalyzer.generated.h"

class AExitPoint;
class AAgentStation;
class ASidewalkSegment;

/** Read-only congestion metrics for exits, stations, and path segments. */
UCLASS()
class THESIS_API UCongestionAnalyzer : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Simulation|Congestion")
	int32 GetQueueDepth(AExitPoint* Exit) const;

	UFUNCTION(BlueprintCallable, Category = "Simulation|Congestion")
	int32 GetMaxCapacity(AExitPoint* Exit) const;

	UFUNCTION(BlueprintCallable, Category = "Simulation|Congestion")
	float GetUtilization(AExitPoint* Exit) const;

	UFUNCTION(BlueprintCallable, Category = "Simulation|Congestion")
	float GetEstimatedWaitSeconds(AExitPoint* Exit) const;

	UFUNCTION(BlueprintCallable, Category = "Simulation|Congestion")
	float GetCongestionLevel(AExitPoint* Exit) const;

	UFUNCTION(BlueprintCallable, Category = "Simulation|Congestion")
	bool IsExitFull(AExitPoint* Exit) const;

	UFUNCTION(BlueprintCallable, Category = "Simulation|Congestion|Station")
	int32 GetStationQueueDepth(AAgentStation* Station) const;

	UFUNCTION(BlueprintCallable, Category = "Simulation|Congestion|Station")
	int32 GetStationMaxCapacity(AAgentStation* Station) const;

	UFUNCTION(BlueprintCallable, Category = "Simulation|Congestion|Station")
	float GetStationUtilization(AAgentStation* Station) const;

	UFUNCTION(BlueprintCallable, Category = "Simulation|Congestion|Station")
	float GetStationCongestionLevel(AAgentStation* Station) const;

	/** Agents currently overlapping a sidewalk segment volume (proxy congestion). */
	UFUNCTION(BlueprintCallable, Category = "Simulation|Congestion|Path")
	int32 GetPathSegmentAgentCount(ASidewalkSegment* Segment) const;

	UFUNCTION(BlueprintCallable, Category = "Simulation|Congestion|Path")
	float GetPathSegmentCongestionLevel(ASidewalkSegment* Segment, int32 NominalCapacity = 4) const;

	/** Stations with highest utilization right now. */
	UFUNCTION(BlueprintCallable, Category = "Simulation|Congestion")
	TArray<AAgentStation*> GetStationCongestionHotspots(int32 MaxResults = 5) const;
};
