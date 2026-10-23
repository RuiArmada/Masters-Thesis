#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/FileManagerGeneric.h"
#include "Misc/FileHelper.h"
#include "IncidentManager.h"
#include "AI/StationInteractionTypes.h"
#include "AI/AgentStationAnimationTypes.h"
#include "BoidsAB.generated.h"

#define COLLISION_AVOIDANCE ECC_GameTraceChannel1

class UStaticMeshComponent;
class USphereComponent;
class AFlockController;
class AAgentStation;
class UAgentNeedsComponent;
class UAgentPrioritySelectorComponent;
class UAgentFallbackHandlerComponent;
class UAgentLeadershipComponent;
class UAgentCrimeRoleComponent;
class UPedestrianInteractionComponent;
class UAnimMontage;
class UAgentAction;
struct FStationInteraction;

UENUM(BlueprintType)
enum class EBoidBehaviorState : uint8
{
    Normal,
    Fleeing,
    Exiting,
    MovingToSafeZone,
    EvacuationSuccess,
    Casualty
};

/** Set on StartMoveTo when the priority layer is satisfying hunger (food) or exhaustion (home). Used for alternate-destination fallbacks. */
UENUM(BlueprintType)
enum class ENeedNavIntent : uint8
{
    None UMETA(DisplayName = "None"),
    CriticalFood UMETA(DisplayName = "Critical Food"),
    CriticalHome UMETA(DisplayName = "Critical Home")
};

class AExitPoint;
class ABoidsAB;
class AEvacuationSafeZone;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAgentFleeStarted, ABoidsAB*, Agent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAgentExitChosen, ABoidsAB*, Agent, AExitPoint*, Exit);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnAgentExitReassigned, ABoidsAB*, Agent, AExitPoint*, OldExit, AExitPoint*, NewExit);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAgentExitCompleted, ABoidsAB*, Agent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAgentEvacuationSuccess, ABoidsAB*, Agent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAgentCasualty, ABoidsAB*, Agent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAgentReactionDelayComplete, ABoidsAB*, Agent, float, ReactionDelaySeconds);

UCLASS()
class THESIS_API ABoidsAB : public ACharacter
{
    GENERATED_BODY()

public:
    ABoidsAB();

protected:
    virtual void BeginPlay() override;

public:
    virtual void Tick(float DeltaTime) override;

#pragma region Parameters

    FTimerHandle ScheduleRetryHandle;
    void TryRegisterWithSchedule();

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Agent")
    FName AgentId;

    /** Layered AI extensions (needs, priority, fallback). Created with the agent; do not remove for schedule integration. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Agent|AI Layers")
    TObjectPtr<UAgentNeedsComponent> NeedsComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Agent|AI Layers")
    TObjectPtr<UAgentPrioritySelectorComponent> PrioritySelectorComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Agent|AI Layers")
    TObjectPtr<UAgentFallbackHandlerComponent> FallbackHandlerComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Agent|AI Layers")
    TObjectPtr<UAgentLeadershipComponent> LeadershipComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Agent|AI Layers")
    TObjectPtr<UAgentCrimeRoleComponent> CrimeRoleComponent;

    UFUNCTION(BlueprintCallable, Category = "Agent|AI Layers")
    UAgentNeedsComponent* GetNeedsComponent() const { return NeedsComponent; }

    UFUNCTION(BlueprintCallable, Category = "Agent|AI Layers")
    UAgentPrioritySelectorComponent* GetPrioritySelector() const { return PrioritySelectorComponent; }

    UFUNCTION(BlueprintCallable, Category = "Agent|AI Layers")
    UAgentFallbackHandlerComponent* GetFallbackHandler() const { return FallbackHandlerComponent; }

    UFUNCTION(BlueprintCallable, Category = "Agent|AI Layers")
    UAgentLeadershipComponent* GetLeadershipComponent() const { return LeadershipComponent; }

    UFUNCTION(BlueprintCallable, Category = "Agent|AI Layers")
    UAgentCrimeRoleComponent* GetCrimeRoleComponent() const { return CrimeRoleComponent; }

    UFUNCTION(BlueprintCallable, Category = "Agent|Daily Life")
    UPedestrianInteractionComponent* GetPedestrianInteraction() const { return PedestrianInteractionComponent; }

    /** Per-agent schedule offset (minutes) applied by DailyUrbanLifeManager for variation. */
    UFUNCTION(BlueprintCallable, Category = "Agent|Daily Life")
    int32 GetScheduleMinuteOffset() const { return ScheduleMinuteOffset; }

    UFUNCTION(BlueprintCallable, Category = "Agent|Daily Life")
    void SetScheduleMinuteOffset(int32 OffsetMinutes) { ScheduleMinuteOffset = OffsetMinutes; }

    /** Returns rule minute adjusted by this agent's offset (wraps at 1440). */
    UFUNCTION(BlueprintCallable, Category = "Agent|Daily Life")
    int32 GetScheduleAdjustedMinute(int32 RuleMinute) const;

    UFUNCTION(BlueprintCallable, Category = "Agent|Daily Life")
    FName GetAssignedHomeTag() const { return AssignedHomeTag; }

    UFUNCTION(BlueprintCallable, Category = "Agent|Daily Life")
    FName GetAssignedWorkTag() const { return AssignedWorkTag; }

    UFUNCTION(BlueprintCallable, Category = "Agent|Daily Life")
    void AssignDailyDestinations(FName HomeTag, FName WorkTag);

    /** Resolve home/work/food locations from level actor tags. */
    UFUNCTION(BlueprintCallable, Category = "Agent|Daily Life")
    void ResolveDestinationsFromTags();

    UFUNCTION(BlueprintCallable, Category = "Agent|Daily Life")
    bool IsAtAssignedHome(float Radius = 250.f) const;

    UFUNCTION(BlueprintCallable, Category = "Agent|Daily Life")
    bool IsAtAssignedWorkplace(float Radius = 250.f) const;

    UFUNCTION(BlueprintCallable, Category = "Agent|Daily Life")
    void TeleportToAssignedHome();

    /** Idle with optional timed auto-resume (duration <= 0 = indefinite). */
    UFUNCTION(BlueprintCallable, Category = "Agent|Daily Life")
    void SetIdleTimed(bool bIdleState, float DurationSeconds = -1.f);

    /** True while post-incident lock is active (only StartMoveTo clears it). */
    UFUNCTION(BlueprintCallable, Category = "Agent|AI Layers")
    bool IsMovementLockedAfterIncident() const { return bMovementDisabledAfterIncident; }

    UFUNCTION(BlueprintCallable, Category = "Agent|AI Layers")
    bool IsQueuedAtStation() const { return bIsQueuedAtStation; }

    /** True during UC3 infrastructure planning runs (schedule-only movement policy). */
    UFUNCTION(BlueprintCallable, Category = "Agent|AI Layers")
    bool IsInfrastructurePlanningActive() const;

    /** True when SimulationConfig mode is Fast (research batch). */
    UFUNCTION(BlueprintCallable, Category = "Agent|AI Layers")
    bool IsFastSimulationMode() const;

    /** True when SimulationConfig mode is AccurateTime (clock-locked movement). */
    UFUNCTION(BlueprintCallable, Category = "Agent|AI Layers")
    bool IsAccurateTimeSimulationMode() const;

    /** Re-runs nav mesh pathing to TargetL without changing schedule intent. */
    UFUNCTION(BlueprintCallable, Category = "Agent|AI Layers")
    void RecomputePathToCurrentTarget();

    /** Intent for the current move (None for schedule-driven moves). Used by fallback after path failures. */
    UFUNCTION(BlueprintCallable, Category = "Agent|AI Layers")
    ENeedNavIntent GetActiveNeedNavIntent() const { return ActiveNeedNavIntent; }

    /** All known food points from the map (or single FoodLocation). */
    UFUNCTION(BlueprintCallable, Category = "Agent|AI Layers")
    TArray<FVector> GetFoodCandidateLocations() const;

    /** Populate FoodLocations and pick a per-agent FoodLocation from map food points. */
    UFUNCTION(BlueprintCallable, Category = "Agent|Daily Life")
    void AssignFoodDestinationFromMap(const TArray<FVector>& AllFoodLocations, int32 AgentIndex);

    UFUNCTION(BlueprintCallable, Category = "Agent|AI Layers")
    FVector GetNearestFoodLocation() const;

    UFUNCTION(BlueprintCallable, Category = "Agent|AI Layers")
    bool IsNearAnyFoodLocation(float Radius) const;

    /** All known home points from the map (or single HomeLocation). */
    UFUNCTION(BlueprintCallable, Category = "Agent|AI Layers")
    TArray<FVector> GetHomeCandidateLocations() const;

    bool TryNavigateToAlternateFoodLocation(const FVector& FailedTarget, float MinSeparation = 200.f);

    UFUNCTION(BlueprintCallable, Category = "Agent|AI Layers")
    bool TryNavigateToAlternateHomeLocation(const FVector& FailedTarget, float MinSeparation = 200.f);

protected:
    UPROPERTY(VisibleAnywhere, Category = "Agent-based Boids | Components")
    USphereComponent* BoidCollisionRadius;

    UPROPERTY(VisibleAnywhere, Category = "Agent-based Boids | Components")
    UStaticMeshComponent* BoidMesh;

    UPROPERTY(EditAnywhere, Category = "Agent-based Boids | Components")
    USphereComponent* BoidPerceptionRadius;

#pragma endregion

#pragma region Flock_Controller

protected:
    AFlockController* FlockController;

public:
    inline AFlockController* GetFlockController() const { return FlockController; }

#pragma endregion

#pragma region Movement

protected:
    FVector BoidSpeed;

public:
    UFUNCTION(BlueprintCallable, Category = "Agent-based Boids | Movement")
    inline FVector GetBoidSpeed() const { return BoidSpeed; }

    UFUNCTION(BlueprintCallable, Category = "Agent-based Boids | Movement")
    void SetBoidSpeed(FVector NewSpeed);

    void ComputePath(const FVector& Destination);
    void MoveAlongWaypoints(float DeltaTime);

    bool ShouldFollowWalkableRule();
    bool IsWalkableLocationAvailable(const FVector& TargetLocation);

#pragma endregion

#pragma region Steering

protected:
    FVector Separation(TArray<AActor*> Group);
    FVector Allignment(TArray<AActor*> Group);
    FVector Cohesion(TArray<AActor*> Group);
    void SteeringBehaviour(float DeltaTime);

    /** Neighbours inside BoidPerceptionRadius (existing overlap query). */
    void GatherNearbyBoids(TArray<AActor*>& OutMates) const;

    /** Sum of Separation + Alignment + Cohesion + AvoidEnemies + obstacle + TargetForceList. */
    FVector ComputeSteeringForces(const TArray<AActor*>& Mates);

    /**
     * Next waypoint, else TargetL, else zero. Does not advance the waypoint index.
     */
    FVector ResolveProgressDirection() const;

    /**
     * Blend path progress with flock: progress is forward; flock adds capped lateral only.
     */
    void BlendMovement(const FVector& ProgressDirection, const FVector& SteeringForce,
                       FVector& OutDirection, float& OutScale) const;

    void ApplyMovement(const FVector& Direction, float Scale, bool bUpdateFacing);

    /** True when scheduled path follow should include flock forces. */
    bool ShouldBlendSteeringWhileMoving() const;

    /** Project back onto NavMesh when off-mesh (cooldown gated). */
    void TryStayOnNavMesh();

    /** Look-ahead along the current path segment (horizontal corridor). */
    FVector ComputePathSegmentProgress(const FVector& SegmentStart, const FVector& SegmentEnd) const;

    /** Slide along walls when flock/progress would walk into a bottleneck corner. */
    FVector RefineDirectionAroundWalls(const FVector& DesiredDir, const FVector& ProgressDir) const;

#pragma endregion

#pragma region Collision

protected:
    void SetCollisionResponses(bool bEnable);
    bool IsObstacleInFront();
    FVector AvoidObstacle();

#pragma endregion

#pragma region Steering_Forces  

public:
    TArray<FVector> TargetForceList;
    void AddTargetForce(FVector Force);

#pragma endregion

    UPROPERTY(EditAnywhere, Category = "Agent-based Boids | Debug")
    bool DEBUG = true;

#pragma region Waypoints

protected:
    TArray<FVector> Waypoints;  // List of waypoints for precomputed path
    bool bPathComputed = false;
    int32 CurrentWaypointIndex = 0;

#pragma endregion

#pragma region GoTo

public:
    bool bShouldGoTo = false;
    bool bHasArrived = false;
    bool bMovingTowardsLogged = false;
    bool bIsMoving = false;
    bool bIsIdle = false;
    bool bReachedDestination = false;
    FVector TargetL;
    void MoveTowardsTarget(float DeltaTime);
    void LogArrival(const FString& BoidName, const FVector& Location, const FString& LogFilePath);

    void StartMoveTo(const FVector& Target, ENeedNavIntent NeedIntent = ENeedNavIntent::None);
    void NotifyScheduleMoveTo(const FVector& Target, const FString& DestinationTag);

    /** Deterministic XY spread from AgentId so shared Home/Plaza/Food targets do not stack. */
    FVector GetDailyLifeDestinationSpread(int32 Salt = 0) const;

    /** True when DestinationTag is a DUL gather point that should receive arrival spread. */
    static bool ShouldApplyDailyLifeDestinationSpread(const FString& DestinationTag, const FString& BreakAreaTag);

    /** Release blockers so a scheduled home commute can start. */
    void ReleaseBlockersForScheduledHomeCommute(const FString& DestinationTag);
    void StopMovement();
    void SetIdle(bool bIdleState);
    void HandleScheduleMovementArrival();

    bool IsIdle() const { return bIsIdle; }
    bool IsMoving() const { return bIsMoving; }

    /** True when a NavMesh path with waypoints is available for the current move. */
    bool HasComputedPath() const { return Waypoints.Num() > 0; }

    /**
     * Store Visual-mode base MaxWalkSpeed and apply Fast multiplier when Fast Mode is active.
     * Visual Mode applies BaseSpeed unchanged.
     */
    void SetBaseWalkSpeed(float BaseSpeed);

    /** Re-apply MaxWalkSpeed from stored base × Fast multiplier (no-op while fleeing/crime run). */
    void RefreshWalkSpeedForSimulationMode();

    void ClearWaypoints();
    void ClearSteeringForces();

    FVector GetCurrentTargetLocation() const { return TargetL; }

public:
    UPROPERTY(EditAnywhere, Category = "Flee")
    float FleeMaxSpeed = 450.f;

    /** Distance within which a boid is considered to have reached its assigned queue slot. */
    UPROPERTY(EditAnywhere, Category = "Flee")
    float QueueSlotArrivalRadius = 150.f;

    UPROPERTY(VisibleAnywhere, Category = "Flee")
    float CachedMaxWalkSpeed = 200.f;

    /** Walk speed before Fast Mode multiplier (and before flee/crime overrides). */
    float BaseWalkSpeed = 200.f;

    /** CMC step/accel captured once so Accurate Time can restore Visual/Fast values. */
    bool bCapturedMovementStepDefaults = false;
    int32 DefaultMaxSimulationIterations = 8;
    float DefaultMaxSimulationTimeStep = 0.05f;
    float DefaultMaxAcceleration = 2048.f;
    float DefaultBrakingDecelerationWalking = 2048.f;

    void OnIncident(EIncidentType Type, FVector IncidentLocation);
    void OnIncidentEnded(); // Called by IncidentManager when incident ends
    void StartFlee();
    void ChooseFleeDestination();
    void FinishExit();
    void OnExitApproved();
    void EnterCasualtyState();
    void UpdateExitQueuePosition(int32 NewIndex, FVector NewSlotLocation);
    void RetryFleeExit();
    void HandleEvacuationPathFailure(const FVector& AttemptedTarget);
    FVector PickNearestExit() const;

    UFUNCTION(BlueprintCallable, Category = "Flee")
    AExitPoint* GetAssignedExit() const { return CurrentExit; }

    UFUNCTION(BlueprintCallable, Category = "Flee")
    int32 GetExitReassignmentCount() const { return ExitReassignmentCount; }

    UPROPERTY(BlueprintAssignable, Category = "Flee|Events")
    FOnAgentFleeStarted OnAgentFleeStarted;

    UPROPERTY(BlueprintAssignable, Category = "Flee|Events")
    FOnAgentExitChosen OnAgentExitChosen;

    UPROPERTY(BlueprintAssignable, Category = "Flee|Events")
    FOnAgentExitReassigned OnAgentExitReassigned;

    UPROPERTY(BlueprintAssignable, Category = "Flee|Events")
    FOnAgentExitCompleted OnAgentExitCompleted;

    UPROPERTY(BlueprintAssignable, Category = "Flee|Events")
    FOnAgentEvacuationSuccess OnAgentEvacuationSuccess;

    UPROPERTY(BlueprintAssignable, Category = "Flee|Events")
    FOnAgentCasualty OnAgentCasualty;

    UPROPERTY(BlueprintAssignable, Category = "Flee|Events")
    FOnAgentReactionDelayComplete OnAgentReactionDelayComplete;

    /** Used by ScheduleManager to skip schedule updates while fleeing/exiting. */
    bool IsFleeing() const { return CurrentBehavior == EBoidBehaviorState::Fleeing || CurrentBehavior == EBoidBehaviorState::Exiting; }

    /** True when agent has reached a safe zone or legacy safe-at-exit state. */
    UFUNCTION(BlueprintCallable, Category = "Flee")
    bool IsSafelyEvacuated() const;

    UFUNCTION(BlueprintCallable, Category = "Flee")
    bool IsCasualty() const { return CurrentBehavior == EBoidBehaviorState::Casualty || bIsCasualty; }

    UFUNCTION(BlueprintCallable, Category = "Flee")
    bool IsInEvacuationSuccessState() const { return CurrentBehavior == EBoidBehaviorState::EvacuationSuccess; }

    UFUNCTION(BlueprintCallable, Category = "Flee")
    bool IsAwaitingEvacuationReaction() const { return bAwaitingReactionDelay; }

    UFUNCTION(BlueprintCallable, Category = "Flee")
    float GetRecordedReactionDelaySeconds() const { return RecordedReactionDelaySeconds; }

    /** Per-agent routing modifiers (evacuation feature only). */
    float GetEvacuationCongestionSwitchOffset() const { return CongestionSwitchThresholdOffset; }
    float GetEvacuationNearestExitBiasScale() const { return NearestExitBiasScale; }

    /** Schedules, needs, and flock should not run during any active evacuation phase. */
    bool ShouldIgnoreScheduleDuringEvacuation() const;

    bool IsSafeAtExit() const { return bIsSafeAtExit; }


private:
    FVector LastIncidentLocation = FVector::ZeroVector;
    bool bHasIncidentLocation = false;

    /** Used when no valid exit/path is available. */
    FVector FleeDirection = FVector::ZeroVector;

    /** Blocks schedules until the incident ends. */
    bool bIsSafeAtExit = false;

    // After an incident ends, only ScheduleManager (via StartMoveTo) may resume movement.
    bool bMovementDisabledAfterIncident = false;

    FTimerHandle FleeRetryHandle;

    /** Periodic congestion re-evaluation during evacuation (only when evacuation feature enabled). */
    FTimerHandle FleeReevalHandle;

    bool bIsFirstFleeExitSelection = true;
    int32 ExitReassignmentCount = 0;

    /** Prevents notifying ExitPoint every frame once the queue slot is reached. */
    bool bHasNotifiedExitReady = false;

    /** Nav-valid queue slot this boid is pathing toward during flee/exit. */
    FVector AssignedQueueSlotLocation = FVector::ZeroVector;

    int32 EvacuationPathFailCount = 0;

    bool bAwaitingReactionDelay = false;
    bool bEvacuationPersonalityInitialized = false;
    bool bIsCasualty = false;
    float ReactionDelaySeconds = 0.f;
    float RecordedReactionDelaySeconds = 0.f;
    float EvacuationSpeedMultiplier = 1.f;
    float CongestionSwitchThresholdOffset = 0.f;
    float NearestExitBiasScale = 1.f;
    double EvacuationNotifiedWorldTime = 0.0;
    FTimerHandle ReactionDelayHandle;
    TWeakObjectPtr<AEvacuationSafeZone> AssignedSafeZone;

    bool IsAtAssignedQueueSlot() const;
    bool HasSafeZonesInLevel() const;
    bool ShouldUseSafeZoneFlow() const;
    AEvacuationSafeZone* FindNearestSafeZone() const;
    bool IsAtAssignedSafeZone() const;
    void InitializeEvacuationPersonality();
    void ScheduleReactionDelay();
    void BeginMovementToSafeZone(AEvacuationSafeZone* Zone);
    void EnterEvacuationSuccessState();
    void ClearEvacuationPersonality();

    void AssignExitAndPath(AExitPoint* Exit, int32 SlotIndex);
    void ScheduleFleeRetryWhenAllExitsFull();

    void ChooseFleeDestination_Legacy();
    void StartEvacuationReevalTimer();
    void ClearEvacuationReevalTimer();
    bool ShouldUseEvacuationRouting() const;

    UFUNCTION()
    void ReevaluateFleeExitIfNeeded();

    UFUNCTION()
    void BeginFleeAfterReactionDelay();

#pragma endregion

#pragma region Schedule

private:
    TArray<FVector> WorkLocations;
    TArray<FVector> HomeLocations;
    TArray<FVector> FoodLocations;
    TArray<FVector> WalkableLocations;  // Array to store walkable locations
    FVector PickRandomLocation(const TArray<FVector>& Locations) const;

public:
    FVector WorkLocation;
    FVector HomeLocation;
    FVector FoodLocation;
    FVector LastLoggedLocation;
    FString LastLoggedDestination;
    float TimeAccelerationFactor = 400.0f;

    FName AssignedHomeTag;
    FName AssignedWorkTag;
    FVector AssignedHomeLocation = FVector::ZeroVector;
    FVector AssignedWorkLocation = FVector::ZeroVector;
    int32 ScheduleMinuteOffset = 0;
    FString PendingDestinationTagForAnalytics;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Agent|Daily Life")
    TObjectPtr<UPedestrianInteractionComponent> PedestrianInteractionComponent;

    FTimerHandle IdleDurationHandle;
    void ClearIdleDurationTimer();
    UFUNCTION()
    void OnIdleDurationExpired();

    bool IsWithinActivePeriod(const FString& Period, const FString& CurrentTime);
    void UpdateScheduleAndMove(float DeltaTime);

    /** Start a schedule-owned Action (cancels any previous non-terminal action). */
    void StartScheduleAction(UAgentAction* Action);

    /** Tick the active schedule Action if present. */
    void TickScheduleAction(float DeltaSeconds);

    UFUNCTION(BlueprintCallable, Category = "Agent|Schedule")
    UAgentAction* GetActiveScheduleAction() const { return ActiveScheduleAction; }

protected:
    UPROPERTY()
    TObjectPtr<UAgentAction> ActiveScheduleAction;

public:

#pragma endregion

#pragma region Schedule_Triggers

public:
    // ScheduleManager Trigger tasks
    void OnScheduleTrigger(const FString& TriggerName);

private:
    EBoidBehaviorState CurrentBehavior = EBoidBehaviorState::Normal;

    class AExitPoint* CurrentExit = nullptr;
    int32 ExitQueueIndex = INDEX_NONE;

    /** Tracks whether the active move is need-driven (for alternate POI fallbacks). */
    ENeedNavIntent ActiveNeedNavIntent = ENeedNavIntent::None;

    float PathFailLogThrottleTime = -1000.f;
    float LastNavSnapTime = -1000.f;
    float LastStationNavStopLogTime = -1000.f;
    FVector PathFailLogThrottleTarget = FVector::ZeroVector;
    float LastHelloWorldTime = -1000.f;

#pragma endregion


#pragma region Social

private:
    TArray<ABoidsAB*> Friends;
    TArray<ABoidsAB*> Enemies;
    TMap<ABoidsAB*, TSet<ABoidsAB*>> DailyGreetings;
    TMap<ABoidsAB*, TSet<ABoidsAB*>> DailyAvoidances;
    FString LastGreetingDay;
    TMap<ABoidsAB*, float> LastHelloTime;

public:
    void InitializeFriends(const TArray<ABoidsAB*>& AllBoids, int32 NumFriends);
    void LogFriendInteraction(ABoidsAB* Friend);
    void LogEnemyAvoidance(ABoidsAB* Enemy);
    void ResetDailyGreetings();
    void ResetDailyAvoidances();
    bool ShouldLogPathFollowing() const;
    FVector AvoidEnemies(TArray<AActor*> Group);

    void TrySayHello(const TArray<AActor*>& NearbyBoids);
    UFUNCTION(BlueprintImplementableEvent)
    void OnHelloWave();

#pragma endregion

#pragma region Logs

public:
    void InitializeLogs();
    void WriteToLogFile(const FString& Message);

private:
    FString CentralizedLogFilePath;

#pragma endregion

#pragma region Utility

FString GetCurrentFormattedTime() const;
FString GetCurrentDay() const;

#pragma endregion

#pragma region Station

public:
    /** Move to a station (queues if occupied). Ignored while fleeing. */
    UFUNCTION(BlueprintCallable, Category = "Station")
    void MoveToStation(AAgentStation* Station, float Duration = -1.0f);

    UFUNCTION(BlueprintCallable, Category = "Station")
    void OnStationReached();

    UFUNCTION(BlueprintCallable, Category = "Station")
    AAgentStation* GetTargetStation() const { return TargetStation; }

    /**
     * Align to the station interaction transform (snap or interpolate), then start interaction.
     * Called by AAgentStation when InteractionPositionMode is not None.
     */
    UFUNCTION(BlueprintCallable, Category = "Station|Position")
    void BeginStationInteractionPose(AAgentStation* Station);

    UFUNCTION(BlueprintCallable, Category = "Station|Position")
    bool IsPerformingStationInteractionPose() const { return bStationInteractionPosing; }

    /** True while this agent is the active occupant (ScheduleManager pauses during interaction). */
    UFUNCTION(BlueprintCallable, Category = "Station")
    bool IsInteractingWithStation() const;

    UFUNCTION(BlueprintCallable, Category = "Station")
    bool IsWaitingAtStation() const { return bIsQueuedAtStation; }

    UFUNCTION(BlueprintNativeEvent, Category = "Station|Animation")
    void OnStationInteractStart();
    virtual void OnStationInteractStart_Implementation();

    UFUNCTION(BlueprintNativeEvent, Category = "Station|Animation")
    void OnStationInteractEnd();
    virtual void OnStationInteractEnd_Implementation();

    /**
     * Called by the station when the interaction timer completes normally.
     * Distinguishes successful stage completion from early LeaveStation / flee.
     */
    UFUNCTION(BlueprintCallable, Category = "Station|Interaction")
    void NotifyStationInteractionCompleted();

    void OnStationBecameAvailable();
    void LeaveCurrentStation();

    /**
     * Begin a multi-stage (or single-stage) station interaction pipeline.
     * ScheduleManager / Blueprint / other systems may call this.
     * Agent owns execution, advance, interrupt, and cancel.
     */
    UFUNCTION(BlueprintCallable, Category = "Station|Interaction")
    bool BeginStationInteractionPipeline(const TArray<FStationInteraction>& Steps);

    UFUNCTION(BlueprintCallable, Category = "Station|Interaction")
    void CancelStationInteractionPipeline();

    UFUNCTION(BlueprintCallable, Category = "Station|Interaction")
    bool HasActiveStationInteractionPipeline() const { return InteractionPipeline.Num() > 0; }

    /** Negative = use the station's default duration. */
    float GetPendingStationDuration() const { return PendingStationDuration; }

private:
    UPROPERTY()
    AAgentStation* TargetStation = nullptr;

    bool bIsQueuedAtStation = false;

    /** Seconds from schedule; negative = station default. */
    float PendingStationDuration = -1.0f;

    /** Montage currently driven by a station interaction (if any). */
    UPROPERTY()
    TObjectPtr<UAnimMontage> ActiveStationInteractionMontage;

    /** Active interaction pipeline owned by this agent (empty = none). */
    UPROPERTY()
    TArray<FStationInteraction> InteractionPipeline;

    /** Index of the step currently in progress (or about to start). */
    int32 InteractionPipelineIndex = INDEX_NONE;

    /** True while advancing to the next pipeline step (suppress cancel during normal release). */
    bool bAdvancingStationPipeline = false;

    /** Set by NotifyStationInteractionCompleted so End can advance the pipeline. */
    bool bStationInteractionCompletedNormally = false;

    /** Play optional animation assigned on TargetStation. No-op when unset. */
    void PlayStationInteractionAnimation();

    /** Stop station interaction animation and restore locomotion to AnimBP. */
    void StopStationInteractionAnimation();

    /** Resolve and MoveToStation for InteractionPipeline[InteractionPipelineIndex]. */
    bool ExecuteCurrentStationInteractionStep();

    /** After a successful stage release, start the next step or finish the pipeline. */
    void AdvanceStationInteractionPipeline();

    /** Find area by tag and pick best available station of the requested type. */
    AAgentStation* ResolveStationForInteractionStep(const FStationInteraction& Step) const;

    /** Apply world location / optional rotation for station interaction pose. */
    void ApplyStationInteractionTransform(const FTransform& WorldXform, bool bAlignRotation);

    /** Clear pose state without starting interaction (leave / cancel). */
    void ClearStationInteractionPose();

    /** Pose finished — start timer + animation via the station. */
    void FinishStationInteractionPose();

    void TickStationInteractionPose(float DeltaTime);

    /** True while snapping/interpolating to a station interaction transform. */
    bool bStationInteractionPosing = false;

    EStationInteractionPositionMode ActiveStationPoseMode = EStationInteractionPositionMode::None;

    FTransform StationPoseStart = FTransform::Identity;
    FTransform StationPoseTarget = FTransform::Identity;

    float StationPoseElapsed = 0.f;
    float StationPoseDuration = 0.35f;
    bool bStationPoseAlignRotation = true;

    TWeakObjectPtr<AAgentStation> StationPoseStation;

#pragma endregion
};