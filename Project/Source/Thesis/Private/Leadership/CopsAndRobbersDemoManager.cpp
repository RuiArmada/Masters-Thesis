#include "Leadership/CopsAndRobbersDemoManager.h"
#include "BoidsAB.h"
#include "AI/AgentCrimeRoleComponent.h"
#include "Simulation/SimulationFeatureRegistry.h"
#include "Simulation/SimulationAnalytics.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "HAL/PlatformFileManager.h"

ACopsAndRobbersDemoManager::ACopsAndRobbersDemoManager()
{
	PrimaryActorTick.bCanEverTick = false;

	// Default demo: two police, two robbers; remaining agents stay Civilian.
	FCrimeRoleAssignment P0;
	P0.AgentId = FName(TEXT("Agent_5"));
	P0.Role = ECrimeRole::Police;
	RoleAssignments.Add(P0);

	FCrimeRoleAssignment P1;
	P1.AgentId = FName(TEXT("Agent_6"));
	P1.Role = ECrimeRole::Police;
	RoleAssignments.Add(P1);

	FCrimeRoleAssignment R0;
	R0.AgentId = FName(TEXT("Agent_7"));
	R0.Role = ECrimeRole::Robber;
	RoleAssignments.Add(R0);

	FCrimeRoleAssignment R1;
	R1.AgentId = FName(TEXT("Agent_8"));
	R1.Role = ECrimeRole::Robber;
	RoleAssignments.Add(R1);
}

void ACopsAndRobbersDemoManager::BeginPlay()
{
	Super::BeginPlay();

	if (bAutoAssignOnBeginPlay && GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimer(
			InitDelayHandle,
			this,
			&ACopsAndRobbersDemoManager::DelayedInitialize,
			0.7f,
			false);
	}
}

void ACopsAndRobbersDemoManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bExportAnalyticsOnEndPlay)
	{
		ExportAnalytics();
	}
	Super::EndPlay(EndPlayReason);
}

void ACopsAndRobbersDemoManager::DelayedInitialize()
{
	ApplyAssignments();
}

ABoidsAB* ACopsAndRobbersDemoManager::FindBoidById(FName AgentId) const
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

void ACopsAndRobbersDemoManager::ApplyAssignments()
{
	USimulationFeatureRegistry* Registry = GetWorld()
		? GetWorld()->GetSubsystem<USimulationFeatureRegistry>()
		: nullptr;

	if (!Registry || !Registry->IsFeatureEnabled(ESimulationFeature::CopsAndRobbers))
	{
		UE_LOG(LogTemp, Log, TEXT("[CopsAndRobbers] Feature disabled — assignments skipped"));
		return;
	}

	if (USimulationAnalytics* Analytics = GetWorld()->GetSubsystem<USimulationAnalytics>())
	{
		Analytics->BeginCopsAndRobbersSession();
	}

	int32 Assigned = 0;
	for (const FCrimeRoleAssignment& Entry : RoleAssignments)
	{
		ABoidsAB* Boid = FindBoidById(Entry.AgentId);
		if (!Boid || !Boid->GetCrimeRoleComponent())
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[CopsAndRobbers] Agent '%s' not found for role assignment"),
				*Entry.AgentId.ToString());
			continue;
		}

		Boid->GetCrimeRoleComponent()->SetRole(Entry.Role);
		++Assigned;

		const TCHAR* RoleName = TEXT("Civilian");
		if (Entry.Role == ECrimeRole::Police)
		{
			RoleName = TEXT("Police");
		}
		else if (Entry.Role == ECrimeRole::Robber)
		{
			RoleName = TEXT("Robber");
		}

		UE_LOG(LogTemp, Log, TEXT("[CopsAndRobbers] %s -> %s"),
			*Entry.AgentId.ToString(), RoleName);
	}

	UE_LOG(LogTemp, Log, TEXT("[CopsAndRobbers] Assigned %d role(s)"), Assigned);
}

void ACopsAndRobbersDemoManager::ExportAnalytics()
{
	USimulationFeatureRegistry* Registry = GetWorld()
		? GetWorld()->GetSubsystem<USimulationFeatureRegistry>()
		: nullptr;
	if (!Registry || !Registry->IsFeatureEnabled(ESimulationFeature::CopsAndRobbers))
	{
		return;
	}

	USimulationAnalytics* Analytics = GetWorld()
		? GetWorld()->GetSubsystem<USimulationAnalytics>()
		: nullptr;
	if (!Analytics)
	{
		return;
	}

	Analytics->EndCopsAndRobbersSession();

	const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Analytics"));
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.CreateDirectoryTree(*Dir);

	const FString Stamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	const FString Path = FPaths::Combine(Dir, FString::Printf(TEXT("CopsAndRobbers_%s.csv"), *Stamp));
	if (Analytics->ExportCopsAndRobbersSessionToCSV(Path))
	{
		UE_LOG(LogTemp, Log, TEXT("[CopsAndRobbers] Exported analytics to %s"), *Path);
	}
}
