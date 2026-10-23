#include "DailyUrbanLife/DailyUrbanLifeManager.h"
#include "BoidsAB.h"
#include "ScheduleManager.h"
#include "AgentArea.h"
#include "AI/AgentNeedsComponent.h"
#include "Simulation/SimulationFeatureRegistry.h"
#include "Simulation/SimulationAnalytics.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"

ADailyUrbanLifeManager::ADailyUrbanLifeManager()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ADailyUrbanLifeManager::BeginPlay()
{
	Super::BeginPlay();

	if (bAutoInitializeOnBeginPlay && GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimer(
			InitDelayHandle,
			this,
			&ADailyUrbanLifeManager::InitializeDailyUrbanLife,
			0.5f,
			false);
	}
}

void ADailyUrbanLifeManager::InitializeDailyUrbanLife()
{
	if (!GetWorld())
	{
		return;
	}

	USimulationFeatureRegistry* Registry = GetWorld()->GetSubsystem<USimulationFeatureRegistry>();
	if (!Registry || !Registry->IsFeatureEnabled(ESimulationFeature::DailyUrbanLife))
	{
		UE_LOG(LogTemp, Log, TEXT("[DailyUrbanLife] Feature disabled — manager skipped"));
		return;
	}

	if (Registry->IsValidationMap())
	{
		UE_LOG(LogTemp, Log,
			TEXT("[DailyUrbanLife] Validation map — ScheduleManager keeps map-authored schedule; skipping DUL setup"));
		return;
	}

	Registry->SetCurrentScenario(ESimulationScenario::DailyUrbanLife);
	const FDailyUrbanLifeSettings& Settings = Registry->GetDailyUrbanLifeSettings();

	CopyScheduleToSaved(Settings.DefaultScheduleFile);
	ConfigureScheduleManager();
	AssignAgentDestinations();
	ApplyAgentVariation();
	ValidateMapSetup();

	if (USimulationAnalytics* Analytics = GetWorld()->GetSubsystem<USimulationAnalytics>())
	{
		Analytics->BeginDailyUrbanLifeSession();
	}

	UE_LOG(LogTemp, Log, TEXT("[DailyUrbanLife] Initialized scenario with schedule %s"), *Settings.DefaultScheduleFile);
}

void ADailyUrbanLifeManager::CopyScheduleToSaved(const FString& RelativePath) const
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	const FString ProjectPath = FPaths::Combine(FPaths::ProjectDir(), RelativePath);
	const FString SavedPath = FPaths::Combine(FPaths::ProjectSavedDir(), RelativePath);
	const FString SavedParent = FPaths::GetPath(SavedPath);
	if (!PlatformFile.DirectoryExists(*SavedParent))
	{
		PlatformFile.CreateDirectoryTree(*SavedParent);
	}

	if (PlatformFile.FileExists(*ProjectPath))
	{
		PlatformFile.CopyFile(*SavedPath, *ProjectPath);
	}
}

void ADailyUrbanLifeManager::ConfigureScheduleManager() const
{
	AScheduleManager* SM = Cast<AScheduleManager>(
		UGameplayStatics::GetActorOfClass(GetWorld(), AScheduleManager::StaticClass()));
	if (!SM)
	{
		return;
	}

	const FDailyUrbanLifeSettings& Settings =
		GetWorld()->GetSubsystem<USimulationFeatureRegistry>()->GetDailyUrbanLifeSettings();
	SM->SetScheduleFilePath(Settings.DefaultScheduleFile);
}

bool ADailyUrbanLifeManager::ParseAgentIndex(const FName& AgentId, int32& OutIndex)
{
	const FString Id = AgentId.ToString();
	if (!Id.StartsWith(TEXT("Agent_")))
	{
		return false;
	}
	OutIndex = FCString::Atoi(*Id.Mid(6));
	return OutIndex >= 0;
}

void ADailyUrbanLifeManager::AssignAgentDestinations()
{
	const FDailyUrbanLifeSettings& Settings =
		GetWorld()->GetSubsystem<USimulationFeatureRegistry>()->GetDailyUrbanLifeSettings();

	TArray<AActor*> Boids;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ABoidsAB::StaticClass(), Boids);

	for (AActor* Actor : Boids)
	{
		ABoidsAB* Boid = Cast<ABoidsAB>(Actor);
		if (!Boid)
		{
			continue;
		}

		int32 Index = 0;
		if (!ParseAgentIndex(Boid->AgentId, Index))
		{
			continue;
		}

		// Map provides Home1..Home25 and Work0..Work6; wrap so Agent_25+ reuse existing tags.
		const FName HomeTag = FName(*FString::Printf(TEXT("Home%d"), (Index % 25) + 1));
		const FName WorkTag = FName(*FString::Printf(TEXT("Work%d"), Index % 7));
		Boid->AssignDailyDestinations(HomeTag, WorkTag);
		Boid->ResolveDestinationsFromTags();

		if (SharedFoodTag != NAME_None)
		{
			TArray<AActor*> FoodActors;
			UGameplayStatics::GetAllActorsWithTag(GetWorld(), SharedFoodTag, FoodActors);
			if (FoodActors.Num() > 0)
			{
				TArray<FVector> FoodLocations;
				FoodLocations.Reserve(FoodActors.Num());
				for (AActor* FoodActor : FoodActors)
				{
					if (FoodActor)
					{
						FoodLocations.Add(FoodActor->GetActorLocation());
					}
				}
				Boid->AssignFoodDestinationFromMap(FoodLocations, Index);
			}
		}

		if (Settings.bStartAgentsAtHome)
		{
			Boid->TeleportToAssignedHome();
		}
	}
}

void ADailyUrbanLifeManager::ApplyAgentVariation()
{
	const FDailyUrbanLifeSettings& Settings =
		GetWorld()->GetSubsystem<USimulationFeatureRegistry>()->GetDailyUrbanLifeSettings();

	TArray<AActor*> Boids;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ABoidsAB::StaticClass(), Boids);

	for (AActor* Actor : Boids)
	{
		ABoidsAB* Boid = Cast<ABoidsAB>(Actor);
		if (!Boid)
		{
			continue;
		}

		if (Settings.MaxScheduleMinuteOffset > 0)
		{
			Boid->SetScheduleMinuteOffset(FMath::RandRange(0, Settings.MaxScheduleMinuteOffset));
		}

		Boid->SetBaseWalkSpeed(FMath::FRandRange(Settings.WalkingSpeedMin, Settings.WalkingSpeedMax));

		if (Settings.bEnableNeedsDuringDailyLife && Boid->GetNeedsComponent())
		{
			UAgentNeedsComponent* Needs = Boid->GetNeedsComponent();
			Needs->HungerIncreasePerSecond *= Settings.NeedsRateScale;
			Needs->EnergyDrainPerSecond *= Settings.NeedsRateScale;
		}
	}
}

void ADailyUrbanLifeManager::ValidateMapSetup() const
{
	if (!GetWorld())
	{
		return;
	}

	const FDailyUrbanLifeSettings& Settings =
		GetWorld()->GetSubsystem<USimulationFeatureRegistry>()->GetDailyUrbanLifeSettings();

	int32 MissingHomes = 0;
	for (int32 HomeIndex = 1; HomeIndex <= 25; ++HomeIndex)
	{
		TArray<AActor*> Found;
		UGameplayStatics::GetAllActorsWithTag(
			GetWorld(),
			FName(*FString::Printf(TEXT("Home%d"), HomeIndex)),
			Found);
		if (Found.Num() == 0)
		{
			++MissingHomes;
		}
	}

	if (MissingHomes > 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[DailyUrbanLife] Map validation: %d/25 Home tags missing (Home1..Home25)"),
			MissingHomes);
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("[DailyUrbanLife] Map validation: all 25 Home tags found"));
	}

	int32 WorkAreasMissing = 0;
	int32 WorkAreasUnderCapacity = 0;
	for (int32 WorkIndex = 0; WorkIndex <= 6; ++WorkIndex)
	{
		TArray<AActor*> Found;
		UGameplayStatics::GetAllActorsWithTag(
			GetWorld(),
			FName(*FString::Printf(TEXT("Work%d"), WorkIndex)),
			Found);

		bool bFoundArea = false;
		for (AActor* Actor : Found)
		{
			if (const AAgentArea* Area = Cast<AAgentArea>(Actor))
			{
				bFoundArea = true;
				const int32 StationCount = Area->GetStationCount();
				if (StationCount < Settings.MinStationsPerWorkArea)
				{
					++WorkAreasUnderCapacity;
					UE_LOG(LogTemp, Warning,
						TEXT("[DailyUrbanLife] Work%d has %d stations — recommend >= %d for 25 agents"),
						WorkIndex,
						StationCount,
						Settings.MinStationsPerWorkArea);
				}
			}
		}

		if (!bFoundArea)
		{
			++WorkAreasMissing;
			UE_LOG(LogTemp, Warning, TEXT("[DailyUrbanLife] Map validation: Work%d area not found"), WorkIndex);
		}
	}

	if (WorkAreasMissing == 0 && WorkAreasUnderCapacity == 0)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[DailyUrbanLife] Map validation: Work0..Work6 present with >= %d stations each"),
			Settings.MinStationsPerWorkArea);
	}

	if (!Settings.BreakAreaTag.IsEmpty())
	{
		TArray<AActor*> PlazaActors;
		UGameplayStatics::GetAllActorsWithTag(GetWorld(), FName(*Settings.BreakAreaTag), PlazaActors);
		if (PlazaActors.Num() == 0)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[DailyUrbanLife] Break area tag '%s' not found — Break will pause in place only"),
				*Settings.BreakAreaTag);
		}
	}

	if (SharedFoodTag != NAME_None)
	{
		TArray<AActor*> FoodActors;
		UGameplayStatics::GetAllActorsWithTag(GetWorld(), SharedFoodTag, FoodActors);
		if (FoodActors.Num() == 0)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[DailyUrbanLife] Food tag '%s' not found — needs layer may lack a food target"),
				*SharedFoodTag.ToString());
		}
	}
}
