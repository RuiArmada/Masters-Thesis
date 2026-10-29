#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DailyUrbanLifeManager.generated.h"

class ABoidsAB;
class AScheduleManager;

/** Daily urban life scenario bootstrap. */
UCLASS()
class THESIS_API ADailyUrbanLifeManager : public AActor
{
	GENERATED_BODY()

public:
	ADailyUrbanLifeManager();

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, Category = "Daily Urban Life")
	bool bAutoInitializeOnBeginPlay = true;

	FTimerHandle InitDelayHandle;

	/** Tag shared by all food locations on the map (e.g. Food, FoodLocation). */
	UPROPERTY(EditAnywhere, Category = "Daily Urban Life|Destinations")
	FName SharedFoodTag = FName(TEXT("Food"));

	/** Radius for spawning agents near home. */
	UPROPERTY(EditAnywhere, Category = "Daily Urban Life|Destinations")
	float HomeSpawnRadius = 120.f;

	UFUNCTION(BlueprintCallable, Category = "Daily Urban Life")
	void InitializeDailyUrbanLife();

	void CopyScheduleToSaved(const FString& RelativePath) const;
	void ConfigureScheduleManager() const;
	void AssignAgentDestinations();
	void ApplyAgentVariation();
	void ValidateMapSetup() const;
	static bool ParseAgentIndex(const FName& AgentId, int32& OutIndex);
};
