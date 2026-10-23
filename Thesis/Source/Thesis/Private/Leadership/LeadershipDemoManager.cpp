#include "Leadership/LeadershipDemoManager.h"
#include "BoidsAB.h"
#include "AI/AgentLeadershipComponent.h"
#include "Simulation/SimulationFeatureRegistry.h"
#include "Simulation/SimulationAnalytics.h"
#include "Kismet/GameplayStatics.h"

ALeadershipDemoManager::ALeadershipDemoManager()
{
	PrimaryActorTick.bCanEverTick = false;

	// Default family / dog / police demo groups (AgentIds must exist in the level).
	FLeadershipGroupAssignment Family;
	Family.GroupLabel = TEXT("Family");
	Family.LeaderAgentId = FName(TEXT("Agent_0"));
	Family.FollowerAgentIds = { FName(TEXT("Agent_1")), FName(TEXT("Agent_2")) };
	Groups.Add(Family);

	FLeadershipGroupAssignment Dog;
	Dog.GroupLabel = TEXT("Dog");
	Dog.LeaderAgentId = FName(TEXT("Agent_3"));
	Dog.FollowerAgentIds = { FName(TEXT("Agent_4")) };
	Groups.Add(Dog);

	FLeadershipGroupAssignment Police;
	Police.GroupLabel = TEXT("Police");
	Police.LeaderAgentId = FName(TEXT("Agent_5"));
	Police.FollowerAgentIds = {
		FName(TEXT("Agent_6")),
		FName(TEXT("Agent_7")),
		FName(TEXT("Agent_8"))
	};
	Groups.Add(Police);
}

void ALeadershipDemoManager::BeginPlay()
{
	Super::BeginPlay();

	if (bAutoAssignOnBeginPlay && GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimer(
			InitDelayHandle,
			this,
			&ALeadershipDemoManager::DelayedInitialize,
			0.6f,
			false);
	}
}

void ALeadershipDemoManager::DelayedInitialize()
{
	ApplyAssignments();
}

ABoidsAB* ALeadershipDemoManager::FindBoidById(FName AgentId) const
{
	if (AgentId.IsNone() || !GetWorld())
	{
		return nullptr;
	}

	TArray<AActor*> Found;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ABoidsAB::StaticClass(), Found);
	for (AActor* Actor : Found)
	{
		ABoidsAB* Boid = Cast<ABoidsAB>(Actor);
		if (Boid && Boid->AgentId == AgentId)
		{
			return Boid;
		}
	}
	return nullptr;
}

void ALeadershipDemoManager::ApplyAssignments()
{
	USimulationFeatureRegistry* Registry = GetWorld()
		? GetWorld()->GetSubsystem<USimulationFeatureRegistry>()
		: nullptr;

	if (!Registry || !Registry->IsFeatureEnabled(ESimulationFeature::Leadership))
	{
		UE_LOG(LogTemp, Log, TEXT("[LeadershipDemo] Feature disabled — assignments skipped"));
		return;
	}

	USimulationAnalytics* Analytics = GetWorld()->GetSubsystem<USimulationAnalytics>();
	int32 Linked = 0;

	for (const FLeadershipGroupAssignment& Group : Groups)
	{
		ABoidsAB* Leader = FindBoidById(Group.LeaderAgentId);
		if (!Leader || !Leader->GetLeadershipComponent())
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[LeadershipDemo] Leader '%s' not found for group '%s'"),
				*Group.LeaderAgentId.ToString(),
				*Group.GroupLabel);
			continue;
		}

		Leader->GetLeadershipComponent()->BecomeLeader();

		for (const FName& FollowerId : Group.FollowerAgentIds)
		{
			ABoidsAB* Follower = FindBoidById(FollowerId);
			if (!Follower || !Follower->GetLeadershipComponent())
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[LeadershipDemo] Follower '%s' not found for group '%s'"),
					*FollowerId.ToString(),
					*Group.GroupLabel);
				continue;
			}

			if (Follower->GetLeadershipComponent()->AssignLeader(Leader))
			{
				++Linked;
				UE_LOG(LogTemp, Log,
					TEXT("[LeadershipDemo] %s: %s follows %s"),
					*Group.GroupLabel,
					*FollowerId.ToString(),
					*Group.LeaderAgentId.ToString());
			}
		}
	}

	if (Analytics)
	{
		Analytics->RecordLeadershipEvent(FString::Printf(TEXT("DemoLinked_%d"), Linked));
	}

	UE_LOG(LogTemp, Log, TEXT("[LeadershipDemo] Linked %d follower(s) across %d group(s)"),
		Linked, Groups.Num());
}
