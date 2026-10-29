#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VerificationValidationManager.generated.h"

/**
 * Academic Verification & Validation orchestrator for Mall A vs Mall B.
 * Reuses USimulationAnalytics infrastructure CSVs — does not redesign metrics.
 */
UCLASS()
class THESIS_API AVerificationValidationManager : public AActor
{
	GENERATED_BODY()

public:
	AVerificationValidationManager();

	/** When true, attempts package generation shortly after BeginPlay (if CSVs already exist). */
	UPROPERTY(EditAnywhere, Category = "Verification")
	bool bTryGenerateOnBeginPlay = false;

	UPROPERTY(EditAnywhere, Category = "Verification")
	FString BaselineVariantOverride;

	UPROPERTY(EditAnywhere, Category = "Verification")
	FString ComparisonVariantOverride;

	UFUNCTION(BlueprintCallable, Category = "Verification")
	bool GenerateComparisonPackage();

	UFUNCTION(BlueprintCallable, Category = "Verification")
	void LogVerificationStatus() const;

protected:
	virtual void BeginPlay() override;

	FTimerHandle DelayedGenerateHandle;
	void DelayedGenerate();

	void ResolveVariantNames(FString& OutBaseline, FString& OutVariant) const;
};
