#include "World/ExitPoint.h"
#include "BoidsAB.h"
#include "TimerManager.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Navigation/NavFilter_SidewalkOnly.h"
#include "NavFilters/NavigationQueryFilter.h"

AExitPoint::AExitPoint()
{
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

    // Tick is used only when staggered path updates are pending.
    // Disabled by default; enabled on demand by ScheduleQueueAdvancement().
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
}


void AExitPoint::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    PurgeInvalidEntries();
    ProcessPendingPathUpdates();
}


bool AExitPoint::RequestQueueSlot(ABoidsAB* Boid, int32& OutQueueIndex)
{
    if (!IsValid(Boid))
        return false;

    if (Queue.Contains(Boid))
    {
        OutQueueIndex = Queue.Find(Boid);
        return true;
    }

    if (Queue.Num() >= MaxQueueSize)
        return false;

    const int32 ProspectiveIndex = Queue.Num();
    FVector ResolvedSlot;
    if (!TryGetReachableQueueSlot(Boid, ProspectiveIndex, ResolvedSlot))
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[ExitPoint] Queue slot %d nav validation failed for %s on %s — rejecting assignment"),
            ProspectiveIndex,
            *Boid->GetName(),
            *GetName());
        return false;
    }

    Queue.Add(Boid);
    OutQueueIndex = Queue.Num() - 1;
    OnQueueDepthChanged.Broadcast(this, Queue.Num());

    UE_LOG(LogTemp, Log,
        TEXT("[ExitPoint] %s joined queue at position %d on %s (slot %s)"),
        *Boid->GetName(),
        OutQueueIndex,
        *GetName(),
        *ResolvedSlot.ToString()
    );

    return true;
}

void AExitPoint::NotifyBoidReady(ABoidsAB* Boid)
{
    if (!IsValid(Boid))
        return;

    ReadyBoids.Add(Boid);

    int32 Index = Queue.Find(Boid);
    if (Index == 0 && !bIsProcessing)
    {
        StartProcessingNextBoid();
    }
}

void AExitPoint::LeaveQueue(ABoidsAB* Boid)
{
    // Intentionally uses raw null check, not IsValid():
    // the boid may already be pending-kill when the incident ends,
    // but we still need to remove its pointer from our collections.
    if (!Boid)
        return;

    if (CurrentProcessingBoid == Boid)
    {
        if (GetWorld())
        {
            GetWorld()->GetTimerManager().ClearTimer(ExitProcessTimer);
        }
        CurrentProcessingBoid = nullptr;
        bIsProcessing = false;
    }

    int32 Index = Queue.Find(Boid);
    if (Index != INDEX_NONE)
    {
        Queue.RemoveAt(Index);
        ReadyBoids.Remove(Boid);

        UE_LOG(LogTemp, Log,
            TEXT("[ExitPoint] %s left queue on %s"),
            *Boid->GetName(),
            *GetName()
        );

        OnQueueDepthChanged.Broadcast(this, Queue.Num());

        ScheduleQueueAdvancement();

        if (!bIsProcessing && Queue.Num() > 0)
        {
            StartProcessingNextBoid();
        }
    }
}

float AExitPoint::GetEstimatedWaitSeconds() const
{
    float Wait = Queue.Num() * ExitProcessDuration;
    if (bIsProcessing)
    {
        Wait += ExitProcessDuration * 0.5f;
    }
    return Wait;
}

FVector AExitPoint::GetQueueWorldLocation(int32 QueueIndex) const
{
    FVector Resolved;
    if (TryResolveQueueSlotLocation(QueueIndex, Resolved, nullptr))
    {
        return Resolved;
    }

    const FVector Raw = ComputeRawQueueSlotLocation(QueueIndex);
    UE_LOG(LogTemp, Warning,
        TEXT("[ExitPoint] %s slot %d: nav snap failed, using raw location %s"),
        *GetName(),
        QueueIndex,
        *Raw.ToString());
    return Raw;
}

bool AExitPoint::TryGetReachableQueueSlot(const ABoidsAB* Agent, int32 QueueIndex, FVector& OutLocation) const
{
    return TryResolveQueueSlotLocation(QueueIndex, OutLocation, Agent);
}

FVector AExitPoint::ComputeRawQueueSlotLocation(int32 QueueIndex) const
{
    FVector BackDir = -GetActorForwardVector();
    BackDir.Z = 0.f;
    BackDir = BackDir.GetSafeNormal();
    return GetActorLocation() + BackDir * QueueIndex * QueueSpacing;
}

bool AExitPoint::ProjectToSidewalkNav(const FVector& QueryPoint, FVector& OutLocation) const
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return false;
    }

    UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
    if (!NavSys)
    {
        return false;
    }

    const ANavigationData* NavData = NavSys->GetDefaultNavDataInstance(FNavigationSystem::DontCreate);
    if (!NavData)
    {
        return false;
    }

    const FSharedConstNavQueryFilter QueryFilter =
        NavData->GetQueryFilter(UNavFilter_SidewalkOnly::StaticClass());

    FNavLocation NavLoc;
    const FVector Extent(QueueNavSearchRadius, QueueNavSearchRadius, 300.f);
    if (NavSys->ProjectPointToNavigation(QueryPoint, NavLoc, Extent, NavData, QueryFilter))
    {
        OutLocation = NavLoc.Location;
        return true;
    }

    return false;
}

bool AExitPoint::HasSidewalkPath(const ABoidsAB* Agent, const FVector& TargetLocation) const
{
    UWorld* World = GetWorld();
    if (!Agent || !World)
    {
        return false;
    }

    UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
    if (!NavSys)
    {
        return false;
    }

    UNavigationPath* NavPath = NavSys->FindPathToLocationSynchronously(
        World,
        Agent->GetActorLocation(),
        TargetLocation,
        nullptr,
        UNavFilter_SidewalkOnly::StaticClass());

    return NavPath && NavPath->PathPoints.Num() > 0;
}

bool AExitPoint::TryResolveQueueSlotLocation(int32 QueueIndex, FVector& OutLocation, const ABoidsAB* PathTestAgent) const
{
    const FVector Raw = ComputeRawQueueSlotLocation(QueueIndex);
    FVector BackDir = -GetActorForwardVector();
    BackDir.Z = 0.f;
    BackDir = BackDir.GetSafeNormal();
    const FVector RightDir = FVector::CrossProduct(FVector::UpVector, BackDir).GetSafeNormal();

    TArray<FVector> Candidates;
    Candidates.Reserve(12);
    Candidates.Add(Raw);

    for (const float Lateral : { -QueueSpacing * 0.25f, QueueSpacing * 0.25f })
    {
        Candidates.Add(Raw + RightDir * Lateral);
    }

    for (int32 Step = 1; Step <= 3; ++Step)
    {
        const int32 CloserIndex = FMath::Max(0, QueueIndex - Step);
        Candidates.Add(ComputeRawQueueSlotLocation(CloserIndex));
    }

    for (const FVector& Candidate : Candidates)
    {
        FVector Snapped;
        if (!ProjectToSidewalkNav(Candidate, Snapped))
        {
            continue;
        }

        if (PathTestAgent && !HasSidewalkPath(PathTestAgent, Snapped))
        {
            UE_LOG(LogTemp, Verbose,
                TEXT("[ExitPoint] %s slot %d candidate %s snapped to %s but no sidewalk path from %s"),
                *GetName(),
                QueueIndex,
                *Candidate.ToString(),
                *Snapped.ToString(),
                *PathTestAgent->GetName());
            continue;
        }

        OutLocation = Snapped;
        UE_LOG(LogTemp, Log,
            TEXT("[ExitPoint] %s slot %d resolved: raw %s -> nav %s%s"),
            *GetName(),
            QueueIndex,
            *Raw.ToString(),
            *Snapped.ToString(),
            PathTestAgent ? *FString::Printf(TEXT(" (reachable from %s)"), *PathTestAgent->GetName()) : TEXT(""));
        return true;
    }

    UE_LOG(LogTemp, Warning,
        TEXT("[ExitPoint] %s slot %d: no valid sidewalk queue location (raw %s)"),
        *GetName(),
        QueueIndex,
        *Raw.ToString());
    return false;
}


void AExitPoint::StartProcessingNextBoid()
{
    if (bIsProcessing)
        return;

    // Iteratively skip invalid front entries (no recursion)
    while (Queue.Num() > 0)
    {
        ABoidsAB* FrontBoid = Queue[0];

        if (!IsValid(FrontBoid))
        {
            Queue.RemoveAt(0);
            ReadyBoids.Remove(FrontBoid);
            continue;
        }

        // Front boid must have reached its slot before processing starts
        if (!ReadyBoids.Contains(FrontBoid))
            return;

        CurrentProcessingBoid = FrontBoid;
        bIsProcessing = true;

        UE_LOG(LogTemp, Warning,
            TEXT("[ExitPoint] %s started processing on %s (duration %.1fs)"),
            *FrontBoid->GetName(),
            *GetName(),
            ExitProcessDuration
        );

        if (GetWorld())
        {
            GetWorld()->GetTimerManager().SetTimer(
                ExitProcessTimer,
                this,
                &AExitPoint::FinishProcessingCurrentBoid,
                ExitProcessDuration,
                false
            );
        }
        return;
    }
}

void AExitPoint::FinishProcessingCurrentBoid()
{
    if (!CurrentProcessingBoid)
    {
        bIsProcessing = false;
        return;
    }

    ABoidsAB* ProcessedBoid = CurrentProcessingBoid;

    Queue.Remove(ProcessedBoid);
    ReadyBoids.Remove(ProcessedBoid);
    CurrentProcessingBoid = nullptr;
    bIsProcessing = false;

    if (IsValid(ProcessedBoid))
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[ExitPoint] %s finished exit on %s"),
            *ProcessedBoid->GetName(),
            *GetName()
        );
        OnBoidExited.Broadcast(this, ProcessedBoid);
        ProcessedBoid->OnExitApproved();
    }

    ScheduleQueueAdvancement();

    // Next boid starts only once the new front is ready
    if (Queue.Num() > 0)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[ExitPoint] Promoting next boid on %s (%d remaining)"),
            *GetName(),
            Queue.Num()
        );
        StartProcessingNextBoid();
    }
}


void AExitPoint::ScheduleQueueAdvancement()
{
    // Discard any in-flight pending updates from a previous advancement;
    // positions are recomputed from the current queue state.
    PendingPathUpdates.Empty();

    for (int32 i = 0; i < Queue.Num(); i++)
    {
        ABoidsAB* Boid = Queue[i];
        if (!IsValid(Boid))
            continue;

        // Boid must re-reach its new slot before being considered ready
        ReadyBoids.Remove(Boid);

        FVector NewSlot;
        if (!TryGetReachableQueueSlot(Boid, i, NewSlot))
        {
            NewSlot = GetQueueWorldLocation(i);
            UE_LOG(LogTemp, Warning,
                TEXT("[ExitPoint] %s advancement: slot %d for %s fell back to %s"),
                *GetName(),
                i,
                *Boid->GetName(),
                *NewSlot.ToString());
        }

        if (i == 0)
        {
            // Front boid receives its path update immediately so exit
            // processing can resume with at most one frame of walk delay.
            Boid->UpdateExitQueuePosition(i, NewSlot);
        }
        else
        {
            // Non-front boids are deferred to spread nav queries across frames.
            FPendingPathUpdate Update;
            Update.Boid = Boid;
            Update.TargetLocation = NewSlot;
            PendingPathUpdates.Add(Update);
        }
    }

    if (PendingPathUpdates.Num() > 0)
    {
        SetActorTickEnabled(true);
    }
    else
    {
        SetActorTickEnabled(false);
    }
}

void AExitPoint::ProcessPendingPathUpdates()
{
    int32 Processed = 0;

    while (PendingPathUpdates.Num() > 0 && Processed < PathUpdatesPerFrame)
    {
        FPendingPathUpdate Update = PendingPathUpdates[0];
        PendingPathUpdates.RemoveAt(0);

        ABoidsAB* Boid = Update.Boid.Get();
        if (!IsValid(Boid))
            continue;

        if (!Queue.Contains(Boid))
            continue;

        int32 CurrentIndex = Queue.Find(Boid);
        Boid->UpdateExitQueuePosition(CurrentIndex, Update.TargetLocation);
        Processed++;
    }

    if (PendingPathUpdates.Num() == 0)
    {
        SetActorTickEnabled(false);
    }
}


void AExitPoint::PurgeInvalidEntries()
{
    bool bQueueChanged = false;

    // Reverse iterate so removals stay safe
    for (int32 i = Queue.Num() - 1; i >= 0; --i)
    {
        if (!IsValid(Queue[i]))
        {
            Queue.RemoveAt(i);
            bQueueChanged = true;
        }
    }

    // Two-pass to avoid iterator invalidation
    {
        TArray<ABoidsAB*> InvalidReady;
        for (ABoidsAB* Boid : ReadyBoids)
        {
            if (!IsValid(Boid))
            {
                InvalidReady.Add(Boid);
            }
        }
        for (ABoidsAB* Boid : InvalidReady)
        {
            ReadyBoids.Remove(Boid);
        }
    }

    if (CurrentProcessingBoid && !IsValid(CurrentProcessingBoid))
    {
        if (GetWorld())
        {
            GetWorld()->GetTimerManager().ClearTimer(ExitProcessTimer);
        }
        CurrentProcessingBoid = nullptr;
        bIsProcessing = false;
        bQueueChanged = true;
    }

    PendingPathUpdates.RemoveAll([](const FPendingPathUpdate& Update) {
        return !Update.Boid.IsValid();
    });

    // If the queue changed, recalculate slot positions so remaining boids
    // don't walk to stale locations.
    if (bQueueChanged && Queue.Num() > 0)
    {
        ScheduleQueueAdvancement();

        if (!bIsProcessing)
        {
            StartProcessingNextBoid();
        }
    }
}
