#include "InfrastructurePlanning/InfrastructurePlanningManager.h"
#include "InfrastructurePlanning/InfrastructureVisualizationActor.h"
#include "BoidsAB.h"
#include "ScheduleManager.h"
#include "Simulation/SimulationClock.h"
#include "AgentArea.h"
#include "AgentStation.h"
#include "DailyUrbanLife/SidewalkSegment.h"
#include "Simulation/SimulationFeatureRegistry.h"
#include "Simulation/SimulationAnalytics.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"

AInfrastructurePlanningManager::AInfrastructurePlanningManager()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AInfrastructurePlanningManager::BeginPlay()
{
	Super::BeginPlay();

	if (bAutoInitializeOnBeginPlay && GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimer(
			InitDelayHandle,
			this,
			&AInfrastructurePlanningManager::InitializeInfrastructurePlanning,
			0.5f,
			false);
	}
}

void AInfrastructurePlanningManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	FinalizeInfrastructurePlanning();
	Super::EndPlay(EndPlayReason);
}

void AInfrastructurePlanningManager::InitializeInfrastructurePlanning()
{
	if (!GetWorld())
	{
		return;
	}

	USimulationFeatureRegistry* Registry = GetWorld()->GetSubsystem<USimulationFeatureRegistry>();
	if (!Registry || !Registry->IsFeatureEnabled(ESimulationFeature::InfrastructureAnalysis))
	{
		UE_LOG(LogTemp, Log, TEXT("[InfrastructurePlanning] Feature disabled — manager skipped"));
		return;
	}

	Registry->SetCurrentScenario(ESimulationScenario::InfrastructurePlanning);
	bRunFinalized = false;

	ApplyMapDrivenInfrastructureSettings();

	const FInfrastructurePlanningSettings& ResolvedSettings = Registry->GetInfrastructurePlanningSettings();
	CopyScheduleToSaved(ResolvedSettings.ScheduleFile);
	ConfigureScheduleManager();
	ApplyLayoutVariantSettings();

	if (ResolvedSettings.bReuseDailyUrbanLifeBootstrap)
	{
		BootstrapDailyLifeAgents();
	}

	ValidateMapSetup();

	if (USimulationAnalytics* Analytics = GetWorld()->GetSubsystem<USimulationAnalytics>())
	{
		Analytics->BeginInfrastructureSession(ResolvedSettings.LayoutVariantName);
	}

	if (ResolvedSettings.bEnableVisualization)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		VisualizationActor = GetWorld()->SpawnActor<AInfrastructureVisualizationActor>(
			AInfrastructureVisualizationActor::StaticClass(),
			FVector::ZeroVector,
			FRotator::ZeroRotator,
			Params);
	}

	StartSampling();

	UE_LOG(LogTemp, Log,
		TEXT("[InfrastructurePlanning] Initialized layout '%s' with schedule %s"),
		*ResolvedSettings.LayoutVariantName,
		*ResolvedSettings.ScheduleFile);
}

void AInfrastructurePlanningManager::FinalizeInfrastructurePlanning()
{
	if (!GetWorld() || bRunFinalized)
	{
		return;
	}

	bRunFinalized = true;

	GetWorld()->GetTimerManager().ClearTimer(SamplingTimerHandle);
	GetWorld()->GetTimerManager().ClearTimer(EndRunPollTimerHandle);

	if (USimulationAnalytics* Analytics = GetWorld()->GetSubsystem<USimulationAnalytics>())
	{
		Analytics->EndInfrastructureSession();
	}

	ExportSessionResults();
}

void AInfrastructurePlanningManager::CopyScheduleToSaved(const FString& RelativePath) const
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	const FString ProjectPath = FPaths::Combine(FPaths::ProjectDir(), RelativePath);
	const FString SavedPath = FPaths::Combine(FPaths::ProjectSavedDir(), RelativePath);
	const FString SavedParent = FPaths::GetPath(SavedPath);
	if (!PlatformFile.DirectoryExists(*SavedParent))
	{
		PlatformFile.CreateDirectoryTree(*SavedParent);
	}

	if (PlatformFile.FileExists(*ProjectPath))
	{
		PlatformFile.CopyFile(*SavedPath, *ProjectPath);
	}
}

void AInfrastructurePlanningManager::ConfigureScheduleManager() const
{
	AScheduleManager* SM = Cast<AScheduleManager>(
		UGameplayStatics::GetActorOfClass(GetWorld(), AScheduleManager::StaticClass()));
	if (!SM)
	{
		return;
	}

	const FInfrastructurePlanningSettings& Settings =
		GetWorld()->GetSubsystem<USimulationFeatureRegistry>()->GetInfrastructurePlanningSettings();
	SM->SetScheduleFilePath(Settings.ScheduleFile);
}

void AInfrastructurePlanningManager::BootstrapDailyLifeAgents()
{
	const FDailyUrbanLifeSettings& DailySettings =
		GetWorld()->GetSubsystem<USimulationFeatureRegistry>()->GetDailyUrbanLifeSettings();

	TArray<AActor*> Boids;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ABoidsAB::StaticClass(), Boids);

	for (AActor* Actor : Boids)
	{
		ABoidsAB* Boid = Cast<ABoidsAB>(Actor);
		if (!Boid)
		{
			continue;
		}

		FString Id = Boid->AgentId.ToString();
		if (!Id.StartsWith(TEXT("Agent_")))
		{
			continue;
		}

		const int32 Index = FCString::Atoi(*Id.Mid(6));

		FName EntranceTag = FName(TEXT("Entrance1"));
		if (Index % 2 == 1)
		{
			TArray<AActor*> SecondaryEntrances;
			UGameplayStatics::GetAllActorsWithTag(GetWorld(), FName(TEXT("Entrance2")), SecondaryEntrances);
			if (SecondaryEntrances.Num() > 0)
			{
				EntranceTag = FName(TEXT("Entrance2"));
			}
		}

		Boid->AssignDailyDestinations(EntranceTag, FName(TEXT("FoodCourt")));
		Boid->ResolveDestinationsFromTags();

		if (DailySettings.bStartAgentsAtHome)
		{
			Boid->TeleportToAssignedHome();

			const float AngleRad = FMath::DegreesToRadians(Index * 37.f);
			const float RingRadius = 100.f + (Index % 4) * 35.f;
			const FVector Spread(
				FMath::Cos(AngleRad) * RingRadius,
				FMath::Sin(AngleRad) * RingRadius,
				0.f);
			Boid->SetActorLocation(Boid->GetActorLocation() + Spread);
			Boid->StopMovement();
			// Do NOT leave SetIdle(true): UC3 agents only move on schedule. Idle freeze
			// stranded any Agent_* without matching XML rules at the entrance forever.
			Boid->SetIdle(false);
		}
	}

	WarnAboutUnscheduledAgents();
}

void AInfrastructurePlanningManager::WarnAboutUnscheduledAgents() const
{
	AScheduleManager* SM = Cast<AScheduleManager>(
		UGameplayStatics::GetActorOfClass(GetWorld(), AScheduleManager::StaticClass()));
	if (!SM)
	{
		return;
	}

	TArray<AActor*> Boids;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ABoidsAB::StaticClass(), Boids);

	int32 Unscheduled = 0;
	for (AActor* Actor : Boids)
	{
		const ABoidsAB* Boid = Cast<ABoidsAB>(Actor);
		if (!Boid || Boid->AgentId.IsNone())
		{
			continue;
		}

		const TArray<FScheduleRule>* Rules = SM->GetRulesForAgent(Boid->AgentId);
		if (!Rules || Rules->Num() == 0)
		{
			++Unscheduled;
			UE_LOG(LogTemp, Warning,
				TEXT("[InfrastructurePlanning] %s has no schedule rules — will stay at entrance. "
					 "Add <agent id=\"%s\"> to the active Mall schedule."),
				*Boid->AgentId.ToString(),
				*Boid->AgentId.ToString());
		}
	}

	if (Unscheduled > 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[InfrastructurePlanning] %d agent(s) have no schedule entries (stranded at spawn)"),
			Unscheduled);
	}
}

static void WarnIfTagMissing(const UWorld* World, FName Tag, const TCHAR* Label)
{
	if (!World)
	{
		return;
	}

	TArray<AActor*> Found;
	UGameplayStatics::GetAllActorsWithTag(const_cast<UWorld*>(World), Tag, Found);
	if (Found.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[InfrastructurePlanning] Missing %s tag '%s'"), Label, *Tag.ToString());
	}
}

void AInfrastructurePlanningManager::ValidateMapSetup() const
{
	if (!GetWorld())
	{
		return;
	}

	const FInfrastructurePlanningSettings& Settings =
		GetWorld()->GetSubsystem<USimulationFeatureRegistry>()->GetInfrastructurePlanningSettings();

	WarnIfTagMissing(GetWorld(), FName(TEXT("Entrance1")), TEXT("entrance"));
	WarnIfTagMissing(GetWorld(), FName(TEXT("ShopA")), TEXT("shop area"));
	WarnIfTagMissing(GetWorld(), FName(TEXT("ShopB")), TEXT("shop area"));
	WarnIfTagMissing(GetWorld(), FName(TEXT("FoodCourt")), TEXT("food court"));
	WarnIfTagMissing(GetWorld(), FName(TEXT("SeatingArea")), TEXT("seating area"));
	WarnIfTagMissing(GetWorld(), FName(TEXT("Exit1")), TEXT("exit"));

	TArray<AActor*> SecondaryEntrances;
	UGameplayStatics::GetAllActorsWithTag(GetWorld(), FName(TEXT("Entrance2")), SecondaryEntrances);
	if (SecondaryEntrances.Num() == 0 && Settings.LayoutVariantName.Contains(TEXT("MallB"), ESearchCase::IgnoreCase))
	{
		UE_LOG(LogTemp, Warning, TEXT("[InfrastructurePlanning] MallB layout expects Entrance2 tag"));
	}

	if (Settings.LayoutVariantName.Contains(TEXT("MallB"), ESearchCase::IgnoreCase))
	{
		WarnIfTagMissing(GetWorld(), FName(TEXT("FoodCourt2")), TEXT("food court"));
		WarnIfTagMissing(GetWorld(), FName(TEXT("EntranceButton2")), TEXT("entrance button"));
	}

	TArray<AActor*> ServiceStations;
	UGameplayStatics::GetAllActorsWithTag(GetWorld(), Settings.ServiceStationTag, ServiceStations);
	if (ServiceStations.Num() == 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[InfrastructurePlanning] No stations tagged '%s'"),
			*Settings.ServiceStationTag.ToString());
	}

	TArray<AActor*> Segments;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ASidewalkSegment::StaticClass(), Segments);
	if (Segments.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[InfrastructurePlanning] No sidewalk/path segments for utilization tracking"));
	}
}

void AInfrastructurePlanningManager::ApplyMapDrivenInfrastructureSettings()
{
	USimulationFeatureRegistry* Registry = GetWorld()
		? GetWorld()->GetSubsystem<USimulationFeatureRegistry>()
		: nullptr;
	if (!Registry)
	{
		return;
	}

	FInfrastructurePlanningSettings Settings = Registry->GetInfrastructurePlanningSettings();
	if (!Settings.bAutoSelectFromMap)
	{
		return;
	}

	const FString MapName = GetWorld()->GetMapName();
	FString DetectedLayout;
	int32 DefaultCapacity = Settings.ServiceStationCapacity;

	if (MapName.Contains(TEXT("MallB"), ESearchCase::IgnoreCase))
	{
		DetectedLayout = TEXT("MallB");
		DefaultCapacity = 2;
	}
	else if (MapName.Contains(TEXT("MallA"), ESearchCase::IgnoreCase))
	{
		DetectedLayout = TEXT("MallA");
		DefaultCapacity = 1;
	}

	if (DetectedLayout.IsEmpty())
	{
		UE_LOG(LogTemp, Log,
			TEXT("[InfrastructurePlanning] AutoSelectFromMap: map '%s' is not MallA/MallB — keeping config schedule '%s'"),
			*MapName,
			*Settings.ScheduleFile);
		return;
	}

	// Prefer canonical MallA/MallB schedules (full Agent_0..Agent_9 shopping).
	// Thin EntranceRouteDemo is opt-in via infrastructurePlanning.scheduleFile — do not auto-override.
	FString ResolvedSchedule = FString::Printf(
		TEXT("Schedules/InfrastructurePlanningSchedule_%s.xml"),
		*DetectedLayout);

	const FString ProjectPath = FPaths::Combine(FPaths::ProjectDir(), ResolvedSchedule);
	if (!FPaths::FileExists(ProjectPath))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[InfrastructurePlanning] AutoSelectFromMap: expected schedule missing at %s — keeping config '%s'"),
			*ProjectPath,
			*Settings.ScheduleFile);
		Settings.LayoutVariantName = DetectedLayout;
		Settings.ServiceStationCapacity = DefaultCapacity;
		Registry->SetInfrastructurePlanningSettings(Settings);
		Registry->SetFeatureEnabled(ESimulationFeature::DailyUrbanLife, false);
		Registry->SetFeatureEnabled(ESimulationFeature::InfrastructureAnalysis, true);
		return;
	}

	Settings.LayoutVariantName = DetectedLayout;
	Settings.ScheduleFile = ResolvedSchedule;
	Settings.ServiceStationCapacity = DefaultCapacity;
	Registry->SetInfrastructurePlanningSettings(Settings);

	// Mall runs must not activate Daily Urban Life (schedule / pedestrian interactions).
	Registry->SetFeatureEnabled(ESimulationFeature::DailyUrbanLife, false);
	Registry->SetFeatureEnabled(ESimulationFeature::InfrastructureAnalysis, true);

	UE_LOG(LogTemp, Log,
		TEXT("[InfrastructurePlanning] AutoSelectFromMap: map '%s' → layout '%s', schedule '%s', capacity %d"),
		*MapName,
		*DetectedLayout,
		*ResolvedSchedule,
		DefaultCapacity);
}

void AInfrastructurePlanningManager::ApplyLayoutVariantSettings()
{
	const FInfrastructurePlanningSettings& Settings =
		GetWorld()->GetSubsystem<USimulationFeatureRegistry>()->GetInfrastructurePlanningSettings();

	if (Settings.ServiceStationCapacity <= 0)
	{
		return;
	}

	TArray<AActor*> Stations;
	UGameplayStatics::GetAllActorsWithTag(GetWorld(), Settings.ServiceStationTag, Stations);
	for (AActor* Actor : Stations)
	{
		if (AAgentStation* Station = Cast<AAgentStation>(Actor))
		{
			Station->SetMaxConcurrentOccupants(Settings.ServiceStationCapacity);
		}
	}

	UE_LOG(LogTemp, Log,
		TEXT("[InfrastructurePlanning] Applied service capacity %d to %d '%s' stations"),
		Settings.ServiceStationCapacity,
		Stations.Num(),
		*Settings.ServiceStationTag.ToString());
}

void AInfrastructurePlanningManager::StartSampling()
{
	const FInfrastructurePlanningSettings& Settings =
		GetWorld()->GetSubsystem<USimulationFeatureRegistry>()->GetInfrastructurePlanningSettings();

	RunStartAccumulatedSimMinutes = 0.0;
	if (AScheduleManager* SM = Cast<AScheduleManager>(
		UGameplayStatics::GetActorOfClass(GetWorld(), AScheduleManager::StaticClass())))
	{
		if (USimulationClock* Clock = SM->GetSimulationClock())
		{
			RunStartAccumulatedSimMinutes = Clock->GetAccumulatedSimMinutes();
		}
		else
		{
			RunStartAccumulatedSimMinutes = SM->AccumulatedSimSeconds;
		}
	}

	if (Settings.SamplingIntervalSeconds > 0.f)
	{
		float SampleWallSeconds = Settings.SamplingIntervalSeconds;
		if (const USimulationFeatureRegistry* Registry = GetWorld()->GetSubsystem<USimulationFeatureRegistry>())
		{
			if (Registry->IsAccurateTimeMode())
			{
				// Same sim spacing as Visual: 5 real seconds at clock 60 is 300 sim seconds.
				SampleWallSeconds = Registry->ScaleAuthoredDurationToRealSeconds(Settings.SamplingIntervalSeconds);
			}
		}
		GetWorld()->GetTimerManager().SetTimer(
			SamplingTimerHandle,
			this,
			&AInfrastructurePlanningManager::HandleSamplingTick,
			SampleWallSeconds,
			true);
	}

	// End when simulation clock advances RunDurationSimMinutes (not wall-clock * 60).
	if (Settings.RunDurationSimMinutes > 0.f)
	{
		GetWorld()->GetTimerManager().SetTimer(
			EndRunPollTimerHandle,
			this,
			&AInfrastructurePlanningManager::CheckSimDurationEnd,
			0.25f,
			true);
	}
}

void AInfrastructurePlanningManager::CheckSimDurationEnd()
{
	if (bRunFinalized || !GetWorld())
	{
		return;
	}

	const FInfrastructurePlanningSettings& Settings =
		GetWorld()->GetSubsystem<USimulationFeatureRegistry>()->GetInfrastructurePlanningSettings();
	if (Settings.RunDurationSimMinutes <= 0.f)
	{
		return;
	}

	double NowSimMinutes = RunStartAccumulatedSimMinutes;
	if (AScheduleManager* SM = Cast<AScheduleManager>(
		UGameplayStatics::GetActorOfClass(GetWorld(), AScheduleManager::StaticClass())))
	{
		if (USimulationClock* Clock = SM->GetSimulationClock())
		{
			NowSimMinutes = Clock->GetAccumulatedSimMinutes();
		}
		else
		{
			NowSimMinutes = SM->AccumulatedSimSeconds;
		}
	}

	const double ElapsedSimMinutes = NowSimMinutes - RunStartAccumulatedSimMinutes;
	if (ElapsedSimMinutes >= static_cast<double>(Settings.RunDurationSimMinutes))
	{
		UE_LOG(LogTemp, Log,
			TEXT("[InfrastructurePlanning] Sim duration reached (%.1f sim min) — finalizing"),
			ElapsedSimMinutes);
		FinalizeInfrastructurePlanning();
	}
}

void AInfrastructurePlanningManager::HandleSamplingTick()
{
	if (USimulationAnalytics* Analytics = GetWorld()->GetSubsystem<USimulationAnalytics>())
	{
		const FInfrastructurePlanningSettings& Settings =
			GetWorld()->GetSubsystem<USimulationFeatureRegistry>()->GetInfrastructurePlanningSettings();
		Analytics->RecordInfrastructureSample(Settings.LayoutVariantName);
	}

	CheckSimDurationEnd();
}

void AInfrastructurePlanningManager::ExportSessionResults() const
{
	const FInfrastructurePlanningSettings& Settings =
		GetWorld()->GetSubsystem<USimulationFeatureRegistry>()->GetInfrastructurePlanningSettings();

	if (!Settings.bAutoExportOnEnd)
	{
		return;
	}

	const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Analytics"));
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*Dir))
	{
		PlatformFile.CreateDirectoryTree(*Dir);
	}

	const FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	const FString FilePath = FPaths::Combine(
		Dir,
		FString::Printf(TEXT("Infrastructure_%s_%s.csv"), *Settings.LayoutVariantName, *Timestamp));

	if (USimulationAnalytics* Analytics = GetWorld()->GetSubsystem<USimulationAnalytics>())
	{
		if (Analytics->ExportInfrastructureSessionToCSV(FilePath))
		{
			UE_LOG(LogTemp, Log, TEXT("[InfrastructurePlanning] Exported metrics to %s"), *FilePath);

			if (Settings.bAutoExportComparisonOnEnd)
			{
				const FString Baseline = Settings.ComparisonBaselineVariant;
				const FString Variant = Settings.ComparisonVariantName.IsEmpty()
					                        ? TEXT("MallB")
					                        : Settings.ComparisonVariantName;

				// Prefer comparing baseline vs the layout just finished when they differ.
				FString Left = Baseline;
				FString Right = Settings.LayoutVariantName;
				if (Left.Equals(Right, ESearchCase::IgnoreCase))
				{
					Right = Variant;
				}

				FString ComparisonPath;
				FString SummaryPath;
				if (Analytics->GenerateInfrastructureVerificationPackage(Left, Right, ComparisonPath, SummaryPath))
				{
					UE_LOG(LogTemp, Log, TEXT("[InfrastructurePlanning] V&V package ready"));
				}
			}
		}
	}
}
