#include "BoidsAB.h"
#include "FlockController.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

#include "ScheduleManager.h"

#include "Navigation/NavFilter_SidewalkOnly.h"
#include "Navigation/NavArea_Sidewalk.h"
#include "World/MallEntranceGate.h"
#include "World/ExitPoint.h"
#include "World/EvacuationSafeZone.h"
#include "AgentStation.h"
#include "AI/AgentNeedsComponent.h"
#include "AI/AgentPrioritySelectorComponent.h"
#include "AI/AgentFallbackHandlerComponent.h"
#include "AI/AgentLeadershipComponent.h"
#include "AI/AgentCrimeRoleComponent.h"
#include "AI/AgentStationAnimationTypes.h"
#include "AI/StationInteractionTypes.h"
#include "AgentArea.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "DailyUrbanLife/PedestrianInteractionComponent.h"
#include "Actions/AgentAction.h"
#include "Simulation/SimulationFeatureRegistry.h"
#include "Simulation/SimulationAnalytics.h"
#include "Simulation/CrowdRoutingService.h"

// Station nav log diagnostics (no in-world draw).
static constexpr bool GStationNavDebug = true;
static constexpr float GStationNavStopLogCooldown = 2.f;

#pragma region UE_Basics

ABoidsAB::ABoidsAB()
{
    PrimaryActorTick.bCanEverTick = true;

    NeedsComponent = CreateDefaultSubobject<UAgentNeedsComponent>(TEXT("NeedsComponent"));
    PrioritySelectorComponent = CreateDefaultSubobject<UAgentPrioritySelectorComponent>(TEXT("PrioritySelectorComponent"));
    FallbackHandlerComponent = CreateDefaultSubobject<UAgentFallbackHandlerComponent>(TEXT("FallbackHandlerComponent"));
    LeadershipComponent = CreateDefaultSubobject<UAgentLeadershipComponent>(TEXT("LeadershipComponent"));
    CrimeRoleComponent = CreateDefaultSubobject<UAgentCrimeRoleComponent>(TEXT("CrimeRoleComponent"));
    PedestrianInteractionComponent = CreateDefaultSubobject<UPedestrianInteractionComponent>(TEXT("PedestrianInteractionComponent"));

    BoidCollisionRadius = CreateDefaultSubobject<USphereComponent>(TEXT("Boid Collision Radius"));
    BoidCollisionRadius->SetupAttachment(RootComponent);
    BoidCollisionRadius->SetCollisionObjectType(ECC_Pawn);
    BoidCollisionRadius->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    BoidCollisionRadius->SetCollisionResponseToAllChannels(ECR_Overlap);

    BoidPerceptionRadius = CreateDefaultSubobject<USphereComponent>(TEXT("Boid Sonar Radius"));
    BoidPerceptionRadius->SetupAttachment(RootComponent);
    BoidPerceptionRadius->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    BoidPerceptionRadius->SetCollisionResponseToAllChannels(ECR_Ignore);
    BoidPerceptionRadius->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
    BoidPerceptionRadius->SetSphereRadius(300.0f);

    BoidSpeed = FVector::ZeroVector;

    CentralizedLogFilePath = TEXT("C:/Users/aepbr/Documents/GitHub/B.A.R.S/Logs/LogsCentralized_Logs.txt");

    LastGreetingDay = TEXT("");
}

void ABoidsAB::TryRegisterWithSchedule()
{
    AScheduleManager* SM =
        Cast<AScheduleManager>(
            UGameplayStatics::GetActorOfClass(
                GetWorld(),
                AScheduleManager::StaticClass()
            )
        );

    if (!SM)
        return;

    SM->RegisterBoid(this);

    GetWorld()->GetTimerManager().ClearTimer(ScheduleRetryHandle);
}

void ABoidsAB::BeginPlay()
{
    Super::BeginPlay();

    GetWorld()->GetTimerManager().SetTimer(
        ScheduleRetryHandle,
        this,
        &ABoidsAB::TryRegisterWithSchedule,
        0.2f,
        true
    );

    if (!Controller)
    {
        SpawnDefaultController();
    }

    if (!FlockController)
    {
        TArray<AActor*> Controllers;
        UGameplayStatics::GetAllActorsOfClass(
            GetWorld(),
            AFlockController::StaticClass(),
            Controllers
        );

        if (Controllers.Num() > 0)
        {
            FlockController = Cast<AFlockController>(Controllers[0]);
        }
    }

    ensureMsgf(FlockController, TEXT("FlockController NOT FOUND"));

    UCharacterMovementComponent* MoveComp = GetCharacterMovement();
    SetBaseWalkSpeed(200.f);
    MoveComp->bOrientRotationToMovement = true;
    MoveComp->GravityScale = 1.f;

    bUseControllerRotationYaw = false;

    InitializeLogs();

    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), ABoidsAB::StaticClass(), AllActors);

    TArray<ABoidsAB*> AllBoids;
    for (AActor* A : AllActors)
    {
        if (ABoidsAB* B = Cast<ABoidsAB>(A))
        {
            AllBoids.Add(B);
        }
    }

    InitializeFriends(AllBoids, 2);

    UE_LOG(LogTemp, Log,
        TEXT("[BOID] %s has %d friends"),
        *GetName(),
        Friends.Num()
    );

}

void ABoidsAB::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    TickScheduleAction(DeltaTime);

    if (bStationInteractionPosing)
    {
        TickStationInteractionPose(DeltaTime);
    }

    if (NeedsComponent)
    {
        const bool bFoodValid = !FoodLocation.IsZero() || FoodLocations.Num() > 0;
        const bool bHomeValid = !HomeLocation.IsZero();
        bool bRestingAtHome = false;
        if (bHomeValid && NeedsComponent->IsExhausted() && bIsIdle)
        {
            if (FVector::Dist(GetActorLocation(), HomeLocation) <= NeedsComponent->GetRecoveryRadius())
            {
                bRestingAtHome = true;
            }
        }

        float NeedsDelta = DeltaTime;
        if (USimulationFeatureRegistry* Registry = GetWorld()->GetSubsystem<USimulationFeatureRegistry>())
        {
            if (Registry->GetCurrentScenario() == ESimulationScenario::InfrastructurePlanning)
            {
                NeedsDelta = 0.f;
            }
            else if (Registry->IsValidationMap())
            {
                // Validation reuses the DUL schedule pipeline but has no homes/food.
                // Accurate Time would scale needs ×30 and permanently defer Idle/MoveTo.
                NeedsDelta = 0.f;
            }
            else if (Registry->IsFeatureEnabled(ESimulationFeature::DailyUrbanLife)
                && !Registry->GetDailyUrbanLifeSettings().bEnableNeedsDuringDailyLife)
            {
                NeedsDelta = 0.f;
            }
            else if (Registry->IsAccurateTimeMode())
            {
                // Needs rates are per real second. Scale once by clock speed so they track simulation seconds.
                NeedsDelta *= Registry->GetAccurateTimeSettings().SimulationSpeed;
            }
        }

        if (NeedsDelta > 0.f)
        {
            FVector FoodForNeeds = GetNearestFoodLocation();
            if (IsNearAnyFoodLocation(NeedsComponent->GetRecoveryRadius()))
            {
                FoodForNeeds = GetActorLocation();
            }
            NeedsComponent->AdvanceNeeds(NeedsDelta, GetActorLocation(), FoodForNeeds, HomeLocation, bFoodValid, bHomeValid, bRestingAtHome);
        }
    }

    if (bIsSafeAtExit)
    {
        return;
    }

    if (CurrentBehavior == EBoidBehaviorState::EvacuationSuccess)
    {
        return;
    }

    if (CurrentBehavior == EBoidBehaviorState::Casualty)
    {
        return;
    }

    if (CurrentBehavior == EBoidBehaviorState::MovingToSafeZone)
    {
        MoveAlongWaypoints(DeltaTime);

        if (Waypoints.Num() == 0 && AssignedSafeZone.IsValid() && !IsAtAssignedSafeZone())
        {
            const FVector ToZone = AssignedSafeZone->GetZoneLocation() - GetActorLocation();
            FVector ToZoneFlat = ToZone;
            ToZoneFlat.Z = 0.f;
            if (!ToZoneFlat.IsNearlyZero())
            {
                AddMovementInput(ToZoneFlat.GetSafeNormal(), 1.0f);
            }
        }

        if (IsAtAssignedSafeZone())
        {
            EnterEvacuationSuccessState();
        }
        return;
    }

    if (CurrentBehavior == EBoidBehaviorState::Fleeing)
    {
        MoveAlongWaypoints(DeltaTime);

        if (CurrentExit)
        {
            if (Waypoints.Num() == 0 && !IsAtAssignedQueueSlot() && !AssignedQueueSlotLocation.IsNearlyZero())
            {
                FVector ToSlot = AssignedQueueSlotLocation - GetActorLocation();
                ToSlot.Z = 0.f;
                if (!ToSlot.IsNearlyZero())
                {
                    AddMovementInput(ToSlot.GetSafeNormal(), 1.0f);
                }
            }

            if (IsAtAssignedQueueSlot() && !bHasNotifiedExitReady)
            {
                CurrentBehavior = EBoidBehaviorState::Exiting;
                GetCharacterMovement()->StopMovementImmediately();
                bHasNotifiedExitReady = true;
                CurrentExit->NotifyBoidReady(this);
            }
        }
        else
        {
            // No exit assigned — flee in a direction instead
            if (Waypoints.Num() == 0 && !FleeDirection.IsNearlyZero())
            {
                AddMovementInput(FleeDirection, 1.0f);
            }
        }
        return;
    }
    if (CurrentBehavior == EBoidBehaviorState::Exiting)
    {
        // Exit destroyed mid-flee
        if (!CurrentExit)
        {
            bIsSafeAtExit = true;
            CurrentBehavior = EBoidBehaviorState::Normal;
            StopMovement();
            ClearWaypoints();
            return;
        }

        MoveAlongWaypoints(DeltaTime);

        if (Waypoints.Num() == 0 && !IsAtAssignedQueueSlot() && !AssignedQueueSlotLocation.IsNearlyZero())
        {
            FVector ToSlot = AssignedQueueSlotLocation - GetActorLocation();
            ToSlot.Z = 0.f;
            if (!ToSlot.IsNearlyZero())
            {
                AddMovementInput(ToSlot.GetSafeNormal(), 1.0f);
            }
        }

        if (IsAtAssignedQueueSlot() && !bHasNotifiedExitReady)
        {
            bHasNotifiedExitReady = true;
            GetCharacterMovement()->StopMovementImmediately();
            CurrentExit->NotifyBoidReady(this);
        }
        return;
    }

    // Post-incident lock: wait for ScheduleManager before moving again.
    if (bMovementDisabledAfterIncident)
    {
        return;
    }

    if (PrioritySelectorComponent)
    {
        PrioritySelectorComponent->TickDecisionLayer(DeltaTime, this);
    }

    if (FallbackHandlerComponent)
    {
        FallbackHandlerComponent->TickFallback(DeltaTime, this);
    }

    if (PedestrianInteractionComponent && !IsFastSimulationMode() && !IsAccurateTimeSimulationMode())
    {
        PedestrianInteractionComponent->TickInteractions(DeltaTime, this);
    }

    if (bIsIdle || (PedestrianInteractionComponent && PedestrianInteractionComponent->IsPausedForInteraction()))
        return;

    if (bIsMoving)
    {
        UpdateScheduleAndMove(DeltaTime);
    }
    else if (IsInfrastructurePlanningActive() || IsFastSimulationMode())
    {
        // UC3 / Fast: schedule-only movement between rules / after stations.
        StopMovement();
    }
    else
    {
        SteeringBehaviour(DeltaTime);
        MoveAlongWaypoints(DeltaTime);
    }

    FString CurrentDay = GetCurrentDay();
    if (CurrentDay != LastGreetingDay)
    {
        ResetDailyGreetings();
        ResetDailyAvoidances();
        LastGreetingDay = CurrentDay;
    }
}

#pragma endregion

void ABoidsAB::SetBoidSpeed(FVector NewSpeed)
{
    BoidSpeed = NewSpeed;
}

void ABoidsAB::AddTargetForce(FVector TargetForce)
{
    TargetForceList.Add(TargetForce);
}

#pragma region Movement

FVector ABoidsAB::Separation(TArray<AActor*> Group)
{
    if (FlockController == nullptr) return FVector::ZeroVector;

    FVector SteeringForce = FVector::ZeroVector;
    FVector SeparationForce = FVector::ZeroVector;

    int32 Counter = 0;

    float ProxFactor = 0.0f;

    for (AActor* OverlapActor : Group) {
        ABoidsAB* Mate = Cast<ABoidsAB>(OverlapActor);

        if (Mate != nullptr && Mate != this) {

            if (FVector::DotProduct(this->GetActorForwardVector(), (Mate->GetActorLocation() - this->GetActorLocation()).GetSafeNormal()) <= FlockController->GetSeparationFOV()) continue;

            SeparationForce = this->GetActorLocation() - Mate->GetActorLocation();
            SeparationForce = SeparationForce.GetSafeNormal();

            ProxFactor = 1.0f - (SeparationForce.Size() / this->BoidPerceptionRadius->GetScaledSphereRadius());

            if (ProxFactor < 0.0f) continue;

            SteeringForce += (ProxFactor * SeparationForce);

            Counter++;
        }
    }

    if (Counter > 0) {
        SteeringForce /= Counter;
        SteeringForce.GetSafeNormal() -= this->BoidSpeed.GetSafeNormal();
        SteeringForce *= FlockController->GetSeparationWeight();

        return SteeringForce;
    } else return FVector::ZeroVector;
}

FVector ABoidsAB::Allignment(TArray<AActor*> Group)
{
    if (FlockController == nullptr) return FVector::ZeroVector;

    FVector SteeringForce = FVector::ZeroVector;

    int32 Counter = 0;

    for (AActor* OverlapActor : Group) {
        ABoidsAB* Mate = Cast<ABoidsAB>(OverlapActor);

        if (Mate != nullptr && Mate != this) {

            if (FVector::DotProduct(this->GetActorForwardVector(), (Mate->GetActorLocation() - this->GetActorLocation()).GetSafeNormal()) <= FlockController->GetAlignmentFOV()) continue;

            SteeringForce += Mate->BoidSpeed.GetSafeNormal();

            Counter++;
        }
    }

    if (Counter > 0) {
        SteeringForce /= Counter;
        SteeringForce.GetSafeNormal() -= this->BoidSpeed.GetSafeNormal();
        SteeringForce *= FlockController->GetAlignmentWeight();

        return SteeringForce;
    } else return FVector::ZeroVector;
}

FVector ABoidsAB::Cohesion(TArray<AActor*> Group)
{
    if (FlockController == nullptr) return FVector::ZeroVector;

    FVector SteeringForce = FVector::ZeroVector;
    FVector AveragePosition = FVector::ZeroVector;

    int32 Counter = 0;

    for (AActor* OverlapActor : Group) {
        ABoidsAB* Mate = Cast<ABoidsAB>(OverlapActor);

        if (Mate != nullptr && Mate != this) {

            if (FVector::DotProduct(this->GetActorForwardVector(), (Mate->GetActorLocation() - this->GetActorLocation()).GetSafeNormal()) <= FlockController->GetCohesionFOV()) continue;

            AveragePosition += Mate->GetActorLocation();

            Counter++;
        }
    }

    if (Counter > 0) {
        AveragePosition /= Counter;

        SteeringForce = AveragePosition - this->GetActorLocation();
        SteeringForce.GetSafeNormal() -= this->BoidSpeed.GetSafeNormal();
        SteeringForce *= FlockController->GetCohesionWeight();

        return SteeringForce;
    } else return FVector::ZeroVector;
}

bool ABoidsAB::IsInfrastructurePlanningActive() const
{
    const UWorld* World = GetWorld();
    if (!World)
    {
        return false;
    }

    const USimulationFeatureRegistry* Registry = World->GetSubsystem<USimulationFeatureRegistry>();
    if (!Registry)
    {
        return false;
    }

    return Registry->GetCurrentScenario() == ESimulationScenario::InfrastructurePlanning
        || Registry->IsFeatureEnabled(ESimulationFeature::InfrastructureAnalysis);
}

bool ABoidsAB::IsFastSimulationMode() const
{
    const UWorld* World = GetWorld();
    if (!World)
    {
        return false;
    }
    const USimulationFeatureRegistry* Registry = World->GetSubsystem<USimulationFeatureRegistry>();
    return Registry && Registry->IsFastSimulationMode();
}

bool ABoidsAB::IsAccurateTimeSimulationMode() const
{
    const UWorld* World = GetWorld();
    if (!World)
    {
        return false;
    }
    const USimulationFeatureRegistry* Registry = World->GetSubsystem<USimulationFeatureRegistry>();
    return Registry && Registry->IsAccurateTimeMode();
}

void ABoidsAB::GatherNearbyBoids(TArray<AActor*>& OutMates) const
{
    OutMates.Reset();
    if (BoidPerceptionRadius)
    {
        BoidPerceptionRadius->GetOverlappingActors(OutMates, TSubclassOf<ABoidsAB>());
    }
}

FVector ABoidsAB::ComputeSteeringForces(const TArray<AActor*>& Mates)
{
    FVector Acceleration = FVector::ZeroVector;

    Acceleration += Separation(Mates);
    Acceleration += Allignment(Mates);
    Acceleration += Cohesion(Mates);
    Acceleration += AvoidEnemies(Mates);

    if (IsObstacleInFront())
    {
        Acceleration += AvoidObstacle();
    }

    for (const FVector& TargetForce : TargetForceList)
    {
        Acceleration += TargetForce;
    }
    TargetForceList.Empty();

    return Acceleration;
}

FVector ABoidsAB::ResolveProgressDirection() const
{
    FVector ToTarget = FVector::ZeroVector;

    if (Waypoints.Num() > 0 && CurrentWaypointIndex < Waypoints.Num())
    {
        ToTarget = Waypoints[CurrentWaypointIndex] - GetActorLocation();
    }
    else if (bIsMoving && !TargetL.IsNearlyZero())
    {
        ToTarget = TargetL - GetActorLocation();
    }

    ToTarget.Z = 0.f;
    if (ToTarget.IsNearlyZero())
    {
        return FVector::ZeroVector;
    }
    return ToTarget.GetSafeNormal();
}

bool ABoidsAB::ShouldBlendSteeringWhileMoving() const
{
    if (IsFastSimulationMode())
    {
        return false;
    }
    if (!FlockController || !FlockController->IsSteeringWhileMovingEnabled())
    {
        return false;
    }
    if (!bIsMoving || bIsIdle || bMovementDisabledAfterIncident)
    {
        return false;
    }
    if (IsInteractingWithStation() || bIsQueuedAtStation)
    {
        return false;
    }
    if (CurrentBehavior == EBoidBehaviorState::Fleeing
        || CurrentBehavior == EBoidBehaviorState::Exiting
        || CurrentBehavior == EBoidBehaviorState::MovingToSafeZone
        || CurrentBehavior == EBoidBehaviorState::EvacuationSuccess
        || CurrentBehavior == EBoidBehaviorState::Casualty)
    {
        return false;
    }
    return true;
}

void ABoidsAB::BlendMovement(const FVector& ProgressDirection, const FVector& SteeringForce,
                             FVector& OutDirection, float& OutScale) const
{
    OutDirection = FVector::ZeroVector;
    OutScale = 1.f;

    FVector Progress = ProgressDirection;
    Progress.Z = 0.f;
    if (Progress.IsNearlyZero())
    {
        OutDirection = ProgressDirection;
        return;
    }
    Progress.Normalize();

    if (!FlockController)
    {
        OutDirection = Progress;
        return;
    }

    // Path progress is the forward basis; flock is lateral-only so agents stay in corridor.
    FVector Lateral = FVector::ZeroVector;
    FVector Soft = SteeringForce;
    Soft.Z = 0.f;
    if (!Soft.IsNearlyZero())
    {
        Soft = Soft.GetSafeNormal() * FlockController->GetMovingSteeringWeight();
        Lateral = Soft - Progress * FVector::DotProduct(Soft, Progress);

        const float MaxRadians = FMath::DegreesToRadians(
            FMath::Clamp(FlockController->GetMaxSteerDeviationDegrees(), 1.f, 35.f));
        const float MaxLateral = FMath::Tan(MaxRadians);
        const float LateralSize = Lateral.Size();
        if (LateralSize > MaxLateral && LateralSize > KINDA_SMALL_NUMBER)
        {
            Lateral *= MaxLateral / LateralSize;
        }
    }

    FVector Blended = Progress + Lateral;
    Blended.Z = 0.f;
    if (Blended.IsNearlyZero())
    {
        OutDirection = Progress;
        return;
    }

    OutDirection = Blended.GetSafeNormal();
    OutScale = 1.f;
}

void ABoidsAB::TryStayOnNavMesh()
{
    if (!FlockController || !GetWorld())
    {
        return;
    }

    const float Now = GetWorld()->GetTimeSeconds();
    if ((Now - LastNavSnapTime) < FlockController->GetNavMeshSnapCooldownSeconds())
    {
        return;
    }

    UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if (!NavSys)
    {
        return;
    }

    const FVector AgentLoc = GetActorLocation();
    FNavLocation Projected;
    const FVector Extent(200.f, 200.f, 200.f);
    if (!NavSys->ProjectPointToNavigation(AgentLoc, Projected, Extent))
    {
        return;
    }

    const float DistToNav = FVector::Dist2D(AgentLoc, Projected.Location);
    if (DistToNav <= FlockController->GetNavMeshSnapDistance())
    {
        return;
    }

    LastNavSnapTime = Now;
    FVector Fixed = Projected.Location;
    Fixed.Z = AgentLoc.Z;
    SetActorLocation(Fixed, false, nullptr, ETeleportType::TeleportPhysics);
}

FVector ABoidsAB::ComputePathSegmentProgress(const FVector& SegmentStart, const FVector& SegmentEnd) const
{
    const FVector AgentLoc = GetActorLocation();
    const FVector P0 = SegmentStart;
    const FVector P1 = SegmentEnd;

    // Horizontal look-ahead only. 3D ClosestPointOnSegment on steep stairs can snap
    // toward the high endpoint and reverse a descent.
    FVector P0Flat = P0;
    FVector P1Flat = P1;
    FVector AgentFlat = AgentLoc;
    P0Flat.Z = 0.f;
    P1Flat.Z = 0.f;
    AgentFlat.Z = 0.f;

    FVector SegFlat = P1Flat - P0Flat;
    if (SegFlat.IsNearlyZero())
    {
        FVector ToWp = P1 - AgentLoc;
        ToWp.Z = 0.f;
        if (ToWp.IsNearlyZero())
        {
            return GetActorForwardVector().GetSafeNormal2D();
        }
        return ToWp.GetSafeNormal();
    }

    const FVector SegDirFlat = SegFlat.GetSafeNormal();
    const FVector ClosestFlat = FMath::ClosestPointOnSegment(AgentFlat, P0Flat, P1Flat);

    const float LookAhead = FlockController ? FlockController->GetPathLookAheadDistance() : 140.f;
    FVector AheadFlat = ClosestFlat + SegDirFlat * LookAhead;
    if (FVector::DotProduct(AheadFlat - P1Flat, SegDirFlat) > 0.f)
    {
        AheadFlat = P1Flat;
    }

    FVector Progress = AheadFlat - AgentFlat;
    Progress.Z = 0.f;
    if (Progress.IsNearlyZero())
    {
        Progress = SegDirFlat;
    }
    else
    {
        Progress.Normalize();
        // Reject look-ahead that points against the segment (stair U-turns).
        if (FVector::DotProduct(Progress, SegDirFlat) < 0.f)
        {
            Progress = SegDirFlat;
        }
    }
    return Progress;
}

FVector ABoidsAB::RefineDirectionAroundWalls(const FVector& DesiredDir, const FVector& ProgressDir) const
{
    FVector Dir = DesiredDir;
    Dir.Z = 0.f;
    if (Dir.IsNearlyZero() || !GetWorld())
    {
        return ProgressDir.GetSafeNormal2D();
    }
    Dir.Normalize();

    FVector Progress = ProgressDir;
    Progress.Z = 0.f;
    if (!Progress.IsNearlyZero())
    {
        Progress.Normalize();
    }
    else
    {
        Progress = Dir;
    }

    const float ProbeDist = FlockController ? FlockController->GetWallProbeDistance() : 110.f;
    const FVector AgentLoc = GetActorLocation();

    auto TraceWall = [this, &AgentLoc](const FVector& TraceDir, float Dist, FHitResult& OutHit) -> bool
    {
        FCollisionQueryParams Params(SCENE_QUERY_STAT(BoidWallProbe), false, this);
        const FVector End = AgentLoc + TraceDir.GetSafeNormal() * Dist;
        // Avoidance channel first; WorldStatic fallback for untagged level geometry.
        if (GetWorld()->LineTraceSingleByChannel(OutHit, AgentLoc, End, COLLISION_AVOIDANCE, Params)
            && OutHit.bBlockingHit)
        {
            return true;
        }
        return GetWorld()->LineTraceSingleByChannel(OutHit, AgentLoc, End, ECC_WorldStatic, Params)
            && OutHit.bBlockingHit;
    };

    // Drop lateral flock that pushes into a nearby side wall.
    const FVector Right(-Progress.Y, Progress.X, 0.f);
    const float Clearance = FlockController ? FlockController->GetWallClearanceDistance() : 95.f;
    const float SideProbe = FMath::Max(ProbeDist * 0.75f, Clearance);
    FHitResult LeftHit, RightHit;
    const bool bLeftWall = TraceWall(-Right, SideProbe, LeftHit);
    const bool bRightWall = TraceWall(Right, SideProbe, RightHit);
    const float Side = FVector::DotProduct(Dir, Right);
    if (bLeftWall && Side < 0.f)
    {
        Dir -= Right * Side;
    }
    if (bRightWall && Side > 0.f)
    {
        Dir -= Right * Side;
    }
    Dir.Z = 0.f;
    if (Dir.IsNearlyZero())
    {
        Dir = Progress;
    }
    else
    {
        Dir.Normalize();
    }

    // Soft side clearance at T-junctions / corridor corners (net L/R; does not replace path).
    {
        float LeftPush = 0.f;
        float RightPush = 0.f;
        if (bLeftWall && Clearance > KINDA_SMALL_NUMBER)
        {
            LeftPush = FMath::Clamp(1.f - (LeftHit.Distance / Clearance), 0.f, 1.f);
        }
        if (bRightWall && Clearance > KINDA_SMALL_NUMBER)
        {
            RightPush = FMath::Clamp(1.f - (RightHit.Distance / Clearance), 0.f, 1.f);
        }
        const float ClearNet = (LeftPush - RightPush)
            * (FlockController ? FlockController->GetWallClearanceStrength() : 0.45f);
        if (FMath::Abs(ClearNet) > KINDA_SMALL_NUMBER)
        {
            Dir = (Dir + Right * ClearNet).GetSafeNormal();

            // Clamp clearance to the path deviation cone.
            const float MaxDeg = FlockController
                ? FMath::Clamp(FlockController->GetMaxSteerDeviationDegrees(), 1.f, 35.f)
                : 18.f;
            const float MaxRad = FMath::DegreesToRadians(MaxDeg);
            const float Dot = FMath::Clamp(FVector::DotProduct(Dir, Progress), -1.f, 1.f);
            if (FMath::Acos(Dot) > MaxRad)
            {
                FVector Lat = Dir - Progress * Dot;
                Lat.Z = 0.f;
                if (!Lat.IsNearlyZero())
                {
                    Dir = (Progress + Lat.GetSafeNormal() * FMath::Tan(MaxRad)).GetSafeNormal();
                }
                else
                {
                    Dir = Progress;
                }
            }
        }
    }

    // Forward wall hit: slide along the surface while favoring Progress.
    FHitResult FwdHit;
    if (TraceWall(Dir, ProbeDist, FwdHit) && FwdHit.bBlockingHit)
    {
        FVector N = FwdHit.ImpactNormal;
        N.Z = 0.f;
        if (!N.IsNearlyZero())
        {
            N.Normalize();
            FVector Slid = Dir - N * FVector::DotProduct(Dir, N);
            Slid.Z = 0.f;
            if (Slid.IsNearlyZero())
            {
                // Head-on into the wall — choose the tangent that still advances Progress.
                Slid = FVector(-N.Y, N.X, 0.f);
                if (FVector::DotProduct(Slid, Progress) < 0.f)
                {
                    Slid *= -1.f;
                }
            }
            Dir = Slid.GetSafeNormal();
        }
    }

    return Dir;
}

void ABoidsAB::ApplyMovement(const FVector& Direction, float Scale, bool bUpdateFacing)
{
    FVector Dir = Direction;
    Dir.Z = 0.f;
    if (Dir.IsNearlyZero())
    {
        return;
    }
    Dir.Normalize();

    AddMovementInput(Dir, Scale);

    if (bUpdateFacing)
    {
        SetActorRotation(Dir.ToOrientationQuat());
    }
}

void ABoidsAB::SteeringBehaviour(float DeltaTime)
{
    if (!FlockController) return;

    if (IsInfrastructurePlanningActive() || IsInteractingWithStation() || bIsQueuedAtStation)
    {
        TargetForceList.Empty();
        return;
    }

    TArray<AActor*> Mates;
    GatherNearbyBoids(Mates);

    TrySayHello(Mates);

    if (bIsMoving || bIsIdle || bMovementDisabledAfterIncident)
    {
        TargetForceList.Empty();
        return;
    }

    const FVector Acceleration = ComputeSteeringForces(Mates);

    float SpeedMultiplier = (CurrentBehavior == EBoidBehaviorState::Fleeing) ? 1.5f : 1.0f;
    BoidSpeed += Acceleration * DeltaTime * SpeedMultiplier;
    BoidSpeed = BoidSpeed.GetClampedToSize(
        FlockController->GetMinSpeed(),
        FlockController->GetMaxSpeed()
    );

    FVector HorizontalVelocity = BoidSpeed;
    HorizontalVelocity.Z = 0.f;

    const FVector MoveDir = HorizontalVelocity.GetSafeNormal();
    const float MoveScale = HorizontalVelocity.Size() / FlockController->GetMaxSpeed();

    ApplyMovement(MoveDir, MoveScale, true);
}

FVector ABoidsAB::AvoidEnemies(TArray<AActor*> Group)
{
    FVector SteeringForce = FVector::ZeroVector;
    int32 Counter = 0;

    for (AActor* OverlapActor : Group) {
        ABoidsAB* Mate = Cast<ABoidsAB>(OverlapActor);
        if (Mate != nullptr && Enemies.Contains(Mate)) {
            FVector AvoidanceForce = this->GetActorLocation() - Mate->GetActorLocation();
            AvoidanceForce = AvoidanceForce.GetSafeNormal();

            SteeringForce += AvoidanceForce;
            Counter++;

            if (!DailyAvoidances.FindOrAdd(this).Contains(Mate)) {
                LogEnemyAvoidance(Mate);
                DailyAvoidances.FindOrAdd(this).Add(Mate);
            }
        }
    }

    if (Counter > 0) {
        SteeringForce /= Counter;
        SteeringForce.GetSafeNormal() -= this->BoidSpeed.GetSafeNormal();
        SteeringForce *= FlockController->GetSeparationWeight();
    }

    return SteeringForce;
}

void ABoidsAB::TrySayHello(const TArray<AActor*>& NearbyBoids)
{
    if (CurrentBehavior == EBoidBehaviorState::Fleeing ||
        CurrentBehavior == EBoidBehaviorState::Exiting)
    {
        return;
    }

    USimulationFeatureRegistry* Registry = GetWorld()->GetSubsystem<USimulationFeatureRegistry>();
    if (IsInfrastructurePlanningActive())
    {
        return;
    }

    const bool bIsDailyLife = Registry && Registry->IsFeatureEnabled(ESimulationFeature::DailyUrbanLife);

    if (bIsDailyLife)
    {
        if (bIsIdle || bIsMoving || IsInteractingWithStation() || bIsQueuedAtStation)
        {
            return;
        }

        if (NeedsComponent && NeedsComponent->IsHungry())
        {
            return;
        }

        if (IsNearAnyFoodLocation(450.f))
        {
            return;
        }
    }

    const float Now = GetWorld()->GetTimeSeconds();
    constexpr float HelloDistanceSq = FMath::Square(400.f);
    constexpr float HelloCooldown = 12.f;
    constexpr float HelloGlobalCooldown = 4.f;

    if (bIsDailyLife && (Now - LastHelloWorldTime) < HelloGlobalCooldown)
    {
        return;
    }

    for (AActor* A : NearbyBoids)
    {
        ABoidsAB* Friend = Cast<ABoidsAB>(A);
        if (!Friend || Friend == this)
            continue;

        if (bIsDailyLife)
        {
            if (Friend->IsIdle() || Friend->IsMoving()
                || Friend->IsInteractingWithStation() || Friend->IsWaitingAtStation())
            {
                continue;
            }

            if (Friend->GetNeedsComponent() && Friend->GetNeedsComponent()->IsHungry())
            {
                continue;
            }

            if (Friend->IsNearAnyFoodLocation(450.f))
            {
                continue;
            }
        }

        const float DistSq = FVector::DistSquared(GetActorLocation(), Friend->GetActorLocation());

        if (DistSq < HelloDistanceSq)
        {
            float* LastTimePtr = LastHelloTime.Find(Friend);
            if (LastTimePtr)
            {
                if (Now - *LastTimePtr < HelloCooldown)
                    continue;

                *LastTimePtr = Now;
            }
            else
            {
                LastHelloTime.Add(Friend, Now);
            }

            OnHelloWave();
            LogFriendInteraction(Friend);
            LastHelloWorldTime = Now;

            UE_LOG(LogTemp, Verbose,
                TEXT("[HELLO] %s waved at %s"),
                *GetName(),
                *Friend->GetName()
            );

            break;
        }
    }
}


#pragma endregion

#pragma region Collision

void ABoidsAB::SetCollisionResponses(bool bEnable)
{
    BoidPerceptionRadius->SetCollisionEnabled(
        bEnable ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
}

bool ABoidsAB::IsObstacleInFront()
{
    if (!FlockController || FlockController->GetSonarDir().Num() == 0)
    {
        return false;
    }

    const FQuat SensorRotation = FQuat::FindBetweenVectors(
        FlockController->GetSonarDir()[0],
        GetActorForwardVector());
    const FVector NewSensorDirection = SensorRotation.RotateVector(FlockController->GetSonarDir()[0]);

    FHitResult Hit;
    FCollisionQueryParams TraceParameters;
    GetWorld()->LineTraceSingleByChannel(
        Hit,
        GetActorLocation(),
        GetActorLocation() + NewSensorDirection * FlockController->GetSonarRange(),
        COLLISION_AVOIDANCE,
        TraceParameters);

    return Hit.bBlockingHit;
}

FVector ABoidsAB::AvoidObstacle()
{
    if (FlockController == nullptr) return FVector::ZeroVector;

    FVector SteeringForce = FVector::ZeroVector;
    FVector NewSonarDir = FVector::ZeroVector;

    FQuat SonarRotation = FQuat::FindBetweenVectors(FlockController->GetSonarDir()[0], this->GetActorForwardVector());

    FCollisionQueryParams CollisionParams;

    FHitResult Hit;

    for (FVector SonarDir : FlockController->GetSonarDir()) {
        NewSonarDir = SonarRotation.RotateVector(SonarDir);

        GetWorld()->LineTraceSingleByChannel(Hit, this->GetActorLocation(), this->GetActorLocation() + NewSonarDir * FlockController->GetSonarRange(), COLLISION_AVOIDANCE, CollisionParams);

        if (Hit.bBlockingHit && DEBUG) {
        } else {
            SteeringForce = NewSonarDir.GetSafeNormal() - this->BoidSpeed.GetSafeNormal();
            SteeringForce *= FlockController->GetAvoidanceWeight();

            return SteeringForce;
        }
    }

    return FVector::ZeroVector;
}

#pragma endregion

#pragma region Pathfinding

void ABoidsAB::MoveAlongWaypoints(float DeltaTime)
{
    if (Waypoints.Num() == 0 || CurrentWaypointIndex >= Waypoints.Num())
    {
        if (bIsMoving)
        {
            if (GStationNavDebug && TargetStation && GetWorld())
            {
                const float Now = GetWorld()->GetTimeSeconds();
                if ((Now - LastStationNavStopLogTime) >= GStationNavStopLogCooldown)
                {
                    LastStationNavStopLogTime = Now;
                    const FVector AgentLoc = GetActorLocation();
                    const FVector StationLoc = TargetStation->GetActorLocation();
                    UE_LOG(LogTemp, Warning,
                        TEXT("[StationNavDebug] %s MoveAlongWaypoints → arrival check | WP=%d Idx=%d | AgentZ=%.1f StationZ=%.1f ΔZ=%.1f | reason_candidate=%s"),
                        *GetName(),
                        Waypoints.Num(),
                        CurrentWaypointIndex,
                        AgentLoc.Z,
                        StationLoc.Z,
                        FMath::Abs(AgentLoc.Z - StationLoc.Z),
                        Waypoints.Num() == 0 ? TEXT("path_empty_or_failed") : TEXT("reached_last_waypoint"));
                }
            }
            HandleScheduleMovementArrival();
        }
        return;
    }

    FVector CurrentTarget = Waypoints[CurrentWaypointIndex];
    const FVector AgentLoc = GetActorLocation();
    // Path points are floor samples; compare capsule feet Z, not actor center.
    float AgentFeetZ = AgentLoc.Z;
    if (const UCapsuleComponent* Cap = GetCapsuleComponent())
    {
        AgentFeetZ -= Cap->GetScaledCapsuleHalfHeight();
    }

    const float DistXY = FVector::Dist2D(AgentLoc, CurrentTarget);
    const float DistZ = FMath::Abs(AgentFeetZ - CurrentTarget.Z);

    // Advance only when feet are near WP height (avoids skipping elevated stair samples in XY).
    constexpr float WaypointArriveXY = 100.f;
    constexpr float WaypointArriveZ = 28.f;
    if (DistXY < WaypointArriveXY && DistZ < WaypointArriveZ)
    {
        CurrentWaypointIndex++;
        if (CurrentWaypointIndex >= Waypoints.Num())
        {
            if (GStationNavDebug && TargetStation)
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("[StationNavDebug] %s reached LAST waypoint index (WP count=%d) → HandleScheduleMovementArrival | AgentZ=%.1f FeetZ=%.1f LastWP.Z=%.1f"),
                    *GetName(), Waypoints.Num(), AgentLoc.Z, AgentFeetZ, CurrentTarget.Z);
            }
            HandleScheduleMovementArrival();
        }
        return;
    }

    const FVector SegmentStart = (CurrentWaypointIndex > 0)
        ? Waypoints[CurrentWaypointIndex - 1]
        : GetActorLocation();
    FVector Progress = ComputePathSegmentProgress(SegmentStart, CurrentTarget);

    // Same XY as WP but wrong floor — keep segment-forward motion onto the ramp.
    if (DistXY < WaypointArriveXY && DistZ >= WaypointArriveZ)
    {
        FVector SegForward = CurrentTarget - SegmentStart;
        SegForward.Z = 0.f;
        if (SegForward.IsNearlyZero() && CurrentWaypointIndex + 1 < Waypoints.Num())
        {
            SegForward = Waypoints[CurrentWaypointIndex + 1] - CurrentTarget;
            SegForward.Z = 0.f;
        }
        if (!SegForward.IsNearlyZero())
        {
            Progress = SegForward.GetSafeNormal();
        }
    }

    if (ShouldBlendSteeringWhileMoving())
    {
        TryStayOnNavMesh();

        TArray<AActor*> Mates;
        GatherNearbyBoids(Mates);
        const FVector Steering = ComputeSteeringForces(Mates);

        FVector FinalDir;
        float FinalScale = 1.f;
        BlendMovement(Progress, Steering, FinalDir, FinalScale);
        FinalDir = RefineDirectionAroundWalls(FinalDir, Progress);

        // CMC owns facing (bOrientRotationToMovement); snaps caused heading jitter in flock FOV.
        ApplyMovement(FinalDir, FinalScale, false);

        if (FlockController && !FinalDir.IsNearlyZero())
        {
            const FVector Desired = FinalDir * FlockController->GetMaxSpeed();
            BoidSpeed = FMath::VInterpTo(BoidSpeed, Desired, DeltaTime, 8.f);
            BoidSpeed.Z = 0.f;
        }
        return;
    }

    TryStayOnNavMesh();
    if (IsFastSimulationMode())
    {
        ApplyMovement(Progress, 1.f, false);
    }
    else
    {
        ApplyMovement(RefineDirectionAroundWalls(Progress, Progress), 1.f, false);
    }
}

void ABoidsAB::ComputePath(const FVector& TargetLocation)
{
    UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if (!NavSys)
    {
        if (GStationNavDebug && TargetStation)
        {
            UE_LOG(LogTemp, Error,
                TEXT("[StationNavDebug] %s ComputePath FAILED — no NavSys | TargetL=%s"),
                *GetName(), *TargetLocation.ToString());
        }
        if (FallbackHandlerComponent)
        {
            FallbackHandlerComponent->OnPathResult(this, false, TargetLocation);
        }
        return;
    }

    const FVector AgentStart = GetActorLocation();

    // Entrance-route demo: log gate availability for each new A* query (SidewalkOnly).
    {
        TArray<AActor*> GateActors;
        UGameplayStatics::GetAllActorsOfClass(GetWorld(), AMallEntranceGate::StaticClass(), GateActors);
        for (AActor* GateActor : GateActors)
        {
            if (const AMallEntranceGate* Gate = Cast<AMallEntranceGate>(GateActor))
            {
                UE_LOG(LogTemp, Log,
                    TEXT("[Path] %s path requested | %s %s"),
                    *AgentId.ToString(),
                    *Gate->GetGateDebugLabel(),
                    Gate->IsOpen() ? TEXT("available") : TEXT("unavailable"));
            }
        }
    }

    UNavigationPath* NavPath = NavSys->FindPathToLocationSynchronously(GetWorld(), AgentStart, TargetLocation, nullptr,
        UNavFilter_SidewalkOnly::StaticClass());

    if (!NavPath || NavPath->PathPoints.Num() == 0)
    {
        if (GStationNavDebug && TargetStation)
        {
            UE_LOG(LogTemp, Error,
                TEXT("[StationNavDebug] %s ComputePath PATH FAILED (empty) | Start=%s TargetL=%s | stop_reason=path_failed"),
                *GetName(), *AgentStart.ToString(), *TargetLocation.ToString());
        }

        const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
        const bool bShouldLog = !GetWorld()
            || (Now - PathFailLogThrottleTime) >= 8.f
            || FVector::DistSquared(TargetLocation, PathFailLogThrottleTarget) > FMath::Square(100.f);

        if (bShouldLog)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[Boid %s] No sidewalk-only path to %s%s"),
                *GetName(),
                *TargetLocation.ToString(),
                IsFleeing() ? TEXT(" (evacuation)") : TEXT(""));

            PathFailLogThrottleTime = Now;
            PathFailLogThrottleTarget = TargetLocation;
        }

        if (FallbackHandlerComponent)
        {
            FallbackHandlerComponent->OnPathResult(this, false, TargetLocation);
        }
        return;
    }

    if (NavPath && NavPath->PathPoints.Num() > 0)
    {
        Waypoints.Empty();
        Waypoints = NavPath->PathPoints;

        // Prove corridor use: any path point inside an OPEN gate volume.
        {
            TArray<AActor*> GateActors;
            UGameplayStatics::GetAllActorsOfClass(GetWorld(), AMallEntranceGate::StaticClass(), GateActors);
            for (AActor* GateActor : GateActors)
            {
                const AMallEntranceGate* Gate = Cast<AMallEntranceGate>(GateActor);
                if (!Gate)
                {
                    continue;
                }
                const FBox GateBox = Gate->GetGateBounds().ExpandBy(50.f);
                bool bPathUsesGate = false;
                for (const FVector& WP : Waypoints)
                {
                    if (GateBox.IsInside(WP))
                    {
                        bPathUsesGate = true;
                        break;
                    }
                }
                if (bPathUsesGate)
                {
                    UE_LOG(LogTemp, Log,
                        TEXT("[Path] %s path USES %s corridor (open=%s, waypoints=%d)"),
                        *AgentId.ToString(),
                        *Gate->GetGateDebugLabel(),
                        Gate->IsOpen() ? TEXT("true") : TEXT("false"),
                        Waypoints.Num());
                }
            }
        }

        if (GStationNavDebug && TargetStation)
        {
            const bool bPartial = NavPath->IsPartial();
            const FVector LastWP = Waypoints.Last();
            const FVector StationLoc = TargetStation->GetActorLocation();
            UE_LOG(LogTemp, Warning,
                TEXT("[StationNavDebug] %s ComputePath OK | IsPartial=%s | WPCount=%d | Start=%s | TargetL=%s"),
                *GetName(),
                bPartial ? TEXT("TRUE") : TEXT("FALSE"),
                Waypoints.Num(),
                *AgentStart.ToString(),
                *TargetLocation.ToString());

            for (int32 i = 0; i < Waypoints.Num(); ++i)
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("[StationNavDebug] %s   WP[%d]=%s"),
                    *GetName(), i, *Waypoints[i].ToString());
            }

            UE_LOG(LogTemp, Warning,
                TEXT("[StationNavDebug] %s LAST_WP=%s | Station=%s | ΔZ(lastWP-station)=%.1f | Dist3D(lastWP,station)=%.1f | Dist2D=%.1f"),
                *GetName(),
                *LastWP.ToString(),
                *StationLoc.ToString(),
                LastWP.Z - StationLoc.Z,
                FVector::Dist(LastWP, StationLoc),
                FVector::Dist2D(LastWP, StationLoc));
        }

        if (IsFleeing())
        {
            EvacuationPathFailCount = 0;
        }

        if (ShouldLogPathFollowing())
        {
            WriteToLogFile(FString::Printf(TEXT("Boid %s is following a path with %d waypoints."), *GetName(), Waypoints.Num()));
        }

        ActiveNeedNavIntent = ENeedNavIntent::None;

        if (FallbackHandlerComponent)
        {
            FallbackHandlerComponent->OnPathResult(this, true, TargetLocation);
        }
    }
}

bool ABoidsAB::ShouldLogPathFollowing() const
{
    return !IsFastSimulationMode();
}

#pragma endregion

#pragma region GoTo

void ABoidsAB::MoveTowardsTarget(float DeltaTime)
{
    if (!bIsMoving || bIsIdle)
        return;

    FVector Current = GetActorLocation();
    FVector Direction = (TargetL - Current);
    Direction.Z = 0.f;

    float Distance = Direction.Size();

    if (Distance < 80.f)
    {
        // Station gap-close: HandleScheduleMovementArrival owns OnStationReached (XY+Z).
        if (TargetStation)
        {
            return;
        }

        StopMovement();
        return;
    }

    Direction.Normalize();

    if (ShouldBlendSteeringWhileMoving())
    {
        TryStayOnNavMesh();

        TArray<AActor*> Mates;
        GatherNearbyBoids(Mates);
        const FVector Steering = ComputeSteeringForces(Mates);

        FVector FinalDir;
        float FinalScale = 1.f;
        BlendMovement(Direction, Steering, FinalDir, FinalScale);
        FinalDir = RefineDirectionAroundWalls(FinalDir, Direction);
        ApplyMovement(FinalDir, FinalScale, false);

        if (FlockController && !FinalDir.IsNearlyZero())
        {
            const FVector Desired = FinalDir * FlockController->GetMaxSpeed();
            BoidSpeed = FMath::VInterpTo(BoidSpeed, Desired, DeltaTime, 8.f);
            BoidSpeed.Z = 0.f;
        }
        return;
    }

    TryStayOnNavMesh();
    if (IsFastSimulationMode())
    {
        ApplyMovement(Direction, 1.0f, false);
    }
    else
    {
        ApplyMovement(RefineDirectionAroundWalls(Direction, Direction), 1.0f, false);
    }
}

#pragma endregion

#pragma region Schedule

FVector ABoidsAB::PickRandomLocation(const TArray<FVector>& Locations) const
{
    if (Locations.Num() > 0)
    {
        int32 RandomIndex = FMath::RandRange(0, Locations.Num() - 1);
        return Locations[RandomIndex];
    }
    return FVector::ZeroVector;
}

bool ABoidsAB::IsWithinActivePeriod(const FString& Period, const FString& CurrentTime)
{
    FString StartTime = Period.Left(5);
    FString EndTime = Period.Right(5);

    return CurrentTime >= StartTime && CurrentTime <= EndTime;
}

void ABoidsAB::UpdateScheduleAndMove(float DeltaTime)
{
    if (!bIsMoving || bIsIdle)
        return;

    if (Waypoints.Num() > 0)
    {
        MoveAlongWaypoints(DeltaTime);
        return;
    }

    MoveTowardsTarget(DeltaTime);
}

#pragma endregion


void ABoidsAB::OnScheduleTrigger(const FString& TriggerName)
{
    UE_LOG(LogTemp, Log,
        TEXT("[Boid %s] Schedule trigger received: %s"),
        *GetName(),
        *TriggerName
    );

    if (TriggerName.Equals(TEXT("Flee"), ESearchCase::IgnoreCase))
    {
        bHasIncidentLocation = false;
        LastIncidentLocation = FVector::ZeroVector;
        StartFlee();
        return;
    }

    if (TriggerName.Equals(TEXT("Break"), ESearchCase::IgnoreCase)
        || TriggerName.Equals(TEXT("Pause"), ESearchCase::IgnoreCase))
    {
        LeaveCurrentStation();
        SetIdle(false);

        if (USimulationFeatureRegistry* Registry = GetWorld()->GetSubsystem<USimulationFeatureRegistry>())
        {
            const FDailyUrbanLifeSettings& Settings = Registry->GetDailyUrbanLifeSettings();
            float Pause = FMath::FRandRange(Settings.InteractionPauseMinSeconds, Settings.InteractionPauseMaxSeconds);
            bool bStartedBreakCommute = false;

            if (TriggerName.Equals(TEXT("Break"), ESearchCase::IgnoreCase)
                && Registry->IsFeatureEnabled(ESimulationFeature::DailyUrbanLife)
                && !Settings.BreakAreaTag.IsEmpty())
            {
                TArray<AActor*> BreakActors;
                UGameplayStatics::GetAllActorsWithTag(GetWorld(), FName(*Settings.BreakAreaTag), BreakActors);
                if (BreakActors.Num() > 0)
                {
                    // Distribute across Plaza actors by AgentId — never pile everyone on [0].
                    int32 AgentIndex = 0;
                    const FString Id = AgentId.ToString();
                    if (Id.StartsWith(TEXT("Agent_")))
                    {
                        AgentIndex = FMath::Max(0, FCString::Atoi(*Id.Mid(6)));
                    }
                    const int32 Pick = AgentIndex % BreakActors.Num();
                    AActor* BreakDest = BreakActors[Pick] ? BreakActors[Pick] : BreakActors[0];
                    NotifyScheduleMoveTo(BreakDest->GetActorLocation(), Settings.BreakAreaTag);
                    bStartedBreakCommute = true;
                    UE_LOG(LogTemp, Log,
                        TEXT("[Boid %s] Break — leaving work, moving toward '%s' (slot %d/%d)"),
                        *GetName(),
                        *Settings.BreakAreaTag,
                        Pick,
                        BreakActors.Num());
                }
            }

            if (!bStartedBreakCommute)
            {
                if (PedestrianInteractionComponent)
                {
                    const float PauseWall = Registry->IsAccurateTimeMode()
                        ? Registry->ScaleAuthoredDurationToRealSeconds(Pause)
                        : Pause;
                    PedestrianInteractionComponent->BeginBriefPause(PauseWall);
                }
                else
                {
                    SetIdleTimed(true, Pause);
                }
            }
        }
        return;
    }

    if (TriggerName.Equals(TEXT("Greet"), ESearchCase::IgnoreCase))
    {
        OnHelloWave();
        if (USimulationAnalytics* Analytics = GetWorld()->GetSubsystem<USimulationAnalytics>())
        {
            Analytics->RecordDailyInteraction(this, nullptr, TEXT("ScheduleGreet"));
        }
    }
}

void ABoidsAB::OnIncident(EIncidentType Type, FVector IncidentLocation)
{
    const bool bWasSafeAtExit = bIsSafeAtExit;

    // New incident overrides safe-at-exit
    if (bIsSafeAtExit)
    {
        bIsSafeAtExit = false;
        bHasNotifiedExitReady = false;
        AssignedQueueSlotLocation = FVector::ZeroVector;
        if (CurrentExit)
        {
            CurrentExit->LeaveQueue(this);
            CurrentExit = nullptr;
        }
        ExitQueueIndex = INDEX_NONE;
    }

    LastIncidentLocation = IncidentLocation;
    bHasIncidentLocation = true;

    // Hybrid evacuation can notify twice; ignore if already evacuating.
    if (!bWasSafeAtExit && (IsFleeing() || bAwaitingReactionDelay))
    {
        UE_LOG(LogTemp, Log,
            TEXT("[Boid %s] Already evacuating — incident location updated, skipping duplicate flee"),
            *GetName());
        return;
    }

    UE_LOG(LogTemp, Log,
        TEXT("[Boid %s] Received incident %d"),
        *GetName(),
        static_cast<int32>(Type)
    );

    switch (Type)
    {
    case EIncidentType::Accident:
    case EIncidentType::Fire:
    case EIncidentType::Explosion:
        if (ShouldUseEvacuationRouting())
        {
            InitializeEvacuationPersonality();
            if (ReactionDelaySeconds > KINDA_SMALL_NUMBER)
            {
                ScheduleReactionDelay();
            }
            else
            {
                StartFlee();
            }
        }
        else
        {
            StartFlee();
        }
        break;

    default:
        break;
    }
}

void ABoidsAB::OnIncidentEnded()
{
    UE_LOG(LogTemp, Log,
        TEXT("[Boid %s] Incident ended - Entering idle until schedule resumes movement"),
        *GetName()
    );

    const bool bPreserveTerminalEvacState = IsCasualty() || IsInEvacuationSuccessState();

    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(FleeRetryHandle);
        GetWorld()->GetTimerManager().ClearTimer(ReactionDelayHandle);
    }
    ClearEvacuationReevalTimer();
    bAwaitingReactionDelay = false;

    if (CurrentExit)
    {
        CurrentExit->LeaveQueue(this);
        CurrentExit = nullptr;
    }
    ExitQueueIndex = INDEX_NONE;
    bHasNotifiedExitReady = false;
    AssignedQueueSlotLocation = FVector::ZeroVector;

    if (bPreserveTerminalEvacState)
    {
        return;
    }

    bIsSafeAtExit = false;
    bIsCasualty = false;
    AssignedSafeZone = nullptr;
    EvacuationPathFailCount = 0;
    ClearEvacuationPersonality();

    StopMovement();
    ClearWaypoints();
    ClearSteeringForces();

    CurrentBehavior = EBoidBehaviorState::Normal;
    RefreshWalkSpeedForSimulationMode();

    // Lock autonomous movement until ScheduleManager resumes via StartMoveTo
    bIsIdle = true;
    bMovementDisabledAfterIncident = true;
}

void ABoidsAB::StartFlee()
{
    if (CurrentBehavior == EBoidBehaviorState::Fleeing)
        return;

    if (ShouldUseEvacuationRouting() && !bEvacuationPersonalityInitialized)
    {
        InitializeEvacuationPersonality();
    }

    bAwaitingReactionDelay = false;

    UE_LOG(LogTemp, Warning,
        TEXT("[Boid %s] ENTERING FLEE STATE"),
        *GetName()
    );

    bIsIdle = false;
    bIsMoving = false;

    ActiveNeedNavIntent = ENeedNavIntent::None;

    ClearWaypoints();
    ClearSteeringForces();

    LeaveCurrentStation();

    bHasNotifiedExitReady = false;
    AssignedQueueSlotLocation = FVector::ZeroVector;
    EvacuationPathFailCount = 0;
    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(FleeRetryHandle);
    }

    // Base flee direction when no exit/path is available
    if (bHasIncidentLocation)
    {
        FVector Away = GetActorLocation() - LastIncidentLocation;
        Away.Z = 0.f;

        if (!Away.IsNearlyZero())
        {
            FleeDirection = Away.GetSafeNormal();
        }
        else
        {
            FleeDirection = GetActorForwardVector();
        }
    }
    else
    {
        FVector HorizontalVel = BoidSpeed;
        HorizontalVel.Z = 0.f;

        if (!HorizontalVel.IsNearlyZero())
        {
            FleeDirection = HorizontalVel.GetSafeNormal();
        }
        else
        {
            FleeDirection = GetActorForwardVector();
        }
    }

    UCharacterMovementComponent* MoveComp = GetCharacterMovement();
    if (MoveComp)
    {
        CachedMaxWalkSpeed = BaseWalkSpeed;
        float FleeSpeed = FleeMaxSpeed * EvacuationSpeedMultiplier;
        if (const UWorld* World = GetWorld())
        {
            if (const USimulationFeatureRegistry* Registry = World->GetSubsystem<USimulationFeatureRegistry>())
            {
                if (Registry->IsFastSimulationMode())
                {
                    FleeSpeed *= Registry->GetFastSimulationSettings().GetClampedMovementSpeedMultiplier();
                }
                else if (Registry->IsAccurateTimeMode())
                {
                    // Same single scale as the clock. Do not also scale flee DeltaTime.
                    FleeSpeed *= Registry->GetAccurateTimeSettings().SimulationSpeed;
                }
            }
        }
        MoveComp->MaxWalkSpeed = FleeSpeed;
    }

    {
        float FleeSpeed = FleeMaxSpeed * EvacuationSpeedMultiplier;
        if (const UWorld* World = GetWorld())
        {
            if (const USimulationFeatureRegistry* Registry = World->GetSubsystem<USimulationFeatureRegistry>())
            {
                if (Registry->IsFastSimulationMode())
                {
                    FleeSpeed *= Registry->GetFastSimulationSettings().GetClampedMovementSpeedMultiplier();
                }
                else if (Registry->IsAccurateTimeMode())
                {
                    FleeSpeed *= Registry->GetAccurateTimeSettings().SimulationSpeed;
                }
            }
        }
        BoidSpeed = BoidSpeed.GetSafeNormal() * FleeSpeed;
    }

    CurrentBehavior = EBoidBehaviorState::Fleeing;

    bIsFirstFleeExitSelection = true;
    ExitReassignmentCount = 0;

    if (ShouldUseEvacuationRouting())
    {
        OnAgentFleeStarted.Broadcast(this);
        StartEvacuationReevalTimer();
    }

    ChooseFleeDestination();
}

bool ABoidsAB::ShouldUseEvacuationRouting() const
{
    if (!GetWorld())
    {
        return false;
    }
    const USimulationFeatureRegistry* Registry = GetWorld()->GetSubsystem<USimulationFeatureRegistry>();
    return Registry && Registry->IsFeatureEnabled(ESimulationFeature::Evacuation);
}

void ABoidsAB::StartEvacuationReevalTimer()
{
    if (!ShouldUseEvacuationRouting() || !GetWorld())
    {
        return;
    }

    const USimulationFeatureRegistry* Registry = GetWorld()->GetSubsystem<USimulationFeatureRegistry>();
    const float Interval = Registry ? Registry->GetEvacuationSettings().CongestionReevaluationIntervalSeconds : 5.f;
    if (Interval <= 0.f)
    {
        return;
    }

    GetWorld()->GetTimerManager().SetTimer(
        FleeReevalHandle,
        this,
        &ABoidsAB::ReevaluateFleeExitIfNeeded,
        Interval,
        true);
}

void ABoidsAB::ClearEvacuationReevalTimer()
{
    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(FleeReevalHandle);
    }
}

void ABoidsAB::AssignExitAndPath(AExitPoint* Exit, int32 SlotIndex)
{
    if (!Exit)
    {
        return;
    }

    FVector QueueTarget;
    if (!Exit->TryGetReachableQueueSlot(this, SlotIndex, QueueTarget))
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[Boid %s] Evacuation: no reachable queue slot %d on %s — trying another exit"),
            *GetName(),
            SlotIndex,
            *Exit->GetName());

        Exit->LeaveQueue(this);
        CurrentExit = nullptr;
        ExitQueueIndex = INDEX_NONE;
        AssignedQueueSlotLocation = FVector::ZeroVector;
        ChooseFleeDestination();
        return;
    }

    CurrentExit = Exit;
    ExitQueueIndex = SlotIndex;
    bIsFirstFleeExitSelection = false;
    AssignedQueueSlotLocation = QueueTarget;
    TargetL = QueueTarget;
    EvacuationPathFailCount = 0;

    ClearWaypoints();
    ComputePath(QueueTarget);

    if (ShouldUseEvacuationRouting())
    {
        OnAgentExitChosen.Broadcast(this, Exit);
    }
}

bool ABoidsAB::IsAtAssignedQueueSlot() const
{
    if (AssignedQueueSlotLocation.IsNearlyZero())
    {
        return false;
    }

    const float DistSq = FVector::DistSquared2D(GetActorLocation(), AssignedQueueSlotLocation);
    return DistSq <= FMath::Square(QueueSlotArrivalRadius);
}

void ABoidsAB::HandleEvacuationPathFailure(const FVector& AttemptedTarget)
{
    UE_LOG(LogTemp, Warning,
        TEXT("[Boid %s] Evacuation path failure to %s (fail count %d)"),
        *GetName(),
        *AttemptedTarget.ToString(),
        EvacuationPathFailCount + 1);

    EvacuationPathFailCount++;

    static constexpr int32 MaxEvacuationPathRetries = 3;
    if (!CurrentExit || ExitQueueIndex == INDEX_NONE)
    {
        ChooseFleeDestination();
        return;
    }

    if (EvacuationPathFailCount > MaxEvacuationPathRetries)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[Boid %s] Evacuation: max path retries exceeded on %s — reassigning exit"),
            *GetName(),
            *CurrentExit->GetName());

        AExitPoint* FailedExit = CurrentExit;
        FailedExit->LeaveQueue(this);
        CurrentExit = nullptr;
        ExitQueueIndex = INDEX_NONE;
        AssignedQueueSlotLocation = FVector::ZeroVector;
        EvacuationPathFailCount = 0;
        ChooseFleeDestination();
        return;
    }

    FVector ResolvedSlot;
    if (CurrentExit->TryGetReachableQueueSlot(this, ExitQueueIndex, ResolvedSlot))
    {
        AssignedQueueSlotLocation = ResolvedSlot;
        TargetL = ResolvedSlot;
        ClearWaypoints();
        ComputePath(ResolvedSlot);
        return;
    }

    UE_LOG(LogTemp, Warning,
        TEXT("[Boid %s] Evacuation: queue slot %d on %s no longer reachable — reassigning exit"),
        *GetName(),
        ExitQueueIndex,
        *CurrentExit->GetName());

    CurrentExit->LeaveQueue(this);
    CurrentExit = nullptr;
    ExitQueueIndex = INDEX_NONE;
    AssignedQueueSlotLocation = FVector::ZeroVector;
    EvacuationPathFailCount = 0;
    ChooseFleeDestination();
}

void ABoidsAB::ScheduleFleeRetryWhenAllExitsFull()
{
    UE_LOG(LogTemp, Warning,
        TEXT("[Boid %s] All exit queues full, retrying in 2s"),
        *GetName());

    CurrentExit = nullptr;
    ExitQueueIndex = INDEX_NONE;
    ClearWaypoints();

    if (GetWorld())
    {
        GetWorld()->GetTimerManager().SetTimer(
            FleeRetryHandle,
            this,
            &ABoidsAB::RetryFleeExit,
            2.0f,
            false);
    }
}

void ABoidsAB::ChooseFleeDestination_Legacy()
{
    TArray<AActor*> Exits;
    UGameplayStatics::GetAllActorsWithTag(
        GetWorld(),
        FName("ExitPoint"),
        Exits);

    if (Exits.Num() == 0)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[Boid %s] No ExitPoints found, using fallback flee direction"),
            *GetName());

        CurrentExit = nullptr;
        ExitQueueIndex = INDEX_NONE;
        ClearWaypoints();
        return;
    }

    const FVector MyLoc = GetActorLocation();
    Exits.Sort([&MyLoc](const AActor& A, const AActor& B) {
        return FVector::DistSquared(MyLoc, A.GetActorLocation())
             < FVector::DistSquared(MyLoc, B.GetActorLocation());
    });

    for (AActor* A : Exits)
    {
        AExitPoint* Exit = Cast<AExitPoint>(A);
        if (!Exit)
        {
            continue;
        }

        int32 SlotIndex;
        if (Exit->RequestQueueSlot(this, SlotIndex))
        {
            AssignExitAndPath(Exit, SlotIndex);
            return;
        }
    }

    ScheduleFleeRetryWhenAllExitsFull();
}

void ABoidsAB::ChooseFleeDestination()
{
    if (ShouldUseEvacuationRouting())
    {
        if (UCrowdRoutingService* Routing = GetWorld()->GetSubsystem<UCrowdRoutingService>())
        {
            AExitPoint* Exit = nullptr;
            int32 SlotIndex = INDEX_NONE;
            if (Routing->TryAssignBestExit(this, ECrowdRoutingMode::Evacuation, bIsFirstFleeExitSelection, Exit, SlotIndex))
            {
                AssignExitAndPath(Exit, SlotIndex);
                return;
            }
            ScheduleFleeRetryWhenAllExitsFull();
            return;
        }
    }

    ChooseFleeDestination_Legacy();
}

void ABoidsAB::ReevaluateFleeExitIfNeeded()
{
    if (!ShouldUseEvacuationRouting() || bIsSafeAtExit || IsCasualty() || IsInEvacuationSuccessState())
    {
        return;
    }

    if (CurrentBehavior != EBoidBehaviorState::Fleeing && CurrentBehavior != EBoidBehaviorState::Exiting)
    {
        return;
    }

    const USimulationFeatureRegistry* Registry = GetWorld()->GetSubsystem<USimulationFeatureRegistry>();
    if (!Registry)
    {
        return;
    }

    if (ExitReassignmentCount >= Registry->GetEvacuationSettings().MaxReassignmentAttempts)
    {
        return;
    }

    if (!CurrentExit)
    {
        return;
    }

    UCrowdRoutingService* Routing = GetWorld()->GetSubsystem<UCrowdRoutingService>();
    if (!Routing)
    {
        return;
    }

    AExitPoint* BetterExit = nullptr;
    if (!Routing->ShouldReassignExit(this, CurrentExit, ECrowdRoutingMode::Evacuation, BetterExit))
    {
        return;
    }

    if (!BetterExit || BetterExit == CurrentExit)
    {
        return;
    }

    AExitPoint* OldExit = CurrentExit;
    OldExit->LeaveQueue(this);
    CurrentExit = nullptr;
    ExitQueueIndex = INDEX_NONE;
    bHasNotifiedExitReady = false;

    int32 SlotIndex = INDEX_NONE;
    if (!BetterExit->RequestQueueSlot(this, SlotIndex))
    {
        ChooseFleeDestination();
        return;
    }

    ExitReassignmentCount++;
    CurrentBehavior = EBoidBehaviorState::Fleeing;
    UE_LOG(LogTemp, Warning,
        TEXT("[Boid %s] Evacuation exit reassignment: %s -> %s (attempt %d)"),
        *GetName(),
        *OldExit->GetName(),
        *BetterExit->GetName(),
        ExitReassignmentCount);
    AssignExitAndPath(BetterExit, SlotIndex);
    OnAgentExitReassigned.Broadcast(this, OldExit, BetterExit);
}

FVector ABoidsAB::PickNearestExit() const
{
    TArray<AActor*> Exits;
    UGameplayStatics::GetAllActorsWithTag(GetWorld(), TEXT("ExitPoint"), Exits);

    if (Exits.Num() == 0)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[Boid %s] No ExitPoint actors found"),
            *GetName());
        return FVector::ZeroVector;
    }

    FVector MyLoc = GetActorLocation();
    AActor* BestExit = nullptr;
    float BestDistSq = FLT_MAX;

    for (AActor* Exit : Exits)
    {
        float DistSq = FVector::DistSquared(MyLoc, Exit->GetActorLocation());
        if (DistSq < BestDistSq)
        {
            BestDistSq = DistSq;
            BestExit = Exit;
        }
    }

    return BestExit ? BestExit->GetActorLocation() : FVector::ZeroVector;
}

void ABoidsAB::FinishExit()
{
    UE_LOG(LogTemp, Log,
        TEXT("[Boid %s] REACHED EXIT - Waiting safely"),
        *GetName()
    );

    StopMovement();
    ClearWaypoints();
    ClearSteeringForces();

    // Stay queued at the exit until the incident ends (do not LeaveQueue here).
    bIsSafeAtExit = true;

    // Schedules stay blocked via bIsSafeAtExit
    CurrentBehavior = EBoidBehaviorState::Normal;
    RefreshWalkSpeedForSimulationMode();
}

void ABoidsAB::OnExitApproved()
{
    UE_LOG(LogTemp, Log,
        TEXT("[Boid %s] Exit approved"),
        *GetName()
    );

    StopMovement();
    ClearWaypoints();
    ClearSteeringForces();

    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(FleeRetryHandle);
    }
    ClearEvacuationReevalTimer();

    CurrentExit = nullptr;
    ExitQueueIndex = INDEX_NONE;
    AssignedQueueSlotLocation = FVector::ZeroVector;

    if (ShouldUseEvacuationRouting())
    {
        OnAgentExitCompleted.Broadcast(this);
    }

    if (ShouldUseSafeZoneFlow())
    {
        if (AEvacuationSafeZone* Zone = FindNearestSafeZone())
        {
            BeginMovementToSafeZone(Zone);
            return;
        }
    }

    UE_LOG(LogTemp, Log,
        TEXT("[Boid %s] Exit approved - now safe at exit"),
        *GetName()
    );

    bIsSafeAtExit = true;
    CurrentBehavior = EBoidBehaviorState::Normal;
    RefreshWalkSpeedForSimulationMode();
}

void ABoidsAB::UpdateExitQueuePosition(int32 NewIndex, FVector NewSlotLocation)
{
    ExitQueueIndex = NewIndex;
    bHasNotifiedExitReady = false;

    FVector ResolvedSlot = NewSlotLocation;
    if (CurrentExit && !CurrentExit->TryGetReachableQueueSlot(this, NewIndex, ResolvedSlot))
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[Boid %s] Evacuation: queue advancement slot %d unreachable on %s — using snapped %s"),
            *GetName(),
            NewIndex,
            *CurrentExit->GetName(),
            *NewSlotLocation.ToString());
        ResolvedSlot = NewSlotLocation;
    }

    AssignedQueueSlotLocation = ResolvedSlot;
    TargetL = ResolvedSlot;

    ClearWaypoints();
    ComputePath(ResolvedSlot);
}

void ABoidsAB::RetryFleeExit()
{
    if (CurrentBehavior != EBoidBehaviorState::Fleeing)
        return;

    if (CurrentExit)
        return;

    ChooseFleeDestination();
}

bool ABoidsAB::IsSafelyEvacuated() const
{
    return CurrentBehavior == EBoidBehaviorState::EvacuationSuccess || bIsSafeAtExit;
}

bool ABoidsAB::ShouldIgnoreScheduleDuringEvacuation() const
{
    return IsFleeing()
        || bIsSafeAtExit
        || bAwaitingReactionDelay
        || CurrentBehavior == EBoidBehaviorState::MovingToSafeZone
        || CurrentBehavior == EBoidBehaviorState::EvacuationSuccess
        || CurrentBehavior == EBoidBehaviorState::Casualty;
}

void ABoidsAB::ClearEvacuationPersonality()
{
    bEvacuationPersonalityInitialized = false;
    ReactionDelaySeconds = 0.f;
    RecordedReactionDelaySeconds = 0.f;
    EvacuationSpeedMultiplier = 1.f;
    CongestionSwitchThresholdOffset = 0.f;
    NearestExitBiasScale = 1.f;
    EvacuationNotifiedWorldTime = 0.0;
}

void ABoidsAB::InitializeEvacuationPersonality()
{
    if (bEvacuationPersonalityInitialized || !GetWorld())
    {
        return;
    }

    const USimulationFeatureRegistry* Registry = GetWorld()->GetSubsystem<USimulationFeatureRegistry>();
    const FEvacuationRoutingSettings Settings = Registry ? Registry->GetEvacuationSettings() : FEvacuationRoutingSettings();

    const float DelayMin = FMath::Min(Settings.ReactionDelayMin, Settings.ReactionDelayMax);
    const float DelayMax = FMath::Max(Settings.ReactionDelayMin, Settings.ReactionDelayMax);
    ReactionDelaySeconds = (DelayMax > 0.f || DelayMin > 0.f)
        ? FMath::FRandRange(DelayMin, DelayMax)
        : 0.f;

    const float SpeedMin = FMath::Min(Settings.SpeedVariationMin, Settings.SpeedVariationMax);
    const float SpeedMax = FMath::Max(Settings.SpeedVariationMin, Settings.SpeedVariationMax);
    EvacuationSpeedMultiplier = FMath::FRandRange(SpeedMin, SpeedMax);

    CongestionSwitchThresholdOffset = Settings.CongestionToleranceVariation > 0.f
        ? FMath::FRandRange(-Settings.CongestionToleranceVariation, Settings.CongestionToleranceVariation)
        : 0.f;

    const float BiasSpread = Settings.NearestExitBiasVariation;
    NearestExitBiasScale = BiasSpread > 0.f
        ? FMath::FRandRange(1.f - BiasSpread, 1.f + BiasSpread)
        : 1.f;

    EvacuationNotifiedWorldTime = GetWorld()->GetTimeSeconds();
    bEvacuationPersonalityInitialized = true;

    UE_LOG(LogTemp, Log,
        TEXT("[Boid %s] Evacuation personality: reaction=%.2fs speed=%.2fx congestionOffset=%.2f nearestBias=%.2f"),
        *GetName(),
        ReactionDelaySeconds,
        EvacuationSpeedMultiplier,
        CongestionSwitchThresholdOffset,
        NearestExitBiasScale);
}

void ABoidsAB::ScheduleReactionDelay()
{
    if (!GetWorld() || ReactionDelaySeconds <= KINDA_SMALL_NUMBER)
    {
        StartFlee();
        return;
    }

    bAwaitingReactionDelay = true;
    float TimerSeconds = ReactionDelaySeconds;
    if (USimulationFeatureRegistry* Registry = GetWorld()->GetSubsystem<USimulationFeatureRegistry>())
    {
        if (Registry->IsAccurateTimeMode())
        {
            TimerSeconds = Registry->ScaleAuthoredDurationToRealSeconds(ReactionDelaySeconds);
        }
    }
    UE_LOG(LogTemp, Log,
        TEXT("[Boid %s] Evacuation reaction delay %.2fs (timer %.2fs)"),
        *GetName(),
        ReactionDelaySeconds,
        TimerSeconds);

    GetWorld()->GetTimerManager().SetTimer(
        ReactionDelayHandle,
        this,
        &ABoidsAB::BeginFleeAfterReactionDelay,
        TimerSeconds,
        false);
}

void ABoidsAB::BeginFleeAfterReactionDelay()
{
    bAwaitingReactionDelay = false;
    RecordedReactionDelaySeconds = ReactionDelaySeconds;
    if (GetWorld() && EvacuationNotifiedWorldTime > 0.0)
    {
        const float RealElapsed = static_cast<float>(GetWorld()->GetTimeSeconds() - EvacuationNotifiedWorldTime);
        RecordedReactionDelaySeconds = RealElapsed;
        if (const USimulationFeatureRegistry* Registry = GetWorld()->GetSubsystem<USimulationFeatureRegistry>())
        {
            if (Registry->IsAccurateTimeMode())
            {
                // Analytics in this mode are simulation seconds. Real elapsed × clock speed.
                RecordedReactionDelaySeconds = RealElapsed * Registry->GetAccurateTimeSettings().SimulationSpeed;
            }
        }
    }

    OnAgentReactionDelayComplete.Broadcast(this, RecordedReactionDelaySeconds);
    StartFlee();
}

bool ABoidsAB::HasSafeZonesInLevel() const
{
    if (!GetWorld())
    {
        return false;
    }

    TArray<AActor*> Zones;
    UGameplayStatics::GetAllActorsWithTag(GetWorld(), FName(TEXT("SafeZone")), Zones);
    return Zones.Num() > 0;
}

bool ABoidsAB::ShouldUseSafeZoneFlow() const
{
    return ShouldUseEvacuationRouting() && HasSafeZonesInLevel();
}

AEvacuationSafeZone* ABoidsAB::FindNearestSafeZone() const
{
    if (!GetWorld())
    {
        return nullptr;
    }

    TArray<AActor*> Zones;
    UGameplayStatics::GetAllActorsWithTag(GetWorld(), FName(TEXT("SafeZone")), Zones);

    AEvacuationSafeZone* Best = nullptr;
    float BestDistSq = FLT_MAX;
    const FVector MyLoc = GetActorLocation();

    for (AActor* Actor : Zones)
    {
        AEvacuationSafeZone* Zone = Cast<AEvacuationSafeZone>(Actor);
        if (!Zone)
        {
            continue;
        }

        const float DistSq = FVector::DistSquared(MyLoc, Zone->GetZoneLocation());
        if (DistSq < BestDistSq)
        {
            BestDistSq = DistSq;
            Best = Zone;
        }
    }

    return Best;
}

bool ABoidsAB::IsAtAssignedSafeZone() const
{
    if (!AssignedSafeZone.IsValid())
    {
        return false;
    }

    return AssignedSafeZone->ContainsLocation(GetActorLocation());
}

void ABoidsAB::BeginMovementToSafeZone(AEvacuationSafeZone* Zone)
{
    if (!Zone)
    {
        bIsSafeAtExit = true;
        CurrentBehavior = EBoidBehaviorState::Normal;
        return;
    }

    AssignedSafeZone = Zone;
    CurrentBehavior = EBoidBehaviorState::MovingToSafeZone;
    TargetL = Zone->GetZoneLocation();
    RefreshWalkSpeedForSimulationMode();

    ClearWaypoints();
    ComputePath(TargetL);

    UE_LOG(LogTemp, Warning,
        TEXT("[Boid %s] Moving to safe zone %s"),
        *GetName(),
        *Zone->GetName());
}

void ABoidsAB::EnterEvacuationSuccessState()
{
    StopMovement();
    ClearWaypoints();
    ClearSteeringForces();

    CurrentBehavior = EBoidBehaviorState::EvacuationSuccess;
    bIsSafeAtExit = false;
    RefreshWalkSpeedForSimulationMode();

    const USimulationFeatureRegistry* Registry = GetWorld() ? GetWorld()->GetSubsystem<USimulationFeatureRegistry>() : nullptr;
    const bool bCelebrate = !Registry || Registry->GetEvacuationSettings().bEnableCelebrationState;

    UE_LOG(LogTemp, Warning,
        TEXT("[Boid %s] Reached safe zone — evacuation success"),
        *GetName());

    if (ShouldUseEvacuationRouting())
    {
        OnAgentEvacuationSuccess.Broadcast(this);
    }

    if (bCelebrate)
    {
    }
}

void ABoidsAB::EnterCasualtyState()
{
    if (IsCasualty() || IsSafelyEvacuated())
    {
        return;
    }

    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(FleeRetryHandle);
        GetWorld()->GetTimerManager().ClearTimer(ReactionDelayHandle);
    }
    ClearEvacuationReevalTimer();

    if (CurrentExit)
    {
        CurrentExit->LeaveQueue(this);
        CurrentExit = nullptr;
    }

    ExitQueueIndex = INDEX_NONE;
    bAwaitingReactionDelay = false;
    StopMovement();
    ClearWaypoints();
    ClearSteeringForces();

    CurrentBehavior = EBoidBehaviorState::Casualty;
    bIsCasualty = true;
    RefreshWalkSpeedForSimulationMode();

    UE_LOG(LogTemp, Warning,
        TEXT("[Boid %s] Marked as evacuation casualty"),
        *GetName());

    OnAgentCasualty.Broadcast(this);
}

#pragma region Social

void ABoidsAB::InitializeFriends(const TArray<ABoidsAB*>& AllBoids, int32 NumFriends)
{
    TArray<ABoidsAB*> PotentialFriends = AllBoids;
    PotentialFriends.Remove(this);

    for (int32 i = 0; i < NumFriends && PotentialFriends.Num() > 0; ++i)
    {
        const int32 Index = FMath::RandRange(0, PotentialFriends.Num() - 1);
        ABoidsAB* Friend = PotentialFriends[Index];

        Friends.Add(Friend);
        Friend->Friends.Add(this);

        PotentialFriends.RemoveAt(Index);
    }
}

void ABoidsAB::LogFriendInteraction(ABoidsAB* Friend)
{
    FString FormattedTime = GetCurrentFormattedTime();
    FString LogMessage = FString::Printf(TEXT("[%s] Boid %s says 'Hi' to friend %s.\n"), *FormattedTime, *this->GetName(), *Friend->GetName());
    WriteToLogFile(LogMessage);
}

void ABoidsAB::LogEnemyAvoidance(ABoidsAB* Enemy)
{
    FString FormattedTime = GetCurrentFormattedTime();
    FString LogMessage = FString::Printf(TEXT("[%s] Boid %s avoids enemy %s.\n"), *FormattedTime, *this->GetName(), *Enemy->GetName());
    WriteToLogFile(LogMessage);
}

void ABoidsAB::ResetDailyGreetings()
{
    DailyGreetings.Empty();
}

void ABoidsAB::ResetDailyAvoidances()
{
    DailyAvoidances.Empty();
}

#pragma endregion

#pragma region Logs

void ABoidsAB::InitializeLogs()
{
    CentralizedLogFilePath = TEXT("C:/Users/aepbr/Documents/GitHub/B.A.R.S/Logs/Centralized_Logs.txt");
    FFileHelper::SaveStringToFile(FString(""), *CentralizedLogFilePath, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_None);
}

void ABoidsAB::WriteToLogFile(const FString& Message)
{
    FFileHelper::SaveStringToFile(Message, *CentralizedLogFilePath, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_Append);
}

#pragma endregion

#pragma region Utility

FString ABoidsAB::GetCurrentFormattedTime() const
{
    float TotalTimeSeconds = GetWorld()->GetTimeSeconds() * TimeAccelerationFactor;
    int Hours = static_cast<int>(TotalTimeSeconds / 3600) % 24;
    int Minutes = static_cast<int>(TotalTimeSeconds / 60) % 60;
    return FString::Printf(TEXT("%02d:%02d"), Hours, Minutes);
}

FString ABoidsAB::GetCurrentDay() const
{
    float TotalTimeSeconds = GetWorld()->GetTimeSeconds() * TimeAccelerationFactor;
    int Day = static_cast<int>(TotalTimeSeconds / 86400); // 86400 seconds in a day
    return FString::Printf(TEXT("Day%d"), Day);
}

#pragma endregion

void ABoidsAB::StartMoveTo(const FVector& Target, ENeedNavIntent NeedIntent)
{
    // Clears post-incident movement lock (ScheduleManager resumes via StartMoveTo).
    bMovementDisabledAfterIncident = false;

    ActiveNeedNavIntent = NeedIntent;

    ClearIdleDurationTimer();
    bIsIdle = false;
    bIsMoving = true;
    bHasArrived = false;
    bReachedDestination = false;

    TargetL = Target;

    if (FallbackHandlerComponent)
    {
        FallbackHandlerComponent->OnMovementCommandIssued(this, Target);
    }

    ClearWaypoints();
    ComputePath(TargetL);
    ClearSteeringForces();
}

void ABoidsAB::ReleaseBlockersForScheduledHomeCommute(const FString& DestinationTag)
{
    if (!GetWorld() || !DestinationTag.StartsWith(TEXT("Home"), ESearchCase::IgnoreCase))
    {
        return;
    }

    USimulationFeatureRegistry* Registry = GetWorld()->GetSubsystem<USimulationFeatureRegistry>();
    if (!Registry || !Registry->IsFeatureEnabled(ESimulationFeature::DailyUrbanLife))
    {
        return;
    }

    LeaveCurrentStation();
    SetIdle(false);

    if (PedestrianInteractionComponent)
    {
        PedestrianInteractionComponent->CancelBriefPause();
    }
}

void ABoidsAB::SetBaseWalkSpeed(float BaseSpeed)
{
    BaseWalkSpeed = FMath::Max(1.f, BaseSpeed);
    CachedMaxWalkSpeed = BaseWalkSpeed;
    RefreshWalkSpeedForSimulationMode();
}

void ABoidsAB::RefreshWalkSpeedForSimulationMode()
{
    UCharacterMovementComponent* MoveComp = GetCharacterMovement();
    if (!MoveComp)
    {
        return;
    }

    if (!bCapturedMovementStepDefaults)
    {
        DefaultMaxSimulationIterations = MoveComp->MaxSimulationIterations;
        DefaultMaxSimulationTimeStep = MoveComp->MaxSimulationTimeStep;
        DefaultMaxAcceleration = MoveComp->MaxAcceleration;
        DefaultBrakingDecelerationWalking = MoveComp->BrakingDecelerationWalking;
        bCapturedMovementStepDefaults = true;
    }

    bool bAccurateTime = false;
    float Mult = 1.f;
    if (const UWorld* World = GetWorld())
    {
        if (const USimulationFeatureRegistry* Registry = World->GetSubsystem<USimulationFeatureRegistry>())
        {
            if (Registry->IsFastSimulationMode())
            {
                // Fast keeps its own walk multiplier. It is NOT equal to the 600x clock.
                Mult = Registry->GetFastSimulationSettings().GetClampedMovementSpeedMultiplier();
            }
            else if (Registry->IsAccurateTimeMode())
            {
                // One scale, matching USimulationClock. Tick still uses the real frame delta.
                bAccurateTime = true;
                Mult = Registry->GetAccurateTimeSettings().SimulationSpeed;
            }
        }
    }

    if (bAccurateTime)
    {
        // CMC stops after 8 substeps of 0.05s. A hitch longer than that advances the clock
        // and leaves the capsule behind. 1.6s of substeps covers that hitch.
        const float Step = FMath::Max(DefaultMaxSimulationTimeStep, 0.01f);
        const int32 HitchIterations = FMath::CeilToInt(1.6f / Step);
        MoveComp->MaxSimulationIterations = FMath::Max(DefaultMaxSimulationIterations, HitchIterations);
        MoveComp->MaxSimulationTimeStep = DefaultMaxSimulationTimeStep;
        // Reach and drop MaxWalkSpeed inside one substep so distance stays on the clock.
        const float Instant = FMath::Max(BaseWalkSpeed * Mult * 50.f, DefaultMaxAcceleration);
        MoveComp->MaxAcceleration = Instant;
        MoveComp->BrakingDecelerationWalking = Instant;
    }
    else
    {
        MoveComp->MaxSimulationIterations = DefaultMaxSimulationIterations;
        MoveComp->MaxSimulationTimeStep = DefaultMaxSimulationTimeStep;
        MoveComp->MaxAcceleration = DefaultMaxAcceleration;
        MoveComp->BrakingDecelerationWalking = DefaultBrakingDecelerationWalking;
    }

    // Do not override evacuation flee speed (restored via CachedMaxWalkSpeed / BaseWalkSpeed).
    if (CurrentBehavior == EBoidBehaviorState::Fleeing)
    {
        return;
    }

    MoveComp->MaxWalkSpeed = BaseWalkSpeed * Mult;
}

bool ABoidsAB::ShouldApplyDailyLifeDestinationSpread(const FString& DestinationTag, const FString& BreakAreaTag)
{
    if (DestinationTag.IsEmpty())
    {
        return false;
    }
    if (DestinationTag.StartsWith(TEXT("Home"), ESearchCase::IgnoreCase))
    {
        return true;
    }
    if (DestinationTag.Equals(TEXT("Food"), ESearchCase::IgnoreCase)
        || DestinationTag.Equals(TEXT("FoodLocation"), ESearchCase::IgnoreCase))
    {
        return true;
    }
    if (!BreakAreaTag.IsEmpty() && DestinationTag.Equals(BreakAreaTag, ESearchCase::IgnoreCase))
    {
        return true;
    }
    if (DestinationTag.Equals(TEXT("Plaza"), ESearchCase::IgnoreCase))
    {
        return true;
    }
    return false;
}

FVector ABoidsAB::GetDailyLifeDestinationSpread(int32 Salt) const
{
    int32 AgentIndex = 0;
    const FString Id = AgentId.ToString();
    if (Id.StartsWith(TEXT("Agent_")))
    {
        AgentIndex = FMath::Max(0, FCString::Atoi(*Id.Mid(6)));
    }
    else
    {
        AgentIndex = static_cast<int32>(GetTypeHash(AgentId) & 0xffff);
    }

    // Deterministic ring so shared Home/Plaza targets do not collapse to one point.
    const float Angle = (AgentIndex * 2.3999632f) + (Salt * 0.73f);
    const float Radius = 90.f + static_cast<float>((AgentIndex + Salt * 3) % 6) * 28.f;
    return FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.f);
}

void ABoidsAB::NotifyScheduleMoveTo(const FVector& Target, const FString& DestinationTag)
{
    ReleaseBlockersForScheduledHomeCommute(DestinationTag);

    FVector AdjustedTarget = Target;
    if (UWorld* World = GetWorld())
    {
        if (USimulationFeatureRegistry* Registry = World->GetSubsystem<USimulationFeatureRegistry>())
        {
            if (Registry->IsFeatureEnabled(ESimulationFeature::DailyUrbanLife)
                && ShouldApplyDailyLifeDestinationSpread(
                    DestinationTag,
                    Registry->GetDailyUrbanLifeSettings().BreakAreaTag))
            {
                AdjustedTarget += GetDailyLifeDestinationSpread(0);
            }
        }
    }

    PendingDestinationTagForAnalytics = DestinationTag;
    StartMoveTo(AdjustedTarget);

    if (USimulationAnalytics* Analytics = GetWorld()->GetSubsystem<USimulationAnalytics>())
    {
        Analytics->RecordDailyCommuteStarted(this, AdjustedTarget, DestinationTag);
    }
}

void ABoidsAB::StartScheduleAction(UAgentAction* Action)
{
    if (ActiveScheduleAction && !ActiveScheduleAction->IsTerminal())
    {
        ActiveScheduleAction->Cancel();
    }

    ActiveScheduleAction = Action;
    if (ActiveScheduleAction)
    {
        ActiveScheduleAction->Start();
    }
}

void ABoidsAB::TickScheduleAction(float DeltaSeconds)
{
    if (!ActiveScheduleAction || ActiveScheduleAction->IsTerminal())
    {
        return;
    }

    ActiveScheduleAction->Tick(DeltaSeconds);
}

void ABoidsAB::RecomputePathToCurrentTarget()
{
    ClearWaypoints();
    ComputePath(TargetL);
}

TArray<FVector> ABoidsAB::GetFoodCandidateLocations() const
{
    TArray<FVector> Out;
    if (FoodLocations.Num() > 0)
    {
        Out = FoodLocations;
    }
    else if (!FoodLocation.IsZero())
    {
        Out.Add(FoodLocation);
    }
    return Out;
}

void ABoidsAB::AssignFoodDestinationFromMap(const TArray<FVector>& AllFoodLocations, int32 AgentIndex)
{
    FoodLocations.Empty();
    for (const FVector& Loc : AllFoodLocations)
    {
        if (!Loc.IsNearlyZero(1.f))
        {
            FoodLocations.AddUnique(Loc);
        }
    }

    if (FoodLocations.Num() == 0)
    {
        return;
    }

    if (!AssignedHomeLocation.IsZero() && FoodLocations.Num() > 1)
    {
        int32 NearestIdx = 0;
        float NearestDistSq = TNumericLimits<float>::Max();
        for (int32 i = 0; i < FoodLocations.Num(); ++i)
        {
            const float DistSq = FVector::DistSquared(AssignedHomeLocation, FoodLocations[i]);
            if (DistSq < NearestDistSq)
            {
                NearestDistSq = DistSq;
                NearestIdx = i;
            }
        }
        FoodLocation = FoodLocations[(NearestIdx + FMath::Max(0, AgentIndex)) % FoodLocations.Num()];
    }
    else
    {
        const int32 SafeIndex = FMath::Max(0, AgentIndex);
        FoodLocation = FoodLocations[SafeIndex % FoodLocations.Num()];
    }
}

FVector ABoidsAB::GetNearestFoodLocation() const
{
    const TArray<FVector> Candidates = GetFoodCandidateLocations();
    if (Candidates.Num() == 0)
    {
        return FoodLocation;
    }

    const FVector Loc = GetActorLocation();
    FVector Best = Candidates[0];
    float BestDistSq = FVector::DistSquared(Loc, Best);
    for (int32 i = 1; i < Candidates.Num(); ++i)
    {
        const float DistSq = FVector::DistSquared(Loc, Candidates[i]);
        if (DistSq < BestDistSq)
        {
            BestDistSq = DistSq;
            Best = Candidates[i];
        }
    }
    return Best;
}

bool ABoidsAB::IsNearAnyFoodLocation(float Radius) const
{
    const float RadiusSq = FMath::Square(Radius);
    const FVector Loc = GetActorLocation();
    for (const FVector& Candidate : GetFoodCandidateLocations())
    {
        if (FVector::DistSquared(Loc, Candidate) <= RadiusSq)
        {
            return true;
        }
    }
    return false;
}

TArray<FVector> ABoidsAB::GetHomeCandidateLocations() const
{
    TArray<FVector> Out;
    if (HomeLocations.Num() > 0)
    {
        Out = HomeLocations;
    }
    else if (!HomeLocation.IsZero())
    {
        Out.Add(HomeLocation);
    }
    return Out;
}

bool ABoidsAB::TryNavigateToAlternateFoodLocation(const FVector& FailedTarget, float MinSeparation)
{
    const TArray<FVector> Cands = GetFoodCandidateLocations();
    const float MinSq = FMath::Square(MinSeparation);
    TArray<FVector> Alts;
    for (const FVector& C : Cands)
    {
        if (C.IsNearlyZero(1.f))
        {
            continue;
        }
        if (FVector::DistSquared(C, FailedTarget) >= MinSq)
        {
            Alts.Add(C);
        }
    }
    if (Alts.Num() == 0)
    {
        return false;
    }
    const FVector Pick = Alts[FMath::RandRange(0, Alts.Num() - 1)];
    StartMoveTo(Pick, ENeedNavIntent::CriticalFood);
    return true;
}

bool ABoidsAB::TryNavigateToAlternateHomeLocation(const FVector& FailedTarget, float MinSeparation)
{
    const TArray<FVector> Cands = GetHomeCandidateLocations();
    const float MinSq = FMath::Square(MinSeparation);
    TArray<FVector> Alts;
    for (const FVector& C : Cands)
    {
        if (C.IsNearlyZero(1.f))
        {
            continue;
        }
        if (FVector::DistSquared(C, FailedTarget) >= MinSq)
        {
            Alts.Add(C);
        }
    }
    if (Alts.Num() == 0)
    {
        return false;
    }
    const FVector Pick = Alts[FMath::RandRange(0, Alts.Num() - 1)];
    StartMoveTo(Pick, ENeedNavIntent::CriticalHome);
    return true;
}

void ABoidsAB::StopMovement()
{
    bIsMoving = false;
    bHasArrived = true;
    bReachedDestination = true;

    ActiveNeedNavIntent = ENeedNavIntent::None;

    GetCharacterMovement()->StopMovementImmediately();
    ClearSteeringForces();
}

void ABoidsAB::SetIdle(bool bIdleState)
{
    ClearIdleDurationTimer();
    bIsIdle = bIdleState;
    if (bIdleState)
    {
        StopMovement();
    }
}

void ABoidsAB::SetIdleTimed(bool bIdleState, float DurationSeconds)
{
    SetIdle(bIdleState);
    if (bIdleState && DurationSeconds > 0.f && GetWorld())
    {
        float TimerSeconds = DurationSeconds;
        if (USimulationFeatureRegistry* Registry = GetWorld()->GetSubsystem<USimulationFeatureRegistry>())
        {
            TimerSeconds = Registry->ScaleAuthoredDurationToRealSeconds(DurationSeconds);
        }

        GetWorld()->GetTimerManager().SetTimer(
            IdleDurationHandle,
            this,
            &ABoidsAB::OnIdleDurationExpired,
            TimerSeconds,
            false);
    }
}

void ABoidsAB::ClearIdleDurationTimer()
{
    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(IdleDurationHandle);
    }
}

void ABoidsAB::OnIdleDurationExpired()
{
    SetIdle(false);
}

int32 ABoidsAB::GetScheduleAdjustedMinute(int32 RuleMinute) const
{
    return (RuleMinute + ScheduleMinuteOffset) % 1440;
}

void ABoidsAB::AssignDailyDestinations(FName HomeTag, FName WorkTag)
{
    AssignedHomeTag = HomeTag;
    AssignedWorkTag = WorkTag;
}

void ABoidsAB::ResolveDestinationsFromTags()
{
    if (!GetWorld())
    {
        return;
    }

    if (!AssignedHomeTag.IsNone())
    {
        TArray<AActor*> Found;
        UGameplayStatics::GetAllActorsWithTag(GetWorld(), AssignedHomeTag, Found);
        if (Found.Num() > 0)
        {
            AssignedHomeLocation = Found[0]->GetActorLocation();
            HomeLocation = AssignedHomeLocation;
            HomeLocations.AddUnique(AssignedHomeLocation);
        }
    }

    if (!AssignedWorkTag.IsNone())
    {
        TArray<AActor*> Found;
        UGameplayStatics::GetAllActorsWithTag(GetWorld(), AssignedWorkTag, Found);
        if (Found.Num() > 0)
        {
            AssignedWorkLocation = Found[0]->GetActorLocation();
            WorkLocation = AssignedWorkLocation;
            WorkLocations.AddUnique(AssignedWorkLocation);
        }
    }
}

bool ABoidsAB::IsAtAssignedHome(float Radius) const
{
    return !AssignedHomeLocation.IsZero()
        && FVector::Dist(GetActorLocation(), AssignedHomeLocation) <= Radius;
}

bool ABoidsAB::IsAtAssignedWorkplace(float Radius) const
{
    if (IsInteractingWithStation())
    {
        return true;
    }
    return !AssignedWorkLocation.IsZero()
        && FVector::Dist(GetActorLocation(), AssignedWorkLocation) <= Radius;
}

void ABoidsAB::TeleportToAssignedHome()
{
    if (AssignedHomeLocation.IsZero())
    {
        return;
    }

    const FVector Offset(FMath::FRandRange(-80.f, 80.f), FMath::FRandRange(-80.f, 80.f), 0.f);
    SetActorLocation(AssignedHomeLocation + Offset);
    StopMovement();
    SetIdle(true);
}

void ABoidsAB::HandleScheduleMovementArrival()
{
    if (!bIsMoving)
    {
        if (GStationNavDebug && TargetStation)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[StationNavDebug] %s HandleScheduleMovementArrival DECISION: abort | bIsMoving=false | OnStationReached=NO"),
                *GetName());
        }
        return;
    }

    const float Dist = FVector::Dist2D(GetActorLocation(), TargetL);
    const bool bNearTarget = Dist <= 180.f || Waypoints.Num() == 0;

    if (TargetStation)
    {
        // Require a real path and XY+Z proximity; empty path must not start interaction.
        constexpr float StationArriveXY = 180.f;
        constexpr float StationArriveZ = 220.f;

        const FVector AgentLoc = GetActorLocation();
        const FVector StationLoc = TargetStation->GetActorLocation();
        const bool bHasWaypoints = Waypoints.Num() > 0;
        const FVector LastWP = bHasWaypoints ? Waypoints.Last() : FVector::ZeroVector;
        const float DistXY = FVector::Dist2D(AgentLoc, TargetL);
        const float DistZ = FMath::Abs(AgentLoc.Z - TargetL.Z);
        const bool bXYOk = DistXY <= StationArriveXY;
        const bool bZOk = DistZ <= StationArriveZ;

        if (GStationNavDebug)
        {
            const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
            const bool bLogThisFrame = (Now - LastStationNavStopLogTime) >= GStationNavStopLogCooldown;
            if (bLogThisFrame)
            {
                LastStationNavStopLogTime = Now;
                UE_LOG(LogTemp, Warning,
                    TEXT("[StationNavDebug] %s HandleScheduleMovementArrival ENTER (station) | bIsMoving=1 WP=%d Idx=%d | Agent=%s | TargetL=%s | Station=%s | LastWP=%s"),
                    *GetName(),
                    Waypoints.Num(),
                    CurrentWaypointIndex,
                    *AgentLoc.ToString(),
                    *TargetL.ToString(),
                    *StationLoc.ToString(),
                    bHasWaypoints ? *LastWP.ToString() : TEXT("<none>"));

                UE_LOG(LogTemp, Warning,
                    TEXT("[StationNavDebug] %s   checks: DistXY=%.1f <= %.0f ? %s | DistZ=%.1f <= %.0f ? %s | WP>0 ? %s | ΔZ(agent-station)=%.1f ΔZ(lastWP-station)=%.1f | after_path: gap-close via MoveTowardsTarget(TargetL)"),
                    *GetName(),
                    DistXY, StationArriveXY, bXYOk ? TEXT("PASS") : TEXT("FAIL"),
                    DistZ, StationArriveZ, bZOk ? TEXT("PASS") : TEXT("FAIL"),
                    bHasWaypoints ? TEXT("PASS") : TEXT("FAIL"),
                    AgentLoc.Z - StationLoc.Z,
                    bHasWaypoints ? (LastWP.Z - StationLoc.Z) : 0.f);

                if (!bHasWaypoints)
                {
                    UE_LOG(LogTemp, Warning,
                        TEXT("[StationNavDebug] %s DECISION: OnStationReached=NO | reason=empty_waypoints | no_gap_close"),
                        *GetName());
                }
                else if (!(bXYOk && bZOk))
                {
                    const TCHAR* FailReason =
                        (!bXYOk && !bZOk) ? TEXT("XY_FAIL_and_Z_FAIL")
                        : (!bXYOk) ? TEXT("XY_FAIL_Z_PASS")
                        : TEXT("XY_PASS_Z_FAIL");
                    UE_LOG(LogTemp, Warning,
                        TEXT("[StationNavDebug] %s DECISION: OnStationReached=NO yet | reason=%s | action=MoveTowardsTarget(station_root/TargetL) gap-close | DistXY=%.1f DistZ=%.1f"),
                        *GetName(), FailReason, DistXY, DistZ);
                }
            }
        }

        if (!bHasWaypoints)
        {
            return;
        }

        if (bXYOk && bZOk)
        {
            if (GStationNavDebug)
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("[StationNavDebug] %s DECISION: OnStationReached=YES | reason=XY_and_Z_within_tolerance | next=StopMovement+NotifyOccupantReached"),
                    *GetName());
            }
            OnStationReached();
            return;
        }

        // Last nav point is short of TargetL; close remaining gap on the station root.
        if (GetWorld())
        {
            MoveTowardsTarget(GetWorld()->GetDeltaSeconds());
        }
        return;
    }

    // Empty waypoints + far from target = path failed; do not fake an arrival.
    if (Waypoints.Num() == 0 && Dist > 180.f)
    {
        return;
    }

    if (!bNearTarget)
    {
        MoveTowardsTarget(GetWorld()->GetDeltaSeconds());
        if (FVector::Dist2D(GetActorLocation(), TargetL) > 120.f)
        {
            return;
        }
    }

    StopMovement();
    // Keep agents able to flock after Home/Plaza arrival (do not freeze as idle).
    SetIdle(false);

    if (USimulationAnalytics* Analytics = GetWorld()->GetSubsystem<USimulationAnalytics>())
    {
        Analytics->RecordDailyDestinationReached(this, TargetL, PendingDestinationTagForAnalytics);
    }
    PendingDestinationTagForAnalytics.Empty();
}

void ABoidsAB::ClearWaypoints()
{
    Waypoints.Empty();
    CurrentWaypointIndex = 0;
}

void ABoidsAB::ClearSteeringForces()
{
    TargetForceList.Empty();
}

#pragma region Station

void ABoidsAB::MoveToStation(AAgentStation* Station, float Duration)
{
    if (IsFleeing())
    {
        return;
    }

    if (!Station)
    {
        return;
    }

    PendingStationDuration = Duration;

    if (TargetStation == Station)
    {
        return;
    }

    if (TargetStation)
    {
        TargetStation->LeaveStation(this);
    }

    TargetStation = Station;
    bIsQueuedAtStation = false;

    if (!Station->RequestEnter(this))
    {
        TargetStation = nullptr;
        CancelStationInteractionPipeline();
        if (FallbackHandlerComponent)
        {
            FallbackHandlerComponent->NotifyScheduleFailure(this, EAgentFallbackFailureKind::StationUnavailable, TEXT("RequestEnter failed"));
        }
        return;
    }

    if (Station->IsAgentActiveOccupant(this))
    {
        const FVector StationLoc = Station->GetActorLocation();
        const FVector InteractLoc = Station->GetWorldInteractionTransform().GetLocation();
        if (GStationNavDebug)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[StationNavDebug] %s MoveToStation BEGIN | Station=%s | StationXYZ=%s | InteractionPointXYZ=%s | AgentXYZ=%s | ΔZ(agent-station)=%.1f"),
                *GetName(),
                *Station->GetName(),
                *StationLoc.ToString(),
                *InteractLoc.ToString(),
                *GetActorLocation().ToString(),
                GetActorLocation().Z - StationLoc.Z);
        }

        StartMoveTo(StationLoc);

        if (GStationNavDebug)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[StationNavDebug] %s after StartMoveTo | TargetL=%s | WPCount=%d"),
                *GetName(), *TargetL.ToString(), Waypoints.Num());
        }
    }
    else
    {
        bIsQueuedAtStation = true;
        // DUL only: ring-wait around the station so shared desks/beds do not stack on one point.
        if (UWorld* World = GetWorld())
        {
            if (USimulationFeatureRegistry* Registry = World->GetSubsystem<USimulationFeatureRegistry>())
            {
                if (Registry->IsFeatureEnabled(ESimulationFeature::DailyUrbanLife)
                    && FVector::Dist2D(GetActorLocation(), Station->GetActorLocation()) < 500.f)
                {
                    const int32 WaitIdx = FMath::Max(0, Station->GetQueueLength() - 1);
                    const float Angle = WaitIdx * 1.15f;
                    const float Radius = 130.f + static_cast<float>(WaitIdx % 4) * 35.f;
                    const FVector WaitLoc = Station->GetActorLocation()
                        + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.f);
                    SetActorLocation(FVector(WaitLoc.X, WaitLoc.Y, GetActorLocation().Z));
                }
            }
        }
        SetIdle(true);
        if (FallbackHandlerComponent)
        {
            FallbackHandlerComponent->OnEnteredStationQueue(this);
        }
    }
}

void ABoidsAB::OnStationReached()
{
    if (IsFleeing())
    {
        return;
    }

    if (!TargetStation)
    {
        return;
    }

    if (TargetStation->IsAgentActiveOccupant(this))
    {
        StopMovement();
        bIsQueuedAtStation = false;
        if (FallbackHandlerComponent)
        {
            FallbackHandlerComponent->OnLeftStationQueue(this);
        }

        TargetStation->NotifyOccupantReached(this);

        UE_LOG(LogTemp, Log,
            TEXT("[Boid %s] Arrived at station %s"),
            *GetName(),
            *TargetStation->GetName()
        );

        if (GStationNavDebug)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[StationNavDebug] %s OnStationReached | Agent=%s Station=%s | stop_reason=arrival_triggered → NotifyOccupantReached"),
                *GetName(),
                *GetActorLocation().ToString(),
                *TargetStation->GetActorLocation().ToString());
        }
    }
}

bool ABoidsAB::IsInteractingWithStation() const
{
    if (!TargetStation)
    {
        return false;
    }

    return TargetStation->IsAgentActiveOccupant(const_cast<ABoidsAB*>(this));
}

void ABoidsAB::OnStationInteractStart_Implementation()
{
    UE_LOG(LogTemp, Verbose,
        TEXT("[Boid %s] Station interaction started"),
        *GetName()
    );

    PlayStationInteractionAnimation();
}

void ABoidsAB::OnStationInteractEnd_Implementation()
{
    UE_LOG(LogTemp, Verbose,
        TEXT("[Boid %s] Station interaction ended"),
        *GetName()
    );

    ClearStationInteractionPose();
    StopStationInteractionAnimation();

    if (FallbackHandlerComponent)
    {
        FallbackHandlerComponent->OnLeftStationQueue(this);
    }
    TargetStation = nullptr;
    bIsQueuedAtStation = false;
    PendingStationDuration = -1.0f;
    // Allow flocking / next schedule move between rules (DUL and shared station path).
    SetIdle(false);

    const bool bCompletedNormally = bStationInteractionCompletedNormally;
    bStationInteractionCompletedNormally = false;

    if (bCompletedNormally && InteractionPipeline.Num() > 0)
    {
        AdvanceStationInteractionPipeline();
    }
    else if (!bAdvancingStationPipeline && InteractionPipeline.Num() > 0)
    {
        CancelStationInteractionPipeline();
    }
}

void ABoidsAB::NotifyStationInteractionCompleted()
{
    bStationInteractionCompletedNormally = true;
    OnStationInteractEnd();
}

bool ABoidsAB::BeginStationInteractionPipeline(const TArray<FStationInteraction>& Steps)
{
    if (IsFleeing())
    {
        return false;
    }

    if (Steps.Num() == 0)
    {
        return false;
    }

    CancelStationInteractionPipeline();

    InteractionPipeline = Steps;
    InteractionPipelineIndex = 0;

    UE_LOG(LogTemp, Log,
        TEXT("[Boid %s] Beginning station interaction pipeline (%d step(s))"),
        *GetName(),
        InteractionPipeline.Num());

    if (!ExecuteCurrentStationInteractionStep())
    {
        CancelStationInteractionPipeline();
        return false;
    }

    return true;
}

void ABoidsAB::CancelStationInteractionPipeline()
{
    InteractionPipeline.Empty();
    InteractionPipelineIndex = INDEX_NONE;
    bAdvancingStationPipeline = false;
    bStationInteractionCompletedNormally = false;
}

void ABoidsAB::AdvanceStationInteractionPipeline()
{
    if (InteractionPipeline.Num() == 0)
    {
        return;
    }

    ++InteractionPipelineIndex;
    if (InteractionPipelineIndex >= InteractionPipeline.Num())
    {
        UE_LOG(LogTemp, Log,
            TEXT("[Boid %s] Station interaction pipeline completed"),
            *GetName());
        CancelStationInteractionPipeline();
        return;
    }

    bAdvancingStationPipeline = true;
    const bool bOk = ExecuteCurrentStationInteractionStep();
    bAdvancingStationPipeline = false;

    if (!bOk)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[Boid %s] Pipeline step %d failed — cancelling remaining stages"),
            *GetName(),
            InteractionPipelineIndex);
        CancelStationInteractionPipeline();
        if (FallbackHandlerComponent)
        {
            FallbackHandlerComponent->NotifyScheduleFailure(this, EAgentFallbackFailureKind::StationUnavailable,
                TEXT("Pipeline stage resolve/move failed"));
        }
    }
}

bool ABoidsAB::ExecuteCurrentStationInteractionStep()
{
    if (!InteractionPipeline.IsValidIndex(InteractionPipelineIndex))
    {
        return false;
    }

    const FStationInteraction& Step = InteractionPipeline[InteractionPipelineIndex];
    AAgentStation* Station = ResolveStationForInteractionStep(Step);
    if (!Station)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[Boid %s] Could not resolve station for pipeline step %d (area='%s' type='%s')"),
            *GetName(),
            InteractionPipelineIndex,
            *Step.AreaTag,
            *Step.StationTypeName);
        if (FallbackHandlerComponent)
        {
            FallbackHandlerComponent->NotifyScheduleFailure(this, EAgentFallbackFailureKind::StationUnavailable,
                FString::Printf(TEXT("Step area '%s' type '%s'"), *Step.AreaTag, *Step.StationTypeName));
        }
        return false;
    }

    MoveToStation(Station, Step.Duration);
    return TargetStation == Station || bIsQueuedAtStation;
}

AAgentStation* ABoidsAB::ResolveStationForInteractionStep(const FStationInteraction& Step) const
{
    if (!GetWorld())
    {
        return nullptr;
    }

    // Optional: bind to a specific station instance by name.
    if (!Step.ExplicitStationName.IsNone())
    {
        TArray<AActor*> Found;
        UGameplayStatics::GetAllActorsOfClass(GetWorld(), AAgentStation::StaticClass(), Found);
        for (AActor* Actor : Found)
        {
            AAgentStation* Station = Cast<AAgentStation>(Actor);
            if (Station && Station->GetFName() == Step.ExplicitStationName)
            {
                return Station;
            }
        }
        TArray<AActor*> Tagged;
        UGameplayStatics::GetAllActorsWithTag(GetWorld(), Step.ExplicitStationName, Tagged);
        for (AActor* Actor : Tagged)
        {
            if (AAgentStation* Station = Cast<AAgentStation>(Actor))
            {
                return Station;
            }
        }
        return nullptr;
    }

    if (Step.AreaTag.IsEmpty())
    {
        return nullptr;
    }

    TArray<AActor*> FoundAreas;
    UGameplayStatics::GetAllActorsWithTag(GetWorld(), FName(*Step.AreaTag), FoundAreas);

    AAgentArea* Area = nullptr;
    for (AActor* Actor : FoundAreas)
    {
        Area = Cast<AAgentArea>(Actor);
        if (Area)
        {
            break;
        }
    }

    if (!Area || Area->IsAreaClosed())
    {
        return nullptr;
    }

    // Prefer a free station of the requested type; otherwise first match (may queue).
    if (!Step.StationTypeName.IsEmpty())
    {
        if (AAgentStation* Typed = Area->FindStationByIdentifier(Step.StationTypeName))
        {
            return Typed;
        }
    }

    if (AAgentStation* Free = Area->FindAvailableStation())
    {
        return Free;
    }

    const TArray<AAgentStation*>& Stations = Area->GetStations();
    return Stations.Num() > 0 ? Stations[0] : nullptr;
}

void ABoidsAB::PlayStationInteractionAnimation()
{
    if (!TargetStation || !TargetStation->HasInteractionAnimation())
    {
        return;
    }

    USkeletalMeshComponent* SkelMesh = GetMesh();
    if (!SkelMesh)
    {
        return;
    }

    UAnimInstance* AnimInstance = SkelMesh->GetAnimInstance();
    if (!AnimInstance)
    {
        return;
    }

    StopStationInteractionAnimation();

    const float BlendIn = TargetStation->GetInteractionAnimBlendIn();
    const float BlendOut = TargetStation->GetInteractionAnimBlendOut();
    const bool bLoop = TargetStation->ShouldLoopInteractionAnimation();

    switch (TargetStation->GetInteractionAnimType())
    {
    case EStationInteractionAnimType::Montage:
    {
        UAnimMontage* Montage = TargetStation->GetInteractionMontage();
        if (!Montage)
        {
            return;
        }

        AnimInstance->Montage_Play(Montage, 1.f, EMontagePlayReturnType::MontageLength, 0.f, true);
        if (bLoop)
        {
            const FName CurrentSection = AnimInstance->Montage_GetCurrentSection(Montage);
            if (!CurrentSection.IsNone())
            {
                AnimInstance->Montage_SetNextSection(CurrentSection, CurrentSection, Montage);
            }
        }
        ActiveStationInteractionMontage = Montage;
        break;
    }
    case EStationInteractionAnimType::Sequence:
    {
        UAnimSequence* Sequence = TargetStation->GetInteractionSequence();
        if (!Sequence)
        {
            return;
        }

        const FName SlotName = TargetStation->GetInteractionAnimSlotName();
        const int32 LoopCount = bLoop ? 1000 : 1;
        UAnimMontage* DynamicMontage = AnimInstance->PlaySlotAnimationAsDynamicMontage(
            Sequence,
            SlotName,
            BlendIn,
            BlendOut,
            1.f,
            LoopCount);
        ActiveStationInteractionMontage = DynamicMontage;
        break;
    }
    default:
        break;
    }

    if (ActiveStationInteractionMontage)
    {
        UE_LOG(LogTemp, Verbose,
            TEXT("[Boid %s] Playing station interaction animation from %s"),
            *GetName(),
            *TargetStation->GetName());
    }
}

void ABoidsAB::StopStationInteractionAnimation()
{
    if (!ActiveStationInteractionMontage)
    {
        return;
    }

    if (USkeletalMeshComponent* SkelMesh = GetMesh())
    {
        if (UAnimInstance* AnimInstance = SkelMesh->GetAnimInstance())
        {
            const float BlendOut = TargetStation
                ? TargetStation->GetInteractionAnimBlendOut()
                : 0.25f;
            AnimInstance->Montage_Stop(BlendOut, ActiveStationInteractionMontage);
        }
    }

    ActiveStationInteractionMontage = nullptr;
}

void ABoidsAB::OnStationBecameAvailable()
{
    // Cache before any movement/side effects; TargetStation must still point at the admitting station.
    AAgentStation* Station = TargetStation;
    if (!IsValid(Station))
    {
        bIsQueuedAtStation = false;
        TargetStation = nullptr;
        return;
    }

    if (IsFleeing())
    {
        CancelStationInteractionPipeline();
        Station->LeaveStation(this);
        TargetStation = nullptr;
        bIsQueuedAtStation = false;
        return;
    }

    bIsQueuedAtStation = false;
    if (FallbackHandlerComponent)
    {
        FallbackHandlerComponent->OnLeftStationQueue(this);
    }

    StartMoveTo(Station->GetActorLocation());

    UE_LOG(LogTemp, Log,
        TEXT("[Boid %s] Station became available, moving to %s"),
        *GetName(),
        *Station->GetName()
    );
}

void ABoidsAB::LeaveCurrentStation()
{
    CancelStationInteractionPipeline();
    ClearStationInteractionPose();

    if (TargetStation)
    {
        if (FallbackHandlerComponent)
        {
            FallbackHandlerComponent->OnLeftStationQueue(this);
        }
        TargetStation->LeaveStation(this);
        TargetStation = nullptr;
        bIsQueuedAtStation = false;
    }
}

void ABoidsAB::BeginStationInteractionPose(AAgentStation* Station)
{
    if (!Station || !Station->HasInteractionPositioning())
    {
        if (Station)
        {
            Station->BeginInteractionForAgent(this);
        }
        return;
    }

    ClearStationInteractionPose();

    StationPoseStation = Station;
    ActiveStationPoseMode = Station->GetInteractionPositionMode();
    StationPoseTarget = Station->GetWorldInteractionTransform();
    StationPoseStart = GetActorTransform();
    StationPoseElapsed = 0.f;
    StationPoseDuration = FMath::Max(0.01f, Station->GetInteractionInterpolateSeconds());
    bStationPoseAlignRotation = Station->ShouldAlignInteractionRotation();
    bStationInteractionPosing = true;

    StopMovement();

    if (ActiveStationPoseMode == EStationInteractionPositionMode::SnapToTransform)
    {
        ApplyStationInteractionTransform(StationPoseTarget, bStationPoseAlignRotation);
        FinishStationInteractionPose();
        return;
    }

    // Non-snap modes lerp in TickStationInteractionPose; anything else finishes immediately.
    if (ActiveStationPoseMode != EStationInteractionPositionMode::InterpolateToTransform)
    {
        FinishStationInteractionPose();
    }
}

void ABoidsAB::ApplyStationInteractionTransform(const FTransform& WorldXform, bool bAlignRotation)
{
    const FVector Loc = WorldXform.GetLocation();
    if (bAlignRotation)
    {
        SetActorLocationAndRotation(Loc, WorldXform.GetRotation().Rotator(), false, nullptr, ETeleportType::TeleportPhysics);
    }
    else
    {
        SetActorLocation(Loc, false, nullptr, ETeleportType::TeleportPhysics);
    }

    if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
    {
        MoveComp->StopMovementImmediately();
        MoveComp->Velocity = FVector::ZeroVector;
    }
}

void ABoidsAB::ClearStationInteractionPose()
{
    bStationInteractionPosing = false;
    ActiveStationPoseMode = EStationInteractionPositionMode::None;
    StationPoseElapsed = 0.f;
    StationPoseDuration = 0.35f;
    bStationPoseAlignRotation = true;
    StationPoseStation.Reset();
    StationPoseStart = FTransform::Identity;
    StationPoseTarget = FTransform::Identity;
}

void ABoidsAB::FinishStationInteractionPose()
{
    AAgentStation* Station = StationPoseStation.Get();
    ClearStationInteractionPose();

    if (Station && Station->IsAgentActiveOccupant(this))
    {
        Station->BeginInteractionForAgent(this);
    }
}

void ABoidsAB::TickStationInteractionPose(float DeltaTime)
{
    if (!bStationInteractionPosing)
    {
        return;
    }

    AAgentStation* Station = StationPoseStation.Get();
    if (!Station || !Station->IsAgentActiveOccupant(this))
    {
        ClearStationInteractionPose();
        return;
    }

    StationPoseTarget = Station->GetWorldInteractionTransform();
    bStationPoseAlignRotation = Station->ShouldAlignInteractionRotation();

    StationPoseElapsed += DeltaTime;
    const float Alpha = FMath::Clamp(StationPoseElapsed / StationPoseDuration, 0.f, 1.f);

    const FVector Loc = FMath::Lerp(StationPoseStart.GetLocation(), StationPoseTarget.GetLocation(), Alpha);
    FTransform Blended(StationPoseStart);
    Blended.SetLocation(Loc);
    if (bStationPoseAlignRotation)
    {
        const FQuat Rot = FQuat::Slerp(
            StationPoseStart.GetRotation(),
            StationPoseTarget.GetRotation(),
            Alpha).GetNormalized();
        Blended.SetRotation(Rot);
    }
    else
    {
        Blended.SetRotation(GetActorQuat());
    }

    ApplyStationInteractionTransform(Blended, bStationPoseAlignRotation);

    if (Alpha >= 1.f)
    {
        ApplyStationInteractionTransform(StationPoseTarget, bStationPoseAlignRotation);
        FinishStationInteractionPose();
    }
}

#pragma endregion
