#include "Simulation/RunRecorderSubsystem.h"
#include "Simulation/SimulationClock.h"
#include "ScheduleManager.h"
#include "AgentStation.h"
#include "BoidsAB.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "RenderTimer.h"
#include "UnrealClient.h"
#include "Camera/CameraActor.h"
#include "Engine/StaticMeshActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "DynamicRHI.h"
#include "Components/CapsuleComponent.h"

namespace
{
	FString RuleKey(FName AgentId, int32 AuthoredMinute)
	{
		return FString::Printf(TEXT("%s|%d"), *AgentId.ToString(), AuthoredMinute);
	}

	void AppendToFile(const FString& Path, FString& Buffer)
	{
		if (Buffer.IsEmpty())
		{
			return;
		}
		FFileHelper::SaveStringToFile(Buffer, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
			&IFileManager::Get(), FILEWRITE_Append);
		Buffer.Reset();
	}
}

bool URunRecorderSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	FString Label;
	return Super::ShouldCreateSubsystem(Outer) && FParse::Value(FCommandLine::Get(), TEXT("RunRecord="), Label);
}

void URunRecorderSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (!InWorld.IsGameWorld())
	{
		return;
	}

	FString Label;
	FParse::Value(FCommandLine::Get(), TEXT("RunRecord="), Label);
	const FString MapName = UGameplayStatics::GetCurrentLevelName(&InWorld, true);
	if (MapName.StartsWith(TEXT("Untitled")))
	{
		return;
	}

	OutDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Analytics"), FString::Printf(TEXT("Run_%s"), *Label));
	FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*OutDir);

	RulesBuffer = TEXT("agent,authored_min,eligible_min,dispatch_min,day,minutes_late,deferrals,action,outcome\n");
	StationsBuffer = TEXT("real_s,sim_min,station,agent,event,queue_len,occupants\n");
	TripsBuffer = TEXT("real_s,sim_min,agent,target,event,detail,remaining_cm\n");
	TimelineBuffer = TEXT("real_s,sim_min,frames,frame_ms_mean,frame_ms_max,game_thread_ms,gpu_ms,agents,moving,idle,queued,at_station,fleeing,safe,casualty\n");

	FString ShotList;
	if (FParse::Value(FCommandLine::Get(), TEXT("ShotAt="), ShotList, false))
	{
		TArray<FString> Parts;
		ShotList.ParseIntoArray(Parts, TEXT(","));
		for (const FString& P : Parts)
		{
			ShotTimes.Add(FCString::Atof(*P));
		}
		ShotTimes.Sort();
		FParse::Value(FCommandLine::Get(), TEXT("ShotFocus="), ShotFocusTag);
		FParse::Value(FCommandLine::Get(), TEXT("ShotRadius="), ShotRadius);
		FParse::Value(FCommandLine::Get(), TEXT("ShotPitch="), ShotPitch);
	}

	// Optional agent positions (-RecordPositions=<seconds>), used to measure lateral spread and overlaps.
	FParse::Value(FCommandLine::Get(), TEXT("RecordPositions="), PositionInterval);
	if (PositionInterval > 0.f)
	{
		PositionsBuffer = TEXT("real_s,sim_min,agent,x,y,z,vx,vy,moving,capsule_radius\n");
	}

	// QA ONLY: -RecordSimTime stamps rows and samples positions in simulated seconds, for fixed-step runs.
	bUseSimTime = FParse::Param(FCommandLine::Get(), TEXT("RecordSimTime"));

	bActive = true;
	StartRealTime = NowSeconds();
	NextSampleRealTime = StartRealTime + 1.0;
	NextPositionRealTime = StartRealTime;
	UE_LOG(LogTemp, Log, TEXT("[RunRecorder] Recording %s to %s"), *MapName, *OutDir);
}

void URunRecorderSubsystem::Deinitialize()
{
	if (bActive)
	{
		Flush();
	}
	bActive = false;
	Super::Deinitialize();
}

double URunRecorderSubsystem::NowSeconds() const
{
	if (bUseSimTime)
	{
		// Simulated seconds from the clock; zero until the schedule manager has created it.
		const double SimMin = GetSimMinutes();
		return SimMin >= 0.0 ? SimMin * 60.0 : 0.0;
	}
	return FPlatformTime::Seconds();
}

double URunRecorderSubsystem::GetSimMinutes() const
{
	AScheduleManager* SM = CachedScheduleManager.Get();
	if (!SM && GetWorld())
	{
		SM = Cast<AScheduleManager>(UGameplayStatics::GetActorOfClass(GetWorld(), AScheduleManager::StaticClass()));
		const_cast<URunRecorderSubsystem*>(this)->CachedScheduleManager = SM;
	}
	if (SM)
	{
		if (USimulationClock* Clock = SM->GetSimulationClock())
		{
			return Clock->GetAccumulatedSimMinutes();
		}
	}
	return -1.0;
}

void URunRecorderSubsystem::NoteRuleDeferred(FName AgentId, int32 AuthoredMinute)
{
	if (bActive)
	{
		PendingDeferrals.FindOrAdd(RuleKey(AgentId, AuthoredMinute))++;
	}
}

void URunRecorderSubsystem::RecordRule(FName AgentId, int32 AuthoredMinute, int32 EligibleMinute, int32 NowMinute,
	int32 Day, const FString& Action, const TCHAR* Outcome)
{
	if (!bActive)
	{
		return;
	}

	int32 Deferrals = 0;
	PendingDeferrals.RemoveAndCopyValue(RuleKey(AgentId, AuthoredMinute), Deferrals);
	int32 Late = NowMinute - EligibleMinute;
	if (Late < 0)
	{
		Late += 1440;
	}
	RulesBuffer += FString::Printf(TEXT("%s,%d,%d,%d,%d,%d,%d,%s,%s\n"), *AgentId.ToString(), AuthoredMinute,
		EligibleMinute, NowMinute, Day, Late, Deferrals, *Action, Outcome);
}

void URunRecorderSubsystem::RecordStationEvent(const AAgentStation* Station, const ABoidsAB* Agent, const TCHAR* Event,
	int32 QueueLength, int32 Occupants)
{
	if (!bActive || !Station)
	{
		return;
	}
	StationsBuffer += FString::Printf(TEXT("%.3f,%.3f,%s,%s,%s,%d,%d\n"),
		NowSeconds() - StartRealTime, GetSimMinutes(), *Station->GetName(),
		Agent ? *Agent->AgentId.ToString() : TEXT(""), Event, QueueLength, Occupants);
}

void URunRecorderSubsystem::RecordTrip(const ABoidsAB* Agent, const FString& Target, const TCHAR* Event,
	const FString& Detail, float RemainingDistance2D)
{
	if (!bActive)
	{
		return;
	}
	const FString CleanDetail = Detail.Replace(TEXT(","), TEXT(";"));
	TripsBuffer += FString::Printf(TEXT("%.3f,%.3f,%s,%s,%s,%s,%.1f\n"),
		NowSeconds() - StartRealTime, GetSimMinutes(), Agent ? *Agent->AgentId.ToString() : TEXT(""),
		*Target, Event, *CleanDetail, RemainingDistance2D);
}

void URunRecorderSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Screenshots: place the camera, wait a few frames for the view to settle, then capture.
	if (ShotFramesPending > 0 && --ShotFramesPending == 0)
	{
		const FString File = FPaths::Combine(OutDir, FString::Printf(TEXT("shot_%02d_%03ds.png"), NextShot,
			FMath::RoundToInt(ShotTimes[NextShot])));
		FScreenshotRequest::RequestScreenshot(File, false, false);
		UE_LOG(LogTemp, Log, TEXT("[RunRecorder] Screenshot %s"), *File);
		++NextShot;
		ShotFramesPending = -1;
	}
	else if (ShotFramesPending < 0 && NextShot < ShotTimes.Num()
		&& NowSeconds() - StartRealTime >= ShotTimes[NextShot])
	{
		PlaceShotCamera();
		ShotFramesPending = 5;
	}

	const double FrameMs = FApp::GetDeltaTime() * 1000.0;
	++FramesThisSecond;
	FrameMsSum += FrameMs;
	FrameMsMax = FMath::Max(FrameMsMax, FrameMs);
	GameThreadMsSum += FPlatformTime::ToMilliseconds(GGameThreadTime);
	GpuMsSum += GDynamicRHI ? FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()) : 0.0;

	const double Now = NowSeconds();
	if (PositionInterval > 0.f && Now >= NextPositionRealTime)
	{
		NextPositionRealTime = Now + PositionInterval;
		const double SimMin = GetSimMinutes();
		for (TActorIterator<ABoidsAB> It(GetWorld()); It; ++It)
		{
			const ABoidsAB* B = *It;
			const FVector P = B->GetActorLocation();
			const FVector V = B->GetVelocity();
			const UCapsuleComponent* Capsule = B->GetCapsuleComponent();
			PositionsBuffer += FString::Printf(TEXT("%.3f,%.3f,%s,%.1f,%.1f,%.1f,%.1f,%.1f,%d,%.1f\n"),
				Now - StartRealTime, SimMin, *B->AgentId.ToString(), P.X, P.Y, P.Z, V.X, V.Y, B->IsMoving() ? 1 : 0,
				Capsule ? Capsule->GetScaledCapsuleRadius() : 0.f);
		}
	}
	if (Now < NextSampleRealTime)
	{
		return;
	}
	NextSampleRealTime = Now + 1.0;

	int32 Agents = 0, Moving = 0, Idle = 0, Queued = 0, AtStation = 0, Fleeing = 0, Safe = 0, Casualty = 0;
	for (TActorIterator<ABoidsAB> It(GetWorld()); It; ++It)
	{
		const ABoidsAB* B = *It;
		++Agents;
		Moving += B->IsMoving() ? 1 : 0;
		Idle += B->IsIdle() ? 1 : 0;
		Queued += B->IsQueuedAtStation() ? 1 : 0;
		AtStation += B->IsInteractingWithStation() ? 1 : 0;
		Fleeing += B->IsFleeing() ? 1 : 0;
		Safe += B->IsSafelyEvacuated() ? 1 : 0;
		Casualty += B->IsCasualty() ? 1 : 0;
	}

	const double N = FMath::Max(1, FramesThisSecond);
	TimelineBuffer += FString::Printf(TEXT("%.3f,%.3f,%d,%.3f,%.3f,%.3f,%.3f,%d,%d,%d,%d,%d,%d,%d,%d\n"),
		Now - StartRealTime, GetSimMinutes(), FramesThisSecond, FrameMsSum / N, FrameMsMax,
		GameThreadMsSum / N, GpuMsSum / N, Agents, Moving, Idle, Queued, AtStation, Fleeing, Safe, Casualty);

	FramesThisSecond = 0;
	FrameMsSum = FrameMsMax = GameThreadMsSum = GpuMsSum = 0.0;
	Flush();
}

void URunRecorderSubsystem::PlaceShotCamera()
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	if (!PC)
	{
		return;
	}

	TArray<FVector> Points;
	if (!ShotFocusTag.IsEmpty())
	{
		TArray<AActor*> Tagged;
		UGameplayStatics::GetAllActorsWithTag(World, FName(*ShotFocusTag), Tagged);
		for (const AActor* A : Tagged)
		{
			Points.Add(A->GetActorLocation());
		}
	}
	if (Points.Num() == 0)
	{
		for (TActorIterator<ABoidsAB> It(World); It; ++It)
		{
			Points.Add(It->GetActorLocation());
		}
	}
	if (Points.Num() == 0)
	{
		return;
	}

	FVector Center = FVector::ZeroVector;
	for (const FVector& P : Points)
	{
		Center += P;
	}
	Center /= Points.Num();
	TArray<float> Dist;
	for (const FVector& P : Points)
	{
		Dist.Add(FVector::Dist2D(P, Center));
	}
	Dist.Sort();
	const float Spread = Dist[FMath::Clamp(FMath::FloorToInt(0.9f * Dist.Num()), 0, Dist.Num() - 1)];
	const float Radius = ShotRadius > 0.f ? ShotRadius : FMath::Max(Spread * 1.3f, 800.f);

	// Oblique view from the south-west, pitched down by ShotPitch degrees.
	const float Pitch = FMath::DegreesToRadians(ShotPitch);
	const float Dist3D = Radius / FMath::Tan(FMath::DegreesToRadians(35.f));
	const FVector Dir = FVector(-0.7071f, -0.7071f, 0.f) * FMath::Cos(Pitch) + FVector(0.f, 0.f, FMath::Sin(Pitch));
	const FVector CamLoc = Center + Dir * Dist3D;

	// Hide roofs and ceilings: static geometry whose lowest point is well above every agent.
	float AgentTopZ = -FLT_MAX;
	for (TActorIterator<ABoidsAB> It(World); It; ++It)
	{
		AgentTopZ = FMath::Max(AgentTopZ, It->GetActorLocation().Z);
	}
	if (AgentTopZ > -FLT_MAX)
	{
		for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
		{
			FVector Origin, Extent;
			It->GetActorBounds(false, Origin, Extent);
			if (Origin.Z - Extent.Z > AgentTopZ + 150.f)
			{
				It->SetActorHiddenInGame(true);
			}
		}
	}

	ACameraActor* Cam = World->SpawnActor<ACameraActor>(CamLoc, (Center - CamLoc).Rotation());
	if (Cam)
	{
		Cam->GetCameraComponent()->SetFieldOfView(70.f);
		PC->SetViewTarget(Cam);
	}
}

void URunRecorderSubsystem::Flush()
{
	if (OutDir.IsEmpty())
	{
		return;
	}
	AppendToFile(FPaths::Combine(OutDir, TEXT("rules.csv")), RulesBuffer);
	AppendToFile(FPaths::Combine(OutDir, TEXT("stations.csv")), StationsBuffer);
	AppendToFile(FPaths::Combine(OutDir, TEXT("trips.csv")), TripsBuffer);
	AppendToFile(FPaths::Combine(OutDir, TEXT("timeline.csv")), TimelineBuffer);
	AppendToFile(FPaths::Combine(OutDir, TEXT("positions.csv")), PositionsBuffer);
}

TStatId URunRecorderSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(URunRecorderSubsystem, STATGROUP_Tickables);
}
