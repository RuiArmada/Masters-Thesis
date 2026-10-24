#include "Emergency/EmergencyManager.h"
#include "IncidentManager.h"
#include "BoidsAB.h"
#include "Simulation/SimulationFeatureRegistry.h"
#include "Simulation/SimulationAnalytics.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"

AEmergencyManager::AEmergencyManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
}

void AEmergencyManager::BeginPlay()
{
	Super::BeginPlay();
	ResolveIncidentManager();
}

void AEmergencyManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bEvacuationTimerActive || !GetWorld())
	{
		return;
	}

	const double Now = GetWorld()->GetTimeSeconds();
	TimerRemainingSeconds = FMath::Max(0.f, static_cast<float>(EvacuationDeadlineWorldTime - Now));
	OnEvacuationTimerUpdated.Broadcast(TimerRemainingSeconds);
}

void AEmergencyManager::ResolveIncidentManager()
{
	if (IncidentManagerRef)
	{
		return;
	}

	IncidentManagerRef = Cast<AIncidentManager>(
		UGameplayStatics::GetActorOfClass(GetWorld(), AIncidentManager::StaticClass()));
}

FVector AEmergencyManager::GetEvacuationCenter() const
{
	if (!EvacuationIncidentLocation.IsZero())
	{
		return EvacuationIncidentLocation;
	}
	return GetActorLocation();
}

void AEmergencyManager::EnsureAllAgentsEvacuate(const FVector& IncidentLocation)
{
	TArray<AActor*> FoundBoids;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ABoidsAB::StaticClass(), FoundBoids);

	for (AActor* Actor : FoundBoids)
	{
		ABoidsAB* Boid = Cast<ABoidsAB>(Actor);
		if (!Boid)
		{
			continue;
		}

		Boid->OnIncident(EIncidentType::Fire, IncidentLocation);
	}
}

void AEmergencyManager::StartEvacuationTimer()
{
	ClearEvacuationTimer();

	USimulationFeatureRegistry* Registry = GetWorld() ? GetWorld()->GetSubsystem<USimulationFeatureRegistry>() : nullptr;
	if (!Registry)
	{
		return;
	}

	const FEvacuationRoutingSettings& Settings = Registry->GetEvacuationSettings();
	if (Settings.EvacuationDuration <= 0.f)
	{
		return;
	}

	const double Now = GetWorld()->GetTimeSeconds();
	float TotalTimeUntilExpiry = Settings.EvacuationTimerStartDelay + Settings.EvacuationDuration;
	if (Registry->IsAccurateTimeMode())
	{
		// Same occupancy as Visual: authored real seconds × Visual clock, then ÷ Accurate Time speed.
		const float Delay = Registry->ScaleAuthoredDurationToRealSeconds(Settings.EvacuationTimerStartDelay);
		const float Duration = Registry->ScaleAuthoredDurationToRealSeconds(Settings.EvacuationDuration);
		TotalTimeUntilExpiry = Delay + Duration;
	}
	EvacuationDeadlineWorldTime = Now + TotalTimeUntilExpiry;
	TimerRemainingSeconds = TotalTimeUntilExpiry;
	bEvacuationTimerActive = true;
	bTimerExpired = false;
	SetActorTickEnabled(true);

	// Leave FirstDelay at default (-1) so the timer fires after TotalTimeUntilExpiry.
	GetWorld()->GetTimerManager().SetTimer(
		EvacuationTimerHandle,
		this,
		&AEmergencyManager::OnEvacuationTimerExpired,
		TotalTimeUntilExpiry,
		false);

	UE_LOG(LogTemp, Log,
	       TEXT("[EmergencyManager] Evacuation timer started: %.0fs total (duration %.0fs + start delay %.0fs)"),
	       TotalTimeUntilExpiry,
	       Settings.EvacuationDuration,
	       Settings.EvacuationTimerStartDelay);
}

void AEmergencyManager::ClearEvacuationTimer()
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(EvacuationTimerHandle);
	}
	bEvacuationTimerActive = false;
	TimerRemainingSeconds = 0.f;
	EvacuationDeadlineWorldTime = 0.0;
	SetActorTickEnabled(false);
}

void AEmergencyManager::ProcessEvacuationCasualties()
{
	USimulationFeatureRegistry* Registry = GetWorld() ? GetWorld()->GetSubsystem<USimulationFeatureRegistry>() : nullptr;
	if (!Registry || !Registry->GetEvacuationSettings().bEnableCasualties)
	{
		return;
	}

	TArray<AActor*> FoundBoids;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ABoidsAB::StaticClass(), FoundBoids);

	for (AActor* Actor : FoundBoids)
	{
		ABoidsAB* Boid = Cast<ABoidsAB>(Actor);
		if (!Boid || Boid->IsSafelyEvacuated() || Boid->IsCasualty())
		{
			continue;
		}

		Boid->EnterCasualtyState();
	}
}

void AEmergencyManager::OnEvacuationTimerExpired()
{
	bTimerExpired = true;
	TimerRemainingSeconds = 0.f;
	OnEvacuationTimerUpdated.Broadcast(0.f);

	UE_LOG(LogTemp, Log, TEXT("[EmergencyManager] Evacuation timer expired"));

	if (USimulationAnalytics* Analytics = GetWorld() ? GetWorld()->GetSubsystem<USimulationAnalytics>() : nullptr)
	{
		Analytics->RecordEvacuationTimerExpired();
	}

	ProcessEvacuationCasualties();
	EndEvacuation();
}

void AEmergencyManager::TriggerGlobalEvacuation()
{
	USimulationFeatureRegistry* Registry = GetWorld() ? GetWorld()->GetSubsystem<USimulationFeatureRegistry>() : nullptr;
	if (!Registry || !Registry->IsFeatureEnabled(ESimulationFeature::Evacuation))
	{
		UE_LOG(LogTemp, Log, TEXT("[EmergencyManager] Evacuation feature disabled — aborting trigger"));
		return;
	}

	if (IsEvacuationActive())
	{
		UE_LOG(LogTemp, Log, TEXT("[EmergencyManager] Evacuation already active"));
		return;
	}

	ResolveIncidentManager();
	if (!IncidentManagerRef)
	{
		UE_LOG(LogTemp, Error, TEXT("[EmergencyManager] No AIncidentManager found in level"));
		return;
	}

	const FVector Center = GetEvacuationCenter();
	const float Radius = Registry->GetEvacuationSettings().GlobalRadius;

	Registry->SetCurrentScenario(ESimulationScenario::EmergencyEvacuation);
	CurrentPhase = EEvacuationPhase::Evacuating;
	bTimerExpired = false;

	UE_LOG(LogTemp, Log, TEXT("[EmergencyManager] Global evacuation started at %s (radius %.0f)"),
	       *Center.ToString(), Radius);

	// Start the analytics session before agents react; it binds the per-agent and exit delegates.
	if (USimulationAnalytics* Analytics = GetWorld()->GetSubsystem<USimulationAnalytics>())
	{
		Analytics->BeginEvacuationSession();
	}

	IncidentManagerRef->TriggerIncident(EIncidentType::Fire, Center, Radius);
	EnsureAllAgentsEvacuate(Center);
	StartEvacuationTimer();

	OnEvacuationStarted.Broadcast();
}

void AEmergencyManager::EndEvacuation()
{
	if (!IsEvacuationActive())
	{
		return;
	}

	ClearEvacuationTimer();

	ResolveIncidentManager();
	if (IncidentManagerRef)
	{
		IncidentManagerRef->EndAllIncidents();
	}

	if (USimulationFeatureRegistry* Registry = GetWorld() ? GetWorld()->GetSubsystem<USimulationFeatureRegistry>() : nullptr)
	{
		if (Registry->IsFeatureEnabled(ESimulationFeature::DailyUrbanLife))
		{
			Registry->SetCurrentScenario(ESimulationScenario::DailyUrbanLife);
		}
		else
		{
			Registry->SetCurrentScenario(ESimulationScenario::None);
		}
	}

	CurrentPhase = EEvacuationPhase::AllClear;

	if (USimulationAnalytics* Analytics = GetWorld() ? GetWorld()->GetSubsystem<USimulationAnalytics>() : nullptr)
	{
		Analytics->RecordEvacuationTimerRemaining(TimerRemainingSeconds, bTimerExpired);

		const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Analytics"));
		IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
		if (!PF.DirectoryExists(*Dir))
		{
			PF.CreateDirectoryTree(*Dir);
		}
		const FString FilePath = FPaths::Combine(Dir,
			FString::Printf(TEXT("Evacuation_%lld.csv"), FDateTime::UtcNow().ToUnixTimestamp()));
		Analytics->ExportEvacuationSessionToCSV(FilePath);
	}

	OnEvacuationEnded.Broadcast();

	UE_LOG(LogTemp, Log, TEXT("[EmergencyManager] Evacuation ended"));
}
