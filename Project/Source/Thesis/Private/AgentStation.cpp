#include "AgentStation.h"
#include "BoidsAB.h"
#include "Simulation/SimulationAnalytics.h"
#include "Simulation/SimulationFeatureRegistry.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

AAgentStation::AAgentStation()
{
    PrimaryActorTick.bCanEverTick = false;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    RootComponent = SceneRoot;

    InteractionPoint = CreateDefaultSubobject<USceneComponent>(TEXT("InteractionPoint"));
    if (InteractionPoint)
    {
        InteractionPoint->SetupAttachment(RootComponent);
    }

#if WITH_EDITORONLY_DATA
    EditorBillboard = CreateDefaultSubobject<UBillboardComponent>(TEXT("EditorBillboard"));
    if (EditorBillboard)
    {
        EditorBillboard->SetupAttachment(RootComponent);
        EditorBillboard->SetHiddenInGame(true);

        static ConstructorHelpers::FObjectFinder<UTexture2D> IconTexture(
            TEXT("/Engine/EditorResources/S_Note.S_Note")
        );
        if (IconTexture.Succeeded())
        {
            EditorBillboard->SetSprite(IconTexture.Object);
        }
    }
#endif
}

ABoidsAB* AAgentStation::GetCurrentOccupant() const
{
    return ActiveOccupants.Num() > 0 ? ActiveOccupants[0] : nullptr;
}

bool AAgentStation::IsAgentActiveOccupant(ABoidsAB* Agent) const
{
    return Agent && ActiveOccupants.Contains(Agent);
}

void AAgentStation::SetMaxConcurrentOccupants(int32 NewCapacity)
{
    MaxConcurrentOccupants = FMath::Max(1, NewCapacity);
    TryFillOpenSlots();
}

float AAgentStation::GetUtilization() const
{
    const int32 Capacity = GetMaxConcurrentOccupants();
    if (Capacity <= 0)
    {
        return 0.f;
    }
    return static_cast<float>(ActiveOccupants.Num() + Queue.Num()) / static_cast<float>(Capacity);
}

bool AAgentStation::RequestEnter(ABoidsAB* Agent)
{
    if (!Agent)
    {
        return false;
    }

    if (IsAgentActiveOccupant(Agent))
    {
        return true;
    }

    if (Queue.Contains(Agent))
    {
        return true;
    }

    if (ActiveOccupants.Num() < GetMaxConcurrentOccupants())
    {
        AdmitAgent(Agent);
        return true;
    }

    Queue.Add(Agent);
    OnAgentEnqueued.Broadcast(Agent);
    if (USimulationAnalytics* Analytics = GetWorld()->GetSubsystem<USimulationAnalytics>())
    {
        Analytics->RecordStationQueueJoin(this, Agent);
    }
    return true;
}

void AAgentStation::LeaveStation(ABoidsAB* Agent)
{
    if (!Agent)
    {
        return;
    }

    if (IsAgentActiveOccupant(Agent))
    {
        ClearInteractionTimer(Agent);
        AwaitingArrival.Remove(Agent);
        ActiveOccupants.Remove(Agent);
        Agent->OnStationInteractEnd();
        // Defer queue admit: OnStationInteractEnd / Break handlers may still be on the stack
        // (pipeline advance, StartMoveTo). Filling immediately caused null crashes on clients.
        ScheduleTryFillOpenSlots();
        return;
    }

    Queue.Remove(Agent);
}

bool AAgentStation::IsOccupied() const
{
    return ActiveOccupants.Num() > 0;
}

bool AAgentStation::HasAvailableSlot() const
{
    return ActiveOccupants.Num() < GetMaxConcurrentOccupants();
}

bool AAgentStation::MatchesIdentifier(const FString& Identifier) const
{
    if (Identifier.IsEmpty())
    {
        return false;
    }

    const FName IdName(*Identifier);
    if (!StationIdentifier.IsNone() && StationIdentifier.IsEqual(IdName, ENameCase::IgnoreCase))
    {
        return true;
    }

    return ActorHasTag(IdName);
}

bool AAgentStation::HasInteractionAnimation() const
{
    switch (InteractionAnimType)
    {
    case EStationInteractionAnimType::Montage:
        return InteractionMontage != nullptr;
    case EStationInteractionAnimType::Sequence:
        return InteractionSequence != nullptr;
    default:
        return false;
    }
}

bool AAgentStation::HasInteractionPositioning() const
{
    return InteractionPositionMode != EStationInteractionPositionMode::None;
}

FTransform AAgentStation::GetWorldInteractionTransform() const
{
    const FTransform Base = InteractionPoint
        ? InteractionPoint->GetComponentTransform()
        : GetActorTransform();

    const FVector WorldLocation = Base.GetLocation()
        + Base.TransformVectorNoScale(InteractionLocationOffset);
    const FQuat WorldRotation = (Base.GetRotation() * InteractionRotationOffset.Quaternion()).GetNormalized();
    return FTransform(WorldRotation, WorldLocation, Base.GetScale3D());
}

void AAgentStation::BeginInteractionForAgent(ABoidsAB* Agent)
{
    if (!Agent || !IsAgentActiveOccupant(Agent))
    {
        return;
    }

    // Avoid double-start if already timing this occupant.
    if (OccupantTimers.Contains(Agent))
    {
        return;
    }

    StartInteractionTimer(Agent);
    Agent->OnStationInteractStart();
}

void AAgentStation::NotifyOccupantReached(ABoidsAB* Agent)
{
    if (!Agent || !IsAgentActiveOccupant(Agent))
    {
        return;
    }

    if (!AwaitingArrival.Contains(Agent))
    {
        return;
    }

    AwaitingArrival.Remove(Agent);

    if (HasInteractionPositioning())
    {
        Agent->BeginStationInteractionPose(this);
        return;
    }

    BeginInteractionForAgent(Agent);
}

void AAgentStation::AdmitAgent(ABoidsAB* Agent)
{
    if (!Agent || IsAgentActiveOccupant(Agent))
    {
        return;
    }

    ActiveOccupants.Add(Agent);
    AwaitingArrival.Add(Agent);
    OnAgentAdmitted.Broadcast(Agent);
    if (USimulationAnalytics* Analytics = GetWorld()->GetSubsystem<USimulationAnalytics>())
    {
        Analytics->RecordStationAdmitted(this, Agent);
    }
}

void AAgentStation::AdmitQueuedAgent(ABoidsAB* Agent)
{
    if (!IsValid(Agent))
    {
        return;
    }

    // Agent must still be waiting on this station (may have been retargeted/destroyed).
    if (Agent->GetTargetStation() != this)
    {
        return;
    }

    if (IsAgentActiveOccupant(Agent))
    {
        Agent->OnStationBecameAvailable();
        return;
    }

    ActiveOccupants.Add(Agent);
    AwaitingArrival.Add(Agent);
    OnAgentAdmitted.Broadcast(Agent);
    if (UWorld* World = GetWorld())
    {
        if (USimulationAnalytics* Analytics = World->GetSubsystem<USimulationAnalytics>())
        {
            Analytics->RecordStationAdmitted(this, Agent);
        }
    }
    Agent->OnStationBecameAvailable();
}

void AAgentStation::TryFillOpenSlots()
{
    while (Queue.Num() > 0 && ActiveOccupants.Num() < GetMaxConcurrentOccupants())
    {
        ABoidsAB* NextAgent = Queue[0];
        Queue.RemoveAt(0);

        if (!IsValid(NextAgent))
        {
            continue;
        }

        // Drop stale queue entries (agent left, fled, or retargeted another station).
        if (NextAgent->GetTargetStation() != this)
        {
            continue;
        }

        AdmitQueuedAgent(NextAgent);
    }
}

void AAgentStation::ScheduleTryFillOpenSlots()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        TryFillOpenSlots();
        return;
    }

    // Collapse multiple leave/timer events in the same frame into one fill.
    if (bTryFillScheduled)
    {
        return;
    }
    bTryFillScheduled = true;

    TWeakObjectPtr<AAgentStation> WeakThis(this);
    World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([WeakThis]()
    {
        if (AAgentStation* Station = WeakThis.Get())
        {
            Station->bTryFillScheduled = false;
            Station->TryFillOpenSlots();
        }
    }));
}

void AAgentStation::StartInteractionTimer(ABoidsAB* Agent)
{
    if (!Agent || !GetWorld())
    {
        return;
    }

    float Duration = InteractionDuration;
    const float PendingDuration = Agent->GetPendingStationDuration();
    if (PendingDuration > 0.0f)
    {
        Duration = PendingDuration;
    }

    if (Duration <= 0.0f)
    {
        return;
    }

    // Visual: authored duration is real seconds.
    // Fast: authored duration is simulation seconds (timer = duration / clock speed).
    // AccurateTime: same Visual number, timer scaled so the sim clock matches Visual occupancy.
    float TimerSeconds = Duration;
    if (UWorld* World = GetWorld())
    {
        if (USimulationFeatureRegistry* Registry = World->GetSubsystem<USimulationFeatureRegistry>())
        {
            TimerSeconds = Registry->ScaleAuthoredDurationToRealSeconds(Duration);
        }
    }

    UE_LOG(LogTemp, Log,
        TEXT("[Station] %s occupying %s for %.1f timer-seconds (authored %.1f)%s"),
        *Agent->GetName(),
        *GetName(),
        TimerSeconds,
        Duration,
        PendingDuration > 0.0f ? TEXT(" (from XML)") : TEXT(" (default)"));

    FTimerHandle Handle;
    GetWorld()->GetTimerManager().SetTimer(
        Handle,
        FTimerDelegate::CreateUObject(this, &AAgentStation::OnInteractionTimerExpired, Agent),
        TimerSeconds,
        false);
    OccupantTimers.Add(Agent, Handle);
}

void AAgentStation::ClearInteractionTimer(ABoidsAB* Agent)
{
    if (!Agent || !GetWorld())
    {
        return;
    }

    if (FTimerHandle* Handle = OccupantTimers.Find(Agent))
    {
        GetWorld()->GetTimerManager().ClearTimer(*Handle);
        OccupantTimers.Remove(Agent);
    }
}

void AAgentStation::OnInteractionTimerExpired(ABoidsAB* Agent)
{
    if (!Agent || !IsAgentActiveOccupant(Agent))
    {
        return;
    }

    ClearInteractionTimer(Agent);
    AwaitingArrival.Remove(Agent);
    ActiveOccupants.Remove(Agent);
    Agent->NotifyStationInteractionCompleted();
    OnAgentReleased.Broadcast(Agent);
    if (USimulationAnalytics* Analytics = GetWorld()->GetSubsystem<USimulationAnalytics>())
    {
        Analytics->RecordStationServiceCompleted(this, Agent);
    }
    ScheduleTryFillOpenSlots();
}
