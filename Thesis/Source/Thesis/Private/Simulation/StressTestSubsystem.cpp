#include "Simulation/StressTestSubsystem.h"
#include "BoidsAB.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformMisc.h"
#include "RenderTimer.h"
#include "DynamicRHI.h"
#include "EngineUtils.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PrimitiveComponent.h"

namespace
{
	float Percentile(TArray<float> Values, float P)
	{
		if (Values.Num() == 0)
		{
			return 0.f;
		}
		Values.Sort();
		const int32 Index = FMath::Clamp(FMath::CeilToInt(P * Values.Num()) - 1, 0, Values.Num() - 1);
		return Values[Index];
	}

	float Mean(const TArray<float>& Values)
	{
		if (Values.Num() == 0)
		{
			return 0.f;
		}
		double Sum = 0.0;
		for (const float V : Values)
		{
			Sum += V;
		}
		return static_cast<float>(Sum / Values.Num());
	}
}

bool UStressTestSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	int32 Requested = 0;
	return Super::ShouldCreateSubsystem(Outer)
		&& FParse::Value(FCommandLine::Get(), TEXT("StressAgents="), Requested)
		&& Requested > 0;
}

void UStressTestSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (!InWorld.IsGameWorld())
	{
		return;
	}

	const TCHAR* Cmd = FCommandLine::Get();
	FParse::Value(Cmd, TEXT("StressAgents="), TargetAgents);
	FParse::Value(Cmd, TEXT("StressWarmup="), WarmupSeconds);
	FParse::Value(Cmd, TEXT("StressSeconds="), MeasureSeconds);
	bQuitWhenDone = FParse::Param(Cmd, TEXT("StressQuit"));

	// Agents are created during the first frame's BeginPlay, so cloning waits for the first tick.
	bActive = true;
	StartRealTime = FPlatformTime::Seconds();
	FrameMs.Reserve(16384);
	GameThreadMs.Reserve(16384);
	RenderThreadMs.Reserve(16384);
	GpuMs.Reserve(16384);
}

void UStressTestSubsystem::SpawnClones(UWorld& InWorld)
{
	TArray<ABoidsAB*> Placed;
	int32 MaxIndex = -1;
	for (TActorIterator<ABoidsAB> It(&InWorld); It; ++It)
	{
		Placed.Add(*It);
		FString Id = It->AgentId.ToString();
		int32 Index = 0;
		if (Id.RemoveFromStart(TEXT("Agent_")) && Id.IsNumeric())
		{
			Index = FCString::Atoi(*Id);
			MaxIndex = FMath::Max(MaxIndex, Index);
		}
	}

	if (Placed.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Stress] No agents in level to clone"));
		return;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	const int32 ToSpawn = FMath::Max(0, TargetAgents - Placed.Num());
	for (int32 i = 0; i < ToSpawn; ++i)
	{
		const ABoidsAB* Template = Placed[i % Placed.Num()];
		const FVector2D Offset = FMath::RandPointInCircle(350.f);
		const FVector Location = Template->GetActorLocation() + FVector(Offset.X, Offset.Y, 0.f);

		ABoidsAB* Clone = InWorld.SpawnActor<ABoidsAB>(Template->GetClass(), Location, Template->GetActorRotation(), Params);
		if (Clone)
		{
			Clone->AgentId = FName(*FString::Printf(TEXT("Agent_%d"), ++MaxIndex));
			++SpawnedAgents;
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[Stress] Level had %d agents; spawned %d clones (target %d)"),
		Placed.Num(), SpawnedAgents, TargetAgents);

	// -StressHideMeshes: hide every agent's meshes and stop their component ticks (skeletal animation, groom),
	// keeping only the capsule, so the measured frame time excludes the cost of the characters.
	if (FParse::Param(FCommandLine::Get(), TEXT("StressHideMeshes")))
	{
		int32 Hidden = 0;
		for (TActorIterator<ABoidsAB> It(&InWorld); It; ++It)
		{
			TInlineComponentArray<UPrimitiveComponent*> Prims;
			It->GetComponents(Prims);
			for (UPrimitiveComponent* Prim : Prims)
			{
				if (!Prim || Prim == It->GetRootComponent())
				{
					continue;
				}
				Prim->SetVisibility(false, true);
				Prim->SetHiddenInGame(true, true);
				Prim->SetComponentTickEnabled(false);
				if (USkeletalMeshComponent* Skel = Cast<USkeletalMeshComponent>(Prim))
				{
					Skel->bPauseAnims = true;
					Skel->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
				}
				++Hidden;
			}
		}
		UE_LOG(LogTemp, Log, TEXT("[Stress] Hid %d agent components"), Hidden);
	}
}

void UStressTestSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bClonesSpawned)
	{
		bClonesSpawned = true;
		if (UWorld* World = GetWorld())
		{
			SpawnClones(*World);
		}
		StartRealTime = FPlatformTime::Seconds();
		return;
	}

	const double Elapsed = FPlatformTime::Seconds() - StartRealTime;
	if (Elapsed < WarmupSeconds)
	{
		return;
	}

	FrameMs.Add(static_cast<float>(FApp::GetDeltaTime() * 1000.0));
	GameThreadMs.Add(static_cast<float>(FPlatformTime::ToMilliseconds(GGameThreadTime)));
	RenderThreadMs.Add(static_cast<float>(FPlatformTime::ToMilliseconds(GRenderThreadTime)));
	GpuMs.Add(GDynamicRHI ? static_cast<float>(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles())) : 0.f);
	PeakUsedPhysicalBytes = FMath::Max<uint64>(PeakUsedPhysicalBytes, FPlatformMemory::GetStats().UsedPhysical);

	if (Elapsed >= WarmupSeconds + MeasureSeconds)
	{
		FinishAndExport();
	}
}

void UStressTestSubsystem::FinishAndExport()
{
	bFinished = true;

	UWorld* World = GetWorld();
	int32 LiveAgents = 0;
	if (World)
	{
		for (TActorIterator<ABoidsAB> It(World); It; ++It)
		{
			++LiveAgents;
		}
	}

	double TotalSeconds = 0.0;
	for (const float Ms : FrameMs)
	{
		TotalSeconds += Ms / 1000.0;
	}

	const FString MapName = World ? UGameplayStatics::GetCurrentLevelName(World, true) : TEXT("Unknown");
	FString CSV = TEXT("Metric,Value\n");
	CSV += FString::Printf(TEXT("Map,%s\n"), *MapName);
	CSV += FString::Printf(TEXT("TargetAgents,%d\n"), TargetAgents);
	CSV += FString::Printf(TEXT("LiveAgents,%d\n"), LiveAgents);
	CSV += FString::Printf(TEXT("SpawnedClones,%d\n"), SpawnedAgents);
	CSV += FString::Printf(TEXT("WarmupSeconds,%.1f\n"), WarmupSeconds);
	CSV += FString::Printf(TEXT("MeasuredSeconds,%.2f\n"), TotalSeconds);
	CSV += FString::Printf(TEXT("Frames,%d\n"), FrameMs.Num());
	CSV += FString::Printf(TEXT("MeanFPS,%.2f\n"), TotalSeconds > 0.0 ? FrameMs.Num() / TotalSeconds : 0.0);
	CSV += FString::Printf(TEXT("FrameMsMean,%.3f\n"), Mean(FrameMs));
	CSV += FString::Printf(TEXT("FrameMsP50,%.3f\n"), Percentile(FrameMs, 0.50f));
	CSV += FString::Printf(TEXT("FrameMsP95,%.3f\n"), Percentile(FrameMs, 0.95f));
	CSV += FString::Printf(TEXT("FrameMsP99,%.3f\n"), Percentile(FrameMs, 0.99f));
	CSV += FString::Printf(TEXT("FrameMsMax,%.3f\n"), Percentile(FrameMs, 1.0f));
	CSV += FString::Printf(TEXT("GameThreadMsMean,%.3f\n"), Mean(GameThreadMs));
	CSV += FString::Printf(TEXT("GameThreadMsP95,%.3f\n"), Percentile(GameThreadMs, 0.95f));
	CSV += FString::Printf(TEXT("RenderThreadMsMean,%.3f\n"), Mean(RenderThreadMs));
	CSV += FString::Printf(TEXT("GpuMsMean,%.3f\n"), Mean(GpuMs));
	CSV += FString::Printf(TEXT("GpuMsP95,%.3f\n"), Percentile(GpuMs, 0.95f));
	CSV += FString::Printf(TEXT("PeakUsedPhysicalMB,%.1f\n"), PeakUsedPhysicalBytes / (1024.0 * 1024.0));

	const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Analytics"),
		FString::Printf(TEXT("Stress_%s_%d_%s.csv"), *MapName, TargetAgents, *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))));
	FFileHelper::SaveStringToFile(CSV, *Path);
	UE_LOG(LogTemp, Log, TEXT("[Stress] Wrote %s"), *Path);

	if (bQuitWhenDone)
	{
		FPlatformMisc::RequestExit(false);
	}
}

TStatId UStressTestSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UStressTestSubsystem, STATGROUP_Tickables);
}
