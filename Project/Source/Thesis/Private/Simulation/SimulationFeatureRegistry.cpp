#include "Simulation/SimulationFeatureRegistry.h"
#include "Simulation/SimulationConfigLoader.h"
#include "Simulation/SimulationTimeContract.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"

void USimulationFeatureRegistry::ApplyDefaults()
{
	// Used only when SimulationConfig.xml cannot be loaded.
	// Canonical thesis defaults live in Config/SimulationConfig.xml;
	// runtime loads Project first, then overlays Saved when present.
	FeatureStates.Empty();
	FeatureStates.Add(ESimulationFeature::Evacuation, true);
	FeatureStates.Add(ESimulationFeature::Leadership, false);
	FeatureStates.Add(ESimulationFeature::CopsAndRobbers, false);
	FeatureStates.Add(ESimulationFeature::InfrastructureAnalysis, false);
	FeatureStates.Add(ESimulationFeature::DailyUrbanLife, false);
	FeatureStates.Add(ESimulationFeature::ValidationStats, false);
	CurrentScenario = ESimulationScenario::None;
	EvacuationSettings = FEvacuationRoutingSettings();
	DailyUrbanLifeSettings = FDailyUrbanLifeSettings();
	InfrastructurePlanningSettings = FInfrastructurePlanningSettings();
	LeadershipSettings = FLeadershipSettings();
	CopsAndRobbersSettings = FCopsAndRobbersSettings();
	ValidationAnalyticsSettings = FValidationAnalyticsSettings();
	SimulationClockSettings = FSimulationClockSettings();
	SimulationMode = ESimulationMode::Visual;
	FastSimulationSettings = FFastSimulationSettings();
	AccurateTimeSettings = FAccurateTimeSettings();
}

void USimulationFeatureRegistry::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ApplyDefaults();
	LoadConfigFromDisk();
	ApplyMapDrivenFeatureGates();
}

bool USimulationFeatureRegistry::LoadConfigFromDisk()
{
	const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Config"));
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*Dir))
	{
		PlatformFile.CreateDirectoryTree(*Dir);
	}

	const FString SavedPath = FPaths::Combine(Dir, TEXT("SimulationConfig.xml"));
	const FString ProjectPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Config/SimulationConfig.xml"));

	/*
	 * Precedence:
	 *   - Saved/Config/SimulationConfig.xml is the runtime overlay (local edits).
	 *   - If Saved is missing, OR Project is newer than Saved, copy Project → Saved
	 *     so editing Config/SimulationConfig.xml is respected on the next load/PIE.
	 *   - If Saved is newer (intentional local override), it is kept and overlays Project.
	 */
	if (PlatformFile.FileExists(*ProjectPath))
	{
		const bool bSavedExists = PlatformFile.FileExists(*SavedPath);
		bool bShouldSyncFromProject = !bSavedExists;
		if (bSavedExists)
		{
			const FDateTime ProjectTime = PlatformFile.GetTimeStamp(*ProjectPath);
			const FDateTime SavedTime = PlatformFile.GetTimeStamp(*SavedPath);
			bShouldSyncFromProject = ProjectTime > SavedTime;
		}

		if (bShouldSyncFromProject)
		{
			PlatformFile.CopyFile(*SavedPath, *ProjectPath);
			UE_LOG(LogTemp, Log,
				TEXT("[SimulationConfig] Synced Saved from Project (%s) — Project is authoritative when newer/missing Saved"),
				bSavedExists ? TEXT("Project newer than Saved") : TEXT("Saved missing"));
		}
	}

	// 1) Load project template as baseline (new Phase 2/3 keys always available).
	bool bLoadedAny = false;
	if (PlatformFile.FileExists(*ProjectPath))
	{
		bLoadedAny = FSimulationConfigLoader::LoadIntoRegistry(this, ProjectPath) || bLoadedAny;
	}

	const FSimulationClockSettings ProjectClock = SimulationClockSettings;
	const ESimulationMode ProjectMode = SimulationMode;
	const FFastSimulationSettings ProjectFast = FastSimulationSettings;

	// 2) Overlay Saved for local overrides without dropping Project clock/mode/fast if Saved omits them.
	if (PlatformFile.FileExists(*SavedPath))
	{
		FSimulationConfigLoader::FClockModeSectionPresence Presence;
		const bool bInspected = FSimulationConfigLoader::InspectClockModeSections(SavedPath, Presence);

		bLoadedAny = FSimulationConfigLoader::LoadIntoRegistry(this, SavedPath) || bLoadedAny;

		if (bInspected)
		{
			if (!Presence.bHasSimulationClock)
			{
				SetSimulationClockSettings(ProjectClock);
			}
			if (!Presence.bHasSimulationMode)
			{
				SetSimulationMode(ProjectMode);
			}
			if (!Presence.bHasFastSimulation)
			{
				SetFastSimulationSettings(ProjectFast);
			}
		}

		UE_LOG(LogTemp, Log,
			TEXT("[SimulationFeatureRegistry] Config merge: Project baseline + Saved overlay (clock=%s mode=%s fast=%s)"),
			Presence.bHasSimulationClock ? TEXT("Saved") : TEXT("Project"),
			Presence.bHasSimulationMode ? TEXT("Saved") : TEXT("Project"),
			Presence.bHasFastSimulation ? TEXT("Saved") : TEXT("Project"));
	}

	if (!bLoadedAny)
	{
		UE_LOG(LogTemp, Log, TEXT("[SimulationConfig] No SimulationConfig.xml found — defaults applied"));
		return false;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[SimulationConfig] Loaded simulation mode: %s | VisualClock=%.1f | FastClock=%.1f | FastMove×%.1f | AccurateClock=%.1f"),
		*GetSimulationModeLabel(),
		SimulationClockSettings.SimulationSpeed,
		FastSimulationSettings.SimulationSpeed,
		FastSimulationSettings.GetClampedMovementSpeedMultiplier(),
		AccurateTimeSettings.SimulationSpeed);

	return true;
}

void USimulationFeatureRegistry::ReloadConfig()
{
	ApplyDefaults();
	LoadConfigFromDisk();
	ApplyMapDrivenFeatureGates();
}

void USimulationFeatureRegistry::ApplyMapDrivenFeatureGates()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FString MapName = World->GetMapName();
	bValidationMapActive = false;

	const bool bIsMallMap = MapName.Contains(TEXT("MallA"), ESearchCase::IgnoreCase)
		|| MapName.Contains(TEXT("MallB"), ESearchCase::IgnoreCase);

	const bool bIsDailyUrbanLifeMap = MapName.Contains(TEXT("DailyUrban"), ESearchCase::IgnoreCase)
		|| MapName.Contains(TEXT("UrbanLife"), ESearchCase::IgnoreCase)
		|| MapName.Contains(TEXT("DailyLife"), ESearchCase::IgnoreCase);

	// Validation maps reuse the Daily Urban Life schedule pipeline (Idle / MoveTo only).
	const bool bIsValidationMap = MapName.Contains(TEXT("Validation"), ESearchCase::IgnoreCase)
		|| MapName.Contains(TEXT("V01_"), ESearchCase::IgnoreCase)
		|| MapName.Contains(TEXT("V02_"), ESearchCase::IgnoreCase)
		|| MapName.Contains(TEXT("V03_"), ESearchCase::IgnoreCase)
		|| MapName.Contains(TEXT("V04_"), ESearchCase::IgnoreCase)
		|| MapName.Contains(TEXT("V05_"), ESearchCase::IgnoreCase)
		|| MapName.Contains(TEXT("V06_"), ESearchCase::IgnoreCase)
		|| MapName.Contains(TEXT("V07_"), ESearchCase::IgnoreCase);

	bValidationMapActive = bIsValidationMap;

	if (bIsMallMap)
	{
		SetFeatureEnabled(ESimulationFeature::DailyUrbanLife, false);
		SetFeatureEnabled(ESimulationFeature::InfrastructureAnalysis, true);
		UE_LOG(LogTemp, Log,
			TEXT("[SimulationFeatureRegistry] Map '%s' → DailyUrbanLife OFF (mall / infrastructure use case)"),
			*MapName);
		return;
	}

	if (bIsDailyUrbanLifeMap || bIsValidationMap)
	{
		SetFeatureEnabled(ESimulationFeature::DailyUrbanLife, true);
		if (bIsValidationMap)
		{
			SetFeatureEnabled(ESimulationFeature::ValidationStats, true);
		}
		UE_LOG(LogTemp, Log,
			TEXT("[SimulationFeatureRegistry] Map '%s' → DailyUrbanLife ON%s"),
			*MapName,
			bIsValidationMap ? TEXT(" (validation schedule pipeline + ValidationStats)") : TEXT(""));
		return;
	}

	// Leadership / Cops / Evacuation / other maps: never leave DUL on from a shared config.
	if (IsFeatureEnabled(ESimulationFeature::DailyUrbanLife))
	{
		SetFeatureEnabled(ESimulationFeature::DailyUrbanLife, false);
		UE_LOG(LogTemp, Log,
			TEXT("[SimulationFeatureRegistry] Map '%s' → DailyUrbanLife OFF (not a Daily Urban Life map)"),
			*MapName);
	}
}

bool USimulationFeatureRegistry::IsFeatureEnabled(ESimulationFeature Feature) const
{
	if (const bool* Value = FeatureStates.Find(Feature))
	{
		return *Value;
	}
	return false;
}

void USimulationFeatureRegistry::SetFeatureEnabled(ESimulationFeature Feature, bool bEnabled)
{
	FeatureStates.Add(Feature, bEnabled);
}

FString USimulationFeatureRegistry::GetSimulationModeLabel() const
{
	switch (SimulationMode)
	{
	case ESimulationMode::Fast:
		return TEXT("Fast");
	case ESimulationMode::AccurateTime:
		return TEXT("AccurateTime");
	default:
		return TEXT("Visual");
	}
}

float USimulationFeatureRegistry::GetActiveClockSimulationSpeed() const
{
	switch (SimulationMode)
	{
	case ESimulationMode::Fast:
		return FastSimulationSettings.SimulationSpeed;
	case ESimulationMode::AccurateTime:
		return AccurateTimeSettings.SimulationSpeed;
	default:
		return SimulationClockSettings.SimulationSpeed;
	}
}

float USimulationFeatureRegistry::ScaleAuthoredDurationToRealSeconds(float AuthoredDuration) const
{
	if (AuthoredDuration <= 0.f || SimulationMode == ESimulationMode::Visual)
	{
		return AuthoredDuration;
	}

	if (SimulationMode == ESimulationMode::AccurateTime)
	{
		// Keep the Visual sim occupancy (authored real seconds × Visual clock).
		// Do not treat the XML number as already-simulation-seconds; that made a 14s sit last ~0.5s.
		return SimulationTimeContract::AccurateTimeWallSecondsForVisualDuration(
			AuthoredDuration,
			SimulationClockSettings.SimulationSpeed,
			AccurateTimeSettings.SimulationSpeed);
	}

	const float Speed = GetActiveClockSimulationSpeed();
	if (Speed <= KINDA_SMALL_NUMBER)
	{
		return AuthoredDuration;
	}

	// Fast only: authored duration is simulation seconds.
	return AuthoredDuration / Speed;
}
