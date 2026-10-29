#pragma once

#include "CoreMinimal.h"
#include "SimulationTypes.generated.h"

class AExitPoint;

/** Feature toggles loaded from SimulationConfig.xml — query via USimulationFeatureRegistry only. */
UENUM(BlueprintType)
enum class ESimulationFeature : uint8
{
	Evacuation UMETA(DisplayName = "Evacuation"),
	Leadership UMETA(DisplayName = "Leadership"),
	CopsAndRobbers UMETA(DisplayName = "Cops And Robbers"),
	InfrastructureAnalysis UMETA(DisplayName = "Infrastructure Analysis"),
	DailyUrbanLife UMETA(DisplayName = "Daily Urban Life"),
	/** On-screen validation statistics panel + sampling (auto-on for Validation/V0*_ maps). */
	ValidationStats UMETA(DisplayName = "Validation Stats")
};

/** Category for home / work / public areas (Use Case 2 analytics). */
UENUM(BlueprintType)
enum class EDailyDestinationCategory : uint8
{
	None UMETA(DisplayName = "None"),
	Residential UMETA(DisplayName = "Residential"),
	Business UMETA(DisplayName = "Business"),
	Public UMETA(DisplayName = "Public")
};

/** Active high-level simulation scenario (reusable across future use cases). */
UENUM(BlueprintType)
enum class ESimulationScenario : uint8
{
	None UMETA(DisplayName = "None"),
	DailyUrbanLife UMETA(DisplayName = "Daily Urban Life"),
	EmergencyEvacuation UMETA(DisplayName = "Emergency Evacuation"),
	InfrastructurePlanning UMETA(DisplayName = "Infrastructure Planning")
};

/** Crowd routing modes — UCrowdRoutingService dispatches by mode. */
UENUM(BlueprintType)
enum class ECrowdRoutingMode : uint8
{
	Evacuation UMETA(DisplayName = "Evacuation"),
	Leadership UMETA(DisplayName = "Leadership"),
	Pursuit UMETA(DisplayName = "Pursuit"),
	Civilian UMETA(DisplayName = "Civilian")
};

/**
 * Simulation clock settings (SimulationConfig.xml <simulationClock>).
 * SimulationSpeed = sim seconds advanced per real second.
 * Default 60 preserves legacy schedule pace (1 real second ≈ 1 sim minute).
 * StartHour/StartMinute = time-of-day when the simulation begins (not elapsed offset).
 */
USTRUCT(BlueprintType)
struct FSimulationClockSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Clock", meta = (ClampMin = "0"))
	float SimulationSpeed = 60.f;

	/** Simulation clock hour at Reset() (0..23). Default 8 = 08:00 AM. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Clock", meta = (ClampMin = "0", ClampMax = "23"))
	int32 StartHour = 8;

	/** Simulation clock minute at Reset() (0..59). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Clock", meta = (ClampMin = "0", ClampMax = "59"))
	int32 StartMinute = 0;
};

/**
 * Visual = existing debug/playable sim (clock and walk speed are intentionally not locked).
 * Fast = existing accelerated batch (clock 600x, walk multiplier separate).
 * AccurateTime = clock and movement share one scale (default 30 sim seconds per real second).
 */
UENUM(BlueprintType)
enum class ESimulationMode : uint8
{
	Visual UMETA(DisplayName = "Visual"),
	Fast UMETA(DisplayName = "Fast"),
	AccurateTime UMETA(DisplayName = "Accurate Time")
};

/**
 * Accurate Time settings (SimulationConfig.xml <accurateTime>).
 * SimulationSpeed is sim seconds per real second AND the single walk-speed scale.
 * Do not also multiply movement DeltaTime.
 */
USTRUCT(BlueprintType)
struct FAccurateTimeSettings
{
	GENERATED_BODY()

	/** Sim seconds per real second. Also the only movement scale (MaxWalkSpeed *= this). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|AccurateTime", meta = (ClampMin = "1"))
	float SimulationSpeed = 30.f;

	/**
	 * End the stats run after this many simulation-clock minutes from the session start.
	 * Mall A/B schedules run 08:00–10:30 (150 min); 180 reaches 11:00 so the last exit walk finishes.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|AccurateTime", meta = (ClampMin = "0"))
	float RunDurationSimMinutes = 180.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|AccurateTime")
	int32 RandomSeed = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|AccurateTime")
	bool bAutoExportOnEnd = true;

	/** Max schedule HH:MM catch-up steps per frame. Leftover minutes continue next frame (not dropped). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|AccurateTime", meta = (ClampMin = "1"))
	int32 MaxScheduleMinuteStepsPerFrame = 120;
};

/** Fast Mode settings (SimulationConfig.xml <fastSimulation>). Unused when mode is Visual. */
USTRUCT(BlueprintType)
struct FFastSimulationSettings
{
	GENERATED_BODY()

	/** Sim seconds per real second while Fast Mode is active (overrides clock speed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Fast", meta = (ClampMin = "1"))
	float SimulationSpeed = 600.f;

	/**
	 * Multiplies Character MaxWalkSpeed in Fast Mode only (Visual stays 1×).
	 * Capped in code at MaxMovementSpeedMultiplier for CMC/NavMesh stability.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Fast", meta = (ClampMin = "1"))
	float MovementSpeedMultiplier = 8.f;

	/** Hard ceiling for MovementSpeedMultiplier (applied at use sites). */
	static constexpr float MaxMovementSpeedMultiplier = 12.f;

	/** End Fast run after this many simulation-clock minutes from session start (hard upper bound). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Fast", meta = (ClampMin = "0"))
	float RunDurationSimMinutes = 60.f;

	/** 0 = do not force seed. Non-zero calls FMath::RandInit / SRandInit at Fast session start. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Fast")
	int32 RandomSeed = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Fast")
	bool bAutoExportOnEnd = true;

	/** Max schedule HH:MM catch-up steps per frame while Fast (high speed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Fast", meta = (ClampMin = "1"))
	int32 MaxScheduleMinuteStepsPerFrame = 720;

	/** Clamped multiplier used by agents (Visual callers must not use this). */
	float GetClampedMovementSpeedMultiplier() const
	{
		return FMath::Clamp(MovementSpeedMultiplier, 1.f, MaxMovementSpeedMultiplier);
	}
};

UENUM(BlueprintType)
enum class EEvacuationPhase : uint8
{
	Idle UMETA(DisplayName = "Idle"),
	Evacuating UMETA(DisplayName = "Evacuating"),
	AllClear UMETA(DisplayName = "All Clear")
};

/** Tunable evacuation routing parameters (loaded from SimulationConfig.xml). */
USTRUCT(BlueprintType)
struct FEvacuationRoutingSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evacuation")
	float GlobalRadius = 999999.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evacuation")
	float DistanceWeight = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evacuation")
	float CongestionWeight = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evacuation")
	float CongestionReevaluationIntervalSeconds = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evacuation")
	float CongestionSwitchThreshold = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evacuation")
	int32 MaxReassignmentAttempts = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evacuation")
	bool bPreferNearestExitInitially = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evacuation")
	float NearestExitBias = 0.5f;

	/** Per-agent reaction delay range (seconds) before StartFlee. 0/0 = immediate (legacy). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evacuation|Panic")
	float ReactionDelayMin = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evacuation|Panic")
	float ReactionDelayMax = 5.f;

	/** Countdown duration (seconds). 0 = no auto timer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evacuation|Timer")
	float EvacuationDuration = 120.f;

	/** Delay before countdown begins after evacuation starts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evacuation|Timer")
	float EvacuationTimerStartDelay = 0.f;

	/** Multiplier applied to FleeMaxSpeed per agent (1.0 = no change). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evacuation|Panic")
	float SpeedVariationMin = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evacuation|Panic")
	float SpeedVariationMax = 1.15f;

	/** Random offset added to congestion switch threshold per agent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evacuation|Panic")
	float CongestionToleranceVariation = 0.15f;

	/** Random scale on nearest-exit bias per agent (1.0 = neutral). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evacuation|Panic")
	float NearestExitBiasVariation = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evacuation")
	bool bEnableCasualties = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evacuation")
	bool bEnableCelebrationState = true;
};

/** Per-exit score from UCrowdRoutingService (lower is better). */
USTRUCT(BlueprintType)
struct FCrowdRouteScore
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Routing")
	TObjectPtr<AExitPoint> Exit = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Routing")
	float CompositeScore = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Routing")
	float DistanceScore = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Routing")
	float CongestionScore = 0.f;
};

/** Live evacuation metrics snapshot for UI / analytics export. */
USTRUCT(BlueprintType)
struct FEvacuationAnalyticsSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Analytics")
	int32 AgentsFleeing = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Analytics")
	int32 AgentsSafeAtExit = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Analytics")
	int32 TotalEvacuated = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Analytics")
	int32 TotalReassignments = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Analytics")
	float ElapsedSeconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Analytics")
	float AverageEvacuationTimeSeconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Analytics")
	float LongestEvacuationTimeSeconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Analytics")
	int32 TotalCasualties = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Analytics")
	int32 SafeZoneArrivals = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Analytics")
	float AverageReactionTimeSeconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Analytics")
	float TimerRemainingSeconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Analytics")
	bool bTimerExpired = false;

	UPROPERTY(BlueprintReadOnly, Category = "Analytics")
	float EvacuationCompletionRate = 0.f;
};

/** Tunable daily-life parameters (loaded from SimulationConfig.xml). */
USTRUCT(BlueprintType)
struct FDailyUrbanLifeSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Daily Urban Life")
	bool bEnableNeedsDuringDailyLife = true;

	/** Scale applied to hunger/energy rates during UC2 (lower = slower needs). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Daily Urban Life")
	float NeedsRateScale = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Daily Urban Life")
	bool bStartAgentsAtHome = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Daily Urban Life")
	int32 MaxScheduleMinuteOffset = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Daily Urban Life")
	float WalkingSpeedMin = 170.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Daily Urban Life")
	float WalkingSpeedMax = 230.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Daily Urban Life")
	float InteractionPauseMinSeconds = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Daily Urban Life")
	float InteractionPauseMaxSeconds = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Daily Urban Life")
	FString DefaultScheduleFile = TEXT("Schedules/DailyUrbanLifeSchedule.xml");

	/** Scheduled MoveTo home rules take priority over hunger during UC2. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Daily Urban Life")
	bool bScheduledHomeReturnOverridesNeeds = true;

	/** Optional lunch-break destination tag (e.g. Plaza). Empty = pause in place only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Daily Urban Life")
	FString BreakAreaTag = TEXT("Plaza");

	/** Minimum recommended desks per WorkN area (used by map validation warnings). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Daily Urban Life")
	int32 MinStationsPerWorkArea = 4;
};

/** Live daily-life metrics snapshot for UI / analytics export. */
USTRUCT(BlueprintType)
struct FDailyUrbanLifeAnalyticsSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Analytics|Daily")
	int32 AgentsAtHome = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Analytics|Daily")
	int32 AgentsAtWork = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Analytics|Daily")
	int32 DestinationsVisited = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Analytics|Daily")
	int32 InteractionCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Analytics|Daily")
	int32 SidewalkUtilizationHits = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Analytics|Daily")
	float AverageCommuteTimeSeconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Analytics|Daily")
	float ElapsedSimMinutes = 0.f;
};

/** Path segment category for utilization analysis. */
UENUM(BlueprintType)
enum class EPathSegmentCategory : uint8
{
	Sidewalk UMETA(DisplayName = "Sidewalk"),
	Road UMETA(DisplayName = "Road"),
	Crossing UMETA(DisplayName = "Crossing")
};

/** Infrastructure planning settings from SimulationConfig.xml. */
USTRUCT(BlueprintType)
struct FInfrastructurePlanningSettings
{
	GENERATED_BODY()

	/** Active layout label for this run (e.g. MallA, MallB). Overridden from map name when AutoSelectFromMap is true. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Infrastructure Planning")
	FString LayoutVariantName = TEXT("MallA");

	/** Baseline layout name used when exporting comparison reports (e.g. MallA vs MallB). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Infrastructure Planning")
	FString ComparisonBaselineVariant = TEXT("MallA");

	/**
	 * Schedule path relative to project (e.g. Schedules/InfrastructurePlanningSchedule_MallA.xml).
	 * When AutoSelectFromMap is true, replaced with Schedules/InfrastructurePlanningSchedule_<MallA|MallB>.xml.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Infrastructure Planning")
	FString ScheduleFile = TEXT("Schedules/InfrastructurePlanningSchedule_MallA.xml");

	/**
	 * When true, detect MallA/MallB from the loaded map name and pick matching
	 * layoutVariant, scheduleFile, and default serviceStationCapacity (1 / 2).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Infrastructure Planning")
	bool bAutoSelectFromMap = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Infrastructure Planning")
	float SamplingIntervalSeconds = 5.f;

	/**
	 * Elapsed simulation-clock minutes before the infrastructure run finalizes.
	 * 180 covers Mall A/B through the 10:30 exits (start 08:00 is 150 min) plus the walk out.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Infrastructure Planning")
	float RunDurationSimMinutes = 180.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Infrastructure Planning")
	bool bEnableVisualization = true;

	/** Assign agent home/service destinations from level tags (existing pedestrian bootstrap). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Infrastructure Planning")
	bool bReuseDailyUrbanLifeBootstrap = true;

	/** Applied to stations matching ServiceStationTag (0 = keep per-station editor value). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Infrastructure Planning")
	int32 ServiceStationCapacity = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Infrastructure Planning")
	FName ServiceStationTag = FName(TEXT("ServicePoint"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Infrastructure Planning")
	bool bAutoExportOnEnd = true;

	/** After a session CSV export, auto-build Mall A vs Mall B comparison if both CSVs exist. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Infrastructure Planning|Verification")
	bool bAutoExportComparisonOnEnd = true;

	/** Alternate layout name paired with ComparisonBaselineVariant for V&V (e.g. MallB). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Infrastructure Planning|Verification")
	FString ComparisonVariantName = TEXT("MallB");
};

/** Tunable leadership/follow parameters (loaded from SimulationConfig.xml). */
USTRUCT(BlueprintType)
struct FLeadershipSettings
{
	GENERATED_BODY()

	/** Preferred distance behind/near the leader when following. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Leadership")
	float DefaultFollowDistance = 200.f;

	/** How often followers refresh their follow destination (seconds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Leadership")
	float FormationUpdateInterval = 0.5f;

	/** When true, active followers defer schedule commands while following. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Leadership")
	bool bLeadershipOverridesSchedule = true;
};

/** Tunable cops & robbers parameters (loaded from SimulationConfig.xml). */
USTRUCT(BlueprintType)
struct FCopsAndRobbersSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CopsAndRobbers")
	float PoliceDetectionRadius = 1200.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CopsAndRobbers")
	float RobberDetectionRadius = 900.f;

	/** Chance per CrimeCheckInterval that an idle robber enters Crime state (0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CopsAndRobbers", meta = (ClampMin = "0", ClampMax = "1"))
	float CrimeProbability = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CopsAndRobbers")
	float CrimeCheckInterval = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CopsAndRobbers")
	float CrimeDuration = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CopsAndRobbers")
	float CaptureRadius = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CopsAndRobbers")
	float PursuitUpdateInterval = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CopsAndRobbers")
	float FleeDistance = 800.f;

	/** Character MaxWalkSpeed while police pursue or robber flees (run). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CopsAndRobbers", meta = (ClampMin = "1"))
	float ChaseRunSpeed = 450.f;

	/** Civilians optionally move away from nearby robbers in Crime state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CopsAndRobbers")
	bool bCiviliansAvoidRobbers = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CopsAndRobbers")
	float CivilianAvoidRadius = 700.f;
};

/** Live cops & robbers metrics snapshot. */
USTRUCT(BlueprintType)
struct FCopsAndRobbersAnalyticsSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Analytics|Crime")
	int32 CrimesStarted = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Analytics|Crime")
	int32 PoliceResponses = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Analytics|Crime")
	int32 CaptureCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Analytics|Crime")
	float AveragePursuitTimeSeconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Analytics|Crime")
	float ElapsedSeconds = 0.f;
};

/** Per-station infrastructure metrics snapshot. */
USTRUCT(BlueprintType)
struct FInfrastructureStationMetrics
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Infrastructure")
	FString StationName;

	UPROPERTY(BlueprintReadOnly, Category = "Infrastructure")
	int32 PeakQueueLength = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Infrastructure")
	float AverageQueueWaitSeconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Infrastructure")
	int32 TotalThroughput = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Infrastructure")
	float PeakUtilization = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Infrastructure")
	float CurrentUtilization = 0.f;
};

/** Per-path-segment utilization snapshot. */
USTRUCT(BlueprintType)
struct FInfrastructurePathUtilizationEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Infrastructure")
	FString SegmentName;

	UPROPERTY(BlueprintReadOnly, Category = "Infrastructure")
	EPathSegmentCategory Category = EPathSegmentCategory::Sidewalk;

	UPROPERTY(BlueprintReadOnly, Category = "Infrastructure")
	int32 HitCount = 0;
};

/** Live infrastructure-planning metrics snapshot for UI / export. */
USTRUCT(BlueprintType)
struct FInfrastructureAnalyticsSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Infrastructure")
	FString LayoutVariantName;

	UPROPERTY(BlueprintReadOnly, Category = "Infrastructure")
	float AverageTravelTimeSeconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Infrastructure")
	float AverageQueueWaitSeconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Infrastructure")
	int32 TotalPedestriansProcessed = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Infrastructure")
	float AverageCongestionLevel = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Infrastructure")
	float PeakCongestionLevel = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Infrastructure")
	float CongestionDurationSeconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Infrastructure")
	float ElapsedSimMinutes = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Infrastructure")
	TArray<FInfrastructureStationMetrics> StationMetrics;

	UPROPERTY(BlueprintReadOnly, Category = "Infrastructure")
	TArray<FInfrastructurePathUtilizationEntry> PathUtilization;

	UPROPERTY(BlueprintReadOnly, Category = "Infrastructure")
	TMap<FString, int32> DestinationPopularity;
};

/** Tunable validation debug-panel settings (SimulationConfig.xml). */
USTRUCT(BlueprintType)
struct FValidationAnalyticsSettings
{
	GENERATED_BODY()

	/** When true, the debug panel draws on screen (still requires ValidationStats feature). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation|Debug")
	bool bShowOnScreenPanel = true;

	/** How often live world samples (queues, density, waiting) refresh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation|Debug", meta = (ClampMin = "0.1"))
	float SampleIntervalSeconds = 0.5f;

	/** Speed (cm/s) at or below which an agent counts as waiting / nearly stationary. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation|Debug", meta = (ClampMin = "0"))
	float WaitingSpeedThreshold = 20.f;

	/** Speed (cm/s) at or below which an agent accumulates congestion time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation|Debug", meta = (ClampMin = "0"))
	float CongestionSpeedThreshold = 80.f;
};

/**
 * Live validation / pedestrian-flow metrics snapshot.
 * Travel / completion reuse schedule destination hooks; queues reuse CongestionAnalyzer.
 */
USTRUCT(BlueprintType)
struct FValidationAnalyticsSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Analytics|Validation")
	FString ScenarioName;

	UPROPERTY(BlueprintReadOnly, Category = "Analytics|Validation")
	float SimulationTimeSeconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Analytics|Validation")
	int32 AgentsSpawned = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Analytics|Validation")
	int32 AgentsActive = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Analytics|Validation")
	int32 AgentsCompleted = 0;

	/** -1 = not supported by the framework. */
	UPROPERTY(BlueprintReadOnly, Category = "Analytics|Validation")
	int32 AgentsFailed = -1;

	UPROPERTY(BlueprintReadOnly, Category = "Analytics|Validation")
	float AverageTravelTimeSeconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Analytics|Validation")
	float MinTravelTimeSeconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Analytics|Validation")
	float MaxTravelTimeSeconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Analytics|Validation")
	float AverageSpeed = 0.f;

	/** -1 = path length not tracked. */
	UPROPERTY(BlueprintReadOnly, Category = "Analytics|Validation")
	float AveragePathLength = -1.f;

	UPROPERTY(BlueprintReadOnly, Category = "Analytics|Validation")
	float ThroughputAgentsPerSecond = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Analytics|Validation")
	float FlowRate = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Analytics|Validation")
	float AverageQueueLength = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Analytics|Validation")
	int32 MaximumQueueLength = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Analytics|Validation")
	float PeakDensity = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Analytics|Validation")
	float CurrentDensity = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Analytics|Validation")
	float AverageWaitingTimeSeconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Analytics|Validation")
	float MaximumWaitingTimeSeconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Analytics|Validation")
	float AverageCongestionTimeSeconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Analytics|Validation")
	float FPS = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Analytics|Validation")
	int32 CurrentAgentCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Analytics|Validation")
	int32 CurrentMovingAgents = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Analytics|Validation")
	int32 CurrentWaitingAgents = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Analytics|Validation")
	float CurrentAverageVelocity = 0.f;
};
