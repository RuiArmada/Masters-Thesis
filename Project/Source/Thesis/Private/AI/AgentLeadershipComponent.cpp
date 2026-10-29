#include "AI/AgentLeadershipComponent.h"
#include "BoidsAB.h"
#include "Simulation/SimulationFeatureRegistry.h"
#include "Kismet/GameplayStatics.h"

UAgentLeadershipComponent::UAgentLeadershipComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UAgentLeadershipComponent::SetRole(ELeadershipRole NewRole)
{
	Role = NewRole;
	if (Role != ELeadershipRole::Follower)
	{
		AssignedLeader = nullptr;
		AssignedLeaderId = NAME_None;
	}
}

bool UAgentLeadershipComponent::AssignLeader(ABoidsAB* Leader)
{
	ABoidsAB* OwnerBoid = Cast<ABoidsAB>(GetOwner());
	if (!Leader || Leader == OwnerBoid)
	{
		return false;
	}

	AssignedLeader = Leader;
	AssignedLeaderId = Leader->AgentId;
	Role = ELeadershipRole::Follower;
	UpdateTimer = 0.f;
	return true;
}

void UAgentLeadershipComponent::AssignLeaderById(FName LeaderAgentId)
{
	AssignedLeaderId = LeaderAgentId;
	AssignedLeader = nullptr;
	Role = ELeadershipRole::Follower;
	UpdateTimer = 0.f;
	ResolveLeaderIfNeeded();
}

void UAgentLeadershipComponent::ClearLeadership()
{
	Role = ELeadershipRole::None;
	AssignedLeader = nullptr;
	AssignedLeaderId = NAME_None;
	UpdateTimer = 0.f;
}

void UAgentLeadershipComponent::BecomeLeader()
{
	AssignedLeader = nullptr;
	AssignedLeaderId = NAME_None;
	Role = ELeadershipRole::Leader;
	UpdateTimer = 0.f;
}

float UAgentLeadershipComponent::GetEffectiveFollowDistance() const
{
	if (FollowDistanceOverride >= 0.f)
	{
		return FollowDistanceOverride;
	}

	if (const UWorld* World = GetWorld())
	{
		if (const USimulationFeatureRegistry* Registry = World->GetSubsystem<USimulationFeatureRegistry>())
		{
			return Registry->GetLeadershipSettings().DefaultFollowDistance;
		}
	}
	return 200.f;
}

bool UAgentLeadershipComponent::ResolveLeaderIfNeeded()
{
	if (AssignedLeader.IsValid())
	{
		return true;
	}

	if (AssignedLeaderId.IsNone() || !GetWorld())
	{
		return false;
	}

	TArray<AActor*> Boids;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ABoidsAB::StaticClass(), Boids);
	for (AActor* Actor : Boids)
	{
		ABoidsAB* Candidate = Cast<ABoidsAB>(Actor);
		if (Candidate && Candidate->AgentId == AssignedLeaderId && Candidate != GetOwner())
		{
			AssignedLeader = Candidate;
			return true;
		}
	}
	return false;
}

bool UAgentLeadershipComponent::IsActivelyFollowing() const
{
	if (!GetWorld())
	{
		return false;
	}

	const USimulationFeatureRegistry* Registry = GetWorld()->GetSubsystem<USimulationFeatureRegistry>();
	if (!Registry || !Registry->IsFeatureEnabled(ESimulationFeature::Leadership))
	{
		return false;
	}

	if (Role != ELeadershipRole::Follower)
	{
		return false;
	}

	return AssignedLeader.IsValid() || !AssignedLeaderId.IsNone();
}

bool UAgentLeadershipComponent::ComputeFollowDestination(FVector& OutDestination) const
{
	OutDestination = FVector::ZeroVector;
	if (!AssignedLeader.IsValid())
	{
		return false;
	}

	const ABoidsAB* Leader = AssignedLeader.Get();
	const float Distance = GetEffectiveFollowDistance();

	FVector Forward = Leader->GetActorForwardVector();
	Forward.Z = 0.f;
	if (Forward.IsNearlyZero())
	{
		Forward = FVector::ForwardVector;
	}
	else
	{
		Forward.Normalize();
	}

	OutDestination = Leader->GetActorLocation() - Forward * Distance;
	OutDestination.Z = Leader->GetActorLocation().Z;
	return true;
}

float UAgentLeadershipComponent::ConsumeUpdateTimer(float DeltaTime)
{
	UpdateTimer -= DeltaTime;
	return UpdateTimer;
}

void UAgentLeadershipComponent::ResetUpdateTimer(float Interval)
{
	UpdateTimer = FMath::Max(0.05f, Interval);
}
