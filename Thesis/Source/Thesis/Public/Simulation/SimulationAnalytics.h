#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Simulation/SimulationTypes.h"
#include "SimulationAnalytics.generated.h"

class ABoidsAB;
class AExitPoint;
class AEmergencyManager;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAnalyticsSnapshotUpdated, const FEvacuationAnalyticsSnapshot&, Snapshot);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDailyAnalyticsSnapshotUpdated, const FDailyUrbanLifeAnalyticsSnapshot&, Snapshot);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInfrastructureAnalyticsSnapshotUpdated, const FInfrastructureAnalyticsSnapshot&, Snapshot);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnValidationAnalyticsSnapshotUpdated, const FValidationAnalyticsSnapshot&, Snapshot);

/**
 * Simulation analytics for evacuation, daily life, and infrastructure sessions.
 */
UCLASS()
class THESIS_API USimulationAnalytics : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "Simulation|Analytics")
	void BeginEvacuationSession();

	UFUNCTION(BlueprintCallable, Category = "Simulation|Analytics")
	void EndEvacuationSession();

	UFUNCTION(BlueprintCallable, Category = "Simulation|Analytics")
	FEvacuationAnalyticsSnapshot GetEvacuationSnapshot() const;

	UFUNCTION(BlueprintCallable, Category = "Simulation|Analytics")
	bool ExportEvacuationSessionToCSV(const FString& AbsoluteFilePath) const;

	UFUNCTION(BlueprintCallable, Category = "Simulation|Analytics|Daily")
	void BeginDailyUrbanLifeSession();

	UFUNCTION(BlueprintCallable, Category = "Simulation|Analytics|Daily")
	void EndDailyUrbanLifeSession();

	UFUNCTION(BlueprintCallable, Category = "Simulation|Analytics|Daily")
	FDailyUrbanLifeAnalyticsSnapshot GetDailyUrbanLifeSnapshot() const;

	UFUNCTION(BlueprintCallable, Category = "Simulation|Analytics|Daily")
	bool ExportDailyUrbanLifeSessionToCSV(const FString& AbsoluteFilePath) const;

	UFUNCTION(BlueprintCallable, Category = "Simulation|Analytics|Infrastructure")
	void BeginInfrastructureSession(const FString& LayoutVariantName);

	UFUNCTION(BlueprintCallable, Category = "Simulation|Analytics|Infrastructure")
	void EndInfrastructureSession();

	UFUNCTION(BlueprintCallable, Category = "Simulation|Analytics|Infrastructure")
	FInfrastructureAnalyticsSnapshot GetInfrastructureSnapshot() const;

	UFUNCTION(BlueprintCallable, Category = "Simulation|Analytics|Infrastructure")
	bool ExportInfrastructureSessionToCSV(const FString& AbsoluteFilePath) const;

	UFUNCTION(BlueprintCallable, Category = "Simulation|Analytics|Infrastructure")
	bool ExportInfrastructureComparisonReport(const FString& BaselineCsvPath, const FString& VariantCsvPath,
	                                          const FString& OutAbsolutePath) const;

	/** Locate newest Infrastructure_{Layout}_*.csv under Saved/Analytics. */
	UFUNCTION(BlueprintCallable, Category = "Simulation|Analytics|Verification")
	bool FindLatestInfrastructureCsv(const FString& LayoutVariant, FString& OutAbsolutePath) const;

	/** Human-readable academic summary from two infrastructure CSVs. */
	UFUNCTION(BlueprintCallable, Category = "Simulation|Analytics|Verification")
	bool ExportInfrastructureSummaryReport(const FString& BaselineCsvPath, const FString& VariantCsvPath,
	                                       const FString& BaselineLabel, const FString& VariantLabel,
	                                       const FString& OutAbsolutePath) const;

	/**
	 * Build comparison CSV + summary TXT from the latest baseline/variant session exports.
	 * Returns false if either CSV is missing (e.g. only one mall has been run).
	 */
	UFUNCTION(BlueprintCallable, Category = "Simulation|Analytics|Verification")
	bool GenerateInfrastructureVerificationPackage(const FString& BaselineVariant, const FString& VariantName,
	                                               FString& OutComparisonCsvPath, FString& OutSummaryPath) const;

	UFUNCTION(BlueprintCallable, Category = "Simulation|Analytics|Validation")
	void BeginValidationSession(const FString& ScenarioName);

	UFUNCTION(BlueprintCallable, Category = "Simulation|Analytics|Validation")
	void EndValidationSession();

	UFUNCTION(BlueprintCallable, Category = "Simulation|Analytics|Validation")
	FValidationAnalyticsSnapshot GetValidationSnapshot() const;

	UFUNCTION(BlueprintCallable, Category = "Simulation|Analytics|Validation")
	bool IsValidationSessionActive() const { return bValidationSessionActive; }

	/**
	 * Sample live world state into the validation session (queues, density, waiting, FPS).
	 * Called by the debug panel subsystem — does not recalculate travel times already stored.
	 */
	UFUNCTION(BlueprintCallable, Category = "Simulation|Analytics|Validation")
	void SampleValidationWorld(float DeltaSeconds);

	UPROPERTY(BlueprintAssignable, Category = "Simulation|Analytics")
	FOnAnalyticsSnapshotUpdated OnSnapshotUpdated;

	UPROPERTY(BlueprintAssignable, Category = "Simulation|Analytics|Daily")
	FOnDailyAnalyticsSnapshotUpdated OnDailySnapshotUpdated;

	UPROPERTY(BlueprintAssignable, Category = "Simulation|Analytics|Infrastructure")
	FOnInfrastructureAnalyticsSnapshotUpdated OnInfrastructureSnapshotUpdated;

	UPROPERTY(BlueprintAssignable, Category = "Simulation|Analytics|Validation")
	FOnValidationAnalyticsSnapshotUpdated OnValidationSnapshotUpdated;

	void RecordAgentFleeStarted(ABoidsAB* Agent);
	void RecordAgentExitChosen(ABoidsAB* Agent, AExitPoint* Exit);
	void RecordAgentExitReassigned(ABoidsAB* Agent, AExitPoint* OldExit, AExitPoint* NewExit);
	void RecordAgentExitCompleted(ABoidsAB* Agent);
	void RecordAgentEvacuationSuccess(ABoidsAB* Agent);
	void RecordAgentCasualty(ABoidsAB* Agent);
	void RecordAgentReactionDelay(ABoidsAB* Agent, float ReactionDelaySeconds);
	void RecordEvacuationTimerRemaining(float RemainingSeconds, bool bExpired);
	void RecordEvacuationTimerExpired();

	void RecordLeadershipEvent(const FString& EventName);
	void RecordCrimeEvent(const FString& EventName);
	void RecordCrimePursuitStarted(ABoidsAB* Police, ABoidsAB* Robber);
	void RecordCrimePursuitEnded(ABoidsAB* Police, ABoidsAB* Robber, float PursuitSeconds, bool bCaptured);

	UFUNCTION(BlueprintCallable, Category = "Simulation|Analytics|Crime")
	void BeginCopsAndRobbersSession();

	UFUNCTION(BlueprintCallable, Category = "Simulation|Analytics|Crime")
	void EndCopsAndRobbersSession();

	UFUNCTION(BlueprintCallable, Category = "Simulation|Analytics|Crime")
	FCopsAndRobbersAnalyticsSnapshot GetCopsAndRobbersSnapshot() const;

	UFUNCTION(BlueprintCallable, Category = "Simulation|Analytics|Crime")
	bool ExportCopsAndRobbersSessionToCSV(const FString& AbsoluteFilePath) const;

	void RecordInfrastructureSample(const FString& SampleName);
	void RecordPathSegmentUtilization(const FString& SegmentName, EPathSegmentCategory Category);
	void RecordStationQueueJoin(class AAgentStation* Station, ABoidsAB* Agent);
	void RecordStationAdmitted(class AAgentStation* Station, ABoidsAB* Agent);
	void RecordStationServiceCompleted(class AAgentStation* Station, ABoidsAB* Agent);
	void RecordInfrastructureDestination(const FString& DestinationTag);
	void UpdateInfrastructureSimMinute(float SimMinute);
	void RecordDailyCommuteStarted(ABoidsAB* Agent, const FVector& Destination, const FString& DestinationTag);
	void RecordDailyDestinationReached(ABoidsAB* Agent, const FVector& Destination, const FString& DestinationTag);
	void RecordDailyInteraction(ABoidsAB* Agent, ABoidsAB* OtherAgent, const FString& InteractionType);
	void RecordSidewalkUtilization(const FString& SegmentName);
	void RefreshDailyOccupancyCounts();
	void UpdateDailySimMinute(float SimMinute);

	// --- Fast Mode research session (extends existing event hooks; does not replace infra/daily CSV) ---

	UFUNCTION(BlueprintCallable, Category = "Simulation|Analytics|Fast")
	void BeginFastSession(const FString& RunId, const FString& MapName, float SimulationSpeed, int32 RandomSeed);

	/** Same research CSV pipeline as Fast, labeled AccurateTime. Durations use simulation seconds. */
	void BeginAccurateTimeSession(const FString& RunId, const FString& MapName, float SimulationSpeed, int32 RandomSeed);

	UFUNCTION(BlueprintCallable, Category = "Simulation|Analytics|Fast")
	void EndFastSession();

	UFUNCTION(BlueprintCallable, Category = "Simulation|Analytics|Fast")
	bool IsFastSessionActive() const { return bFastSessionActive; }

	UFUNCTION(BlueprintCallable, Category = "Simulation|Analytics|Fast")
	const FString& GetFastRunId() const { return FastRunId; }

	void RecordFastDestinationSent(const FString& DestinationTag);
	void RecordFastDestinationReached(const FString& DestinationTag);
	void RecordFastDestinationFailed(const FString& DestinationTag);
	void RecordFastInteractionAttempt(const FString& TargetName);
	void RecordFastInteractionSuccess(const FString& TargetName);
	void RecordFastInteractionFailure(const FString& TargetName);
	void RecordFastEntranceOpened(const FString& EntranceName, float SimMinutes);
	void NotifyFastAgentSpawned();
	void UpdateFastElapsedSimMinutes(float ElapsedSimMinutesFromSessionStart);

	UFUNCTION(BlueprintCallable, Category = "Simulation|Analytics|Fast")
	bool ExportFastSessionToCSV(const FString& AbsoluteFilePath) const;

	UFUNCTION(BlueprintCallable, Category = "Simulation|Analytics|Fast")
	FString BuildFastSessionSummaryLog() const;

protected:
	bool bSessionActive = false;
	double SessionStartTime = 0.0;
	int32 TotalReassignments = 0;
	int32 TotalEvacuated = 0;

	struct FAgentEvacRecord
	{
		double FleeStartTime = 0.0;
		double ExitCompletedTime = 0.0;
		double SafeZoneArrivalTime = 0.0;
		float ReactionDelaySeconds = 0.f;
		FName AgentId;
		FString InitialExitName;
		FString FinalExitName;
		int32 ReassignmentCount = 0;
	};

	TMap<TWeakObjectPtr<ABoidsAB>, FAgentEvacRecord> AgentRecords;
	TMap<TWeakObjectPtr<AExitPoint>, int32> ExitThroughput;
	TMap<TWeakObjectPtr<AExitPoint>, int32> ExitPeakQueue;

	int32 TotalCasualties = 0;
	int32 SafeZoneArrivals = 0;
	float LastTimerRemainingSeconds = 0.f;
	bool bLastTimerExpired = false;
	int32 TotalAgentsAtSessionStart = 0;

	void BindSimulationDelegates();
	void UnbindSimulationDelegates();
	void RefreshSnapshot();
	double GetWorldTimeSeconds() const;

	UFUNCTION()
	void HandleEvacuationStarted();

	UFUNCTION()
	void HandleEvacuationEnded();

	UFUNCTION()
	void HandleAgentFleeStarted(ABoidsAB* Agent);

	UFUNCTION()
	void HandleAgentExitChosen(ABoidsAB* Agent, AExitPoint* Exit);

	UFUNCTION()
	void HandleAgentExitReassigned(ABoidsAB* Agent, AExitPoint* OldExit, AExitPoint* NewExit);

	UFUNCTION()
	void HandleAgentExitCompleted(ABoidsAB* Agent);

	UFUNCTION()
	void HandleAgentEvacuationSuccess(ABoidsAB* Agent);

	UFUNCTION()
	void HandleAgentCasualty(ABoidsAB* Agent);

	UFUNCTION()
	void HandleAgentReactionDelayComplete(ABoidsAB* Agent, float ReactionDelaySeconds);

	UFUNCTION()
	void HandleEvacuationTimerUpdated(float TimeRemainingSeconds);

	UFUNCTION()
	void HandleBoidExited(AExitPoint* Exit, ABoidsAB* Agent);

	UFUNCTION()
	void HandleQueueDepthChanged(AExitPoint* Exit, int32 NewDepth);

	TWeakObjectPtr<AEmergencyManager> CachedEmergencyManager;

	bool bDailySessionActive = false;
	double DailySessionStartTime = 0.0;
	int32 DailyInteractionCount = 0;
	int32 DailyDestinationsVisited = 0;
	int32 DailySidewalkHits = 0;
	int32 DailyAgentsAtHome = 0;
	int32 DailyAgentsAtWork = 0;
	float DailyElapsedSimMinutes = 0.f;

	struct FDailyCommuteRecord
	{
		double StartTime = 0.0;
		FVector StartLocation = FVector::ZeroVector;
		bool bHasStartLocation = false;
		FString DestinationTag;
		bool bCompleted = false;
		double CompletedTime = 0.0;
	};

	TMap<TWeakObjectPtr<ABoidsAB>, FDailyCommuteRecord> ActiveCommutes;
	TArray<double> CompletedCommuteDurations;

	void RefreshDailySnapshot();

	bool bValidationSessionActive = false;
	FString ValidationScenarioName;
	double ValidationSessionStartTime = 0.0;
	int32 ValidationAgentsSpawned = 0;
	int32 ValidationAgentsActive = 0;
	int32 ValidationAgentsCompleted = 0;
	int32 ValidationCurrentMoving = 0;
	int32 ValidationCurrentWaiting = 0;
	float ValidationCurrentAverageVelocity = 0.f;
	float ValidationCurrentDensity = 0.f;
	float ValidationPeakDensity = 0.f;
	float ValidationAverageQueueLength = 0.f;
	int32 ValidationMaximumQueueLength = 0;
	float ValidationFPS = 0.f;
	float ValidationAverageWaitingTime = 0.f;
	float ValidationMaximumWaitingTime = 0.f;
	float ValidationAverageCongestionTime = 0.f;
	float ValidationThroughput = 0.f;
	float ValidationFlowRate = 0.f;
	float ValidationAverageTravelTime = 0.f;
	float ValidationMinTravelTime = 0.f;
	float ValidationMaxTravelTime = 0.f;
	float ValidationAverageSpeed = 0.f;
	float ValidationAveragePathLength = -1.f;
	TArray<double> ValidationTravelDurations;
	TArray<float> ValidationTripSpeeds;
	TArray<float> ValidationPathLengths;
	TArray<double> ValidationCompletionTimestamps;
	TMap<TWeakObjectPtr<ABoidsAB>, float> ValidationWaitingAccumSeconds;
	TMap<TWeakObjectPtr<ABoidsAB>, float> ValidationCongestionAccumSeconds;
	TSet<FName> ValidationSeenAgentIds;

	void RefreshValidationSnapshot();
	void RecordValidationTripCompleted(ABoidsAB* Agent, const FDailyCommuteRecord& Rec, double NowSeconds);

	bool bInfrastructureSessionActive = false;
	FString InfrastructureLayoutVariant;
	double InfrastructureSessionStartTime = 0.0;
	float InfrastructureElapsedSimMinutes = 0.f;
	float InfrastructurePeakCongestion = 0.f;
	float InfrastructureCongestionDurationSeconds = 0.f;
	double InfrastructureLastCongestionSampleTime = 0.0;
	int32 InfrastructurePedestriansProcessed = 0;

	struct FInfrastructureStationRecord
	{
		int32 PeakQueueLength = 0;
		int32 TotalThroughput = 0;
		float PeakUtilization = 0.f;
		TArray<float> QueueWaitSamplesSeconds;
	};

	TMap<FString, FInfrastructureStationRecord> StationRecords;
	TMap<FString, int32> PathSegmentHits;
	TMap<FString, EPathSegmentCategory> PathSegmentCategories;
	TMap<FString, int32> DestinationPopularity;
	TMap<TWeakObjectPtr<ABoidsAB>, double> StationQueueJoinTimes;
	TArray<double> InfrastructureTravelDurations;

	void RefreshInfrastructureSnapshot();
	FString BuildInfrastructureCsvBody() const;
	static bool ParseMetricCsv(const FString& AbsoluteFilePath, TMap<FString, FString>& OutMetrics);

	bool bCrimeSessionActive = false;
	double CrimeSessionStartTime = 0.0;
	int32 CrimesStarted = 0;
	int32 PoliceResponses = 0;
	int32 CaptureCount = 0;
	TArray<float> CompletedPursuitDurations;
	int32 LeadershipEventCount = 0;

	bool bFastSessionActive = false;
	bool bFastSessionFinalized = false;
	FString MetricsSessionMode = TEXT("Fast");
	FString FastRunId;
	FString FastMapName;
	float FastSimulationSpeed = 0.f;
	int32 FastRandomSeed = 0;
	float FastElapsedSimMinutes = 0.f;
	int32 FastAgentsSpawned = 0;

	struct FFastDestinationRecord
	{
		int32 Sent = 0;
		int32 Reached = 0;
		int32 Failed = 0;
	};

	struct FFastStationRecord
	{
		int32 QueueJoins = 0;
		int32 Admitted = 0;
		int32 Completed = 0;
		int32 PeakQueue = 0;
	};

	struct FFastInteractionRecord
	{
		int32 Attempts = 0;
		int32 Successes = 0;
		int32 Failures = 0;
	};

	TMap<FString, FFastDestinationRecord> FastDestinations;
	TMap<FString, FFastStationRecord> FastStations;
	TMap<FString, FFastInteractionRecord> FastInteractions;
	TMap<FString, float> FastEntranceOpenedSimMinutes;
};

