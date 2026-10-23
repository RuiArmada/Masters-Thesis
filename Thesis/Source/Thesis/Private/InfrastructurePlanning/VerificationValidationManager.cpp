#include "InfrastructurePlanning/VerificationValidationManager.h"
#include "Simulation/SimulationFeatureRegistry.h"
#include "Simulation/SimulationAnalytics.h"

AVerificationValidationManager::AVerificationValidationManager()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AVerificationValidationManager::BeginPlay()
{
	Super::BeginPlay();

	if (bTryGenerateOnBeginPlay && GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimer(
			DelayedGenerateHandle,
			this,
			&AVerificationValidationManager::DelayedGenerate,
			1.0f,
			false);
	}
}

void AVerificationValidationManager::DelayedGenerate()
{
	GenerateComparisonPackage();
}

void AVerificationValidationManager::ResolveVariantNames(FString& OutBaseline, FString& OutVariant) const
{
	OutBaseline = BaselineVariantOverride;
	OutVariant = ComparisonVariantOverride;

	if (const UWorld* World = GetWorld())
	{
		if (const USimulationFeatureRegistry* Registry = World->GetSubsystem<USimulationFeatureRegistry>())
		{
			const FInfrastructurePlanningSettings& Settings = Registry->GetInfrastructurePlanningSettings();
			if (OutBaseline.IsEmpty())
			{
				OutBaseline = Settings.ComparisonBaselineVariant;
			}
			if (OutVariant.IsEmpty())
			{
				OutVariant = Settings.ComparisonVariantName;
			}
		}
	}

	if (OutBaseline.IsEmpty())
	{
		OutBaseline = TEXT("MallA");
	}
	if (OutVariant.IsEmpty())
	{
		OutVariant = TEXT("MallB");
	}
}

bool AVerificationValidationManager::GenerateComparisonPackage()
{
	USimulationFeatureRegistry* Registry = GetWorld()
		? GetWorld()->GetSubsystem<USimulationFeatureRegistry>()
		: nullptr;
	if (!Registry || !Registry->IsFeatureEnabled(ESimulationFeature::InfrastructureAnalysis))
	{
		UE_LOG(LogTemp, Log, TEXT("[VnV] Infrastructure analysis disabled — package skipped"));
		return false;
	}

	USimulationAnalytics* Analytics = GetWorld()->GetSubsystem<USimulationAnalytics>();
	if (!Analytics)
	{
		return false;
	}

	FString Baseline;
	FString Variant;
	ResolveVariantNames(Baseline, Variant);

	FString ComparisonPath;
	FString SummaryPath;
	const bool bOk = Analytics->GenerateInfrastructureVerificationPackage(
		Baseline, Variant, ComparisonPath, SummaryPath);

	if (bOk)
	{
		LogVerificationStatus();
	}
	return bOk;
}

void AVerificationValidationManager::LogVerificationStatus() const
{
	USimulationAnalytics* Analytics = GetWorld()
		? GetWorld()->GetSubsystem<USimulationAnalytics>()
		: nullptr;
	if (!Analytics)
	{
		return;
	}

	FString Baseline;
	FString Variant;
	ResolveVariantNames(Baseline, Variant);

	FString BaselineCsv;
	FString VariantCsv;
	const bool bBase = Analytics->FindLatestInfrastructureCsv(Baseline, BaselineCsv);
	const bool bVar = Analytics->FindLatestInfrastructureCsv(Variant, VariantCsv);

	UE_LOG(LogTemp, Log, TEXT("[VnV] Status checklist:"));
	UE_LOG(LogTemp, Log, TEXT("[VnV]   [%s] Latest %s CSV"), bBase ? TEXT("OK") : TEXT("MISSING"), *Baseline);
	if (bBase)
	{
		UE_LOG(LogTemp, Log, TEXT("[VnV]       %s"), *BaselineCsv);
	}
	UE_LOG(LogTemp, Log, TEXT("[VnV]   [%s] Latest %s CSV"), bVar ? TEXT("OK") : TEXT("MISSING"), *Variant);
	if (bVar)
	{
		UE_LOG(LogTemp, Log, TEXT("[VnV]       %s"), *VariantCsv);
	}
	UE_LOG(LogTemp, Log, TEXT("[VnV]   Visualization: enableVisualization + AInfrastructureVisualizationActor"));
	UE_LOG(LogTemp, Log, TEXT("[VnV]   Metrics: travel time, queues, wait, congestion, station/path util, throughput"));
}
