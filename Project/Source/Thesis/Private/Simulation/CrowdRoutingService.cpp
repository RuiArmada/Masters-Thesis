#include "Simulation/CrowdRoutingService.h"
#include "Simulation/CongestionAnalyzer.h"
#include "Simulation/SimulationFeatureRegistry.h"
#include "BoidsAB.h"
#include "World/ExitPoint.h"
#include "Kismet/GameplayStatics.h"

void UCrowdRoutingService::CollectExitPoints(TArray<AExitPoint*>& OutExits) const
{
	OutExits.Empty();
	if (!GetWorld())
	{
		return;
	}

	TArray<AActor*> Tagged;
	UGameplayStatics::GetAllActorsWithTag(GetWorld(), FName(TEXT("ExitPoint")), Tagged);
	for (AActor* Actor : Tagged)
	{
		if (AExitPoint* Exit = Cast<AExitPoint>(Actor))
		{
			OutExits.Add(Exit);
		}
	}
}

float UCrowdRoutingService::ComputeMaxDistance(const FVector& Origin, const TArray<AExitPoint*>& Exits) const
{
	float MaxDist = 1.f;
	for (AExitPoint* Exit : Exits)
	{
		if (Exit)
		{
			MaxDist = FMath::Max(MaxDist, FVector::Dist(Origin, Exit->GetActorLocation()));
		}
	}
	return MaxDist;
}

float UCrowdRoutingService::ScoreExitEvacuation(ABoidsAB* Agent, AExitPoint* Exit, float MaxDistance,
                                                bool bApplyNearestBias) const
{
	if (!Agent || !Exit || !GetWorld())
	{
		return TNumericLimits<float>::Max();
	}

	const USimulationFeatureRegistry* Registry = GetWorld()->GetSubsystem<USimulationFeatureRegistry>();
	const FEvacuationRoutingSettings Settings = Registry ? Registry->GetEvacuationSettings() : FEvacuationRoutingSettings();

	UCongestionAnalyzer* Analyzer = GetWorld()->GetSubsystem<UCongestionAnalyzer>();
	const float Dist = FVector::Dist(Agent->GetActorLocation(), Exit->GetActorLocation());
	const float DNorm = Dist / FMath::Max(MaxDistance, 1.f);
	const float Congestion = Analyzer ? Analyzer->GetCongestionLevel(Exit) : 0.f;
	const float PenaltyFull = Analyzer && Analyzer->IsExitFull(Exit) ? 1.f : 0.f;

	float Score = Settings.DistanceWeight * DNorm
	            + Settings.CongestionWeight * Congestion
	            + PenaltyFull;

	if (bApplyNearestBias && Settings.bPreferNearestExitInitially)
	{
		const float AgentBiasScale = Agent->GetEvacuationNearestExitBiasScale();
		Score *= (1.f + Settings.NearestExitBias * DNorm * AgentBiasScale);
	}

	return Score;
}

float UCrowdRoutingService::ScoreExit(ABoidsAB* Agent, AExitPoint* Exit, ECrowdRoutingMode Mode,
                                      bool bApplyNearestBias) const
{
	TArray<AExitPoint*> Exits;
	CollectExitPoints(Exits);
	const float MaxDist = Agent ? ComputeMaxDistance(Agent->GetActorLocation(), Exits) : 1.f;

	// Live callers only use Evacuation; other enum values keep the same scoring path.
	(void)Mode;
	return ScoreExitEvacuation(Agent, Exit, MaxDist, bApplyNearestBias);
}

TArray<FCrowdRouteScore> UCrowdRoutingService::RankExits(ABoidsAB* Agent, ECrowdRoutingMode Mode,
                                                         bool bApplyNearestBias) const
{
	TArray<FCrowdRouteScore> Ranked;
	if (!Agent)
	{
		return Ranked;
	}

	TArray<AExitPoint*> Exits;
	CollectExitPoints(Exits);
	const float MaxDist = ComputeMaxDistance(Agent->GetActorLocation(), Exits);

	for (AExitPoint* Exit : Exits)
	{
		FCrowdRouteScore Entry;
		Entry.Exit = Exit;
		Entry.DistanceScore = FVector::Dist(Agent->GetActorLocation(), Exit->GetActorLocation()) / FMath::Max(MaxDist, 1.f);

		UCongestionAnalyzer* Analyzer = GetWorld() ? GetWorld()->GetSubsystem<UCongestionAnalyzer>() : nullptr;
		Entry.CongestionScore = Analyzer ? Analyzer->GetCongestionLevel(Exit) : 0.f;
		Entry.CompositeScore = ScoreExit(Agent, Exit, Mode, bApplyNearestBias);
		Ranked.Add(Entry);
	}

	Ranked.Sort([](const FCrowdRouteScore& A, const FCrowdRouteScore& B) {
		return A.CompositeScore < B.CompositeScore;
	});

	return Ranked;
}

bool UCrowdRoutingService::TryAssignBestExit(ABoidsAB* Agent, ECrowdRoutingMode Mode, bool bApplyNearestBias,
                                             AExitPoint*& OutExit, int32& OutSlotIndex) const
{
	OutExit = nullptr;
	OutSlotIndex = INDEX_NONE;

	if (!Agent)
	{
		return false;
	}

	const TArray<FCrowdRouteScore> Ranked = RankExits(Agent, Mode, bApplyNearestBias);
	for (const FCrowdRouteScore& Entry : Ranked)
	{
		AExitPoint* Exit = Entry.Exit.Get();
		if (!Exit)
		{
			continue;
		}

		int32 SlotIndex = INDEX_NONE;
		if (Exit->RequestQueueSlot(Agent, SlotIndex))
		{
			OutExit = Exit;
			OutSlotIndex = SlotIndex;
			return true;
		}
	}

	return false;
}

bool UCrowdRoutingService::ShouldReassignExit(ABoidsAB* Agent, AExitPoint* CurrentExit, ECrowdRoutingMode Mode,
                                            AExitPoint*& OutBetterExit) const
{
	OutBetterExit = CurrentExit;
	if (!Agent || !CurrentExit || Mode != ECrowdRoutingMode::Evacuation)
	{
		return false;
	}

	const USimulationFeatureRegistry* Registry = GetWorld()->GetSubsystem<USimulationFeatureRegistry>();
	const FEvacuationRoutingSettings Settings = Registry ? Registry->GetEvacuationSettings() : FEvacuationRoutingSettings();

	TArray<AExitPoint*> Exits;
	CollectExitPoints(Exits);
	const float MaxDist = ComputeMaxDistance(Agent->GetActorLocation(), Exits);

	const float CurrentScore = ScoreExitEvacuation(Agent, CurrentExit, MaxDist, false);

	AExitPoint* BestAlt = nullptr;
	float BestAltScore = TNumericLimits<float>::Max();

	for (AExitPoint* Exit : Exits)
	{
		if (!Exit || Exit == CurrentExit)
		{
			continue;
		}

		UCongestionAnalyzer* Analyzer = GetWorld()->GetSubsystem<UCongestionAnalyzer>();
		if (Analyzer && Analyzer->IsExitFull(Exit))
		{
			continue;
		}

		const float Score = ScoreExitEvacuation(Agent, Exit, MaxDist, false);
		if (Score < BestAltScore)
		{
			BestAltScore = Score;
			BestAlt = Exit;
		}
	}

	if (!BestAlt)
	{
		return false;
	}

	const float Gap = CurrentScore - BestAltScore;
	const float SwitchThreshold = Settings.CongestionSwitchThreshold + Agent->GetEvacuationCongestionSwitchOffset();
	if (Gap > SwitchThreshold)
	{
		OutBetterExit = BestAlt;
		return true;
	}

	return false;
}
