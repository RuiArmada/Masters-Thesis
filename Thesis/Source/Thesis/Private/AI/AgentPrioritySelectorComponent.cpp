#include "AI/AgentPrioritySelectorComponent.h"
#include "AI/AgentNeedsComponent.h"
#include "AI/AgentFallbackHandlerComponent.h"
#include "AI/AgentLeadershipComponent.h"
#include "AI/AgentCrimeRoleComponent.h"
#include "AI/AgentAIDebug.h"
#include "Actions/AgentAction.h"
#include "BoidsAB.h"
#include "Simulation/SimulationFeatureRegistry.h"
#include "Simulation/SimulationTypes.h"

UAgentPrioritySelectorComponent::UAgentPrioritySelectorComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

EAgentPriorityLayer UAgentPrioritySelectorComponent::EvaluateLayer(ABoidsAB* Boid) const
{
	if (!Boid)
	{
		LastEvaluatedLayer = EAgentPriorityLayer::IdleFlock;
		LastEvaluationReason = TEXT("No boid");
		return LastEvaluatedLayer;
	}

	if (Boid->ShouldIgnoreScheduleDuringEvacuation())
	{
		LastEvaluatedLayer = EAgentPriorityLayer::PanicEmergency;
		if (Boid->IsCasualty())
		{
			LastEvaluationReason = TEXT("Evacuation casualty");
		}
		else if (Boid->IsInEvacuationSuccessState())
		{
			LastEvaluationReason = TEXT("Evacuation success");
		}
		else if (Boid->IsAwaitingEvacuationReaction())
		{
			LastEvaluationReason = TEXT("Awaiting evacuation reaction");
		}
		else if (Boid->IsSafeAtExit())
		{
			LastEvaluationReason = TEXT("Safe at exit");
		}
		else
		{
			LastEvaluationReason = TEXT("Fleeing or exiting");
		}
		return LastEvaluatedLayer;
	}

	const UAgentNeedsComponent* Needs = Boid->GetNeedsComponent();
	if (Needs && (Needs->IsHungry() || Needs->IsExhausted()))
	{
		LastEvaluatedLayer = EAgentPriorityLayer::CriticalNeeds;
		LastEvaluationReason = Needs->IsHungry() && Needs->IsExhausted()
			                           ? TEXT("Hungry and exhausted")
			                           : (Needs->IsHungry() ? TEXT("Hungry") : TEXT("Exhausted"));
		return LastEvaluatedLayer;
	}

	if (const UAgentCrimeRoleComponent* Crime = Boid->GetCrimeRoleComponent())
	{
		if (Crime->WantsCrimeResponse())
		{
			LastEvaluatedLayer = EAgentPriorityLayer::CrimeResponse;
			switch (Crime->GetBehaviorState())
			{
			case ECrimeBehaviorState::Pursuing:
				LastEvaluationReason = TEXT("Police pursuing robber");
				break;
			case ECrimeBehaviorState::Fleeing:
				LastEvaluationReason = TEXT("Robber fleeing police");
				break;
			case ECrimeBehaviorState::Crime:
				LastEvaluationReason = TEXT("Robber in crime");
				break;
			case ECrimeBehaviorState::Avoiding:
				LastEvaluationReason = TEXT("Civilian avoiding robber");
				break;
			case ECrimeBehaviorState::Captured:
				LastEvaluationReason = TEXT("Robber captured");
				break;
			default:
				LastEvaluationReason = TEXT("Crime response");
				break;
			}
			return LastEvaluatedLayer;
		}
	}

	if (const UAgentLeadershipComponent* Leadership = Boid->GetLeadershipComponent())
	{
		if (Leadership->IsActivelyFollowing())
		{
			LastEvaluatedLayer = EAgentPriorityLayer::Leadership;
			LastEvaluationReason = TEXT("Following leader");
			return LastEvaluatedLayer;
		}
	}

	if (Boid->IsMoving() || Boid->GetTargetStation() != nullptr)
	{
		LastEvaluatedLayer = EAgentPriorityLayer::ScheduledTasks;
		LastEvaluationReason = Boid->IsMoving() ? TEXT("Scheduled / directed move") : TEXT("Station target");
		return LastEvaluatedLayer;
	}

	LastEvaluatedLayer = EAgentPriorityLayer::IdleFlock;
	LastEvaluationReason = Boid->IsInfrastructurePlanningActive()
		                           ? TEXT("Schedule wait")
		                           : TEXT("Idle or flocking");
	return LastEvaluatedLayer;
}

bool UAgentPrioritySelectorComponent::ShouldDeferScheduleCommands(ABoidsAB* Boid) const
{
	if (!Boid)
	{
		return false;
	}

	if (Boid->ShouldIgnoreScheduleDuringEvacuation())
	{
		return false;
	}

	if (bDeferScheduleWhenCriticalNeeds)
	{
		const UAgentNeedsComponent* Needs = Boid->GetNeedsComponent();
		if (Needs && (Needs->IsHungry() || Needs->IsExhausted()))
		{
			return true;
		}
	}

	if (const UAgentCrimeRoleComponent* Crime = Boid->GetCrimeRoleComponent())
	{
		if (Crime->WantsCrimeResponse())
		{
			return true;
		}
	}

	if (const UWorld* World = Boid->GetWorld())
	{
		if (const USimulationFeatureRegistry* Registry = World->GetSubsystem<USimulationFeatureRegistry>())
		{
			if (Registry->IsFeatureEnabled(ESimulationFeature::Leadership)
				&& Registry->GetLeadershipSettings().bLeadershipOverridesSchedule)
			{
				if (const UAgentLeadershipComponent* Leadership = Boid->GetLeadershipComponent())
				{
					if (Leadership->IsActivelyFollowing())
					{
						return true;
					}
				}
			}
		}
	}

	return false;
}

void UAgentPrioritySelectorComponent::TickDecisionLayer(float DeltaTime, ABoidsAB* Boid)
{
	if (!Boid)
	{
		return;
	}

	if (UAgentCrimeRoleComponent* Crime = Boid->GetCrimeRoleComponent())
	{
		Crime->UpdateCrimeLogic(DeltaTime, Boid);
	}

	EvaluateLayer(Boid);

#if !UE_BUILD_SHIPPING
	DebugLogThrottle += DeltaTime;
	if (DebugLogThrottle >= 2.f)
	{
		DebugLogThrottle = 0.f;
		UpdateDebugLog(Boid);
	}
#endif

	if (LastEvaluatedLayer == EAgentPriorityLayer::CrimeResponse && bApplyCrimeMovement)
	{
		ApplyCrimeMovement(DeltaTime, Boid);
		return;
	}

	if (LastEvaluatedLayer == EAgentPriorityLayer::Leadership && bApplyLeadershipMovement)
	{
		ApplyLeadershipMovement(DeltaTime, Boid);
		return;
	}

	if (LastEvaluatedLayer == EAgentPriorityLayer::IdleFlock)
	{
		ApplyDailyLifeIdleMill(DeltaTime, Boid);
		return;
	}

	if (LastEvaluatedLayer != EAgentPriorityLayer::CriticalNeeds || !bApplyCriticalNeedMovement)
	{
		return;
	}

	UAgentNeedsComponent* Needs = Boid->GetNeedsComponent();
	if (!Needs)
	{
		return;
	}

	if (UAgentFallbackHandlerComponent* Fallback = Boid->GetFallbackHandler())
	{
		if (Fallback->ShouldSuppressNeedMovement())
		{
			return;
		}
	}

	if (Boid->IsMovementLockedAfterIncident())
	{
		return;
	}

	FVector Dest = FVector::ZeroVector;
	bool bHaveDest = false;
	ENeedNavIntent Intent = ENeedNavIntent::None;

	if (Needs->IsHungry())
	{
		if (Boid->IsNearAnyFoodLocation(Needs->GetRecoveryRadius()))
		{
			return;
		}

		FVector Food = Boid->GetNearestFoodLocation();
		if (!Food.IsZero())
		{
			if (const UWorld* World = Boid->GetWorld())
			{
				if (const USimulationFeatureRegistry* Registry = World->GetSubsystem<USimulationFeatureRegistry>())
				{
					if (Registry->IsFeatureEnabled(ESimulationFeature::DailyUrbanLife))
					{
						Food += Boid->GetDailyLifeDestinationSpread(2);
					}
				}
			}
			Dest = Food;
			bHaveDest = true;
			Intent = ENeedNavIntent::CriticalFood;
		}
	}

	if (!bHaveDest && Needs->IsExhausted())
	{
		const FVector Home = Boid->HomeLocation;
		if (!Home.IsZero())
		{
			if (FVector::Dist(Boid->GetActorLocation(), Home) <= Needs->GetRecoveryRadius())
			{
				return;
			}

			Dest = Home;
			bHaveDest = true;
			Intent = ENeedNavIntent::CriticalHome;
		}
	}

	if (!bHaveDest)
	{
		return;
	}

	const FVector CurrentTarget = Boid->GetCurrentTargetLocation();
	if (Boid->IsMoving() && FVector::DistSquared(CurrentTarget, Dest) < FMath::Square(25.f))
	{
		return;
	}

	Boid->LeaveCurrentStation();
	Boid->StartMoveTo(Dest, Intent);
}

void UAgentPrioritySelectorComponent::ApplyCrimeMovement(float DeltaTime, ABoidsAB* Boid)
{
	UAgentCrimeRoleComponent* Crime = Boid->GetCrimeRoleComponent();
	if (!Crime)
	{
		return;
	}

	if (Boid->IsMovementLockedAfterIncident())
	{
		return;
	}

	const ECrimeBehaviorState State = Crime->GetBehaviorState();
	if (State == ECrimeBehaviorState::Crime || State == ECrimeBehaviorState::Captured)
	{
		if (Boid->IsMoving())
		{
			Boid->StopMovement();
			Boid->SetIdle(true);
		}
		return;
	}

	const USimulationFeatureRegistry* Registry = Boid->GetWorld()
		? Boid->GetWorld()->GetSubsystem<USimulationFeatureRegistry>()
		: nullptr;
	const float Interval = Registry
		? Registry->GetCopsAndRobbersSettings().PursuitUpdateInterval
		: 0.4f;

	if (Crime->ConsumeUpdateTimer(DeltaTime) > 0.f)
	{
		return;
	}
	Crime->ResetUpdateTimer(Interval);

	FVector Dest = FVector::ZeroVector;
	if (!Crime->ComputeCrimeDestination(Dest))
	{
		return;
	}

	const FVector CurrentTarget = Boid->GetCurrentTargetLocation();
	if (Boid->IsMoving() && FVector::DistSquared(CurrentTarget, Dest) < FMath::Square(40.f))
	{
		return;
	}

	Boid->LeaveCurrentStation();
	Boid->SetIdle(false);
	Boid->StartMoveTo(Dest, ENeedNavIntent::None);
}

void UAgentPrioritySelectorComponent::ApplyLeadershipMovement(float DeltaTime, ABoidsAB* Boid)
{
	UAgentLeadershipComponent* Leadership = Boid->GetLeadershipComponent();
	if (!Leadership)
	{
		return;
	}

	if (Boid->IsMovementLockedAfterIncident())
	{
		return;
	}

	if (!Leadership->ResolveLeaderIfNeeded())
	{
		Leadership->ClearLeadership();
		return;
	}

	const USimulationFeatureRegistry* Registry = Boid->GetWorld()
		? Boid->GetWorld()->GetSubsystem<USimulationFeatureRegistry>()
		: nullptr;
	const float Interval = Registry
		? Registry->GetLeadershipSettings().FormationUpdateInterval
		: 0.5f;

	if (Leadership->ConsumeUpdateTimer(DeltaTime) > 0.f)
	{
		return;
	}
	Leadership->ResetUpdateTimer(Interval);

	FVector Dest = FVector::ZeroVector;
	if (!Leadership->ComputeFollowDestination(Dest))
	{
		return;
	}

	const float FollowDist = Leadership->GetEffectiveFollowDistance();
	const float ArrivalRadius = FMath::Max(60.f, FollowDist * 0.45f);
	if (FVector::Dist2D(Boid->GetActorLocation(), Dest) <= ArrivalRadius)
	{
		if (Boid->IsMoving())
		{
			Boid->StopMovement();
			Boid->SetIdle(true);
		}
		return;
	}

	const FVector CurrentTarget = Boid->GetCurrentTargetLocation();
	if (Boid->IsMoving() && FVector::DistSquared(CurrentTarget, Dest) < FMath::Square(40.f))
	{
		return;
	}

	Boid->LeaveCurrentStation();
	Boid->SetIdle(false);
	Boid->StartMoveTo(Dest, ENeedNavIntent::None);
}

void UAgentPrioritySelectorComponent::ApplyDailyLifeIdleMill(float DeltaTime, ABoidsAB* Boid)
{
	if (!Boid || DeltaTime <= 0.f)
	{
		return;
	}

	const UWorld* World = Boid->GetWorld();
	if (!World)
	{
		return;
	}

	const USimulationFeatureRegistry* Registry = World->GetSubsystem<USimulationFeatureRegistry>();
	if (!Registry || !Registry->IsFeatureEnabled(ESimulationFeature::DailyUrbanLife))
	{
		return;
	}

	// Do not interfere with mall / infrastructure / evacuation / crime paths.
	if (Registry->GetCurrentScenario() != ESimulationScenario::DailyUrbanLife)
	{
		return;
	}

	if (Boid->IsFleeing() || Boid->IsQueuedAtStation() || Boid->IsInteractingWithStation()
		|| Boid->HasActiveStationInteractionPipeline()
		|| Boid->IsMovementLockedAfterIncident())
	{
		return;
	}

	if (const UAgentAction* Active = Boid->GetActiveScheduleAction())
	{
		if (!Active->IsTerminal())
		{
			return;
		}
	}

	// Clear leftover idle (e.g. home teleport) once the agent is free between rules.
	if (Boid->IsIdle())
	{
		Boid->SetIdle(false);
	}

	const FVector Home = Boid->HomeLocation;
	if (Home.IsZero())
	{
		return;
	}

	// Only mill in the home neighborhood after end-of-day return (avoids pulling agents off work/plaza).
	constexpr float HomeMillRadius = 750.f;
	if (FVector::Dist2D(Boid->GetActorLocation(), Home) > HomeMillRadius)
	{
		return;
	}

	if (Boid->IsMoving())
	{
		return;
	}

	DailyLifeIdleMillTimer += DeltaTime;
	if (DailyLifeIdleMillTimer < DailyLifeIdleMillInterval)
	{
		return;
	}
	DailyLifeIdleMillTimer = 0.f;

	FVector Dest = Home + Boid->GetDailyLifeDestinationSpread(FMath::RandRange(1, 8));
	Boid->StartMoveTo(Dest, ENeedNavIntent::None);
}

void UAgentPrioritySelectorComponent::UpdateDebugLog(ABoidsAB* Boid) const
{
	THESIS_AGENT_AI_LOG(LogTemp, Log,
	                    TEXT("[AgentAI] %s | Layer=%d Reason=%s | Hungry=%d Exhausted=%d Moving=%d Idle=%d"),
	                    Boid ? *Boid->GetName() : TEXT("NULL"),
	                    static_cast<int32>(LastEvaluatedLayer),
	                    *LastEvaluationReason,
	                    Boid && Boid->GetNeedsComponent() ? Boid->GetNeedsComponent()->IsHungry() : 0,
	                    Boid && Boid->GetNeedsComponent() ? Boid->GetNeedsComponent()->IsExhausted() : 0,
	                    Boid && Boid->IsMoving() ? 1 : 0,
	                    Boid && Boid->IsIdle() ? 1 : 0);
}
