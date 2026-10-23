#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InfrastructureVisualizationActor.generated.h"

class AAgentStation;
class ASidewalkSegment;

/**
 * Debug overlays for queues, congestion, and path utilization.
 * Read-only — does not affect simulation behavior.
 */
UCLASS()
class THESIS_API AInfrastructureVisualizationActor : public AActor
{
	GENERATED_BODY()

public:
	AInfrastructureVisualizationActor();

protected:
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, Category = "Infrastructure|Visualization")
	bool bDrawQueueIndicators = true;

	UPROPERTY(EditAnywhere, Category = "Infrastructure|Visualization")
	bool bDrawCongestionOverlays = true;

	UPROPERTY(EditAnywhere, Category = "Infrastructure|Visualization")
	bool bDrawPathUtilization = true;

	UPROPERTY(EditAnywhere, Category = "Infrastructure|Visualization")
	bool bDrawStationUtilization = true;

	/** On-screen legend clarifying V&V overlay meanings. */
	UPROPERTY(EditAnywhere, Category = "Infrastructure|Visualization")
	bool bDrawVerificationLegend = true;

	UPROPERTY(EditAnywhere, Category = "Infrastructure|Visualization")
	float OverlayDurationSeconds = 0.f;

	/** Refresh interval for cached station/path actor lists (seconds). */
	UPROPERTY(EditAnywhere, Category = "Infrastructure|Visualization", meta = (ClampMin = "0.25"))
	float ActorCacheRefreshSeconds = 1.f;

	void DrawQueueIndicators() const;
	void DrawCongestionOverlays() const;
	void DrawPathUtilization() const;
	void DrawStationUtilization() const;
	void DrawVerificationLegend() const;

	void RefreshActorCacheIfNeeded(float DeltaSeconds);

	UPROPERTY()
	TArray<TWeakObjectPtr<AAgentStation>> CachedStations;

	UPROPERTY()
	TArray<TWeakObjectPtr<ASidewalkSegment>> CachedSegments;

	float ActorCacheTimer = 0.f;
};
