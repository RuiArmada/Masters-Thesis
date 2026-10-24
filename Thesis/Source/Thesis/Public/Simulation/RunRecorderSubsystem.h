#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RunRecorderSubsystem.generated.h"

class AAgentStation;
class ABoidsAB;
class AScheduleManager;

/**
 * Opt-in per-run recorder, active only when the game is launched with -RunRecord=<Label>.
 *
 * Writes three CSV files under Saved/Analytics/Run_<Label>/, appended once per real second so a run that is
 * closed externally still keeps its data:
 *   rules.csv     one row per rule outcome (authored, eligible and dispatch minute, deferrals, outcome)
 *   stations.csv  one row per station event (admit, queue, arrive, start, complete, leave)
 *   trips.csv     one row per scheduled trip event (start, arrive, fail)
 *   timeline.csv  one row per real second (simulated clock, frame/thread/GPU time, agent state counts)
 */
UCLASS()
class THESIS_API URunRecorderSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return bActive; }

	/** A due rule was not executed this minute (agent busy or blocked); counted until its outcome is recorded. */
	void NoteRuleDeferred(FName AgentId, int32 AuthoredMinute);

	/** Final outcome of a rule for this day: dispatched, stale_skip, or prob_skip. */
	void RecordRule(FName AgentId, int32 AuthoredMinute, int32 EligibleMinute, int32 NowMinute, int32 Day,
		const FString& Action, const TCHAR* Outcome);

	/** Station lifecycle event for one agent. */
	void RecordStationEvent(const AAgentStation* Station, const ABoidsAB* Agent, const TCHAR* Event,
		int32 QueueLength, int32 Occupants);

	/** Scheduled trip (GoTo) event: start, arrive, or fail (Detail holds the reason, e.g. Cancelled or timed out). */
	void RecordTrip(const ABoidsAB* Agent, const FString& Target, const TCHAR* Event, const FString& Detail,
		float RemainingDistance2D);

private:
	double GetSimMinutes() const;
	double NowSeconds() const;
	bool bUseSimTime = false;
	void Flush();

	/** Optional screenshots (-ShotAt=<s>,<s>,...): frames the agents (or actors tagged -ShotFocus=<Tag>) from an oblique camera. */
	void PlaceShotCamera();
	TArray<float> ShotTimes;
	int32 NextShot = 0;
	int32 ShotFramesPending = -1;
	FString ShotFocusTag;
	float ShotRadius = 0.f;
	float ShotPitch = 50.f;

	bool bActive = false;
	FString OutDir;
	FString RulesBuffer;
	FString StationsBuffer;
	FString TripsBuffer;
	FString TimelineBuffer;
	FString PositionsBuffer;
	float PositionInterval = 0.f;
	double NextPositionRealTime = 0.0;
	TMap<FString, int32> PendingDeferrals;

	TWeakObjectPtr<AScheduleManager> CachedScheduleManager;
	double StartRealTime = 0.0;
	double NextSampleRealTime = 0.0;

	// Per-second accumulators for the timeline.
	int32 FramesThisSecond = 0;
	double FrameMsSum = 0.0;
	double FrameMsMax = 0.0;
	double GameThreadMsSum = 0.0;
	double GpuMsSum = 0.0;
};
