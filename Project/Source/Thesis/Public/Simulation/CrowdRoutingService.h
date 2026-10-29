#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Simulation/SimulationTypes.h"
#include "CrowdRoutingService.generated.h"

class ABoidsAB;
class AExitPoint;

/** Crowd routing by ECrowdRoutingMode.
 *  Evacuation scoring is implemented and used by UC1.
 *  Non-evacuation modes fall back to the same evacuation score (no separate stubs).
 */
UCLASS()
class THESIS_API UCrowdRoutingService : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Rank tagged exits (lower score = better). */
	UFUNCTION(BlueprintCallable, Category = "Simulation|Routing")
	TArray<FCrowdRouteScore> RankExits(ABoidsAB* Agent, ECrowdRoutingMode Mode, bool bApplyNearestBias = false) const;

	UFUNCTION(BlueprintCallable, Category = "Simulation|Routing")
	float ScoreExit(ABoidsAB* Agent, AExitPoint* Exit, ECrowdRoutingMode Mode, bool bApplyNearestBias = false) const;

	/** Assign the best ranked exit that still has queue capacity. */
	bool TryAssignBestExit(ABoidsAB* Agent, ECrowdRoutingMode Mode, bool bApplyNearestBias,
	                       AExitPoint*& OutExit, int32& OutSlotIndex) const;

	/** True when another exit scores meaningfully better than CurrentExit. */
	bool ShouldReassignExit(ABoidsAB* Agent, AExitPoint* CurrentExit, ECrowdRoutingMode Mode,
	                        AExitPoint*& OutBetterExit) const;

protected:
	float ScoreExitEvacuation(ABoidsAB* Agent, AExitPoint* Exit, float MaxDistance, bool bApplyNearestBias) const;

	void CollectExitPoints(TArray<AExitPoint*>& OutExits) const;
	float ComputeMaxDistance(const FVector& Origin, const TArray<AExitPoint*>& Exits) const;
};
