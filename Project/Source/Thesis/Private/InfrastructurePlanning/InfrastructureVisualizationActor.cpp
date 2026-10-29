#include "InfrastructurePlanning/InfrastructureVisualizationActor.h"
#include "AgentStation.h"
#include "DailyUrbanLife/SidewalkSegment.h"
#include "Simulation/CongestionAnalyzer.h"
#include "Simulation/SimulationAnalytics.h"
#include "Simulation/SimulationFeatureRegistry.h"
#include "DrawDebugHelpers.h"
#include "Kismet/GameplayStatics.h"

AInfrastructureVisualizationActor::AInfrastructureVisualizationActor()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AInfrastructureVisualizationActor::RefreshActorCacheIfNeeded(float DeltaSeconds)
{
	ActorCacheTimer -= DeltaSeconds;
	if (ActorCacheTimer > 0.f && (CachedStations.Num() > 0 || CachedSegments.Num() > 0))
	{
		return;
	}

	ActorCacheTimer = ActorCacheRefreshSeconds;
	CachedStations.Reset();
	CachedSegments.Reset();

	TArray<AActor*> Stations;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AAgentStation::StaticClass(), Stations);
	for (AActor* Actor : Stations)
	{
		if (AAgentStation* Station = Cast<AAgentStation>(Actor))
		{
			CachedStations.Add(Station);
		}
	}

	TArray<AActor*> Segments;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ASidewalkSegment::StaticClass(), Segments);
	for (AActor* Actor : Segments)
	{
		if (ASidewalkSegment* Segment = Cast<ASidewalkSegment>(Actor))
		{
			CachedSegments.Add(Segment);
		}
	}
}

void AInfrastructureVisualizationActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!GetWorld())
	{
		return;
	}

	USimulationFeatureRegistry* Registry = GetWorld()->GetSubsystem<USimulationFeatureRegistry>();
	if (!Registry || !Registry->IsFeatureEnabled(ESimulationFeature::InfrastructureAnalysis))
	{
		return;
	}

	RefreshActorCacheIfNeeded(DeltaSeconds);

	if (bDrawQueueIndicators)
	{
		DrawQueueIndicators();
	}
	if (bDrawStationUtilization)
	{
		DrawStationUtilization();
	}
	if (bDrawCongestionOverlays)
	{
		DrawCongestionOverlays();
	}
	if (bDrawPathUtilization)
	{
		DrawPathUtilization();
	}
	if (bDrawVerificationLegend)
	{
		DrawVerificationLegend();
	}
}

void AInfrastructureVisualizationActor::DrawQueueIndicators() const
{
	for (const TWeakObjectPtr<AAgentStation>& Ptr : CachedStations)
	{
		AAgentStation* Station = Ptr.Get();
		if (!Station)
		{
			continue;
		}

		const FVector Loc = Station->GetActorLocation() + FVector(0.f, 0.f, 180.f);
		const FString Label = FString::Printf(TEXT("Q:%d  Use:%.0f%%"),
			Station->GetQueueLength(),
			Station->GetUtilization() * 100.f);
		DrawDebugString(GetWorld(), Loc, Label, nullptr, FColor::Yellow, 0.f, true, 1.1f);
	}
}

void AInfrastructureVisualizationActor::DrawStationUtilization() const
{
	UCongestionAnalyzer* Analyzer = GetWorld()->GetSubsystem<UCongestionAnalyzer>();
	if (!Analyzer)
	{
		return;
	}

	for (const TWeakObjectPtr<AAgentStation>& Ptr : CachedStations)
	{
		AAgentStation* Station = Ptr.Get();
		if (!Station)
		{
			continue;
		}

		const float Util = Analyzer->GetStationCongestionLevel(Station);
		const FColor Color = FColor::MakeRedToGreenColorFromScalar(1.f - Util);
		DrawDebugSphere(GetWorld(), Station->GetActorLocation(), 80.f, 12, Color, false, 0.f, 0, 2.f);
	}
}

void AInfrastructureVisualizationActor::DrawCongestionOverlays() const
{
	UCongestionAnalyzer* Analyzer = GetWorld()->GetSubsystem<UCongestionAnalyzer>();
	if (!Analyzer)
	{
		return;
	}

	for (const TWeakObjectPtr<ASidewalkSegment>& Ptr : CachedSegments)
	{
		ASidewalkSegment* Segment = Ptr.Get();
		if (!Segment)
		{
			continue;
		}

		const float Congestion = Analyzer->GetPathSegmentCongestionLevel(Segment);
		const FColor Color = FColor::MakeRedToGreenColorFromScalar(1.f - Congestion);
		const FVector Extent = FVector(200.f, 50.f, 30.f);
		DrawDebugBox(GetWorld(), Segment->GetActorLocation(), Extent, Segment->GetActorQuat(),
			Color, false, 0.f, 0, 3.f);
	}
}

void AInfrastructureVisualizationActor::DrawPathUtilization() const
{
	USimulationAnalytics* Analytics = GetWorld()->GetSubsystem<USimulationAnalytics>();
	if (!Analytics)
	{
		return;
	}

	const FInfrastructureAnalyticsSnapshot Snap = Analytics->GetInfrastructureSnapshot();
	int32 MaxHits = 1;
	for (const FInfrastructurePathUtilizationEntry& Entry : Snap.PathUtilization)
	{
		MaxHits = FMath::Max(MaxHits, Entry.HitCount);
	}

	for (const TWeakObjectPtr<ASidewalkSegment>& Ptr : CachedSegments)
	{
		ASidewalkSegment* Segment = Ptr.Get();
		if (!Segment)
		{
			continue;
		}

		int32 Hits = 0;
		for (const FInfrastructurePathUtilizationEntry& Entry : Snap.PathUtilization)
		{
			if (Entry.SegmentName == Segment->GetSegmentName())
			{
				Hits = Entry.HitCount;
				break;
			}
		}

		const float Ratio = static_cast<float>(Hits) / static_cast<float>(MaxHits);
		const FVector Loc = Segment->GetActorLocation() + FVector(0.f, 0.f, 120.f);
		DrawDebugString(GetWorld(), Loc, FString::Printf(TEXT("%s: %d"), *Segment->GetSegmentName(), Hits),
			nullptr, FColor::Cyan, 0.f, true, 1.f);
		DrawDebugLine(GetWorld(), Loc, Loc + FVector(0.f, 0.f, 60.f + Ratio * 120.f),
			FColor::Cyan, false, 0.f, 0, 4.f);
	}
}

void AInfrastructureVisualizationActor::DrawVerificationLegend() const
{
	APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	if (!PC || !PC->PlayerCameraManager)
	{
		return;
	}

	const FVector CamLoc = PC->PlayerCameraManager->GetCameraLocation();
	const FVector CamFwd = PC->PlayerCameraManager->GetActorForwardVector();
	const FVector Anchor = CamLoc + CamFwd * 600.f + FVector(0.f, 0.f, 80.f);

	DrawDebugString(GetWorld(), Anchor,
		TEXT("V&V: Yellow=Queue growth | Spheres=Station util | Boxes=Congestion | Cyan=Path hits"),
		nullptr, FColor::White, 0.f, true, 1.15f);
}
