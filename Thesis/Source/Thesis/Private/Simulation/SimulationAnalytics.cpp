#include "Simulation/SimulationAnalytics.h"
#include "Emergency/EmergencyManager.h"
#include "BoidsAB.h"
#include "World/ExitPoint.h"
#include "AgentStation.h"
#include "ScheduleManager.h"
#include "Simulation/SimulationClock.h"
#include "Simulation/CongestionAnalyzer.h"
#include "Simulation/SimulationFeatureRegistry.h"
#include "DailyUrbanLife/SidewalkSegment.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"

void USimulationAnalytics::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void USimulationAnalytics::Deinitialize()
{
	UnbindSimulationDelegates();
	Super::Deinitialize();
}

double USimulationAnalytics::GetWorldTimeSeconds() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}

	// Accurate Time: durations and timestamps are simulation seconds from USimulationClock.
	// Visual and Fast keep real world time (existing CSV meaning).
	if (const USimulationFeatureRegistry* Registry = World->GetSubsystem<USimulationFeatureRegistry>())
	{
		if (Registry->IsAccurateTimeMode())
		{
			if (const AScheduleManager* SM = Cast<AScheduleManager>(
				UGameplayStatics::GetActorOfClass(World, AScheduleManager::StaticClass())))
			{
				if (const USimulationClock* Clock = SM->GetSimulationClock())
				{
					return Clock->GetTotalSimSeconds();
				}
			}
		}
	}

	return World->GetTimeSeconds();
}

void USimulationAnalytics::BindSimulationDelegates()
{
	if (!GetWorld())
	{
		return;
	}

	if (AEmergencyManager* EM = Cast<AEmergencyManager>(
		UGameplayStatics::GetActorOfClass(GetWorld(), AEmergencyManager::StaticClass())))
	{
		CachedEmergencyManager = EM;
		EM->OnEvacuationStarted.AddDynamic(this, &USimulationAnalytics::HandleEvacuationStarted);
		EM->OnEvacuationEnded.AddDynamic(this, &USimulationAnalytics::HandleEvacuationEnded);
		EM->OnEvacuationTimerUpdated.AddDynamic(this, &USimulationAnalytics::HandleEvacuationTimerUpdated);
	}

	TArray<AActor*> Boids;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ABoidsAB::StaticClass(), Boids);
	for (AActor* Actor : Boids)
	{
		if (ABoidsAB* Boid = Cast<ABoidsAB>(Actor))
		{
			Boid->OnAgentFleeStarted.AddDynamic(this, &USimulationAnalytics::HandleAgentFleeStarted);
			Boid->OnAgentExitChosen.AddDynamic(this, &USimulationAnalytics::HandleAgentExitChosen);
			Boid->OnAgentExitReassigned.AddDynamic(this, &USimulationAnalytics::HandleAgentExitReassigned);
			Boid->OnAgentExitCompleted.AddDynamic(this, &USimulationAnalytics::HandleAgentExitCompleted);
			Boid->OnAgentEvacuationSuccess.AddDynamic(this, &USimulationAnalytics::HandleAgentEvacuationSuccess);
			Boid->OnAgentCasualty.AddDynamic(this, &USimulationAnalytics::HandleAgentCasualty);
			Boid->OnAgentReactionDelayComplete.AddDynamic(this, &USimulationAnalytics::HandleAgentReactionDelayComplete);
		}
	}

	TArray<AActor*> Exits;
	UGameplayStatics::GetAllActorsWithTag(GetWorld(), FName(TEXT("ExitPoint")), Exits);
	for (AActor* Actor : Exits)
	{
		if (AExitPoint* Exit = Cast<AExitPoint>(Actor))
		{
			Exit->OnBoidExited.AddDynamic(this, &USimulationAnalytics::HandleBoidExited);
			Exit->OnQueueDepthChanged.AddDynamic(this, &USimulationAnalytics::HandleQueueDepthChanged);
		}
	}
}

void USimulationAnalytics::UnbindSimulationDelegates()
{
	if (AEmergencyManager* EM = CachedEmergencyManager.Get())
	{
		EM->OnEvacuationStarted.RemoveDynamic(this, &USimulationAnalytics::HandleEvacuationStarted);
		EM->OnEvacuationEnded.RemoveDynamic(this, &USimulationAnalytics::HandleEvacuationEnded);
		EM->OnEvacuationTimerUpdated.RemoveDynamic(this, &USimulationAnalytics::HandleEvacuationTimerUpdated);
	}

	if (!GetWorld())
	{
		return;
	}

	TArray<AActor*> Boids;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ABoidsAB::StaticClass(), Boids);
	for (AActor* Actor : Boids)
	{
		if (ABoidsAB* Boid = Cast<ABoidsAB>(Actor))
		{
			Boid->OnAgentFleeStarted.RemoveDynamic(this, &USimulationAnalytics::HandleAgentFleeStarted);
			Boid->OnAgentExitChosen.RemoveDynamic(this, &USimulationAnalytics::HandleAgentExitChosen);
			Boid->OnAgentExitReassigned.RemoveDynamic(this, &USimulationAnalytics::HandleAgentExitReassigned);
			Boid->OnAgentExitCompleted.RemoveDynamic(this, &USimulationAnalytics::HandleAgentExitCompleted);
			Boid->OnAgentEvacuationSuccess.RemoveDynamic(this, &USimulationAnalytics::HandleAgentEvacuationSuccess);
			Boid->OnAgentCasualty.RemoveDynamic(this, &USimulationAnalytics::HandleAgentCasualty);
			Boid->OnAgentReactionDelayComplete.RemoveDynamic(this, &USimulationAnalytics::HandleAgentReactionDelayComplete);
		}
	}

	TArray<AActor*> Exits;
	UGameplayStatics::GetAllActorsWithTag(GetWorld(), FName(TEXT("ExitPoint")), Exits);
	for (AActor* Actor : Exits)
	{
		if (AExitPoint* Exit = Cast<AExitPoint>(Actor))
		{
			Exit->OnBoidExited.RemoveDynamic(this, &USimulationAnalytics::HandleBoidExited);
			Exit->OnQueueDepthChanged.RemoveDynamic(this, &USimulationAnalytics::HandleQueueDepthChanged);
		}
	}
}

void USimulationAnalytics::BeginEvacuationSession()
{
	bSessionActive = true;
	SessionStartTime = GetWorldTimeSeconds();
	TotalReassignments = 0;
	TotalEvacuated = 0;
	TotalCasualties = 0;
	SafeZoneArrivals = 0;
	LastTimerRemainingSeconds = 0.f;
	bLastTimerExpired = false;
	AgentRecords.Empty();
	ExitThroughput.Empty();
	ExitPeakQueue.Empty();

	if (GetWorld())
	{
		TArray<AActor*> Boids;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), ABoidsAB::StaticClass(), Boids);
		TotalAgentsAtSessionStart = Boids.Num();
	}
	BindSimulationDelegates();
	RefreshSnapshot();
}

void USimulationAnalytics::EndEvacuationSession()
{
	RefreshSnapshot();
	bSessionActive = false;
	UnbindSimulationDelegates();
}

void USimulationAnalytics::HandleEvacuationStarted()
{
	BeginEvacuationSession();
}

void USimulationAnalytics::HandleEvacuationEnded()
{
	EndEvacuationSession();
}

void USimulationAnalytics::RecordAgentFleeStarted(ABoidsAB* Agent)
{
	if (!bSessionActive || !Agent)
	{
		return;
	}

	FAgentEvacRecord& Rec = AgentRecords.FindOrAdd(Agent);
	Rec.FleeStartTime = GetWorldTimeSeconds();
	Rec.AgentId = Agent->AgentId;
	RefreshSnapshot();
}

void USimulationAnalytics::RecordAgentExitChosen(ABoidsAB* Agent, AExitPoint* Exit)
{
	if (!bSessionActive || !Agent)
	{
		return;
	}

	FAgentEvacRecord& Rec = AgentRecords.FindOrAdd(Agent);
	if (Exit && Rec.InitialExitName.IsEmpty())
	{
		Rec.InitialExitName = Exit->GetName();
	}
	if (Exit)
	{
		Rec.FinalExitName = Exit->GetName();
	}
	RefreshSnapshot();
}

void USimulationAnalytics::RecordAgentExitReassigned(ABoidsAB* Agent, AExitPoint* OldExit, AExitPoint* NewExit)
{
	if (!bSessionActive || !Agent)
	{
		return;
	}

	TotalReassignments++;
	FAgentEvacRecord& Rec = AgentRecords.FindOrAdd(Agent);
	Rec.ReassignmentCount++;
	if (NewExit)
	{
		Rec.FinalExitName = NewExit->GetName();
	}
	RefreshSnapshot();
}

void USimulationAnalytics::RecordAgentReactionDelay(ABoidsAB* Agent, float ReactionDelaySeconds)
{
	if (!bSessionActive || !Agent)
	{
		return;
	}

	FAgentEvacRecord& Rec = AgentRecords.FindOrAdd(Agent);
	Rec.ReactionDelaySeconds = ReactionDelaySeconds;
	RefreshSnapshot();
}

void USimulationAnalytics::RecordAgentEvacuationSuccess(ABoidsAB* Agent)
{
	if (!bSessionActive || !Agent)
	{
		return;
	}

	SafeZoneArrivals++;
	FAgentEvacRecord& Rec = AgentRecords.FindOrAdd(Agent);
	Rec.SafeZoneArrivalTime = GetWorldTimeSeconds();
	RefreshSnapshot();
}

void USimulationAnalytics::RecordAgentCasualty(ABoidsAB* Agent)
{
	if (!bSessionActive || !Agent)
	{
		return;
	}

	TotalCasualties++;
	RefreshSnapshot();
}

void USimulationAnalytics::RecordEvacuationTimerRemaining(float RemainingSeconds, bool bExpired)
{
	LastTimerRemainingSeconds = RemainingSeconds;
	bLastTimerExpired = bExpired;
	RefreshSnapshot();
}

void USimulationAnalytics::RecordEvacuationTimerExpired()
{
	bLastTimerExpired = true;
	LastTimerRemainingSeconds = 0.f;
	RefreshSnapshot();
}

void USimulationAnalytics::HandleAgentReactionDelayComplete(ABoidsAB* Agent, float ReactionDelaySeconds)
{
	RecordAgentReactionDelay(Agent, ReactionDelaySeconds);
}

void USimulationAnalytics::HandleAgentEvacuationSuccess(ABoidsAB* Agent)
{
	RecordAgentEvacuationSuccess(Agent);
}

void USimulationAnalytics::HandleAgentCasualty(ABoidsAB* Agent)
{
	RecordAgentCasualty(Agent);
}

void USimulationAnalytics::HandleEvacuationTimerUpdated(float TimeRemainingSeconds)
{
	LastTimerRemainingSeconds = TimeRemainingSeconds;
	RefreshSnapshot();
}

void USimulationAnalytics::RecordAgentExitCompleted(ABoidsAB* Agent)
{
	if (!bSessionActive || !Agent)
	{
		return;
	}

	TotalEvacuated++;
	FAgentEvacRecord& Rec = AgentRecords.FindOrAdd(Agent);
	Rec.ExitCompletedTime = GetWorldTimeSeconds();
	RefreshSnapshot();
}

void USimulationAnalytics::HandleAgentFleeStarted(ABoidsAB* Agent)
{
	RecordAgentFleeStarted(Agent);
}

void USimulationAnalytics::HandleAgentExitChosen(ABoidsAB* Agent, AExitPoint* Exit)
{
	RecordAgentExitChosen(Agent, Exit);
}

void USimulationAnalytics::HandleAgentExitReassigned(ABoidsAB* Agent, AExitPoint* OldExit, AExitPoint* NewExit)
{
	RecordAgentExitReassigned(Agent, OldExit, NewExit);
}

void USimulationAnalytics::HandleAgentExitCompleted(ABoidsAB* Agent)
{
	RecordAgentExitCompleted(Agent);
}

void USimulationAnalytics::HandleBoidExited(AExitPoint* Exit, ABoidsAB* Agent)
{
	if (!bSessionActive || !Exit)
	{
		return;
	}

	ExitThroughput.FindOrAdd(Exit)++;
	RefreshSnapshot();
}

void USimulationAnalytics::HandleQueueDepthChanged(AExitPoint* Exit, int32 NewDepth)
{
	if (!bSessionActive || !Exit)
	{
		return;
	}

	int32& Peak = ExitPeakQueue.FindOrAdd(Exit);
	Peak = FMath::Max(Peak, NewDepth);
	RefreshSnapshot();
}

void USimulationAnalytics::RefreshSnapshot()
{
	FEvacuationAnalyticsSnapshot Snap;
	Snap.ElapsedSeconds = bSessionActive ? static_cast<float>(GetWorldTimeSeconds() - SessionStartTime) : 0.f;
	Snap.TotalEvacuated = TotalEvacuated;
	Snap.TotalReassignments = TotalReassignments;
	Snap.TotalCasualties = TotalCasualties;
	Snap.SafeZoneArrivals = SafeZoneArrivals;
	Snap.TimerRemainingSeconds = LastTimerRemainingSeconds;
	Snap.bTimerExpired = bLastTimerExpired;

	float TotalReactionTime = 0.f;
	int32 ReactionCount = 0;
	float TotalEvacTime = 0.f;
	int32 EvacTimeCount = 0;

	if (GetWorld())
	{
		TArray<AActor*> Boids;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), ABoidsAB::StaticClass(), Boids);
		for (AActor* Actor : Boids)
		{
			ABoidsAB* Boid = Cast<ABoidsAB>(Actor);
			if (!Boid)
			{
				continue;
			}
			if (Boid->IsFleeing())
			{
				Snap.AgentsFleeing++;
			}
			if (Boid->IsSafelyEvacuated())
			{
				Snap.AgentsSafeAtExit++;
			}
			if (Boid->IsCasualty())
			{
				Snap.TotalCasualties = FMath::Max(Snap.TotalCasualties, TotalCasualties);
			}
		}
	}

	for (const TPair<TWeakObjectPtr<ABoidsAB>, FAgentEvacRecord>& Pair : AgentRecords)
	{
		const FAgentEvacRecord& Rec = Pair.Value;
		if (Rec.ExitCompletedTime > Rec.FleeStartTime && Rec.FleeStartTime > 0.0)
		{
			const float Duration = static_cast<float>(Rec.ExitCompletedTime - Rec.FleeStartTime);
			TotalEvacTime += Duration;
			EvacTimeCount++;
			Snap.LongestEvacuationTimeSeconds = FMath::Max(Snap.LongestEvacuationTimeSeconds, Duration);
		}
		if (Rec.ReactionDelaySeconds > 0.f)
		{
			TotalReactionTime += Rec.ReactionDelaySeconds;
			ReactionCount++;
		}
	}

	if (EvacTimeCount > 0)
	{
		Snap.AverageEvacuationTimeSeconds = TotalEvacTime / EvacTimeCount;
	}

	if (ReactionCount > 0)
	{
		Snap.AverageReactionTimeSeconds = TotalReactionTime / ReactionCount;
	}

	if (TotalAgentsAtSessionStart > 0)
	{
		const int32 SuccessCount = SafeZoneArrivals > 0 ? SafeZoneArrivals : TotalEvacuated;
		Snap.EvacuationCompletionRate = static_cast<float>(SuccessCount) / static_cast<float>(TotalAgentsAtSessionStart);
	}

	OnSnapshotUpdated.Broadcast(Snap);
}

FEvacuationAnalyticsSnapshot USimulationAnalytics::GetEvacuationSnapshot() const
{
	FEvacuationAnalyticsSnapshot Snap;
	Snap.TotalEvacuated = TotalEvacuated;
	Snap.TotalReassignments = TotalReassignments;
	Snap.ElapsedSeconds = bSessionActive ? static_cast<float>(GetWorldTimeSeconds() - SessionStartTime) : 0.f;
	return Snap;
}

bool USimulationAnalytics::ExportEvacuationSessionToCSV(const FString& AbsoluteFilePath) const
{
	FString CSV = TEXT("Metric,Value\n");
	CSV += FString::Printf(TEXT("TotalEvacuated,%d\n"), TotalEvacuated);
	CSV += FString::Printf(TEXT("TotalCasualties,%d\n"), TotalCasualties);
	CSV += FString::Printf(TEXT("SafeZoneArrivals,%d\n"), SafeZoneArrivals);
	CSV += FString::Printf(TEXT("TotalReassignments,%d\n"), TotalReassignments);
	CSV += FString::Printf(TEXT("TimerRemainingSeconds,%.2f\n"), LastTimerRemainingSeconds);
	CSV += FString::Printf(TEXT("TimerExpired,%s\n"), bLastTimerExpired ? TEXT("true") : TEXT("false"));
	CSV += FString::Printf(TEXT("EvacuationCompletionRate,%.3f\n"),
	                       TotalAgentsAtSessionStart > 0
		                       ? static_cast<float>(SafeZoneArrivals > 0 ? SafeZoneArrivals : TotalEvacuated)
			                         / static_cast<float>(TotalAgentsAtSessionStart)
		                       : 0.f);
	CSV += FString::Printf(TEXT("SessionDurationSeconds,%.2f\n"),
	                       bSessionActive ? GetWorldTimeSeconds() - SessionStartTime : 0.0);

	for (const TPair<TWeakObjectPtr<AExitPoint>, int32>& Pair : ExitThroughput)
	{
		if (Pair.Key.IsValid())
		{
			CSV += FString::Printf(TEXT("ExitThroughput_%s,%d\n"), *Pair.Key->GetName(), Pair.Value);
		}
	}

	for (const TPair<TWeakObjectPtr<AExitPoint>, int32>& Pair : ExitPeakQueue)
	{
		if (Pair.Key.IsValid())
		{
			CSV += FString::Printf(TEXT("ExitPeakQueue_%s,%d\n"), *Pair.Key->GetName(), Pair.Value);
		}
	}

	return FFileHelper::SaveStringToFile(CSV, *AbsoluteFilePath, FFileHelper::EEncodingOptions::AutoDetect,
	                                     &IFileManager::Get(), FILEWRITE_None);
}

void USimulationAnalytics::BeginDailyUrbanLifeSession()
{
	bDailySessionActive = true;
	DailySessionStartTime = GetWorldTimeSeconds();
	DailyInteractionCount = 0;
	DailyDestinationsVisited = 0;
	DailySidewalkHits = 0;
	DailyAgentsAtHome = 0;
	DailyAgentsAtWork = 0;
	DailyElapsedSimMinutes = 0.f;
	ActiveCommutes.Empty();
	CompletedCommuteDurations.Empty();
	RefreshDailySnapshot();
}

void USimulationAnalytics::EndDailyUrbanLifeSession()
{
	bDailySessionActive = false;
	RefreshDailySnapshot();
}

FDailyUrbanLifeAnalyticsSnapshot USimulationAnalytics::GetDailyUrbanLifeSnapshot() const
{
	FDailyUrbanLifeAnalyticsSnapshot Snap;
	Snap.AgentsAtHome = DailyAgentsAtHome;
	Snap.AgentsAtWork = DailyAgentsAtWork;
	Snap.DestinationsVisited = DailyDestinationsVisited;
	Snap.InteractionCount = DailyInteractionCount;
	Snap.SidewalkUtilizationHits = DailySidewalkHits;
	Snap.ElapsedSimMinutes = DailyElapsedSimMinutes;
	if (CompletedCommuteDurations.Num() > 0)
	{
		double Total = 0.0;
		for (double D : CompletedCommuteDurations)
		{
			Total += D;
		}
		Snap.AverageCommuteTimeSeconds = static_cast<float>(Total / CompletedCommuteDurations.Num());
	}
	return Snap;
}

bool USimulationAnalytics::ExportDailyUrbanLifeSessionToCSV(const FString& AbsoluteFilePath) const
{
	const FDailyUrbanLifeAnalyticsSnapshot Snap = GetDailyUrbanLifeSnapshot();
	FString CSV = TEXT("Metric,Value\n");
	CSV += FString::Printf(TEXT("AgentsAtHome,%d\n"), Snap.AgentsAtHome);
	CSV += FString::Printf(TEXT("AgentsAtWork,%d\n"), Snap.AgentsAtWork);
	CSV += FString::Printf(TEXT("DestinationsVisited,%d\n"), Snap.DestinationsVisited);
	CSV += FString::Printf(TEXT("InteractionCount,%d\n"), Snap.InteractionCount);
	CSV += FString::Printf(TEXT("SidewalkUtilizationHits,%d\n"), Snap.SidewalkUtilizationHits);
	CSV += FString::Printf(TEXT("AverageCommuteTimeSeconds,%.2f\n"), Snap.AverageCommuteTimeSeconds);
	CSV += FString::Printf(TEXT("ElapsedSimMinutes,%.1f\n"), Snap.ElapsedSimMinutes);
	CSV += FString::Printf(TEXT("SessionDurationSeconds,%.2f\n"),
	                       bDailySessionActive ? GetWorldTimeSeconds() - DailySessionStartTime : 0.0);
	return FFileHelper::SaveStringToFile(CSV, *AbsoluteFilePath, FFileHelper::EEncodingOptions::AutoDetect,
	                                     &IFileManager::Get(), FILEWRITE_None);
}

void USimulationAnalytics::RecordDailyCommuteStarted(ABoidsAB* Agent, const FVector& Destination, const FString& DestinationTag)
{
	if (!Agent)
	{
		return;
	}

	if (bInfrastructureSessionActive)
	{
		RecordInfrastructureDestination(DestinationTag);
	}

	if (bFastSessionActive)
	{
		RecordFastDestinationSent(DestinationTag);
	}

	if (!bInfrastructureSessionActive && !bDailySessionActive && !bValidationSessionActive && !bFastSessionActive)
	{
		return;
	}

	FDailyCommuteRecord Rec;
	Rec.StartTime = GetWorldTimeSeconds();
	Rec.StartLocation = Agent->GetActorLocation();
	Rec.bHasStartLocation = true;
	Rec.DestinationTag = DestinationTag;
	ActiveCommutes.Add(Agent, Rec);
}

void USimulationAnalytics::RecordDailyDestinationReached(ABoidsAB* Agent, const FVector& Destination, const FString& DestinationTag)
{
	if (bDailySessionActive)
	{
		DailyDestinationsVisited++;
	}

	if (bInfrastructureSessionActive)
	{
		RecordInfrastructureDestination(DestinationTag);
	}

	if (bFastSessionActive)
	{
		RecordFastDestinationReached(DestinationTag);
	}

	if (Agent)
	{
		if (FDailyCommuteRecord* Rec = ActiveCommutes.Find(Agent))
		{
			const double Now = GetWorldTimeSeconds();
			const double Duration = Now - Rec->StartTime;

			if (bInfrastructureSessionActive)
			{
				InfrastructureTravelDurations.Add(Duration);
			}
			if (bDailySessionActive)
			{
				Rec->bCompleted = true;
				Rec->CompletedTime = Now;
				CompletedCommuteDurations.Add(Duration);
			}
			if (bValidationSessionActive)
			{
				RecordValidationTripCompleted(Agent, *Rec, Now);
			}
			ActiveCommutes.Remove(Agent);
		}
	}

	if (bDailySessionActive)
	{
		RefreshDailyOccupancyCounts();
		RefreshDailySnapshot();
	}
	if (bValidationSessionActive)
	{
		RefreshValidationSnapshot();
	}
}

void USimulationAnalytics::RecordDailyInteraction(ABoidsAB* Agent, ABoidsAB* OtherAgent, const FString& InteractionType)
{
	if (!bDailySessionActive)
	{
		return;
	}

	DailyInteractionCount++;
	RefreshDailySnapshot();
}

void USimulationAnalytics::RecordSidewalkUtilization(const FString& SegmentName)
{
	if (bDailySessionActive)
	{
		DailySidewalkHits++;
		RefreshDailySnapshot();
	}
}

void USimulationAnalytics::RefreshDailyOccupancyCounts()
{
	if (!GetWorld())
	{
		return;
	}

	int32 AtHome = 0;
	int32 AtWork = 0;
	TArray<AActor*> Boids;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ABoidsAB::StaticClass(), Boids);
	for (AActor* Actor : Boids)
	{
		if (ABoidsAB* Boid = Cast<ABoidsAB>(Actor))
		{
			if (Boid->IsAtAssignedHome())
			{
				AtHome++;
			}
			if (Boid->IsAtAssignedWorkplace())
			{
				AtWork++;
			}
		}
	}
	DailyAgentsAtHome = AtHome;
	DailyAgentsAtWork = AtWork;
}

void USimulationAnalytics::UpdateDailySimMinute(float SimMinute)
{
	if (!bDailySessionActive)
	{
		return;
	}

	DailyElapsedSimMinutes = SimMinute;
	RefreshDailyOccupancyCounts();
	RefreshDailySnapshot();
}

void USimulationAnalytics::RefreshDailySnapshot()
{
	const FDailyUrbanLifeAnalyticsSnapshot Snap = GetDailyUrbanLifeSnapshot();
	OnDailySnapshotUpdated.Broadcast(Snap);
}

void USimulationAnalytics::BeginValidationSession(const FString& ScenarioName)
{
	bValidationSessionActive = true;
	ValidationScenarioName = ScenarioName;
	ValidationSessionStartTime = GetWorldTimeSeconds();
	ValidationAgentsSpawned = 0;
	ValidationAgentsActive = 0;
	ValidationAgentsCompleted = 0;
	ValidationCurrentMoving = 0;
	ValidationCurrentWaiting = 0;
	ValidationCurrentAverageVelocity = 0.f;
	ValidationCurrentDensity = 0.f;
	ValidationPeakDensity = 0.f;
	ValidationAverageQueueLength = 0.f;
	ValidationMaximumQueueLength = 0;
	ValidationFPS = 0.f;
	ValidationAverageWaitingTime = 0.f;
	ValidationMaximumWaitingTime = 0.f;
	ValidationAverageCongestionTime = 0.f;
	ValidationThroughput = 0.f;
	ValidationFlowRate = 0.f;
	ValidationAverageTravelTime = 0.f;
	ValidationMinTravelTime = 0.f;
	ValidationMaxTravelTime = 0.f;
	ValidationAverageSpeed = 0.f;
	ValidationAveragePathLength = -1.f;
	ValidationTravelDurations.Empty();
	ValidationTripSpeeds.Empty();
	ValidationPathLengths.Empty();
	ValidationCompletionTimestamps.Empty();
	ValidationWaitingAccumSeconds.Empty();
	ValidationCongestionAccumSeconds.Empty();
	ValidationSeenAgentIds.Empty();
	RefreshValidationSnapshot();
}

void USimulationAnalytics::EndValidationSession()
{
	bValidationSessionActive = false;
	RefreshValidationSnapshot();
}

void USimulationAnalytics::RecordValidationTripCompleted(ABoidsAB* Agent, const FDailyCommuteRecord& Rec, double NowSeconds)
{
	const double Duration = FMath::Max(NowSeconds - Rec.StartTime, KINDA_SMALL_NUMBER);
	ValidationTravelDurations.Add(Duration);
	ValidationAgentsCompleted++;
	ValidationCompletionTimestamps.Add(NowSeconds);

	if (Rec.bHasStartLocation && Agent)
	{
		const float PathLength = FVector::Dist(Rec.StartLocation, Agent->GetActorLocation());
		ValidationPathLengths.Add(PathLength);
		ValidationTripSpeeds.Add(PathLength / static_cast<float>(Duration));
	}

	double Sum = 0.0;
	double MinT = ValidationTravelDurations[0];
	double MaxT = ValidationTravelDurations[0];
	for (double D : ValidationTravelDurations)
	{
		Sum += D;
		MinT = FMath::Min(MinT, D);
		MaxT = FMath::Max(MaxT, D);
	}
	ValidationAverageTravelTime = static_cast<float>(Sum / ValidationTravelDurations.Num());
	ValidationMinTravelTime = static_cast<float>(MinT);
	ValidationMaxTravelTime = static_cast<float>(MaxT);

	if (ValidationTripSpeeds.Num() > 0)
	{
		float SpeedSum = 0.f;
		for (float S : ValidationTripSpeeds)
		{
			SpeedSum += S;
		}
		ValidationAverageSpeed = SpeedSum / ValidationTripSpeeds.Num();
	}

	if (ValidationPathLengths.Num() > 0)
	{
		float PathSum = 0.f;
		for (float P : ValidationPathLengths)
		{
			PathSum += P;
		}
		ValidationAveragePathLength = PathSum / ValidationPathLengths.Num();
	}

	const float Elapsed = FMath::Max(static_cast<float>(NowSeconds - ValidationSessionStartTime), KINDA_SMALL_NUMBER);
	ValidationThroughput = static_cast<float>(ValidationAgentsCompleted) / Elapsed;
}

FValidationAnalyticsSnapshot USimulationAnalytics::GetValidationSnapshot() const
{
	FValidationAnalyticsSnapshot Snap;
	Snap.ScenarioName = ValidationScenarioName;
	Snap.SimulationTimeSeconds = bValidationSessionActive
		? static_cast<float>(GetWorldTimeSeconds() - ValidationSessionStartTime)
		: 0.f;
	Snap.AgentsSpawned = ValidationAgentsSpawned;
	Snap.AgentsActive = ValidationAgentsActive;
	Snap.AgentsCompleted = ValidationAgentsCompleted;
	Snap.AgentsFailed = -1;
	Snap.AverageTravelTimeSeconds = ValidationAverageTravelTime;
	Snap.MinTravelTimeSeconds = ValidationMinTravelTime;
	Snap.MaxTravelTimeSeconds = ValidationMaxTravelTime;
	Snap.AverageSpeed = ValidationAverageSpeed;
	Snap.AveragePathLength = ValidationAveragePathLength;
	Snap.ThroughputAgentsPerSecond = ValidationThroughput;
	Snap.FlowRate = ValidationFlowRate;
	Snap.AverageQueueLength = ValidationAverageQueueLength;
	Snap.MaximumQueueLength = ValidationMaximumQueueLength;
	Snap.PeakDensity = ValidationPeakDensity;
	Snap.CurrentDensity = ValidationCurrentDensity;
	Snap.AverageWaitingTimeSeconds = ValidationAverageWaitingTime;
	Snap.MaximumWaitingTimeSeconds = ValidationMaximumWaitingTime;
	Snap.AverageCongestionTimeSeconds = ValidationAverageCongestionTime;
	Snap.FPS = ValidationFPS;
	Snap.CurrentAgentCount = ValidationAgentsActive;
	Snap.CurrentMovingAgents = ValidationCurrentMoving;
	Snap.CurrentWaitingAgents = ValidationCurrentWaiting;
	Snap.CurrentAverageVelocity = ValidationCurrentAverageVelocity;
	return Snap;
}

void USimulationAnalytics::RefreshValidationSnapshot()
{
	OnValidationSnapshotUpdated.Broadcast(GetValidationSnapshot());
}

void USimulationAnalytics::SampleValidationWorld(float DeltaSeconds)
{
	if (!bValidationSessionActive || !GetWorld())
	{
		return;
	}

	const FValidationAnalyticsSettings Settings = GetWorld()->GetSubsystem<USimulationFeatureRegistry>()
		? GetWorld()->GetSubsystem<USimulationFeatureRegistry>()->GetValidationAnalyticsSettings()
		: FValidationAnalyticsSettings();

	const float SafeDelta = FMath::Max(DeltaSeconds, KINDA_SMALL_NUMBER);
	ValidationFPS = FMath::Lerp(ValidationFPS > KINDA_SMALL_NUMBER ? ValidationFPS : (1.f / SafeDelta), 1.f / SafeDelta, 0.2f);

	TArray<AActor*> BoidActors;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ABoidsAB::StaticClass(), BoidActors);

	int32 Active = 0;
	int32 Moving = 0;
	int32 Waiting = 0;
	float VelocitySum = 0.f;

	for (AActor* Actor : BoidActors)
	{
		ABoidsAB* Boid = Cast<ABoidsAB>(Actor);
		if (!Boid || Boid->AgentId.IsNone())
		{
			continue;
		}

		Active++;
		ValidationSeenAgentIds.Add(Boid->AgentId);

		const float Speed = Boid->GetVelocity().Size();
		VelocitySum += Speed;

		float& WaitAccum = ValidationWaitingAccumSeconds.FindOrAdd(Boid);
		float& CongestAccum = ValidationCongestionAccumSeconds.FindOrAdd(Boid);

		if (Speed <= Settings.WaitingSpeedThreshold)
		{
			Waiting++;
			WaitAccum += SafeDelta;
		}
		else
		{
			Moving++;
		}

		if (Speed <= Settings.CongestionSpeedThreshold)
		{
			CongestAccum += SafeDelta;
		}
	}

	ValidationAgentsActive = Active;
	ValidationAgentsSpawned = ValidationSeenAgentIds.Num();
	ValidationCurrentMoving = Moving;
	ValidationCurrentWaiting = Waiting;
	ValidationCurrentAverageVelocity = Active > 0 ? (VelocitySum / Active) : 0.f;
	ValidationCurrentDensity = static_cast<float>(Active);
	ValidationPeakDensity = FMath::Max(ValidationPeakDensity, ValidationCurrentDensity);

	float WaitSum = 0.f;
	float WaitMax = 0.f;
	int32 WaitSamples = 0;
	for (const TPair<TWeakObjectPtr<ABoidsAB>, float>& Pair : ValidationWaitingAccumSeconds)
	{
		if (!Pair.Key.IsValid())
		{
			continue;
		}
		WaitSum += Pair.Value;
		WaitMax = FMath::Max(WaitMax, Pair.Value);
		WaitSamples++;
	}
	ValidationAverageWaitingTime = WaitSamples > 0 ? (WaitSum / WaitSamples) : 0.f;
	ValidationMaximumWaitingTime = WaitMax;

	float CongestSum = 0.f;
	int32 CongestSamples = 0;
	for (const TPair<TWeakObjectPtr<ABoidsAB>, float>& Pair : ValidationCongestionAccumSeconds)
	{
		if (!Pair.Key.IsValid())
		{
			continue;
		}
		CongestSum += Pair.Value;
		CongestSamples++;
	}
	ValidationAverageCongestionTime = CongestSamples > 0 ? (CongestSum / CongestSamples) : 0.f;

	// Queues: reuse live exit + station queue lengths (same sources as CongestionAnalyzer).
	int32 QueueSum = 0;
	int32 QueueCount = 0;
	int32 QueueMax = ValidationMaximumQueueLength;

	TArray<AActor*> Exits;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AExitPoint::StaticClass(), Exits);
	for (AActor* Actor : Exits)
	{
		if (AExitPoint* Exit = Cast<AExitPoint>(Actor))
		{
			const int32 Q = Exit->GetQueueLength();
			QueueSum += Q;
			QueueCount++;
			QueueMax = FMath::Max(QueueMax, Q);
		}
	}

	TArray<AActor*> Stations;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AAgentStation::StaticClass(), Stations);
	for (AActor* Actor : Stations)
	{
		if (AAgentStation* Station = Cast<AAgentStation>(Actor))
		{
			const int32 Q = Station->GetQueueLength();
			QueueSum += Q;
			QueueCount++;
			QueueMax = FMath::Max(QueueMax, Q);
		}
	}

	ValidationAverageQueueLength = QueueCount > 0 ? (static_cast<float>(QueueSum) / QueueCount) : 0.f;
	ValidationMaximumQueueLength = QueueMax;

	const double Now = GetWorldTimeSeconds();
	const float Elapsed = FMath::Max(static_cast<float>(Now - ValidationSessionStartTime), KINDA_SMALL_NUMBER);
	ValidationThroughput = static_cast<float>(ValidationAgentsCompleted) / Elapsed;

	// Flow rate = completions in the last 1 real second.
	const double WindowStart = Now - 1.0;
	int32 RecentCompletions = 0;
	for (int32 i = ValidationCompletionTimestamps.Num() - 1; i >= 0; --i)
	{
		if (ValidationCompletionTimestamps[i] < WindowStart)
		{
			break;
		}
		RecentCompletions++;
	}
	ValidationFlowRate = static_cast<float>(RecentCompletions);

	RefreshValidationSnapshot();
}

void USimulationAnalytics::BeginInfrastructureSession(const FString& LayoutVariantName)
{
	bInfrastructureSessionActive = true;
	InfrastructureLayoutVariant = LayoutVariantName;
	InfrastructureSessionStartTime = GetWorldTimeSeconds();
	InfrastructureElapsedSimMinutes = 0.f;
	InfrastructurePeakCongestion = 0.f;
	InfrastructureCongestionDurationSeconds = 0.f;
	InfrastructureLastCongestionSampleTime = InfrastructureSessionStartTime;
	InfrastructurePedestriansProcessed = 0;
	StationRecords.Empty();
	PathSegmentHits.Empty();
	PathSegmentCategories.Empty();
	DestinationPopularity.Empty();
	StationQueueJoinTimes.Empty();
	InfrastructureTravelDurations.Empty();
	RefreshInfrastructureSnapshot();
}

void USimulationAnalytics::EndInfrastructureSession()
{
	bInfrastructureSessionActive = false;
	RefreshInfrastructureSnapshot();
}

FInfrastructureAnalyticsSnapshot USimulationAnalytics::GetInfrastructureSnapshot() const
{
	FInfrastructureAnalyticsSnapshot Snap;
	Snap.LayoutVariantName = InfrastructureLayoutVariant;
	Snap.ElapsedSimMinutes = InfrastructureElapsedSimMinutes;
	Snap.TotalPedestriansProcessed = InfrastructurePedestriansProcessed;
	Snap.PeakCongestionLevel = InfrastructurePeakCongestion;
	Snap.CongestionDurationSeconds = InfrastructureCongestionDurationSeconds;
	Snap.DestinationPopularity = DestinationPopularity;

	if (InfrastructureTravelDurations.Num() > 0)
	{
		double Total = 0.0;
		for (double D : InfrastructureTravelDurations)
		{
			Total += D;
		}
		Snap.AverageTravelTimeSeconds = static_cast<float>(Total / InfrastructureTravelDurations.Num());
	}

	TArray<float> QueueWaits;
	for (const TPair<FString, FInfrastructureStationRecord>& Pair : StationRecords)
	{
		FInfrastructureStationMetrics Metrics;
		Metrics.StationName = Pair.Key;
		Metrics.PeakQueueLength = Pair.Value.PeakQueueLength;
		Metrics.TotalThroughput = Pair.Value.TotalThroughput;
		Metrics.PeakUtilization = Pair.Value.PeakUtilization;
		for (float Wait : Pair.Value.QueueWaitSamplesSeconds)
		{
			QueueWaits.Add(Wait);
		}
		if (Pair.Value.QueueWaitSamplesSeconds.Num() > 0)
		{
			float TotalWait = 0.f;
			for (float Wait : Pair.Value.QueueWaitSamplesSeconds)
			{
				TotalWait += Wait;
			}
			Metrics.AverageQueueWaitSeconds = TotalWait / Pair.Value.QueueWaitSamplesSeconds.Num();
		}
		Snap.StationMetrics.Add(Metrics);
	}

	if (QueueWaits.Num() > 0)
	{
		float Total = 0.f;
		for (float Wait : QueueWaits)
		{
			Total += Wait;
		}
		Snap.AverageQueueWaitSeconds = Total / QueueWaits.Num();
	}

	float CongestionSum = 0.f;
	int32 CongestionSamples = 0;
	for (const TPair<FString, int32>& Pair : PathSegmentHits)
	{
		FInfrastructurePathUtilizationEntry Entry;
		Entry.SegmentName = Pair.Key;
		Entry.HitCount = Pair.Value;
		if (const EPathSegmentCategory* Category = PathSegmentCategories.Find(Pair.Key))
		{
			Entry.Category = *Category;
		}
		Snap.PathUtilization.Add(Entry);
		CongestionSum += Pair.Value;
		CongestionSamples++;
	}
	if (CongestionSamples > 0)
	{
		Snap.AverageCongestionLevel = CongestionSum / CongestionSamples;
	}

	return Snap;
}

void USimulationAnalytics::RefreshInfrastructureSnapshot()
{
	if (!bInfrastructureSessionActive)
	{
		return;
	}
	OnInfrastructureSnapshotUpdated.Broadcast(GetInfrastructureSnapshot());
}

void USimulationAnalytics::RecordInfrastructureSample(const FString& SampleName)
{
	if (!bInfrastructureSessionActive || !GetWorld())
	{
		return;
	}

	UCongestionAnalyzer* Analyzer = GetWorld()->GetSubsystem<UCongestionAnalyzer>();
	if (!Analyzer)
	{
		return;
	}

	float SampleCongestion = 0.f;
	int32 SampleCount = 0;

	TArray<AActor*> Stations;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AAgentStation::StaticClass(), Stations);
	for (AActor* Actor : Stations)
	{
		AAgentStation* Station = Cast<AAgentStation>(Actor);
		if (!Station)
		{
			continue;
		}

		const float Util = Analyzer->GetStationCongestionLevel(Station);
		SampleCongestion += Util;
		SampleCount++;

		FInfrastructureStationRecord& Record = StationRecords.FindOrAdd(Station->GetName());
		Record.PeakQueueLength = FMath::Max(Record.PeakQueueLength, Station->GetQueueLength());
		Record.PeakUtilization = FMath::Max(Record.PeakUtilization, Util);
	}

	TArray<AActor*> Segments;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ASidewalkSegment::StaticClass(), Segments);
	for (AActor* Actor : Segments)
	{
		ASidewalkSegment* Segment = Cast<ASidewalkSegment>(Actor);
		if (!Segment)
		{
			continue;
		}
		SampleCongestion += Analyzer->GetPathSegmentCongestionLevel(Segment);
		SampleCount++;
	}

	if (SampleCount > 0)
	{
		const float Avg = SampleCongestion / SampleCount;
		InfrastructurePeakCongestion = FMath::Max(InfrastructurePeakCongestion, Avg);
		const double Now = GetWorldTimeSeconds();
		if (Avg >= 0.5f)
		{
			InfrastructureCongestionDurationSeconds += static_cast<float>(Now - InfrastructureLastCongestionSampleTime);
		}
		InfrastructureLastCongestionSampleTime = Now;
	}

	RefreshInfrastructureSnapshot();
}

void USimulationAnalytics::RecordPathSegmentUtilization(const FString& SegmentName, EPathSegmentCategory Category)
{
	if (!bInfrastructureSessionActive || SegmentName.IsEmpty())
	{
		return;
	}

	PathSegmentHits.FindOrAdd(SegmentName)++;
	PathSegmentCategories.Add(SegmentName, Category);
}

void USimulationAnalytics::RecordStationQueueJoin(AAgentStation* Station, ABoidsAB* Agent)
{
	if (!Station || !Agent)
	{
		return;
	}

	if (bInfrastructureSessionActive)
	{
		StationQueueJoinTimes.Add(Agent, GetWorldTimeSeconds());
	}

	if (bFastSessionActive)
	{
		FFastStationRecord& Rec = FastStations.FindOrAdd(Station->GetName());
		Rec.QueueJoins++;
		Rec.PeakQueue = FMath::Max(Rec.PeakQueue, Station->GetQueueLength());
	}
}

void USimulationAnalytics::RecordStationAdmitted(AAgentStation* Station, ABoidsAB* Agent)
{
	if (!Station || !Agent)
	{
		return;
	}

	if (bInfrastructureSessionActive)
	{
		FInfrastructureStationRecord& Record = StationRecords.FindOrAdd(Station->GetName());
		if (double* JoinTime = StationQueueJoinTimes.Find(Agent))
		{
			Record.QueueWaitSamplesSeconds.Add(static_cast<float>(GetWorldTimeSeconds() - *JoinTime));
			StationQueueJoinTimes.Remove(Agent);
		}
	}

	if (bFastSessionActive)
	{
		FFastStationRecord& Rec = FastStations.FindOrAdd(Station->GetName());
		Rec.Admitted++;
		Rec.PeakQueue = FMath::Max(Rec.PeakQueue, Station->GetQueueLength());
	}
}

void USimulationAnalytics::RecordStationServiceCompleted(AAgentStation* Station, ABoidsAB* Agent)
{
	if (!Station)
	{
		return;
	}

	if (bInfrastructureSessionActive)
	{
		FInfrastructureStationRecord& Record = StationRecords.FindOrAdd(Station->GetName());
		Record.TotalThroughput++;
		InfrastructurePedestriansProcessed++;

		if (Agent)
		{
			if (double* JoinTime = StationQueueJoinTimes.Find(Agent))
			{
				Record.QueueWaitSamplesSeconds.Add(static_cast<float>(GetWorldTimeSeconds() - *JoinTime));
				StationQueueJoinTimes.Remove(Agent);
			}
		}
	}

	if (bFastSessionActive)
	{
		FFastStationRecord& Rec = FastStations.FindOrAdd(Station->GetName());
		Rec.Completed++;
	}
}

void USimulationAnalytics::RecordInfrastructureDestination(const FString& DestinationTag)
{
	if (!bInfrastructureSessionActive || DestinationTag.IsEmpty())
	{
		return;
	}
	DestinationPopularity.FindOrAdd(DestinationTag)++;
}

void USimulationAnalytics::UpdateInfrastructureSimMinute(float SimMinute)
{
	if (!bInfrastructureSessionActive)
	{
		return;
	}
	InfrastructureElapsedSimMinutes = SimMinute;
	RefreshInfrastructureSnapshot();
}

FString USimulationAnalytics::BuildInfrastructureCsvBody() const
{
	const FInfrastructureAnalyticsSnapshot Snap = GetInfrastructureSnapshot();
	FString CSV = TEXT("Metric,Value\n");
	CSV += FString::Printf(TEXT("LayoutVariant,%s\n"), *Snap.LayoutVariantName);
	CSV += FString::Printf(TEXT("AverageTravelTimeSeconds,%.2f\n"), Snap.AverageTravelTimeSeconds);
	CSV += FString::Printf(TEXT("AverageQueueWaitSeconds,%.2f\n"), Snap.AverageQueueWaitSeconds);
	CSV += FString::Printf(TEXT("TotalPedestriansProcessed,%d\n"), Snap.TotalPedestriansProcessed);
	CSV += FString::Printf(TEXT("AverageCongestionLevel,%.3f\n"), Snap.AverageCongestionLevel);
	CSV += FString::Printf(TEXT("PeakCongestionLevel,%.3f\n"), Snap.PeakCongestionLevel);
	CSV += FString::Printf(TEXT("CongestionDurationSeconds,%.2f\n"), Snap.CongestionDurationSeconds);
	CSV += FString::Printf(TEXT("ElapsedSimMinutes,%.1f\n"), Snap.ElapsedSimMinutes);

	for (const FInfrastructureStationMetrics& Station : Snap.StationMetrics)
	{
		CSV += FString::Printf(TEXT("Station_%s_PeakQueue,%d\n"), *Station.StationName, Station.PeakQueueLength);
		CSV += FString::Printf(TEXT("Station_%s_Throughput,%d\n"), *Station.StationName, Station.TotalThroughput);
		CSV += FString::Printf(TEXT("Station_%s_AvgQueueWait,%.2f\n"), *Station.StationName, Station.AverageQueueWaitSeconds);
		CSV += FString::Printf(TEXT("Station_%s_PeakUtilization,%.3f\n"), *Station.StationName, Station.PeakUtilization);
	}

	for (const FInfrastructurePathUtilizationEntry& Path : Snap.PathUtilization)
	{
		CSV += FString::Printf(TEXT("Path_%s_Hits,%d\n"), *Path.SegmentName, Path.HitCount);
	}

	for (const TPair<FString, int32>& Pair : Snap.DestinationPopularity)
	{
		CSV += FString::Printf(TEXT("Destination_%s,%d\n"), *Pair.Key, Pair.Value);
	}
	return CSV;
}

bool USimulationAnalytics::ExportInfrastructureSessionToCSV(const FString& AbsoluteFilePath) const
{
	return FFileHelper::SaveStringToFile(BuildInfrastructureCsvBody(), *AbsoluteFilePath,
	                                     FFileHelper::EEncodingOptions::AutoDetect,
	                                     &IFileManager::Get(), FILEWRITE_None);
}

bool USimulationAnalytics::ParseMetricCsv(const FString& AbsoluteFilePath, TMap<FString, FString>& OutMetrics)
{
	OutMetrics.Empty();
	FString Content;
	if (!FFileHelper::LoadFileToString(Content, *AbsoluteFilePath))
	{
		return false;
	}

	TArray<FString> Lines;
	Content.ParseIntoArrayLines(Lines);
	for (int32 i = 1; i < Lines.Num(); ++i)
	{
		FString Line = Lines[i].TrimStartAndEnd();
		if (Line.IsEmpty())
		{
			continue;
		}
		FString Key;
		FString Value;
		if (Line.Split(TEXT(","), &Key, &Value))
		{
			OutMetrics.Add(Key, Value);
		}
	}
	return OutMetrics.Num() > 0;
}

bool USimulationAnalytics::ExportInfrastructureComparisonReport(const FString& BaselineCsvPath,
                                                                const FString& VariantCsvPath,
                                                                const FString& OutAbsolutePath) const
{
	TMap<FString, FString> Baseline;
	TMap<FString, FString> Variant;
	if (!ParseMetricCsv(BaselineCsvPath, Baseline) || !ParseMetricCsv(VariantCsvPath, Variant))
	{
		return false;
	}

	TSet<FString> Keys;
	for (const TPair<FString, FString>& Pair : Baseline) { Keys.Add(Pair.Key); }
	for (const TPair<FString, FString>& Pair : Variant) { Keys.Add(Pair.Key); }

	FString Report = TEXT("Metric,Baseline,Variant,Delta\n");
	TArray<FString> SortedKeys = Keys.Array();
	SortedKeys.Sort();

	for (const FString& Key : SortedKeys)
	{
		const FString BaseValue = Baseline.FindRef(Key);
		const FString VarValue = Variant.FindRef(Key);
		const float BaseNum = FCString::Atof(*BaseValue);
		const float VarNum = FCString::Atof(*VarValue);
		const bool bNumeric = !BaseValue.IsEmpty() && !VarValue.IsEmpty()
			&& (BaseValue.IsNumeric() || VarValue.IsNumeric());
		if (bNumeric)
		{
			Report += FString::Printf(TEXT("%s,%s,%s,%.3f\n"), *Key, *BaseValue, *VarValue, VarNum - BaseNum);
		}
		else
		{
			Report += FString::Printf(TEXT("%s,%s,%s,\n"), *Key, *BaseValue, *VarValue);
		}
	}

	return FFileHelper::SaveStringToFile(Report, *OutAbsolutePath, FFileHelper::EEncodingOptions::AutoDetect,
	                                     &IFileManager::Get(), FILEWRITE_None);
}

bool USimulationAnalytics::FindLatestInfrastructureCsv(const FString& LayoutVariant, FString& OutAbsolutePath) const
{
	OutAbsolutePath.Empty();
	if (LayoutVariant.IsEmpty())
	{
		return false;
	}

	const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Analytics"));
	IFileManager& FileManager = IFileManager::Get();
	TArray<FString> Found;
	const FString Pattern = FString::Printf(TEXT("Infrastructure_%s_*.csv"), *LayoutVariant);
	FileManager.FindFiles(Found, *FPaths::Combine(Dir, Pattern), true, false);
	if (Found.Num() == 0)
	{
		return false;
	}

	Found.Sort();
	OutAbsolutePath = FPaths::Combine(Dir, Found.Last());
	return FPaths::FileExists(OutAbsolutePath);
}

static float VnVReadMetric(const TMap<FString, FString>& Metrics, const TCHAR* Key)
{
	if (const FString* Value = Metrics.Find(Key))
	{
		return FCString::Atof(**Value);
	}
	return 0.f;
}

static void VnVAppendChecklistLine(FString& Out, const FString& Label, bool bPass, const FString& Detail)
{
	Out += FString::Printf(TEXT("[%s] %s — %s\n"), bPass ? TEXT("PASS") : TEXT("FAIL"), *Label, *Detail);
}

bool USimulationAnalytics::ExportInfrastructureSummaryReport(const FString& BaselineCsvPath,
                                                             const FString& VariantCsvPath,
                                                             const FString& BaselineLabel,
                                                             const FString& VariantLabel,
                                                             const FString& OutAbsolutePath) const
{
	TMap<FString, FString> Baseline;
	TMap<FString, FString> Variant;
	if (!ParseMetricCsv(BaselineCsvPath, Baseline) || !ParseMetricCsv(VariantCsvPath, Variant))
	{
		return false;
	}

	const float BaseTravel = VnVReadMetric(Baseline, TEXT("AverageTravelTimeSeconds"));
	const float VarTravel = VnVReadMetric(Variant, TEXT("AverageTravelTimeSeconds"));
	const float BaseWait = VnVReadMetric(Baseline, TEXT("AverageQueueWaitSeconds"));
	const float VarWait = VnVReadMetric(Variant, TEXT("AverageQueueWaitSeconds"));
	const float BasePeakCong = VnVReadMetric(Baseline, TEXT("PeakCongestionLevel"));
	const float VarPeakCong = VnVReadMetric(Variant, TEXT("PeakCongestionLevel"));
	const float BaseCongDur = VnVReadMetric(Baseline, TEXT("CongestionDurationSeconds"));
	const float VarCongDur = VnVReadMetric(Variant, TEXT("CongestionDurationSeconds"));
	const float BaseThru = VnVReadMetric(Baseline, TEXT("TotalPedestriansProcessed"));
	const float VarThru = VnVReadMetric(Variant, TEXT("TotalPedestriansProcessed"));
	const float BaseAvgCong = VnVReadMetric(Baseline, TEXT("AverageCongestionLevel"));
	const float VarAvgCong = VnVReadMetric(Variant, TEXT("AverageCongestionLevel"));

	auto SumPrefix = [](const TMap<FString, FString>& Metrics, const FString& Prefix) -> float
	{
		float Sum = 0.f;
		for (const TPair<FString, FString>& Pair : Metrics)
		{
			if (Pair.Key.StartsWith(Prefix))
			{
				Sum += FCString::Atof(*Pair.Value);
			}
		}
		return Sum;
	};

	const float BasePathHits = SumPrefix(Baseline, TEXT("Path_"));
	const float VarPathHits = SumPrefix(Variant, TEXT("Path_"));
	const float BasePeakQueue = SumPrefix(Baseline, TEXT("Station_")) > 0.f
		? VnVReadMetric(Baseline, TEXT("AverageQueueWaitSeconds"))
		: 0.f;

	float BaseMaxStationQueue = 0.f;
	float VarMaxStationQueue = 0.f;
	float BaseMaxStationUtil = 0.f;
	float VarMaxStationUtil = 0.f;
	for (const TPair<FString, FString>& Pair : Baseline)
	{
		if (Pair.Key.Contains(TEXT("_PeakQueue")))
		{
			BaseMaxStationQueue = FMath::Max(BaseMaxStationQueue, FCString::Atof(*Pair.Value));
		}
		if (Pair.Key.Contains(TEXT("_PeakUtilization")))
		{
			BaseMaxStationUtil = FMath::Max(BaseMaxStationUtil, FCString::Atof(*Pair.Value));
		}
	}
	for (const TPair<FString, FString>& Pair : Variant)
	{
		if (Pair.Key.Contains(TEXT("_PeakQueue")))
		{
			VarMaxStationQueue = FMath::Max(VarMaxStationQueue, FCString::Atof(*Pair.Value));
		}
		if (Pair.Key.Contains(TEXT("_PeakUtilization")))
		{
			VarMaxStationUtil = FMath::Max(VarMaxStationUtil, FCString::Atof(*Pair.Value));
		}
	}

	FString Report;
	Report += TEXT("============================================================\n");
	Report += TEXT("Verification & Validation — Infrastructure Comparison\n");
	Report += TEXT("============================================================\n");
	Report += FString::Printf(TEXT("Baseline: %s\n  CSV: %s\n"), *BaselineLabel, *BaselineCsvPath);
	Report += FString::Printf(TEXT("Variant:  %s\n  CSV: %s\n\n"), *VariantLabel, *VariantCsvPath);

	Report += TEXT("------------------------------------------------------------\n");
	Report += TEXT("Key Metrics (Baseline -> Variant, Delta = Variant - Baseline)\n");
	Report += TEXT("------------------------------------------------------------\n");
	auto Line = [&](const TCHAR* Name, float B, float V)
	{
		Report += FString::Printf(TEXT("%-32s  %10.2f  %10.2f  %+10.2f\n"), Name, B, V, V - B);
	};
	Report += FString::Printf(TEXT("%-32s  %10s  %10s  %10s\n"), TEXT("Metric"), *BaselineLabel, *VariantLabel, TEXT("Delta"));
	Line(TEXT("AverageTravelTimeSeconds"), BaseTravel, VarTravel);
	Line(TEXT("AverageQueueWaitSeconds"), BaseWait, VarWait);
	Line(TEXT("PeakStationQueueLength"), BaseMaxStationQueue, VarMaxStationQueue);
	Line(TEXT("PeakStationUtilization"), BaseMaxStationUtil, VarMaxStationUtil);
	Line(TEXT("PeakCongestionLevel"), BasePeakCong, VarPeakCong);
	Line(TEXT("CongestionDurationSeconds"), BaseCongDur, VarCongDur);
	Line(TEXT("AverageCongestionLevel"), BaseAvgCong, VarAvgCong);
	Line(TEXT("PathUtilizationHits(sum)"), BasePathHits, VarPathHits);
	Line(TEXT("TotalPedestriansProcessed"), BaseThru, VarThru);

	Report += TEXT("\n------------------------------------------------------------\n");
	Report += TEXT("Verification Checklist (academic V&V)\n");
	Report += TEXT("------------------------------------------------------------\n");

	const bool bHasTravel = BaseTravel > 0.f || VarTravel > 0.f;
	const bool bHasThroughput = BaseThru > 0.f || VarThru > 0.f;
	const bool bHasStations = BaseMaxStationQueue > 0.f || VarMaxStationQueue > 0.f
		|| BaseMaxStationUtil > 0.f || VarMaxStationUtil > 0.f;
	const bool bHasPaths = BasePathHits > 0.f || VarPathHits > 0.f;
	const bool bComparable = Baseline.Num() > 0 && Variant.Num() > 0;

	VnVAppendChecklistLine(Report, TEXT("Both layout CSVs loadable"), bComparable,
		TEXT("ParseMetricCsv succeeded for baseline and variant"));
	VnVAppendChecklistLine(Report, TEXT("Travel time recorded"), bHasTravel,
		FString::Printf(TEXT("%.2f vs %.2f s"), BaseTravel, VarTravel));
	VnVAppendChecklistLine(Report, TEXT("Queue / station metrics present"), bHasStations,
		FString::Printf(TEXT("peak queue %.0f vs %.0f"), BaseMaxStationQueue, VarMaxStationQueue));
	VnVAppendChecklistLine(Report, TEXT("Path utilization recorded"), bHasPaths,
		FString::Printf(TEXT("hits %.0f vs %.0f"), BasePathHits, VarPathHits));
	VnVAppendChecklistLine(Report, TEXT("Throughput recorded"), bHasThroughput,
		FString::Printf(TEXT("%.0f vs %.0f agents"), BaseThru, VarThru));

	// Soft hypothesis: open layout (variant) should not worsen wait+peak congestion together.
	const bool bWaitImprovedOrEqual = VarWait <= BaseWait + 0.01f;
	const bool bCongImprovedOrEqual = VarPeakCong <= BasePeakCong + 0.01f;
	VnVAppendChecklistLine(Report, TEXT("Hypothesis: variant wait <= baseline"), bWaitImprovedOrEqual,
		FString::Printf(TEXT("delta %+0.2f s"), VarWait - BaseWait));
	VnVAppendChecklistLine(Report, TEXT("Hypothesis: variant peak congestion <= baseline"), bCongImprovedOrEqual,
		FString::Printf(TEXT("delta %+0.3f"), VarPeakCong - BasePeakCong));

	Report += TEXT("\nNotes:\n");
	Report += TEXT("- Metrics reuse USimulationAnalytics infrastructure session exports.\n");
	Report += TEXT("- Visualization: AInfrastructureVisualizationActor (queues, congestion, paths, stations).\n");
	Report += TEXT("- Full numeric deltas: see companion Comparison CSV.\n");

	(void)BasePeakQueue;
	return FFileHelper::SaveStringToFile(Report, *OutAbsolutePath, FFileHelper::EEncodingOptions::AutoDetect,
	                                     &IFileManager::Get(), FILEWRITE_None);
}

bool USimulationAnalytics::GenerateInfrastructureVerificationPackage(const FString& BaselineVariant,
                                                                     const FString& VariantName,
                                                                     FString& OutComparisonCsvPath,
                                                                     FString& OutSummaryPath) const
{
	OutComparisonCsvPath.Empty();
	OutSummaryPath.Empty();

	if (BaselineVariant.IsEmpty() || VariantName.IsEmpty()
		|| BaselineVariant.Equals(VariantName, ESearchCase::IgnoreCase))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[VnV] Baseline and variant must be different non-empty names (got '%s' / '%s')"),
			*BaselineVariant, *VariantName);
		return false;
	}

	FString BaselineCsv;
	FString VariantCsv;
	if (!FindLatestInfrastructureCsv(BaselineVariant, BaselineCsv))
	{
		UE_LOG(LogTemp, Log,
			TEXT("[VnV] Missing baseline CSV for '%s' — run that layout first"), *BaselineVariant);
		return false;
	}
	if (!FindLatestInfrastructureCsv(VariantName, VariantCsv))
	{
		UE_LOG(LogTemp, Log,
			TEXT("[VnV] Missing variant CSV for '%s' — run that layout first"), *VariantName);
		return false;
	}

	const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Analytics"));
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.CreateDirectoryTree(*Dir);

	const FString Stamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	OutComparisonCsvPath = FPaths::Combine(
		Dir,
		FString::Printf(TEXT("VnV_Comparison_%s_vs_%s_%s.csv"), *BaselineVariant, *VariantName, *Stamp));
	OutSummaryPath = FPaths::Combine(
		Dir,
		FString::Printf(TEXT("VnV_Summary_%s_vs_%s_%s.txt"), *BaselineVariant, *VariantName, *Stamp));

	const bool bCmp = ExportInfrastructureComparisonReport(BaselineCsv, VariantCsv, OutComparisonCsvPath);
	const bool bSum = ExportInfrastructureSummaryReport(
		BaselineCsv, VariantCsv, BaselineVariant, VariantName, OutSummaryPath);

	if (bCmp && bSum)
	{
		UE_LOG(LogTemp, Log, TEXT("[VnV] Comparison CSV: %s"), *OutComparisonCsvPath);
		UE_LOG(LogTemp, Log, TEXT("[VnV] Summary report: %s"), *OutSummaryPath);
		return true;
	}

	UE_LOG(LogTemp, Warning, TEXT("[VnV] Package export failed (cmp=%d sum=%d)"), bCmp ? 1 : 0, bSum ? 1 : 0);
	return false;
}

void USimulationAnalytics::RecordLeadershipEvent(const FString& EventName)
{
	++LeadershipEventCount;
	UE_LOG(LogTemp, Verbose, TEXT("[Analytics] LeadershipEvent %s (total=%d)"), *EventName, LeadershipEventCount);
}

void USimulationAnalytics::RecordCrimeEvent(const FString& EventName)
{
	if (!bCrimeSessionActive)
	{
		BeginCopsAndRobbersSession();
	}

	if (EventName.Equals(TEXT("CrimeStarted"), ESearchCase::IgnoreCase))
	{
		++CrimesStarted;
	}
	else if (EventName.Equals(TEXT("PoliceResponse"), ESearchCase::IgnoreCase))
	{
		++PoliceResponses;
	}
	else if (EventName.Equals(TEXT("Capture"), ESearchCase::IgnoreCase))
	{
		++CaptureCount;
	}

	UE_LOG(LogTemp, Verbose, TEXT("[Analytics] CrimeEvent %s | Crimes=%d Responses=%d Captures=%d"),
		*EventName, CrimesStarted, PoliceResponses, CaptureCount);
}

void USimulationAnalytics::RecordCrimePursuitStarted(ABoidsAB* Police, ABoidsAB* Robber)
{
	if (!bCrimeSessionActive)
	{
		BeginCopsAndRobbersSession();
	}
	UE_LOG(LogTemp, Verbose, TEXT("[Analytics] Pursuit started %s -> %s"),
		Police ? *Police->GetName() : TEXT("?"),
		Robber ? *Robber->GetName() : TEXT("?"));
}

void USimulationAnalytics::RecordCrimePursuitEnded(ABoidsAB* Police, ABoidsAB* Robber, float PursuitSeconds, bool bCaptured)
{
	if (PursuitSeconds > 0.f)
	{
		CompletedPursuitDurations.Add(PursuitSeconds);
	}
	UE_LOG(LogTemp, Verbose, TEXT("[Analytics] Pursuit ended %s -> %s (%.2fs, captured=%d)"),
		Police ? *Police->GetName() : TEXT("?"),
		Robber ? *Robber->GetName() : TEXT("?"),
		PursuitSeconds,
		bCaptured ? 1 : 0);
}

void USimulationAnalytics::BeginCopsAndRobbersSession()
{
	bCrimeSessionActive = true;
	CrimeSessionStartTime = GetWorldTimeSeconds();
	CrimesStarted = 0;
	PoliceResponses = 0;
	CaptureCount = 0;
	CompletedPursuitDurations.Empty();
}

void USimulationAnalytics::EndCopsAndRobbersSession()
{
	bCrimeSessionActive = false;
}

FCopsAndRobbersAnalyticsSnapshot USimulationAnalytics::GetCopsAndRobbersSnapshot() const
{
	FCopsAndRobbersAnalyticsSnapshot Snap;
	Snap.CrimesStarted = CrimesStarted;
	Snap.PoliceResponses = PoliceResponses;
	Snap.CaptureCount = CaptureCount;
	Snap.ElapsedSeconds = bCrimeSessionActive
		? static_cast<float>(GetWorldTimeSeconds() - CrimeSessionStartTime)
		: 0.f;

	if (CompletedPursuitDurations.Num() > 0)
	{
		float Total = 0.f;
		for (float D : CompletedPursuitDurations)
		{
			Total += D;
		}
		Snap.AveragePursuitTimeSeconds = Total / static_cast<float>(CompletedPursuitDurations.Num());
	}
	return Snap;
}

bool USimulationAnalytics::ExportCopsAndRobbersSessionToCSV(const FString& AbsoluteFilePath) const
{
	const FCopsAndRobbersAnalyticsSnapshot Snap = GetCopsAndRobbersSnapshot();
	FString CSV = TEXT("Metric,Value\n");
	CSV += FString::Printf(TEXT("CrimesStarted,%d\n"), Snap.CrimesStarted);
	CSV += FString::Printf(TEXT("PoliceResponses,%d\n"), Snap.PoliceResponses);
	CSV += FString::Printf(TEXT("CaptureCount,%d\n"), Snap.CaptureCount);
	CSV += FString::Printf(TEXT("AveragePursuitTimeSeconds,%.2f\n"), Snap.AveragePursuitTimeSeconds);
	CSV += FString::Printf(TEXT("ElapsedSeconds,%.2f\n"), Snap.ElapsedSeconds);
	return FFileHelper::SaveStringToFile(CSV, *AbsoluteFilePath, FFileHelper::EEncodingOptions::AutoDetect,
	                                     &IFileManager::Get(), FILEWRITE_None);
}

void USimulationAnalytics::BeginFastSession(const FString& RunId, const FString& MapName, float SimulationSpeed, int32 RandomSeed)
{
	bFastSessionActive = true;
	bFastSessionFinalized = false;
	MetricsSessionMode = TEXT("Fast");
	FastRunId = RunId;
	FastMapName = MapName;
	FastSimulationSpeed = SimulationSpeed;
	FastRandomSeed = RandomSeed;
	FastElapsedSimMinutes = 0.f;
	FastAgentsSpawned = 0;
	FastDestinations.Empty();
	FastStations.Empty();
	FastInteractions.Empty();
	FastEntranceOpenedSimMinutes.Empty();

	UE_LOG(LogTemp, Log,
		TEXT("[FastSim] Session begin RunId=%s Map=%s Speed=%.1fx Seed=%d"),
		*FastRunId, *FastMapName, FastSimulationSpeed, FastRandomSeed);
}

void USimulationAnalytics::BeginAccurateTimeSession(const FString& RunId, const FString& MapName, float SimulationSpeed, int32 RandomSeed)
{
	BeginFastSession(RunId, MapName, SimulationSpeed, RandomSeed);
	MetricsSessionMode = TEXT("AccurateTime");
	UE_LOG(LogTemp, Log,
		TEXT("[AccurateTime] Session begin RunId=%s Map=%s Speed=%.1f sim-sec/real-sec Seed=%d (station durations keep Visual sim occupancy)"),
		*FastRunId, *FastMapName, FastSimulationSpeed, FastRandomSeed);
}

void USimulationAnalytics::EndFastSession()
{
	bFastSessionActive = false;
}

void USimulationAnalytics::RecordFastDestinationSent(const FString& DestinationTag)
{
	if (!bFastSessionActive || DestinationTag.IsEmpty())
	{
		return;
	}
	FastDestinations.FindOrAdd(DestinationTag).Sent++;
}

void USimulationAnalytics::RecordFastDestinationReached(const FString& DestinationTag)
{
	if (!bFastSessionActive || DestinationTag.IsEmpty())
	{
		return;
	}
	FastDestinations.FindOrAdd(DestinationTag).Reached++;
}

void USimulationAnalytics::RecordFastDestinationFailed(const FString& DestinationTag)
{
	if (!bFastSessionActive || DestinationTag.IsEmpty())
	{
		return;
	}
	FastDestinations.FindOrAdd(DestinationTag).Failed++;
}

void USimulationAnalytics::RecordFastInteractionAttempt(const FString& TargetName)
{
	if (!bFastSessionActive || TargetName.IsEmpty())
	{
		return;
	}
	FastInteractions.FindOrAdd(TargetName).Attempts++;
}

void USimulationAnalytics::RecordFastInteractionSuccess(const FString& TargetName)
{
	if (!bFastSessionActive || TargetName.IsEmpty())
	{
		return;
	}
	FastInteractions.FindOrAdd(TargetName).Successes++;
}

void USimulationAnalytics::RecordFastInteractionFailure(const FString& TargetName)
{
	if (!bFastSessionActive || TargetName.IsEmpty())
	{
		return;
	}
	FastInteractions.FindOrAdd(TargetName).Failures++;
}

void USimulationAnalytics::RecordFastEntranceOpened(const FString& EntranceName, float SimMinutes)
{
	if (!bFastSessionActive || EntranceName.IsEmpty())
	{
		return;
	}
	FastEntranceOpenedSimMinutes.FindOrAdd(EntranceName) = SimMinutes;
}

void USimulationAnalytics::NotifyFastAgentSpawned()
{
	if (bFastSessionActive)
	{
		FastAgentsSpawned++;
	}
}

void USimulationAnalytics::UpdateFastElapsedSimMinutes(float ElapsedSimMinutesFromSessionStart)
{
	if (bFastSessionActive)
	{
		FastElapsedSimMinutes = ElapsedSimMinutesFromSessionStart;
	}
}

FString USimulationAnalytics::BuildFastSessionSummaryLog() const
{
	FString Text;
	Text += FString::Printf(TEXT("=== %s SIMULATION COMPLETE ===\n"), *MetricsSessionMode.ToUpper());
	Text += FString::Printf(TEXT("Run: %s\n"), *FastRunId);
	Text += FString::Printf(TEXT("Map: %s\n"), *FastMapName);
	Text += FString::Printf(TEXT("Mode: %s\n"), *MetricsSessionMode);
	Text += FString::Printf(TEXT("Speed: %.1fx\n"), FastSimulationSpeed);
	Text += FString::Printf(TEXT("Sim Time: %.1f min\n"), FastElapsedSimMinutes);
	Text += FString::Printf(TEXT("Seed: %d\n"), FastRandomSeed);
	Text += FString::Printf(TEXT("Agents Spawned: %d\n"), FastAgentsSpawned);

	Text += TEXT("\nDestinations:\n");
	for (const TPair<FString, FFastDestinationRecord>& Pair : FastDestinations)
	{
		Text += FString::Printf(TEXT("  %s  Sent=%d Reached=%d Failed=%d\n"),
			*Pair.Key, Pair.Value.Sent, Pair.Value.Reached, Pair.Value.Failed);
	}

	Text += TEXT("\nStations:\n");
	for (const TPair<FString, FFastStationRecord>& Pair : FastStations)
	{
		Text += FString::Printf(TEXT("  %s  QueueJoins=%d Admitted=%d Completed=%d PeakQueue=%d\n"),
			*Pair.Key, Pair.Value.QueueJoins, Pair.Value.Admitted, Pair.Value.Completed, Pair.Value.PeakQueue);
	}

	Text += TEXT("\nInteractions:\n");
	for (const TPair<FString, FFastInteractionRecord>& Pair : FastInteractions)
	{
		Text += FString::Printf(TEXT("  %s  Attempts=%d Success=%d Fail=%d\n"),
			*Pair.Key, Pair.Value.Attempts, Pair.Value.Successes, Pair.Value.Failures);
	}

	Text += TEXT("\nEntrances opened (sim minute):\n");
	for (const TPair<FString, float>& Pair : FastEntranceOpenedSimMinutes)
	{
		Text += FString::Printf(TEXT("  %s @ %.1f\n"), *Pair.Key, Pair.Value);
	}

	return Text;
}

bool USimulationAnalytics::ExportFastSessionToCSV(const FString& AbsoluteFilePath) const
{
	FString CSV;
	CSV += TEXT("Section,RunId,Map,SimulationMode,SimulationSpeed,SimulationDurationMinutes,RandomSeed,AgentsSpawned,Key,Sent,Reached,Failed,QueueJoins,Admitted,Completed,PeakQueue,Attempts,Successes,Failures,OpenSimMinute\n");

	CSV += FString::Printf(
		TEXT("Run,%s,%s,%s,%.1f,%.1f,%d,%d,,,,,,,,,,,\n"),
		*FastRunId, *FastMapName, *MetricsSessionMode, FastSimulationSpeed, FastElapsedSimMinutes, FastRandomSeed, FastAgentsSpawned);

	for (const TPair<FString, FFastDestinationRecord>& Pair : FastDestinations)
	{
		CSV += FString::Printf(
			TEXT("Destination,%s,%s,%s,%.1f,%.1f,%d,%d,%s,%d,%d,%d,,,,,,,,\n"),
			*FastRunId, *FastMapName, *MetricsSessionMode, FastSimulationSpeed, FastElapsedSimMinutes, FastRandomSeed, FastAgentsSpawned,
			*Pair.Key, Pair.Value.Sent, Pair.Value.Reached, Pair.Value.Failed);
	}

	for (const TPair<FString, FFastStationRecord>& Pair : FastStations)
	{
		CSV += FString::Printf(
			TEXT("Station,%s,%s,%s,%.1f,%.1f,%d,%d,%s,,,,%d,%d,%d,%d,,,,\n"),
			*FastRunId, *FastMapName, *MetricsSessionMode, FastSimulationSpeed, FastElapsedSimMinutes, FastRandomSeed, FastAgentsSpawned,
			*Pair.Key, Pair.Value.QueueJoins, Pair.Value.Admitted, Pair.Value.Completed, Pair.Value.PeakQueue);
	}

	for (const TPair<FString, FFastInteractionRecord>& Pair : FastInteractions)
	{
		CSV += FString::Printf(
			TEXT("Interaction,%s,%s,%s,%.1f,%.1f,%d,%d,%s,,,,,,,,,%d,%d,%d,\n"),
			*FastRunId, *FastMapName, *MetricsSessionMode, FastSimulationSpeed, FastElapsedSimMinutes, FastRandomSeed, FastAgentsSpawned,
			*Pair.Key, Pair.Value.Attempts, Pair.Value.Successes, Pair.Value.Failures);
	}

	for (const TPair<FString, float>& Pair : FastEntranceOpenedSimMinutes)
	{
		CSV += FString::Printf(
			TEXT("EntranceOpen,%s,%s,%s,%.1f,%.1f,%d,%d,%s,,,,,,,,,,,,%.1f\n"),
			*FastRunId, *FastMapName, *MetricsSessionMode, FastSimulationSpeed, FastElapsedSimMinutes, FastRandomSeed, FastAgentsSpawned,
			*Pair.Key, Pair.Value);
	}

	return FFileHelper::SaveStringToFile(CSV, *AbsoluteFilePath, FFileHelper::EEncodingOptions::AutoDetect,
	                                     &IFileManager::Get(), FILEWRITE_None);
}

