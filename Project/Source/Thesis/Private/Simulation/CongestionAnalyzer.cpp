#include "Simulation/CongestionAnalyzer.h"
#include "World/ExitPoint.h"
#include "AgentStation.h"
#include "DailyUrbanLife/SidewalkSegment.h"
#include "BoidsAB.h"
#include "Kismet/GameplayStatics.h"

int32 UCongestionAnalyzer::GetQueueDepth(AExitPoint* Exit) const
{
	return Exit ? Exit->GetQueueLength() : 0;
}

int32 UCongestionAnalyzer::GetMaxCapacity(AExitPoint* Exit) const
{
	return Exit ? Exit->GetMaxQueueSize() : 0;
}

float UCongestionAnalyzer::GetUtilization(AExitPoint* Exit) const
{
	if (!Exit)
	{
		return 0.f;
	}
	const int32 MaxCap = Exit->GetMaxQueueSize();
	if (MaxCap <= 0)
	{
		return 0.f;
	}
	return static_cast<float>(Exit->GetQueueLength()) / static_cast<float>(MaxCap);
}

float UCongestionAnalyzer::GetEstimatedWaitSeconds(AExitPoint* Exit) const
{
	if (!Exit)
	{
		return 0.f;
	}
	float Wait = Exit->GetQueueLength() * Exit->GetExitProcessDuration();
	if (Exit->IsProcessing())
	{
		Wait += Exit->GetExitProcessDuration() * 0.5f;
	}
	return Wait;
}

float UCongestionAnalyzer::GetCongestionLevel(AExitPoint* Exit) const
{
	return GetUtilization(Exit);
}

bool UCongestionAnalyzer::IsExitFull(AExitPoint* Exit) const
{
	return Exit ? Exit->IsQueueFull() : true;
}

int32 UCongestionAnalyzer::GetStationQueueDepth(AAgentStation* Station) const
{
	return Station ? Station->GetQueueLength() : 0;
}

int32 UCongestionAnalyzer::GetStationMaxCapacity(AAgentStation* Station) const
{
	return Station ? Station->GetMaxConcurrentOccupants() : 0;
}

float UCongestionAnalyzer::GetStationUtilization(AAgentStation* Station) const
{
	return Station ? Station->GetUtilization() : 0.f;
}

float UCongestionAnalyzer::GetStationCongestionLevel(AAgentStation* Station) const
{
	return FMath::Clamp(GetStationUtilization(Station), 0.f, 1.f);
}

int32 UCongestionAnalyzer::GetPathSegmentAgentCount(ASidewalkSegment* Segment) const
{
	if (!Segment)
	{
		return 0;
	}

	TArray<AActor*> Overlapping;
	Segment->GetSegmentOverlappingAgents(Overlapping);
	return Overlapping.Num();
}

float UCongestionAnalyzer::GetPathSegmentCongestionLevel(ASidewalkSegment* Segment, int32 NominalCapacity) const
{
	const int32 Capacity = FMath::Max(1, NominalCapacity);
	return FMath::Clamp(static_cast<float>(GetPathSegmentAgentCount(Segment)) / static_cast<float>(Capacity), 0.f, 1.f);
}

TArray<AAgentStation*> UCongestionAnalyzer::GetStationCongestionHotspots(int32 MaxResults) const
{
	TArray<AAgentStation*> Hotspots;
	if (!GetWorld() || MaxResults <= 0)
	{
		return Hotspots;
	}

	TArray<AActor*> Stations;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AAgentStation::StaticClass(), Stations);
	Stations.Sort([&](const AActor& A, const AActor& B)
	{
		const AAgentStation* SA = Cast<AAgentStation>(&A);
		const AAgentStation* SB = Cast<AAgentStation>(&B);
		return GetStationCongestionLevel(const_cast<AAgentStation*>(SA))
			> GetStationCongestionLevel(const_cast<AAgentStation*>(SB));
	});

	for (int32 i = 0; i < FMath::Min(MaxResults, Stations.Num()); ++i)
	{
		if (AAgentStation* Station = Cast<AAgentStation>(Stations[i]))
		{
			Hotspots.Add(Station);
		}
	}
	return Hotspots;
}
