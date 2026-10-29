#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "XmlFile.h"
#include "AI/StationInteractionTypes.h"

class ABoidsAB;
class AAgentArea;
class AAgentStation;
class USimulationClock;

#include "ScheduleManager.generated.h"

UENUM(BlueprintType)
enum class EScheduledAction : uint8
{
    SA_None       UMETA(DisplayName = "None"),
    SA_MoveTo     UMETA(DisplayName = "MoveTo"),
    SA_Idle       UMETA(DisplayName = "Idle"),
    SA_Trigger         UMETA(DisplayName = "Trigger"),
    SA_UseStation      UMETA(DisplayName = "UseStation"),
    SA_StartEvacuation UMETA(DisplayName = "StartEvacuation"),
    SA_Interact        UMETA(DisplayName = "Interact")
};

USTRUCT(BlueprintType)
struct FScheduleTask
{
    GENERATED_BODY()

    // Minutes from midnight (0..1439)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schedule")
    int32 TimeMinutes = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schedule")
    EScheduledAction Action = EScheduledAction::SA_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schedule")
    FString TargetActorName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schedule")
    FString TriggerName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schedule")
    float Probability = 1.0f;

    // UseStation duration in seconds; negative = station default.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schedule")
    float Duration = -1.0f;

    /** Optional desk identifier within the area (matches station tag / StationIdentifier). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schedule")
    FString StationTypeName;

    /**
     * Ordered interaction steps for UseStation.
     * Empty = synthesize one step from TargetActorName / StationTypeName / Duration (legacy).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schedule")
    TArray<FStationInteraction> InteractionSteps;

    int32 LastExecutedDay = -1;
};

/** Action inside a <then> or <else> block. */
USTRUCT(BlueprintType)
struct FScheduleRuleAction
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schedule|Rule")
    EScheduledAction Action = EScheduledAction::SA_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schedule|Rule")
    FString TargetActorName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schedule|Rule")
    FString TriggerName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schedule|Rule")
    float Probability = 1.0f;

    // UseStation duration in seconds; negative = station default.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schedule|Rule")
    float Duration = -1.0f;

    /** Optional desk identifier within the area (matches station tag / StationIdentifier). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schedule|Rule")
    FString StationTypeName;

    /**
     * Ordered interaction steps for UseStation pipelines.
     * Empty = synthesize one step from area / stationType / duration attributes (legacy).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schedule|Rule")
    TArray<FStationInteraction> InteractionSteps;
};

/** Conditional or unconditional schedule rule. */
USTRUCT(BlueprintType)
struct FScheduleRule
{
    GENERATED_BODY()

    // Minutes from midnight (0..1439)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schedule|Rule")
    int32 TimeMinutes = 0;

    // Empty for unconditional rules
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schedule|Rule")
    FString Condition;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schedule|Rule")
    FScheduleRuleAction ThenAction;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schedule|Rule")
    FScheduleRuleAction ElseAction;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schedule|Rule")
    bool bHasElse = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schedule|Rule")
    bool bIsUnconditional = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schedule|Rule")
    float Probability = 1.0f;

    int32 LastExecutedDay = -1;
};

UCLASS()
class THESIS_API AScheduleManager : public AActor
{
    GENERATED_BODY()

public:
    AScheduleManager();

    virtual void Tick(float DeltaSeconds) override;
    virtual void BeginPlay() override;

    UFUNCTION(BlueprintCallable, Category = "Schedule")
    void ReloadSchedules();

    /** Override schedule file path (relative to project/saved) and reload. */
    UFUNCTION(BlueprintCallable, Category = "Schedule")
    void SetScheduleFilePath(const FString& RelativePath);

protected:
    UPROPERTY(EditAnywhere, Category = "Schedule")
    FString RelativeSchedulePath = TEXT("Schedules/DailySchedule.xml");

    // Legacy task format
    TMap<FName, TArray<FScheduleTask>> AgentSchedules;

    // Rule format
    TMap<FName, TArray<FScheduleRule>> AgentRules;

    /** Optional schedule XML role="Police|Robber|Civilian" — applied on RegisterBoid when feature enabled. */
    TMap<FName, FString> PendingCrimeRoles;

    UPROPERTY()
    TMap<FName, ABoidsAB*> AgentMap;

public:
    void RegisterBoid(ABoidsAB* Boid);
    ABoidsAB* GetBoidById(FName AgentId) const;

    /**
     * Optional per-agent schedule override (spawner / runtime).
     * Parses OverrideRelativePath with the existing XML loader, takes the first
     * <agent> block as a template, and copies those rules/tasks onto AgentId.
     * Empty OverrideRelativePath is a no-op. Survives global ReloadSchedules.
     */
    UFUNCTION(BlueprintCallable, Category = "Schedule")
    bool AssignScheduleOverrideToAgent(FName AgentId, const FString& OverrideRelativePath);

    /** True when this agent currently has a spawner/runtime schedule override. */
    UFUNCTION(BlueprintCallable, Category = "Schedule")
    bool HasScheduleOverride(FName AgentId) const;

    /** Authoritative simulation clock (owned by this manager). */
    UFUNCTION(BlueprintCallable, Category = "Schedule|Clock")
    USimulationClock* GetSimulationClock() const { return SimulationClock; }

    /**
     * Compatibility mirrors synced from USimulationClock each tick.
     * AccumulatedSimSeconds = total sim minutes (TotalSimSeconds / 60), matching legacy meaning.
     */
    UPROPERTY(BlueprintReadOnly, Category = "Schedule|Clock")
    float AccumulatedSimSeconds = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Schedule|Clock")
    int32 CurrentSimMinute = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Schedule|Clock")
    int32 PrevSimMinute = -1;

    UPROPERTY(BlueprintReadOnly, Category = "Schedule|Clock")
    int32 DayCounter = 0;

    // Helpers
    int32 ParseTimeStringToMinutes(const FString& TimeStr) const;
    void LoadSchedulesFromFile();
    void TickSchedules(float DeltaSeconds);
    bool ExecuteTaskForAgent(const FName AgentId, FScheduleTask& Task);

    // Rule parsing helpers
    FScheduleRuleAction ParseRuleAction(const FXmlNode* ActionNode) const;
    EScheduledAction ParseActionString(const FString& ActionStr) const;

    // Accessors for rules (for future evaluation)
    const TMap<FName, TArray<FScheduleRule>>& GetAgentRules() const { return AgentRules; }
    const TArray<FScheduleRule>* GetRulesForAgent(FName AgentId) const { return AgentRules.Find(AgentId); }

    /** Supported: emergency, location == Home|Work|Food */
    bool EvaluateCondition(ABoidsAB* Boid, const FString& Condition) const;

    bool ExecuteRuleForAgent(const FName AgentId, FScheduleRule& Rule);

    bool ExecuteRuleAction(ABoidsAB* Boid, const FName AgentId, const FScheduleRuleAction& Action);

    /** Resolve a station for UseStation, honoring optional StationTypeName (existing selection strategy). */
    AAgentStation* ResolveStationForUse(AAgentArea* Area, const FString& StationTypeName, const FName AgentId, const FString& AreaTag) const;

    /** Build interaction steps from a rule action (legacy attrs and/or nested stages). */
    TArray<FStationInteraction> BuildInteractionStepsFromAction(const FScheduleRuleAction& Action) const;

    /** Build interaction steps from a legacy task. */
    TArray<FStationInteraction> BuildInteractionStepsFromTask(const FScheduleTask& Task) const;

    bool TryExecuteDueTask(const FName AgentId, FScheduleTask& Task, int32 EffectiveMinute);
    bool TryExecuteDueRule(const FName AgentId, FScheduleRule& Rule, int32 EffectiveMinute);

protected:
    /** Distance threshold for location checks (e.g., "location == Home"). */
    UPROPERTY(EditAnywhere, Category = "Schedule|Conditions")
    float LocationCheckRadius = 200.0f;

    /** After this many sim minutes, an unexecuted rule/task is skipped so later steps can proceed. */
    UPROPERTY(EditAnywhere, Category = "Schedule")
    int32 ScheduleRetryWindowMinutes = 45;

    /**
     * AgentId → relative schedule path for optional overrides.
     * Re-applied after LoadSchedulesFromFile so DUL/Mall reload does not drop them.
     */
    TMap<FName, FString> AgentScheduleOverridePaths;

    /** Cached first-agent template parsed from an override XML (key = relative path). */
    struct FScheduleOverrideTemplate
    {
        TArray<FScheduleTask> Tasks;
        TArray<FScheduleRule> Rules;
        FString CrimeRole;
        bool bValid = false;
    };
    TMap<FString, FScheduleOverrideTemplate> ScheduleOverrideTemplateCache;

    bool EnsureScheduleFileInSaved(const FString& RelativePath, FString& OutAbsoluteSavedPath) const;
    bool LoadScheduleOverrideTemplate(const FString& RelativePath, FScheduleOverrideTemplate& OutTemplate);
    void ApplyOverrideTemplateToAgent(FName AgentId, const FScheduleOverrideTemplate& Template);
    void ReapplyScheduleOverrides();

    /** Shared XML agent-node parse used by global load and schedule overrides. */
    bool ParseAgentScheduleNode(
        const FXmlNode* AgentNode,
        FName& OutAgentId,
        TArray<FScheduleTask>& OutTasks,
        TArray<FScheduleRule>& OutRules,
        FString& OutCrimeRole) const;

    /** Parse one <rule> node into OutRule. Returns false if <then> action is missing. */
    bool ParseScheduleRuleNode(const FXmlNode* RuleNode, FScheduleRule& OutRule) const;

    /** Parse <superGroup> and fan out rules to member agents in AgentRules. */
    bool ParseSuperGroupNode(const FXmlNode* SuperGroupNode);

    /** Append rules for AgentId, keeping time order. */
    void MergeRulesForAgent(FName AgentId, TArray<FScheduleRule>&& NewRules);

    void EnsureSimulationClock();
    void SyncClockCompatibilityFields();
    void ProcessDueSchedulesForCurrentMinute();

    UPROPERTY()
    TObjectPtr<USimulationClock> SimulationClock;

    /** Schedule evaluation cursor (day + day-minute). -1 minute = not started. */
    int32 ScheduleCursorDay = 0;
    int32 ScheduleCursorDayMinute = -1;

    /** Safety cap: max schedule minute steps processed per frame (high SimulationSpeed). */
    UPROPERTY(EditAnywhere, Category = "Schedule|Clock", meta = (ClampMin = "1"))
    int32 MaxScheduleMinuteStepsPerFrame = 120;

    void ApplySimulationModeFromRegistry();
    void TickFastSimulationEndCondition();
    void FinalizeFastSimulationRun();

    /** True when any registered agent still has pending schedule work or an in-flight Action/move. */
    bool HasOutstandingFastScheduleWork() const;

    /** Apply Fast walk-speed boost (or restore Visual) on all registered agents. */
    void RefreshFastMovementSpeedsOnAgents();

    float FastSessionStartSimMinutes = 0.f;
    bool bFastRunFinalized = false;

public:
    // Full absolute path computed at BeginPlay
    FString FullScheduleFilePath;
};
