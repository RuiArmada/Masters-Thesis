#include "ScheduleManager.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/FileHelper.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "BoidsAB.h"
#include "IncidentManager.h"
#include "XmlFile.h"
#include "Engine/Engine.h"
#include "AgentArea.h"
#include "AgentStation.h"
#include "AI/AgentPrioritySelectorComponent.h"
#include "AI/AgentFallbackHandlerComponent.h"
#include "AI/AgentCrimeRoleComponent.h"
#include "AI/AgentAIDebug.h"
#include "Emergency/EmergencyManager.h"
#include "Simulation/SimulationAnalytics.h"
#include "Simulation/SimulationFeatureRegistry.h"
#include "Actions/AgentAction.h"
#include "Actions/GoToAction.h"
#include "Actions/InteractAction.h"
#include "Actions/Interactable.h"
#include "Simulation/SimulationClock.h"
#include "Simulation/RunRecorderSubsystem.h"

namespace ScheduleAILayer
{
static bool ShouldDeferScheduleForCriticalNeeds(ABoidsAB* Boid)
{
	if (!Boid)
	{
		return false;
	}
	if (UAgentPrioritySelectorComponent* PS = Boid->GetPrioritySelector())
	{
		return PS->ShouldDeferScheduleCommands(Boid);
	}
	return false;
}

static bool ShouldDeferScheduleForCriticalNeedsWithTarget(
	ABoidsAB* Boid,
	EScheduledAction Action,
	const FString& TargetTag,
	UWorld* World)
{
	if (!Boid || !World)
	{
		return false;
	}

	if (USimulationFeatureRegistry* Registry = World->GetSubsystem<USimulationFeatureRegistry>())
	{
		// Pedestrian-flow maps only run Idle/MoveTo; needs must not block the schedule.
		if (Registry->IsValidationMap())
		{
			return false;
		}

		if ((Action == EScheduledAction::SA_MoveTo || Action == EScheduledAction::SA_Interact)
			&& !TargetTag.IsEmpty())
		{
			if (Registry->IsFeatureEnabled(ESimulationFeature::DailyUrbanLife)
				&& Registry->GetDailyUrbanLifeSettings().bScheduledHomeReturnOverridesNeeds
				&& TargetTag.StartsWith(TEXT("Home"), ESearchCase::IgnoreCase))
			{
				return false;
			}
		}
	}

	return ShouldDeferScheduleForCriticalNeeds(Boid);
}

static bool IsTriggerExemptFromStationBlock(EScheduledAction Action, const FString& TriggerName)
{
	if (Action != EScheduledAction::SA_Trigger)
	{
		return false;
	}

	return TriggerName.Equals(TEXT("Break"), ESearchCase::IgnoreCase)
		|| TriggerName.Equals(TEXT("Pause"), ESearchCase::IgnoreCase);
}

static bool IsExemptFromStationBlock(EScheduledAction Action, const FString& TriggerName)
{
	if (Action == EScheduledAction::SA_StartEvacuation)
	{
		return true;
	}

	return IsTriggerExemptFromStationBlock(Action, TriggerName);
}

static bool IsScheduledHomeMoveTarget(const FString& TargetTag, UWorld* World)
{
	if (TargetTag.IsEmpty() || !World)
	{
		return false;
	}

	if (!TargetTag.StartsWith(TEXT("Home"), ESearchCase::IgnoreCase))
	{
		return false;
	}

	if (USimulationFeatureRegistry* Registry = World->GetSubsystem<USimulationFeatureRegistry>())
	{
		return Registry->IsFeatureEnabled(ESimulationFeature::DailyUrbanLife)
			&& Registry->GetDailyUrbanLifeSettings().bScheduledHomeReturnOverridesNeeds;
	}

	return false;
}

static bool BlocksForStationState(
	ABoidsAB* Boid,
	EScheduledAction Action,
	const FString& TriggerName,
	const FString& TargetTag,
	UWorld* World)
{
	if (!Boid || IsExemptFromStationBlock(Action, TriggerName))
	{
		return false;
	}

	if (Action == EScheduledAction::SA_MoveTo && IsScheduledHomeMoveTarget(TargetTag, World))
	{
		return false;
	}

	return Boid->IsInteractingWithStation() || Boid->IsWaitingAtStation()
		|| Boid->HasActiveStationInteractionPipeline();
}

/**
 * Defer normal schedule work while a non-terminal Action is still running.
 * Prevents StartScheduleAction from cancelling in-flight GoTo/Interact when the
 * clock catch-up makes multiple HH:MM rules due in one frame (esp. Fast Mode).
 * StartEvacuation remains allowed (emergency override).
 */
static bool ShouldDeferForActiveScheduleAction(ABoidsAB* Boid, EScheduledAction Action)
{
	if (!Boid || Action == EScheduledAction::SA_StartEvacuation)
	{
		return false;
	}

	if (const UAgentAction* Active = Boid->GetActiveScheduleAction())
	{
		if (!Active->IsTerminal())
		{
			return true;
		}
	}

	return false;
}

/** Agent is mid schedule movement/station — do not stale-skip deferred rules. */
static bool IsAgentScheduleBusy(ABoidsAB* Boid)
{
	if (!Boid)
	{
		return false;
	}

	if (const UAgentAction* Active = Boid->GetActiveScheduleAction())
	{
		if (!Active->IsTerminal())
		{
			return true;
		}
	}

	return Boid->IsMoving()
		|| Boid->IsInteractingWithStation()
		|| Boid->IsWaitingAtStation()
		|| Boid->HasActiveStationInteractionPipeline();
}

static bool ShouldBlockScheduleForWorldIncidents(UWorld* World, EScheduledAction Action)
{
	if (Action == EScheduledAction::SA_StartEvacuation)
	{
		return false;
	}

	if (!World)
	{
		return false;
	}

	if (AIncidentManager* IncidentMgr = Cast<AIncidentManager>(
		UGameplayStatics::GetActorOfClass(World, AIncidentManager::StaticClass())))
	{
		if (IncidentMgr->HasActiveIncidents())
		{
			return true;
		}
	}

	if (AEmergencyManager* EmergencyMgr = Cast<AEmergencyManager>(
		UGameplayStatics::GetActorOfClass(World, AEmergencyManager::StaticClass())))
	{
		if (EmergencyMgr->IsEvacuationActive())
		{
			return true;
		}
	}

	return false;
}

static TArray<FStationInteraction> BuildOrSynthesizeInteractionSteps(
	const TArray<FStationInteraction>& ExistingSteps,
	EScheduledAction Action,
	const FString& AreaTag,
	const FString& StationTypeName,
	float Duration)
{
	if (ExistingSteps.Num() > 0)
	{
		return ExistingSteps;
	}

	TArray<FStationInteraction> Steps;
	if (Action != EScheduledAction::SA_UseStation || AreaTag.IsEmpty())
	{
		return Steps;
	}

	FStationInteraction Step;
	Step.AreaTag = AreaTag;
	Step.StationTypeName = StationTypeName;
	Step.Duration = Duration;
	Steps.Add(Step);
	return Steps;
}

static int32 MinutesElapsedSince(int32 FromMinute, int32 ToMinute)
{
	if (ToMinute >= FromMinute)
	{
		return ToMinute - FromMinute;
	}

	return (1440 - FromMinute) + ToMinute;
}

static void NotifyScheduleExecutionFailure(ABoidsAB* Boid, EAgentFallbackFailureKind Kind, const FString& Context)
{
	if (!Boid)
	{
		return;
	}
	if (UAgentFallbackHandlerComponent* FH = Boid->GetFallbackHandler())
	{
		FH->NotifyScheduleFailure(Boid, Kind, Context);
	}
}
}

AScheduleManager::AScheduleManager()
{
    PrimaryActorTick.bCanEverTick = true;
    AccumulatedSimSeconds = 0.0f;
    CurrentSimMinute = 0;
    PrevSimMinute = -1;
    DayCounter = 0;
}

void AScheduleManager::EnsureSimulationClock()
{
    if (!SimulationClock)
    {
        SimulationClock = NewObject<USimulationClock>(this, TEXT("SimulationClock"));
    }
}

void AScheduleManager::SyncClockCompatibilityFields()
{
    if (!SimulationClock)
    {
        return;
    }

    PrevSimMinute = CurrentSimMinute;
    AccumulatedSimSeconds = SimulationClock->GetAccumulatedSimMinutes();
    CurrentSimMinute = SimulationClock->GetCurrentDayMinute();
    DayCounter = SimulationClock->GetDay();
}

void AScheduleManager::BeginPlay()
{
    Super::BeginPlay();

    EnsureSimulationClock();

    UWorld* World = GetWorld();
    USimulationFeatureRegistry* Registry = World
        ? World->GetSubsystem<USimulationFeatureRegistry>()
        : nullptr;

    // Re-read SimulationConfig.xml on every play so Visual/Fast edits are not stale.
    if (Registry)
    {
        Registry->ReloadConfig();

        // Validation maps reuse the DUL schedule pipeline but keep the map-authored XML path.
        if (Registry->IsFeatureEnabled(ESimulationFeature::DailyUrbanLife)
            && !Registry->IsValidationMap())
        {
            RelativeSchedulePath = Registry->GetDailyUrbanLifeSettings().DefaultScheduleFile;
        }

        if (Registry->IsValidationMap())
        {
            // Validation XML uses 00:00 / 00:04 as scenario start. Global StartHour=8 would
            // put those rules ~480 min in the past; the 45-min stale window then marks them
            // executed without MoveTo, so agents never leave spawn (Accurate Time also ends).
            SimulationClock->SetStartTimeOfDay(0, 0);
            UE_LOG(LogTemp, Log,
                TEXT("[ScheduleManager] Validation map — clock start forced to 00:00 (schedule times are midnight-based)"));
        }
        else
        {
            const FSimulationClockSettings& ClockSettings = Registry->GetSimulationClockSettings();
            SimulationClock->SetStartTimeOfDay(ClockSettings.StartHour, ClockSettings.StartMinute);
        }
    }

    ApplySimulationModeFromRegistry();
    SimulationClock->Reset();

    // Start the schedule cursor one minute before clock start so HH:MM rules
    // fire at StartHour:StartMinute without replaying midnight→start.
    {
        const int32 StartDay = SimulationClock->GetDay();
        const int32 StartMinuteOfDay = SimulationClock->GetCurrentDayMinute();
        if (StartMinuteOfDay > 0)
        {
            ScheduleCursorDay = StartDay;
            ScheduleCursorDayMinute = StartMinuteOfDay - 1;
        }
        else if (StartDay > 0)
        {
            ScheduleCursorDay = StartDay - 1;
            ScheduleCursorDayMinute = USimulationClock::MinutesPerDay - 1;
        }
        else
        {
            ScheduleCursorDay = 0;
            ScheduleCursorDayMinute = -1;
        }
    }

    // Baseline is clock time-of-day after Reset (08:00 = 480 sim-min), not zero.
    // Capturing it before Reset makes Accurate Time treat the start hour as elapsed and end immediately.
    if (Registry && (Registry->IsFastSimulationMode() || Registry->IsAccurateTimeMode()))
    {
        FastSessionStartSimMinutes = SimulationClock->GetAccumulatedSimMinutes();
    }

    SyncClockCompatibilityFields();
    UE_LOG(LogTemp, Log,
        TEXT("[ScheduleManager] SimulationClock ready — speed=%.1fx (sim sec / real sec), start=%s (configured %02d:%02d)"),
        SimulationClock->GetSimulationSpeed(),
        *SimulationClock->ToTimeOfDayHHMMString(),
        SimulationClock->GetStartHour(),
        SimulationClock->GetStartMinute());

    if (!EnsureScheduleFileInSaved(RelativeSchedulePath, FullScheduleFilePath))
    {
        FullScheduleFilePath = FPaths::Combine(FPaths::ProjectSavedDir(), RelativeSchedulePath);

        FString Dir = FPaths::GetPath(FullScheduleFilePath);
        IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
        if (!PlatformFile.DirectoryExists(*Dir))
        {
            PlatformFile.CreateDirectoryTree(*Dir);
        }
    }

    LoadSchedulesFromFile();

    UE_LOG(LogTemp, Log, TEXT("[ScheduleManager] Initialized. Schedule file: %s"), *FullScheduleFilePath);
}

void AScheduleManager::ApplySimulationModeFromRegistry()
{
    EnsureSimulationClock();
    bFastRunFinalized = false;
    FastSessionStartSimMinutes = 0.f;

    UWorld* World = GetWorld();
    USimulationFeatureRegistry* Registry = World ? World->GetSubsystem<USimulationFeatureRegistry>() : nullptr;
    if (!Registry)
    {
        SimulationClock->SetSimulationSpeed(USimulationClock::DefaultSimulationSpeed);
        return;
    }

    if (Registry->IsFastSimulationMode())
    {
        const FFastSimulationSettings& Fast = Registry->GetFastSimulationSettings();
        SimulationClock->SetSimulationSpeed(Fast.SimulationSpeed);
        MaxScheduleMinuteStepsPerFrame = Fast.MaxScheduleMinuteStepsPerFrame;

        if (Fast.RandomSeed != 0)
        {
            FMath::RandInit(Fast.RandomSeed);
            FMath::SRandInit(Fast.RandomSeed);
        }

        FastSessionStartSimMinutes = SimulationClock->GetAccumulatedSimMinutes();

        if (USimulationAnalytics* Analytics = World->GetSubsystem<USimulationAnalytics>())
        {
            FString MapName = World->GetMapName();
            MapName.RemoveFromStart(World->StreamingLevelsPrefix);
            const FString RunId = FString::Printf(TEXT("Fast_%s_%s"),
                *MapName,
                *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
            Analytics->BeginFastSession(RunId, MapName, Fast.SimulationSpeed, Fast.RandomSeed);
        }

        RefreshFastMovementSpeedsOnAgents();

        UE_LOG(LogTemp, Log, TEXT("[SimulationMode] Active mode: Fast"));
        UE_LOG(LogTemp, Log,
            TEXT("[SimulationMode] Simulation speed: %.1f (sim sec / real sec) | move×%.1f (cap %.1f) | duration=%.1f sim-min"),
            SimulationClock->GetSimulationSpeed(),
            Fast.GetClampedMovementSpeedMultiplier(),
            FFastSimulationSettings::MaxMovementSpeedMultiplier,
            Fast.RunDurationSimMinutes);
    }
    else if (Registry->IsAccurateTimeMode())
    {
        const FAccurateTimeSettings& Accurate = Registry->GetAccurateTimeSettings();
        // One scale: clock speed. Walk speed uses the same value in RefreshWalkSpeed (not a second multiply).
        SimulationClock->SetSimulationSpeed(Accurate.SimulationSpeed);
        MaxScheduleMinuteStepsPerFrame = Accurate.MaxScheduleMinuteStepsPerFrame;

        if (Accurate.RandomSeed != 0)
        {
            FMath::RandInit(Accurate.RandomSeed);
            FMath::SRandInit(Accurate.RandomSeed);
        }

        FastSessionStartSimMinutes = SimulationClock->GetAccumulatedSimMinutes();

        if (USimulationAnalytics* Analytics = World->GetSubsystem<USimulationAnalytics>())
        {
            FString MapName = World->GetMapName();
            MapName.RemoveFromStart(World->StreamingLevelsPrefix);
            const FString RunId = FString::Printf(TEXT("AccurateTime_%s_%s"),
                *MapName,
                *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
            Analytics->BeginAccurateTimeSession(RunId, MapName, Accurate.SimulationSpeed, Accurate.RandomSeed);
        }

        RefreshFastMovementSpeedsOnAgents();

        UE_LOG(LogTemp, Log, TEXT("[SimulationMode] Active mode: AccurateTime"));
        UE_LOG(LogTemp, Log,
            TEXT("[SimulationMode] Simulation speed: %.1f sim-sec/real-sec (movement uses this once via MaxWalkSpeed) | duration=%.1f sim-min"),
            SimulationClock->GetSimulationSpeed(),
            Accurate.RunDurationSimMinutes);
    }
    else
    {
        SimulationClock->SetSimulationSpeed(Registry->GetSimulationClockSettings().SimulationSpeed);
        RefreshFastMovementSpeedsOnAgents();
        UE_LOG(LogTemp, Log, TEXT("[SimulationMode] Active mode: Visual"));
        UE_LOG(LogTemp, Log,
            TEXT("[SimulationMode] Simulation speed: %.1f (sim sec / real sec)"),
            SimulationClock->GetSimulationSpeed());
    }
}

void AScheduleManager::RefreshFastMovementSpeedsOnAgents()
{
    for (const TPair<FName, ABoidsAB*>& Pair : AgentMap)
    {
        if (ABoidsAB* Boid = Pair.Value)
        {
            Boid->RefreshWalkSpeedForSimulationMode();
        }
    }
}

bool AScheduleManager::HasOutstandingFastScheduleWork() const
{
    for (const TPair<FName, ABoidsAB*>& Pair : AgentMap)
    {
        ABoidsAB* Boid = Pair.Value;
        if (!Boid)
        {
            continue;
        }

        if (ScheduleAILayer::IsAgentScheduleBusy(Boid))
        {
            return true;
        }

        if (const TArray<FScheduleRule>* Rules = AgentRules.Find(Pair.Key))
        {
            for (const FScheduleRule& Rule : *Rules)
            {
                if (Rule.LastExecutedDay != DayCounter)
                {
                    return true;
                }
            }
        }

        if (const TArray<FScheduleTask>* Tasks = AgentSchedules.Find(Pair.Key))
        {
            for (const FScheduleTask& Task : *Tasks)
            {
                if (Task.LastExecutedDay != DayCounter)
                {
                    return true;
                }
            }
        }
    }

    return false;
}

void AScheduleManager::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    TickSchedules(DeltaSeconds);
    TickFastSimulationEndCondition();
}

void AScheduleManager::TickFastSimulationEndCondition()
{
    if (bFastRunFinalized || !SimulationClock)
    {
        return;
    }

    UWorld* World = GetWorld();
    USimulationFeatureRegistry* Registry = World ? World->GetSubsystem<USimulationFeatureRegistry>() : nullptr;
    const bool bTimedStatsRun = Registry && (Registry->IsFastSimulationMode() || Registry->IsAccurateTimeMode());
    if (!bTimedStatsRun)
    {
        return;
    }

    const float RunDurationSimMinutes = Registry->IsAccurateTimeMode()
        ? Registry->GetAccurateTimeSettings().RunDurationSimMinutes
        : Registry->GetFastSimulationSettings().RunDurationSimMinutes;
    const float Elapsed = SimulationClock->GetAccumulatedSimMinutes() - FastSessionStartSimMinutes;
    if (USimulationAnalytics* Analytics = World->GetSubsystem<USimulationAnalytics>())
    {
        Analytics->UpdateFastElapsedSimMinutes(Elapsed);
    }

    // Hard upper bound (always finalize when configured duration is reached).
    const bool bHitDurationCap = RunDurationSimMinutes > 0.f
        && Elapsed >= RunDurationSimMinutes;

    // Early finish: all schedule rules/tasks for the day are done and no agent is mid-Action/move/station.
    // Require registered agents so an empty first frame cannot end the run.
    const bool bWorkSettled = AgentMap.Num() > 0
        && Elapsed >= 1.f
        && !HasOutstandingFastScheduleWork();

    if (bHitDurationCap || bWorkSettled)
    {
        if (bWorkSettled && !bHitDurationCap)
        {
            UE_LOG(LogTemp, Log,
                TEXT("[StatsRun] Early end — schedule work settled at %.1f sim-min (cap %.1f, mode %s)"),
                Elapsed, RunDurationSimMinutes, *Registry->GetSimulationModeLabel());
        }
        FinalizeFastSimulationRun();
    }
}

void AScheduleManager::FinalizeFastSimulationRun()
{
    if (bFastRunFinalized)
    {
        return;
    }
    bFastRunFinalized = true;

    UWorld* World = GetWorld();
    USimulationFeatureRegistry* Registry = World ? World->GetSubsystem<USimulationFeatureRegistry>() : nullptr;
    USimulationAnalytics* Analytics = World ? World->GetSubsystem<USimulationAnalytics>() : nullptr;
    if (!Analytics || !Analytics->IsFastSessionActive())
    {
        return;
    }

    if (SimulationClock)
    {
        Analytics->UpdateFastElapsedSimMinutes(
            SimulationClock->GetAccumulatedSimMinutes() - FastSessionStartSimMinutes);
    }

    // Pause further schedule clock advance for this manager.
    // QA ONLY: AccurateTime runs keep the clock going so the infrastructure export still reaches its run length.
    if (!Registry || !Registry->IsAccurateTimeMode())
    {
        SimulationClock->SetSimulationSpeed(0.f);
    }

    const FString Summary = Analytics->BuildFastSessionSummaryLog();
    UE_LOG(LogTemp, Log, TEXT("%s"), *Summary);

    const bool bAutoExport = Registry && Registry->IsAccurateTimeMode()
        ? Registry->GetAccurateTimeSettings().bAutoExportOnEnd
        : (Registry ? Registry->GetFastSimulationSettings().bAutoExportOnEnd : true);

    if (bAutoExport)
    {
        const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Analytics"));
        IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
        if (!PlatformFile.DirectoryExists(*Dir))
        {
            PlatformFile.CreateDirectoryTree(*Dir);
        }

        const TCHAR* Prefix = (Registry && Registry->IsAccurateTimeMode()) ? TEXT("AccurateTime") : TEXT("FastSim");
        const FString FilePath = FPaths::Combine(Dir,
            FString::Printf(TEXT("%s_%s.csv"), Prefix, *Analytics->GetFastRunId()));
        if (Analytics->ExportFastSessionToCSV(FilePath))
        {
            UE_LOG(LogTemp, Log, TEXT("[StatsRun] Results: %s"), *FilePath);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[StatsRun] Failed to write CSV: %s"), *FilePath);
        }
    }

    Analytics->EndFastSession();
}

void AScheduleManager::ReloadSchedules()
{
    LoadSchedulesFromFile();
    UE_LOG(LogTemp, Log, TEXT("[ScheduleManager] Reloaded schedules."));
}

void AScheduleManager::SetScheduleFilePath(const FString& RelativePath)
{
    RelativeSchedulePath = RelativePath;
    FullScheduleFilePath = FPaths::Combine(FPaths::ProjectSavedDir(), RelativeSchedulePath);
    ReloadSchedules();
}

void AScheduleManager::RegisterBoid(ABoidsAB* Boid)
{
    if (!Boid || Boid->AgentId.IsNone())
        return;

    AgentMap.Add(Boid->AgentId, Boid);
    Boid->RefreshWalkSpeedForSimulationMode();

    if (USimulationAnalytics* Analytics = GetWorld() ? GetWorld()->GetSubsystem<USimulationAnalytics>() : nullptr)
    {
        Analytics->NotifyFastAgentSpawned();
    }

    if (const FString* RoleStr = PendingCrimeRoles.Find(Boid->AgentId))
    {
        if (UAgentCrimeRoleComponent* Crime = Boid->GetCrimeRoleComponent())
        {
            ECrimeRole ParsedRole = ECrimeRole::Civilian;
            if (UAgentCrimeRoleComponent::TryParseRoleString(*RoleStr, ParsedRole))
            {
                if (const UWorld* World = GetWorld())
                {
                    if (const USimulationFeatureRegistry* Registry = World->GetSubsystem<USimulationFeatureRegistry>())
                    {
                        if (Registry->IsFeatureEnabled(ESimulationFeature::CopsAndRobbers))
                        {
                            Crime->SetRole(ParsedRole);
                            UE_LOG(LogTemp, Log,
                                TEXT("[ScheduleManager] Applied schedule role '%s' to %s"),
                                *(*RoleStr),
                                *Boid->AgentId.ToString());
                        }
                    }
                }
            }
        }
    }

    UE_LOG(LogTemp, Log,
        TEXT("[ScheduleManager] Registered %s"),
        *Boid->AgentId.ToString()
    );
}

ABoidsAB* AScheduleManager::GetBoidById(FName AgentId) const
{
    ABoidsAB* const* Found = AgentMap.Find(AgentId);
    if (Found)
    {
        return *Found;
    }
    return nullptr;
}

int32 AScheduleManager::ParseTimeStringToMinutes(const FString& TimeStr) const
{
    // HH:MM → minutes from midnight (simulation time-of-day), NOT elapsed-from-start.
    if (TimeStr.IsEmpty()) return 0;
    TArray<FString> Parts;
    TimeStr.ParseIntoArray(Parts, TEXT(":"), true);
    if (Parts.Num() != 2) return 0;
    int32 H = FCString::Atoi(*Parts[0]);
    int32 M = FCString::Atoi(*Parts[1]);
    H = FMath::Clamp(H, 0, 23);
    M = FMath::Clamp(M, 0, 59);
    return H * 60 + M;
}

EScheduledAction AScheduleManager::ParseActionString(const FString& ActionStr) const
{
    if (ActionStr.Equals(TEXT("MoveTo"), ESearchCase::IgnoreCase)
        || ActionStr.Equals(TEXT("GoTo"), ESearchCase::IgnoreCase))
    {
        return EScheduledAction::SA_MoveTo;
    }
    else if (ActionStr.Equals(TEXT("Idle"), ESearchCase::IgnoreCase))
    {
        return EScheduledAction::SA_Idle;
    }
    else if (ActionStr.Equals(TEXT("Trigger"), ESearchCase::IgnoreCase))
    {
        return EScheduledAction::SA_Trigger;
    }
    else if (ActionStr.Equals(TEXT("UseStation"), ESearchCase::IgnoreCase))
    {
        return EScheduledAction::SA_UseStation;
    }
    else if (ActionStr.Equals(TEXT("StartEvacuation"), ESearchCase::IgnoreCase))
    {
        return EScheduledAction::SA_StartEvacuation;
    }
    else if (ActionStr.Equals(TEXT("Interact"), ESearchCase::IgnoreCase))
    {
        return EScheduledAction::SA_Interact;
    }
    return EScheduledAction::SA_None;
}

FScheduleRuleAction AScheduleManager::ParseRuleAction(const FXmlNode* ActionNode) const
{
    FScheduleRuleAction RuleAction;

    if (!ActionNode)
    {
        return RuleAction;
    }

    FString ActionStr = ActionNode->GetAttribute(TEXT("action"));
    RuleAction.Action = ParseActionString(ActionStr);
    
    RuleAction.TargetActorName = ActionNode->GetAttribute(TEXT("area"));
    if (RuleAction.TargetActorName.IsEmpty())
    {
        RuleAction.TargetActorName = ActionNode->GetAttribute(TEXT("superStation"));
    }
    if (RuleAction.TargetActorName.IsEmpty())
    {
        // Interact (and legacy tasks) may use target="Tag"
        RuleAction.TargetActorName = ActionNode->GetAttribute(TEXT("target"));
    }

    RuleAction.TriggerName = ActionNode->GetAttribute(TEXT("id"));

    FString ProbStr = ActionNode->GetAttribute(TEXT("prob"));
    RuleAction.Probability = ProbStr.IsEmpty() ? 1.0f : FCString::Atof(*ProbStr);

    FString DurStr = ActionNode->GetAttribute(TEXT("duration"));
    RuleAction.Duration = DurStr.IsEmpty() ? -1.0f : FCString::Atof(*DurStr);

    RuleAction.StationTypeName = ActionNode->GetAttribute(TEXT("stationType"));

    // Optional multi-stage children: <stage area="..." stationType="..." duration="..."/>
    if (RuleAction.Action == EScheduledAction::SA_UseStation)
    {
        for (const FXmlNode* Child : ActionNode->GetChildrenNodes())
        {
            if (!Child)
            {
                continue;
            }
            if (!Child->GetTag().Equals(TEXT("stage"), ESearchCase::IgnoreCase)
                && !Child->GetTag().Equals(TEXT("step"), ESearchCase::IgnoreCase)
                && !Child->GetTag().Equals(TEXT("interaction"), ESearchCase::IgnoreCase))
            {
                continue;
            }

            FStationInteraction Step;
            Step.AreaTag = Child->GetAttribute(TEXT("area"));
            if (Step.AreaTag.IsEmpty())
            {
                Step.AreaTag = Child->GetAttribute(TEXT("superStation"));
            }
            Step.StationTypeName = Child->GetAttribute(TEXT("stationType"));
            const FString StepDur = Child->GetAttribute(TEXT("duration"));
            Step.Duration = StepDur.IsEmpty() ? -1.0f : FCString::Atof(*StepDur);
            const FString Explicit = Child->GetAttribute(TEXT("station"));
            if (!Explicit.IsEmpty())
            {
                Step.ExplicitStationName = FName(*Explicit);
            }
            if (!Step.AreaTag.IsEmpty() || !Step.ExplicitStationName.IsNone())
            {
                RuleAction.InteractionSteps.Add(Step);
            }
        }
    }

    return RuleAction;
}

TArray<FStationInteraction> AScheduleManager::BuildInteractionStepsFromAction(const FScheduleRuleAction& Action) const
{
    return ScheduleAILayer::BuildOrSynthesizeInteractionSteps(
        Action.InteractionSteps,
        Action.Action,
        Action.TargetActorName,
        Action.StationTypeName,
        Action.Duration);
}

TArray<FStationInteraction> AScheduleManager::BuildInteractionStepsFromTask(const FScheduleTask& Task) const
{
    return ScheduleAILayer::BuildOrSynthesizeInteractionSteps(
        Task.InteractionSteps,
        Task.Action,
        Task.TargetActorName,
        Task.StationTypeName,
        Task.Duration);
}

bool AScheduleManager::ParseScheduleRuleNode(const FXmlNode* RuleNode, FScheduleRule& OutRule) const
{
    OutRule = FScheduleRule();

    if (!RuleNode || !RuleNode->GetTag().Equals(TEXT("rule"), ESearchCase::IgnoreCase))
    {
        return false;
    }

    OutRule.TimeMinutes = ParseTimeStringToMinutes(RuleNode->GetAttribute(TEXT("time")));

    const FString ProbStr = RuleNode->GetAttribute(TEXT("prob"));
    OutRule.Probability = ProbStr.IsEmpty() ? 1.0f : FCString::Atof(*ProbStr);
    OutRule.LastExecutedDay = -1;

    for (const FXmlNode* RuleChild : RuleNode->GetChildrenNodes())
    {
        if (!RuleChild)
        {
            continue;
        }

        const FString RuleChildTag = RuleChild->GetTag();
        if (RuleChildTag.Equals(TEXT("if"), ESearchCase::IgnoreCase))
        {
            OutRule.Condition = RuleChild->GetAttribute(TEXT("condition"));
        }
        else if (RuleChildTag.Equals(TEXT("then"), ESearchCase::IgnoreCase))
        {
            OutRule.ThenAction = ParseRuleAction(RuleChild);
        }
        else if (RuleChildTag.Equals(TEXT("else"), ESearchCase::IgnoreCase))
        {
            const FString ElseAction = RuleChild->GetAttribute(TEXT("action"));
            if (!ElseAction.IsEmpty())
            {
                OutRule.ElseAction = ParseRuleAction(RuleChild);
            }
            else
            {
                for (const FXmlNode* ElseChild : RuleChild->GetChildrenNodes())
                {
                    if (ElseChild && ElseChild->GetTag().Equals(TEXT("then"), ESearchCase::IgnoreCase))
                    {
                        OutRule.ElseAction = ParseRuleAction(ElseChild);
                        break;
                    }
                }
            }
            OutRule.bHasElse = true;
        }
    }

    if (OutRule.ThenAction.Action == EScheduledAction::SA_None)
    {
        return false;
    }

    OutRule.bIsUnconditional = OutRule.Condition.IsEmpty();
    return true;
}

void AScheduleManager::MergeRulesForAgent(FName AgentId, TArray<FScheduleRule>&& NewRules)
{
    if (NewRules.Num() == 0)
    {
        return;
    }

    TArray<FScheduleRule>& Existing = AgentRules.FindOrAdd(AgentId);
    Existing.Append(MoveTemp(NewRules));
    Existing.Sort([](const FScheduleRule& A, const FScheduleRule& B) { return A.TimeMinutes < B.TimeMinutes; });
}

bool AScheduleManager::ParseSuperGroupNode(const FXmlNode* SuperGroupNode)
{
    if (!SuperGroupNode || !SuperGroupNode->GetTag().Equals(TEXT("superGroup"), ESearchCase::IgnoreCase))
    {
        return false;
    }

    const FString GroupId = SuperGroupNode->GetAttribute(TEXT("id"));
    TArray<FName> MemberIds;
    TArray<FScheduleRule> GroupRules;

    for (const FXmlNode* ChildNode : SuperGroupNode->GetChildrenNodes())
    {
        if (!ChildNode)
        {
            continue;
        }

        const FString Tag = ChildNode->GetTag();
        if (Tag.Equals(TEXT("members"), ESearchCase::IgnoreCase))
        {
            for (const FXmlNode* MemberNode : ChildNode->GetChildrenNodes())
            {
                if (!MemberNode || !MemberNode->GetTag().Equals(TEXT("agent"), ESearchCase::IgnoreCase))
                {
                    continue;
                }

                const FString Ref = MemberNode->GetAttribute(TEXT("ref"));
                if (!Ref.IsEmpty())
                {
                    MemberIds.Add(FName(*Ref));
                }
            }
        }
        else if (Tag.Equals(TEXT("rule"), ESearchCase::IgnoreCase))
        {
            FScheduleRule Rule;
            if (ParseScheduleRuleNode(ChildNode, Rule))
            {
                GroupRules.Add(Rule);
            }
            else
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("[ScheduleManager] Invalid rule in superGroup '%s' at %02d:%02d (missing <then> action)"),
                    *GroupId,
                    ParseTimeStringToMinutes(ChildNode->GetAttribute(TEXT("time"))) / 60,
                    ParseTimeStringToMinutes(ChildNode->GetAttribute(TEXT("time"))) % 60);
            }
        }
    }

    if (MemberIds.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ScheduleManager] superGroup '%s' has no members"), *GroupId);
        return false;
    }

    if (GroupRules.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ScheduleManager] superGroup '%s' has no valid rules"), *GroupId);
        return false;
    }

    for (const FName MemberId : MemberIds)
    {
        MergeRulesForAgent(MemberId, TArray<FScheduleRule>(GroupRules));
    }

    UE_LOG(LogTemp, Log,
        TEXT("[ScheduleManager] SuperGroup '%s' -> %d agents, %d rules"),
        *GroupId,
        MemberIds.Num(),
        GroupRules.Num());

    return true;
}

bool AScheduleManager::ParseAgentScheduleNode(
    const FXmlNode* AgentNode,
    FName& OutAgentId,
    TArray<FScheduleTask>& OutTasks,
    TArray<FScheduleRule>& OutRules,
    FString& OutCrimeRole) const
{
    OutTasks.Reset();
    OutRules.Reset();
    OutCrimeRole.Reset();
    OutAgentId = NAME_None;

    if (!AgentNode || !AgentNode->GetTag().Equals(TEXT("agent"), ESearchCase::IgnoreCase))
    {
        return false;
    }

    const FString AgentIdStr = AgentNode->GetAttribute(TEXT("id"));
    if (AgentIdStr.IsEmpty())
    {
        return false;
    }

    OutAgentId = FName(*AgentIdStr);
    OutCrimeRole = AgentNode->GetAttribute(TEXT("role"));

    for (const FXmlNode* ChildNode : AgentNode->GetChildrenNodes())
    {
        if (!ChildNode)
        {
            continue;
        }

        const FString Tag = ChildNode->GetTag();

        if (Tag.Equals(TEXT("task"), ESearchCase::IgnoreCase))
        {
            FScheduleTask Task;
            Task.TimeMinutes = ParseTimeStringToMinutes(ChildNode->GetAttribute(TEXT("time")));
            Task.Action = ParseActionString(ChildNode->GetAttribute(TEXT("action")));
            Task.TargetActorName = ChildNode->GetAttribute(TEXT("target"));
            Task.TriggerName = ChildNode->GetAttribute(TEXT("id"));

            const FString ProbStr = ChildNode->GetAttribute(TEXT("prob"));
            Task.Probability = ProbStr.IsEmpty() ? 1.0f : FCString::Atof(*ProbStr);

            const FString DurStr = ChildNode->GetAttribute(TEXT("duration"));
            Task.Duration = DurStr.IsEmpty() ? -1.0f : FCString::Atof(*DurStr);

            Task.StationTypeName = ChildNode->GetAttribute(TEXT("stationType"));
            Task.LastExecutedDay = -1;
            OutTasks.Add(Task);
        }
        else if (Tag.Equals(TEXT("rule"), ESearchCase::IgnoreCase))
        {
            FScheduleRule Rule;
            if (ParseScheduleRuleNode(ChildNode, Rule))
            {
                OutRules.Add(Rule);

                if (Rule.bIsUnconditional)
                {
                    UE_LOG(LogTemp, Log,
                        TEXT("[ScheduleManager] Loaded unconditional rule at %02d:%02d for agent %s"),
                        Rule.TimeMinutes / 60,
                        Rule.TimeMinutes % 60,
                        *AgentIdStr);
                }
            }
            else
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("[ScheduleManager] Invalid rule at %02d:%02d for agent %s (missing <then> action)"),
                    ParseTimeStringToMinutes(ChildNode->GetAttribute(TEXT("time"))) / 60,
                    ParseTimeStringToMinutes(ChildNode->GetAttribute(TEXT("time"))) % 60,
                    *AgentIdStr);
            }
        }
    }

    if (OutTasks.Num() > 0)
    {
        OutTasks.Sort([](const FScheduleTask& A, const FScheduleTask& B) { return A.TimeMinutes < B.TimeMinutes; });
    }
    if (OutRules.Num() > 0)
    {
        OutRules.Sort([](const FScheduleRule& A, const FScheduleRule& B) { return A.TimeMinutes < B.TimeMinutes; });
    }

    return OutTasks.Num() > 0 || OutRules.Num() > 0 || !OutCrimeRole.IsEmpty();
}

void AScheduleManager::LoadSchedulesFromFile()
{
    AgentSchedules.Empty();
    AgentRules.Empty();
    PendingCrimeRoles.Empty();

    if (!FPaths::FileExists(FullScheduleFilePath))
    {
        UE_LOG(LogTemp, Warning, TEXT("[ScheduleManager] Schedule file not found: %s"), *FullScheduleFilePath);
        ReapplyScheduleOverrides();
        return;
    }

    FXmlFile Xml(FullScheduleFilePath);
    if (!Xml.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("[ScheduleManager] Failed to parse XML: %s"), *FullScheduleFilePath);
        ReapplyScheduleOverrides();
        return;
    }

    const FXmlNode* Root = Xml.GetRootNode();
    if (!Root)
    {
        UE_LOG(LogTemp, Error, TEXT("[ScheduleManager] XML root not valid."));
        ReapplyScheduleOverrides();
        return;
    }

    // Expect <schedule> -> <agent> / <superGroup> -> rules or tasks
    for (const FXmlNode* ChildNode : Root->GetChildrenNodes())
    {
        if (!ChildNode)
        {
            continue;
        }

        if (ChildNode->GetTag().Equals(TEXT("superGroup"), ESearchCase::IgnoreCase))
        {
            ParseSuperGroupNode(ChildNode);
            continue;
        }

        FName AgentId;
        TArray<FScheduleTask> Tasks;
        TArray<FScheduleRule> Rules;
        FString CrimeRole;
        if (!ParseAgentScheduleNode(ChildNode, AgentId, Tasks, Rules, CrimeRole))
        {
            continue;
        }

        if (!CrimeRole.IsEmpty())
        {
            PendingCrimeRoles.Add(AgentId, CrimeRole);
        }
        if (Tasks.Num() > 0)
        {
            AgentSchedules.Add(AgentId, Tasks);
            UE_LOG(LogTemp, Log, TEXT("[ScheduleManager] Loaded %d tasks for agent %s"), Tasks.Num(), *AgentId.ToString());
        }
        if (Rules.Num() > 0)
        {
            MergeRulesForAgent(AgentId, MoveTemp(Rules));
            UE_LOG(LogTemp, Log, TEXT("[ScheduleManager] Loaded %d rules for agent %s"), AgentRules.FindChecked(AgentId).Num(), *AgentId.ToString());
        }
    }

    // Re-apply optional roles for agents already registered (reload path).
    for (const TPair<FName, FString>& Pair : PendingCrimeRoles)
    {
        if (ABoidsAB* Boid = GetBoidById(Pair.Key))
        {
            RegisterBoid(Boid);
        }
    }

    // Spawner/runtime overrides must survive global reload (DUL / Mall SetScheduleFilePath).
    ReapplyScheduleOverrides();
}

bool AScheduleManager::EnsureScheduleFileInSaved(const FString& RelativePath, FString& OutAbsoluteSavedPath) const
{
    OutAbsoluteSavedPath.Reset();
    if (RelativePath.IsEmpty())
    {
        return false;
    }

    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    const FString ProjectPath = FPaths::Combine(FPaths::ProjectDir(), RelativePath);
    OutAbsoluteSavedPath = FPaths::Combine(FPaths::ProjectSavedDir(), RelativePath);

    const FString SavedParent = FPaths::GetPath(OutAbsoluteSavedPath);
    if (!PlatformFile.DirectoryExists(*SavedParent))
    {
        PlatformFile.CreateDirectoryTree(*SavedParent);
    }

    if (PlatformFile.FileExists(*ProjectPath))
    {
        PlatformFile.CopyFile(*OutAbsoluteSavedPath, *ProjectPath);
    }

    return PlatformFile.FileExists(*OutAbsoluteSavedPath);
}

bool AScheduleManager::LoadScheduleOverrideTemplate(const FString& RelativePath, FScheduleOverrideTemplate& OutTemplate)
{
    OutTemplate = FScheduleOverrideTemplate();

    if (const FScheduleOverrideTemplate* Cached = ScheduleOverrideTemplateCache.Find(RelativePath))
    {
        OutTemplate = *Cached;
        return OutTemplate.bValid;
    }

    FString AbsolutePath;
    if (!EnsureScheduleFileInSaved(RelativePath, AbsolutePath))
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[ScheduleManager] Schedule override file not found: %s"),
            *RelativePath);
        return false;
    }

    FXmlFile Xml(AbsolutePath);
    if (!Xml.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("[ScheduleManager] Failed to parse override XML: %s"), *AbsolutePath);
        return false;
    }

    const FXmlNode* Root = Xml.GetRootNode();
    if (!Root)
    {
        return false;
    }

    // First <agent> with content is the template (id in file is ignored; remapped to spawned AgentId).
    for (const FXmlNode* AgentNode : Root->GetChildrenNodes())
    {
        FName UnusedId;
        TArray<FScheduleTask> Tasks;
        TArray<FScheduleRule> Rules;
        FString CrimeRole;
        if (!ParseAgentScheduleNode(AgentNode, UnusedId, Tasks, Rules, CrimeRole))
        {
            continue;
        }

        OutTemplate.Tasks = MoveTemp(Tasks);
        OutTemplate.Rules = MoveTemp(Rules);
        OutTemplate.CrimeRole = CrimeRole;
        OutTemplate.bValid = true;
        ScheduleOverrideTemplateCache.Add(RelativePath, OutTemplate);
        return true;
    }

    UE_LOG(LogTemp, Warning,
        TEXT("[ScheduleManager] Schedule override XML has no usable <agent> content: %s"),
        *RelativePath);
    return false;
}

void AScheduleManager::ApplyOverrideTemplateToAgent(FName AgentId, const FScheduleOverrideTemplate& Template)
{
    if (AgentId.IsNone() || !Template.bValid)
    {
        return;
    }

    if (Template.Tasks.Num() > 0)
    {
        TArray<FScheduleTask> Tasks = Template.Tasks;
        for (FScheduleTask& Task : Tasks)
        {
            Task.LastExecutedDay = -1;
        }
        AgentSchedules.Add(AgentId, MoveTemp(Tasks));
    }
    else
    {
        AgentSchedules.Remove(AgentId);
    }

    if (Template.Rules.Num() > 0)
    {
        TArray<FScheduleRule> Rules = Template.Rules;
        for (FScheduleRule& Rule : Rules)
        {
            Rule.LastExecutedDay = -1;
        }
        AgentRules.Add(AgentId, MoveTemp(Rules));
    }
    else
    {
        AgentRules.Remove(AgentId);
    }

    if (!Template.CrimeRole.IsEmpty())
    {
        PendingCrimeRoles.Add(AgentId, Template.CrimeRole);
        if (ABoidsAB* Boid = GetBoidById(AgentId))
        {
            RegisterBoid(Boid);
        }
    }
}

void AScheduleManager::ReapplyScheduleOverrides()
{
    if (AgentScheduleOverridePaths.Num() == 0)
    {
        return;
    }

    for (const TPair<FName, FString>& Pair : AgentScheduleOverridePaths)
    {
        FScheduleOverrideTemplate Template;
        if (LoadScheduleOverrideTemplate(Pair.Value, Template))
        {
            ApplyOverrideTemplateToAgent(Pair.Key, Template);
        }
    }
}

bool AScheduleManager::AssignScheduleOverrideToAgent(FName AgentId, const FString& OverrideRelativePath)
{
    if (AgentId.IsNone() || OverrideRelativePath.IsEmpty())
    {
        return false;
    }

    FScheduleOverrideTemplate Template;
    if (!LoadScheduleOverrideTemplate(OverrideRelativePath, Template))
    {
        return false;
    }

    AgentScheduleOverridePaths.Add(AgentId, OverrideRelativePath);
    ApplyOverrideTemplateToAgent(AgentId, Template);

    UE_LOG(LogTemp, Log,
        TEXT("[ScheduleManager] Assigned schedule override '%s' to %s (%d rules, %d tasks)"),
        *OverrideRelativePath,
        *AgentId.ToString(),
        Template.Rules.Num(),
        Template.Tasks.Num());
    return true;
}

bool AScheduleManager::HasScheduleOverride(FName AgentId) const
{
    return AgentScheduleOverridePaths.Contains(AgentId);
}

void AScheduleManager::TickSchedules(float DeltaSeconds)
{
    EnsureSimulationClock();
    SimulationClock->Tick(DeltaSeconds);

    const int32 PrevDay = DayCounter;
    SyncClockCompatibilityFields();

    if (DayCounter > PrevDay)
    {
        UE_LOG(LogTemp, Log, TEXT("[ScheduleManager] New day %d (sim time %s)"),
            DayCounter, *SimulationClock->ToTimeOfDayString());
    }

    // Catch up one schedule minute at a time so high SimulationSpeed cannot skip HH:MM rules.
    int32 Steps = 0;
    while (Steps < MaxScheduleMinuteStepsPerFrame
        && SimulationClock->AdvanceScheduleCursor(ScheduleCursorDay, ScheduleCursorDayMinute))
    {
        CurrentSimMinute = ScheduleCursorDayMinute;
        DayCounter = ScheduleCursorDay;
        ProcessDueSchedulesForCurrentMinute();
        ++Steps;
    }

    SyncClockCompatibilityFields();
}

void AScheduleManager::ProcessDueSchedulesForCurrentMinute()
{
    if (USimulationAnalytics* Analytics = GetWorld()->GetSubsystem<USimulationAnalytics>())
    {
        Analytics->UpdateDailySimMinute(static_cast<float>(CurrentSimMinute));
        Analytics->UpdateInfrastructureSimMinute(static_cast<float>(CurrentSimMinute));
    }

    // Old task format: retry the earliest due unexecuted task per agent (one per minute).
    for (auto& Pair : AgentSchedules)
    {
        const FName AgentId = Pair.Key;
        TArray<FScheduleTask>& Tasks = Pair.Value;
        ABoidsAB* Boid = GetBoidById(AgentId);

        for (FScheduleTask& Task : Tasks)
        {
            const int32 EffectiveMinute = Boid ? Boid->GetScheduleAdjustedMinute(Task.TimeMinutes) : Task.TimeMinutes;
            if (Task.LastExecutedDay == DayCounter)
            {
                continue;
            }

            if (EffectiveMinute > CurrentSimMinute)
            {
                continue;
            }

            // Do not stale-skip while the agent is still finishing prior schedule work.
            // Also require the agent to be registered — otherwise a late spawn permanently
            // loses midnight rules that were only "stale" because the boid was not in AgentMap yet.
            if (Boid
                && !ScheduleAILayer::IsAgentScheduleBusy(Boid)
                && ScheduleAILayer::MinutesElapsedSince(EffectiveMinute, CurrentSimMinute) > ScheduleRetryWindowMinutes)
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("[ScheduleManager] Skipping stale task for %s at %02d:%02d (retry window %d min)"),
                    *AgentId.ToString(),
                    Task.TimeMinutes / 60,
                    Task.TimeMinutes % 60,
                    ScheduleRetryWindowMinutes);
                Task.LastExecutedDay = DayCounter;
                continue;
            }

            if (TryExecuteDueTask(AgentId, Task, EffectiveMinute))
            {
                break;
            }
        }
    }

    // Rule format: retry the earliest due unexecuted rule per agent (one per minute).
    for (auto& Pair : AgentRules)
    {
        const FName AgentId = Pair.Key;
        TArray<FScheduleRule>& Rules = Pair.Value;
        ABoidsAB* Boid = GetBoidById(AgentId);

        for (FScheduleRule& Rule : Rules)
        {
            const int32 EffectiveMinute = Boid ? Boid->GetScheduleAdjustedMinute(Rule.TimeMinutes) : Rule.TimeMinutes;
            if (Rule.LastExecutedDay == DayCounter)
            {
                continue;
            }

            if (EffectiveMinute > CurrentSimMinute)
            {
                continue;
            }

            if (Boid
                && !ScheduleAILayer::IsAgentScheduleBusy(Boid)
                && ScheduleAILayer::MinutesElapsedSince(EffectiveMinute, CurrentSimMinute) > ScheduleRetryWindowMinutes)
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("[ScheduleManager] Skipping stale rule for %s at %02d:%02d (retry window %d min)"),
                    *AgentId.ToString(),
                    Rule.TimeMinutes / 60,
                    Rule.TimeMinutes % 60,
                    ScheduleRetryWindowMinutes);
                RecordRuleOutcome(AgentId, Rule, EffectiveMinute, TEXT("stale_skip"));
                Rule.LastExecutedDay = DayCounter;
                continue;
            }

            if (TryExecuteDueRule(AgentId, Rule, EffectiveMinute))
            {
                break;
            }
        }
    }
}

bool AScheduleManager::TryExecuteDueTask(const FName AgentId, FScheduleTask& Task, int32 EffectiveMinute)
{
    float R = FMath::FRand();
    if (R > Task.Probability)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[ScheduleManager] Agent %s skipped task at %02d:%02d (prob)"),
            *AgentId.ToString(),
            Task.TimeMinutes / 60,
            Task.TimeMinutes % 60);
        Task.LastExecutedDay = DayCounter;
        return true;
    }

    if (ExecuteTaskForAgent(AgentId, Task))
    {
        Task.LastExecutedDay = DayCounter;
        return true;
    }

    return false;
}

bool AScheduleManager::TryExecuteDueRule(const FName AgentId, FScheduleRule& Rule, int32 EffectiveMinute)
{
    float R = FMath::FRand();
    if (R > Rule.Probability)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[ScheduleManager] Agent %s skipped rule at %02d:%02d (prob)"),
            *AgentId.ToString(),
            Rule.TimeMinutes / 60,
            Rule.TimeMinutes % 60);
        RecordRuleOutcome(AgentId, Rule, EffectiveMinute, TEXT("prob_skip"));
        Rule.LastExecutedDay = DayCounter;
        return true;
    }

    if (ExecuteRuleForAgent(AgentId, Rule))
    {
        RecordRuleOutcome(AgentId, Rule, EffectiveMinute, TEXT("dispatched"));
        Rule.LastExecutedDay = DayCounter;
        return true;
    }

    if (URunRecorderSubsystem* Recorder = GetWorld()->GetSubsystem<URunRecorderSubsystem>())
    {
        Recorder->NoteRuleDeferred(AgentId, Rule.TimeMinutes);
    }
    return false;
}

void AScheduleManager::RecordRuleOutcome(const FName AgentId, const FScheduleRule& Rule, int32 EffectiveMinute,
                                         const TCHAR* Outcome) const
{
    URunRecorderSubsystem* Recorder = GetWorld() ? GetWorld()->GetSubsystem<URunRecorderSubsystem>() : nullptr;
    if (!Recorder)
    {
        return;
    }

    FString ActionName = UEnum::GetValueAsString(Rule.ThenAction.Action);
    int32 SeparatorIndex = INDEX_NONE;
    if (ActionName.FindLastChar(TEXT(':'), SeparatorIndex))
    {
        ActionName.RightChopInline(SeparatorIndex + 1);
    }
    ActionName.RemoveFromStart(TEXT("SA_"));
    Recorder->RecordRule(AgentId, Rule.TimeMinutes, EffectiveMinute, CurrentSimMinute, DayCounter, ActionName, Outcome);
}

AAgentStation* AScheduleManager::ResolveStationForUse(
    AAgentArea* Area,
    const FString& StationTypeName,
    const FName AgentId,
    const FString& AreaTag) const
{
    if (!Area)
    {
        return nullptr;
    }

    AAgentStation* Station = nullptr;
    if (!StationTypeName.IsEmpty())
    {
        Station = Area->FindStationByIdentifier(StationTypeName);
        if (Station)
        {
            if (!Station->HasAvailableSlot())
            {
                UE_LOG(LogTemp, Log,
                    TEXT("[Schedule] %s -> UseStation '%s' desk '%s' (no free slot, will queue)"),
                    *AgentId.ToString(),
                    *AreaTag,
                    *StationTypeName);
            }
            else
            {
                UE_LOG(LogTemp, Log,
                    TEXT("[Schedule] %s -> UseStation '%s' desk '%s' (available)"),
                    *AgentId.ToString(),
                    *AreaTag,
                    *StationTypeName);
            }
            return Station;
        }

        UE_LOG(LogTemp, Warning,
            TEXT("[Schedule] Desk '%s' not found in area '%s' for %s — using any available station"),
            *StationTypeName,
            *AreaTag,
            *AgentId.ToString());
    }

    Station = Area->FindAvailableStation();
    if (!Station)
    {
        const TArray<AAgentStation*>& Stations = Area->GetStations();
        if (Stations.Num() > 0)
        {
            Station = Stations[0];
            UE_LOG(LogTemp, Log,
                TEXT("[Schedule] %s -> UseStation '%s' (all occupied, will queue)"),
                *AgentId.ToString(),
                *AreaTag);
        }
    }
    else
    {
        UE_LOG(LogTemp, Log,
            TEXT("[Schedule] %s -> UseStation '%s' (station available)"),
            *AgentId.ToString(),
            *AreaTag);
    }

    return Station;
}

bool AScheduleManager::ExecuteTaskForAgent(const FName AgentId, FScheduleTask& Task)
{
    ABoidsAB* Boid = GetBoidById(AgentId);
    if (!Boid)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[Schedule] Agent %s not spawned yet"),
            *AgentId.ToString()
        );
        return false;
    }

    if (Boid->ShouldIgnoreScheduleDuringEvacuation())
    {
        UE_LOG(LogTemp, Log,
            TEXT("[Schedule] Agent %s is in evacuation phase, ignoring scheduled task at %02d:%02d"),
            *AgentId.ToString(),
            Task.TimeMinutes / 60,
            Task.TimeMinutes % 60
        );
        return false;
    }

    if (ScheduleAILayer::BlocksForStationState(Boid, Task.Action, Task.TriggerName, Task.TargetActorName, GetWorld()))
    {
        UE_LOG(LogTemp, Log,
            TEXT("[Schedule] Agent %s is at a station, deferring task at %02d:%02d"),
            *AgentId.ToString(),
            Task.TimeMinutes / 60,
            Task.TimeMinutes % 60
        );
        return false;
    }

    if (ScheduleAILayer::ShouldDeferForActiveScheduleAction(Boid, Task.Action))
    {
        UE_LOG(LogTemp, Log,
            TEXT("[Schedule] Agent %s has active Action, deferring task at %02d:%02d"),
            *AgentId.ToString(),
            Task.TimeMinutes / 60,
            Task.TimeMinutes % 60);
        return false;
    }

    if (ScheduleAILayer::ShouldBlockScheduleForWorldIncidents(GetWorld(), Task.Action))
    {
        UE_LOG(LogTemp, Log,
            TEXT("[Schedule] World incident/evacuation active, ignoring scheduled task for %s at %02d:%02d"),
            *AgentId.ToString(),
            Task.TimeMinutes / 60,
            Task.TimeMinutes % 60
        );
        return false;
    }

    if (ScheduleAILayer::ShouldDeferScheduleForCriticalNeedsWithTarget(
        Boid, Task.Action, Task.TargetActorName, GetWorld()))
    {
        UE_LOG(LogTemp, Log,
            TEXT("[Schedule] Agent %s has critical needs, deferring task at %02d:%02d"),
            *AgentId.ToString(),
            Task.TimeMinutes / 60,
            Task.TimeMinutes % 60);
        return false;
    }

    if (Task.Action == EScheduledAction::SA_MoveTo)
    {
        AActor* TargetActor = nullptr;

        if (!Task.TargetActorName.IsEmpty())
        {
            TArray<AActor*> Found;
            UGameplayStatics::GetAllActorsWithTag(GetWorld(), FName(*Task.TargetActorName), Found);

            if (Found.Num() > 0)
                TargetActor = Found[0];
        }

        if (!TargetActor)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[Schedule] Target '%s' not found for %s"),
                *Task.TargetActorName, *AgentId.ToString());
            ScheduleAILayer::NotifyScheduleExecutionFailure(Boid, EAgentFallbackFailureKind::ScheduleTargetMissing,
                                                          FString::Printf(TEXT("MoveTo tag '%s'"), *Task.TargetActorName));
            if (USimulationAnalytics* Analytics = GetWorld()->GetSubsystem<USimulationAnalytics>())
            {
                Analytics->RecordFastDestinationFailed(Task.TargetActorName);
            }
            return true;
        }

        FVector Loc = TargetActor->GetActorLocation();
        UGoToAction* GoTo = NewObject<UGoToAction>(Boid);
        GoTo->Configure(Boid, Loc, Task.TargetActorName);
        Boid->StartScheduleAction(GoTo);

        UE_LOG(LogTemp, Log,
            TEXT("[Schedule] %s -> MoveTo/GoToAction '%s' (%s) at %02d:%02d"),
            *AgentId.ToString(),
            *Task.TargetActorName,
            *Loc.ToString(),
            Task.TimeMinutes / 60,
            Task.TimeMinutes % 60
        );

        UE_LOG(LogTemp, Log,
            TEXT("[Schedule][Move] %s Distance=%.1f"),
            *AgentId.ToString(),
            FVector::Dist(Boid->GetActorLocation(), Loc)
        );

        return true;
    }

    if (Task.Action == EScheduledAction::SA_Interact)
    {
        AActor* TargetActor = nullptr;
        if (!Task.TargetActorName.IsEmpty())
        {
            TArray<AActor*> Found;
            UGameplayStatics::GetAllActorsWithTag(GetWorld(), FName(*Task.TargetActorName), Found);
            if (Found.Num() > 0)
            {
                TargetActor = Found[0];
            }
        }

        if (!TargetActor || !Cast<IInteractable>(TargetActor))
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[Schedule] Interact target '%s' missing/non-interactable for %s"),
                *Task.TargetActorName, *AgentId.ToString());
            ScheduleAILayer::NotifyScheduleExecutionFailure(Boid, EAgentFallbackFailureKind::ScheduleTargetMissing,
                                                          FString::Printf(TEXT("Task Interact '%s'"), *Task.TargetActorName));
            return true;
        }

        UInteractAction* Interact = NewObject<UInteractAction>(Boid);
        Interact->Configure(Boid, TargetActor);
        Boid->StartScheduleAction(Interact);

        UE_LOG(LogTemp, Log,
            TEXT("[Schedule] %s -> InteractAction '%s' at %02d:%02d"),
            *AgentId.ToString(),
            *Task.TargetActorName,
            Task.TimeMinutes / 60,
            Task.TimeMinutes % 60);
        return true;
    }

    if (Task.Action == EScheduledAction::SA_Idle)
    {
        Boid->SetIdleTimed(true, Task.Duration);

        UE_LOG(LogTemp, Log,
            TEXT("[Schedule] %s -> Idle at %02d:%02d"),
            *AgentId.ToString(),
            Task.TimeMinutes / 60,
            Task.TimeMinutes % 60
        );

        return true;
    }

    if (Task.Action == EScheduledAction::SA_Trigger)
    {
        if (!Task.TriggerName.IsEmpty())
        {
            Boid->OnScheduleTrigger(Task.TriggerName);

            UE_LOG(LogTemp, Log,
                TEXT("[Schedule] %s -> Trigger '%s' at %02d:%02d"),
                *AgentId.ToString(),
                *Task.TriggerName,
                Task.TimeMinutes / 60,
                Task.TimeMinutes % 60
            );
        }
        else
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[Schedule] Trigger task has no name for agent %s"),
                *AgentId.ToString()
            );
        }

        return true;
    }

    if (Task.Action == EScheduledAction::SA_UseStation)
    {
        const TArray<FStationInteraction> Steps = BuildInteractionStepsFromTask(Task);
        if (Steps.Num() == 0)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[Schedule] UseStation task has no target area for agent %s"),
                *AgentId.ToString()
            );
            ScheduleAILayer::NotifyScheduleExecutionFailure(Boid, EAgentFallbackFailureKind::ScheduleAreaMissing,
                                                            TEXT("UseStation missing area tag"));
            return true;
        }

        // ScheduleManager only builds the pipeline; the agent owns execution.
        if (!Boid->BeginStationInteractionPipeline(Steps))
        {
            ScheduleAILayer::NotifyScheduleExecutionFailure(Boid, EAgentFallbackFailureKind::StationUnavailable,
                                                            TEXT("UseStation pipeline failed to start"));
        }
        return true;
    }

    if (Task.Action == EScheduledAction::SA_StartEvacuation)
    {
        AEmergencyManager* EmergencyMgr = Cast<AEmergencyManager>(
            UGameplayStatics::GetActorOfClass(GetWorld(), AEmergencyManager::StaticClass()));
        if (EmergencyMgr)
        {
            EmergencyMgr->TriggerGlobalEvacuation();
            UE_LOG(LogTemp, Log,
                TEXT("[Schedule] %s -> StartEvacuation at %02d:%02d"),
                *AgentId.ToString(),
                Task.TimeMinutes / 60,
                Task.TimeMinutes % 60);
        }
        else
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[Schedule] StartEvacuation: no AEmergencyManager in level (agent %s)"),
                *AgentId.ToString());
        }
        return true;
    }

    UE_LOG(LogTemp, Warning,
        TEXT("[Schedule] Agent %s has unsupported action: %d"),
        *AgentId.ToString(),
        static_cast<int32>(Task.Action)
    );
    return true;
}


bool AScheduleManager::EvaluateCondition(ABoidsAB* Boid, const FString& Condition) const
{
    if (!Boid || Condition.IsEmpty())
    {
        return false;
    }

    // Normalize condition string (trim whitespace)
    FString Cond = Condition.TrimStartAndEnd();

    if (Cond.StartsWith(TEXT("emergency"), ESearchCase::IgnoreCase))
    {
        // Extract the value after ==
        int32 EqIndex = Cond.Find(TEXT("=="));
        if (EqIndex != INDEX_NONE)
        {
            FString ValueStr = Cond.Mid(EqIndex + 2).TrimStartAndEnd();
            bool bExpectedValue = ValueStr.Equals(TEXT("true"), ESearchCase::IgnoreCase);

            bool bIsEmergency = Boid->IsFleeing();

            THESIS_SCHEDULE_VERBOSE_LOG(LogTemp, Log,
                TEXT("[Condition] %s: emergency == %s ? (actual: %s) -> %s"),
                *Boid->GetName(),
                bExpectedValue ? TEXT("true") : TEXT("false"),
                bIsEmergency ? TEXT("true") : TEXT("false"),
                (bIsEmergency == bExpectedValue) ? TEXT("TRUE") : TEXT("FALSE")
            );

            return bIsEmergency == bExpectedValue;
        }
    }

    if (Cond.StartsWith(TEXT("location"), ESearchCase::IgnoreCase))
    {
        int32 EqIndex = Cond.Find(TEXT("=="));
        if (EqIndex != INDEX_NONE)
        {
            FString LocationName = Cond.Mid(EqIndex + 2).TrimStartAndEnd();
            FVector TargetLocation = FVector::ZeroVector;
            bool bValidLocation = false;

            if (LocationName.Equals(TEXT("Home"), ESearchCase::IgnoreCase))
            {
                TargetLocation = Boid->HomeLocation;
                bValidLocation = !TargetLocation.IsZero();
            }
            else if (LocationName.Equals(TEXT("Work"), ESearchCase::IgnoreCase))
            {
                TargetLocation = Boid->WorkLocation;
                bValidLocation = !TargetLocation.IsZero();
            }
            else if (LocationName.Equals(TEXT("Food"), ESearchCase::IgnoreCase))
            {
                TargetLocation = Boid->FoodLocation;
                bValidLocation = !TargetLocation.IsZero();
            }

            if (bValidLocation)
            {
                float Distance = FVector::Dist(Boid->GetActorLocation(), TargetLocation);
                bool bResult = Distance <= LocationCheckRadius;

                THESIS_SCHEDULE_VERBOSE_LOG(LogTemp, Log,
                    TEXT("[Condition] %s: location == %s ? (distance: %.1f, radius: %.1f) -> %s"),
                    *Boid->GetName(),
                    *LocationName,
                    Distance,
                    LocationCheckRadius,
                    bResult ? TEXT("TRUE") : TEXT("FALSE")
                );

                return bResult;
            }
            else
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("[Condition] %s: Unknown or unset location '%s'"),
                    *Boid->GetName(),
                    *LocationName
                );
                return false;
            }
        }
    }

    UE_LOG(LogTemp, Warning,
        TEXT("[Condition] Unknown condition format: '%s'"),
        *Condition
    );
    return false;
}

bool AScheduleManager::ExecuteRuleForAgent(const FName AgentId, FScheduleRule& Rule)
{
    ABoidsAB* Boid = GetBoidById(AgentId);
    if (!Boid)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[Rule] Agent %s not spawned yet"),
            *AgentId.ToString()
        );
        return false;
    }

    if (Boid->ShouldIgnoreScheduleDuringEvacuation())
    {
        UE_LOG(LogTemp, Log,
            TEXT("[Rule] Agent %s is in evacuation phase, ignoring rule at %02d:%02d"),
            *AgentId.ToString(),
            Rule.TimeMinutes / 60,
            Rule.TimeMinutes % 60
        );
        return false;
    }

    FScheduleRuleAction ActionToRun;
    bool bConditionMet = false;

    if (Rule.bIsUnconditional)
    {
        ActionToRun = Rule.ThenAction;
    }
    else
    {
        bConditionMet = EvaluateCondition(Boid, Rule.Condition);

        UE_LOG(LogTemp, Log,
            TEXT("[Rule] %s at %02d:%02d: condition '%s' -> %s"),
            *AgentId.ToString(),
            Rule.TimeMinutes / 60,
            Rule.TimeMinutes % 60,
            *Rule.Condition,
            bConditionMet ? TEXT("THEN") : TEXT("ELSE")
        );

        if (bConditionMet)
        {
            ActionToRun = Rule.ThenAction;
        }
        else if (Rule.bHasElse)
        {
            ActionToRun = Rule.ElseAction;
        }
        else
        {
            return true;
        }
    }

    if (ActionToRun.Action == EScheduledAction::SA_None)
    {
        return true;
    }

    if (ScheduleAILayer::ShouldBlockScheduleForWorldIncidents(GetWorld(), ActionToRun.Action))
    {
        UE_LOG(LogTemp, Log,
            TEXT("[Rule] World incident/evacuation active, ignoring rule for %s at %02d:%02d"),
            *AgentId.ToString(),
            Rule.TimeMinutes / 60,
            Rule.TimeMinutes % 60
        );
        return false;
    }

    if (ScheduleAILayer::BlocksForStationState(
        Boid, ActionToRun.Action, ActionToRun.TriggerName, ActionToRun.TargetActorName, GetWorld()))
    {
        UE_LOG(LogTemp, Log,
            TEXT("[Rule] Agent %s is at a station, deferring rule at %02d:%02d"),
            *AgentId.ToString(),
            Rule.TimeMinutes / 60,
            Rule.TimeMinutes % 60
        );
        return false;
    }

    if (ScheduleAILayer::ShouldDeferForActiveScheduleAction(Boid, ActionToRun.Action))
    {
        UE_LOG(LogTemp, Log,
            TEXT("[Rule] Agent %s has active Action, deferring rule at %02d:%02d"),
            *AgentId.ToString(),
            Rule.TimeMinutes / 60,
            Rule.TimeMinutes % 60);
        return false;
    }

    if (ScheduleAILayer::ShouldDeferScheduleForCriticalNeedsWithTarget(
        Boid, ActionToRun.Action, ActionToRun.TargetActorName, GetWorld()))
    {
        UE_LOG(LogTemp, Log,
            TEXT("[Rule] Agent %s has critical needs, deferring rule at %02d:%02d"),
            *AgentId.ToString(),
            Rule.TimeMinutes / 60,
            Rule.TimeMinutes % 60);
        return false;
    }

    if (Rule.bIsUnconditional)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[ScheduleManager] Executing unconditional rule at %02d:%02d for %s"),
            Rule.TimeMinutes / 60,
            Rule.TimeMinutes % 60,
            *AgentId.ToString()
        );

        return ExecuteRuleAction(Boid, AgentId, Rule.ThenAction);
    }

    if (bConditionMet)
    {
        return ExecuteRuleAction(Boid, AgentId, Rule.ThenAction);
    }

    return ExecuteRuleAction(Boid, AgentId, Rule.ElseAction);
}

bool AScheduleManager::ExecuteRuleAction(ABoidsAB* Boid, const FName AgentId, const FScheduleRuleAction& Action)
{
    if (!Boid)
    {
        return false;
    }

    float R = FMath::FRand();
    if (R > Action.Probability)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[Rule] %s action skipped (prob)"),
            *AgentId.ToString()
        );
        return true;
    }

    switch (Action.Action)
    {
    case EScheduledAction::SA_MoveTo:
        {
            AActor* TargetActor = nullptr;
            if (!Action.TargetActorName.IsEmpty())
            {
                TArray<AActor*> Found;
                UGameplayStatics::GetAllActorsWithTag(GetWorld(), FName(*Action.TargetActorName), Found);
                if (Found.Num() > 0)
                {
                    TargetActor = Found[0];
                }
            }

            if (TargetActor)
            {
                const FVector Loc = TargetActor->GetActorLocation();
                UGoToAction* GoTo = NewObject<UGoToAction>(Boid);
                GoTo->Configure(Boid, Loc, Action.TargetActorName);
                Boid->StartScheduleAction(GoTo);

                UE_LOG(LogTemp, Log,
                    TEXT("[Rule] %s -> MoveTo/GoToAction '%s' (%s)"),
                    *AgentId.ToString(),
                    *Action.TargetActorName,
                    *Loc.ToString()
                );
                return true;
            }

            UE_LOG(LogTemp, Warning,
                TEXT("[Rule] Target '%s' not found for %s"),
                *Action.TargetActorName,
                *AgentId.ToString()
            );
            ScheduleAILayer::NotifyScheduleExecutionFailure(Boid, EAgentFallbackFailureKind::ScheduleTargetMissing,
                                                          FString::Printf(TEXT("Rule MoveTo tag '%s'"), *Action.TargetActorName));
            if (USimulationAnalytics* Analytics = GetWorld()->GetSubsystem<USimulationAnalytics>())
            {
                Analytics->RecordFastDestinationFailed(Action.TargetActorName);
            }
            return true;
        }

    case EScheduledAction::SA_Interact:
        {
            AActor* TargetActor = nullptr;
            if (!Action.TargetActorName.IsEmpty())
            {
                TArray<AActor*> Found;
                UGameplayStatics::GetAllActorsWithTag(GetWorld(), FName(*Action.TargetActorName), Found);
                if (Found.Num() > 0)
                {
                    TargetActor = Found[0];
                }
            }

            if (!TargetActor)
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("[Rule] Interact target '%s' not found for %s"),
                    *Action.TargetActorName,
                    *AgentId.ToString());
                ScheduleAILayer::NotifyScheduleExecutionFailure(Boid, EAgentFallbackFailureKind::ScheduleTargetMissing,
                                                              FString::Printf(TEXT("Rule Interact tag '%s'"), *Action.TargetActorName));
                return true;
            }

            if (!Cast<IInteractable>(TargetActor))
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("[Rule] Interact target '%s' is not IInteractable (agent %s)"),
                    *Action.TargetActorName,
                    *AgentId.ToString());
                ScheduleAILayer::NotifyScheduleExecutionFailure(Boid, EAgentFallbackFailureKind::ScheduleTargetMissing,
                                                              FString::Printf(TEXT("Rule Interact non-interactable '%s'"), *Action.TargetActorName));
                return true;
            }

            UInteractAction* Interact = NewObject<UInteractAction>(Boid);
            Interact->Configure(Boid, TargetActor);
            Boid->StartScheduleAction(Interact);

            UE_LOG(LogTemp, Log,
                TEXT("[Rule] %s -> InteractAction '%s'"),
                *AgentId.ToString(),
                *Action.TargetActorName);
            return true;
        }

    case EScheduledAction::SA_Idle:
        Boid->SetIdleTimed(true, Action.Duration);
        UE_LOG(LogTemp, Log,
            TEXT("[Rule] %s -> Idle"),
            *AgentId.ToString()
        );
        return true;

    case EScheduledAction::SA_Trigger:
        if (!Action.TriggerName.IsEmpty())
        {
            Boid->OnScheduleTrigger(Action.TriggerName);
            UE_LOG(LogTemp, Log,
                TEXT("[Rule] %s -> Trigger '%s'"),
                *AgentId.ToString(),
                *Action.TriggerName
            );
        }
        return true;

    case EScheduledAction::SA_UseStation:
        {
            const TArray<FStationInteraction> Steps = BuildInteractionStepsFromAction(Action);
            if (Steps.Num() == 0)
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("[Rule] UseStation action has no target area for %s"),
                    *AgentId.ToString()
                );
                ScheduleAILayer::NotifyScheduleExecutionFailure(Boid, EAgentFallbackFailureKind::ScheduleAreaMissing,
                                                                TEXT("Rule UseStation missing area tag"));
                return true;
            }

            // ScheduleManager only builds the pipeline; the agent owns execution.
            if (!Boid->BeginStationInteractionPipeline(Steps))
            {
                ScheduleAILayer::NotifyScheduleExecutionFailure(Boid, EAgentFallbackFailureKind::StationUnavailable,
                                                                TEXT("Rule UseStation pipeline failed to start"));
            }
            return true;
        }

    case EScheduledAction::SA_StartEvacuation:
        {
            AEmergencyManager* EmergencyMgr = Cast<AEmergencyManager>(
                UGameplayStatics::GetActorOfClass(GetWorld(), AEmergencyManager::StaticClass()));
            if (EmergencyMgr)
            {
                EmergencyMgr->TriggerGlobalEvacuation();
                UE_LOG(LogTemp, Log,
                    TEXT("[Rule] %s -> StartEvacuation"),
                    *AgentId.ToString());
            }
            else
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("[Rule] StartEvacuation: no AEmergencyManager in level (agent %s)"),
                    *AgentId.ToString());
            }
            return true;
        }

    default:
        UE_LOG(LogTemp, Warning,
            TEXT("[Rule] %s has unsupported action: %d"),
            *AgentId.ToString(),
            static_cast<int32>(Action.Action)
        );
        return true;
    }
}
