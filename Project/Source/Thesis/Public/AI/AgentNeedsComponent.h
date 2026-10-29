#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AgentNeedsComponent.generated.h"

/** Hunger/energy data only — other layers decide actions. */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class THESIS_API UAgentNeedsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAgentNeedsComponent();

	/** 0 = not hungry, 1 = fully hungry. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Needs|Hunger", meta = (ClampMin = "0", ClampMax = "1"))
	float HungerLevel = 0.f;

	/** 1 = full energy, 0 = depleted. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Needs|Energy", meta = (ClampMin = "0", ClampMax = "1"))
	float EnergyLevel = 1.f;

	UPROPERTY(EditAnywhere, Category = "Needs|Hunger")
	float HungerIncreasePerSecond = 0.02f;

	UPROPERTY(EditAnywhere, Category = "Needs|Energy")
	float EnergyDrainPerSecond = 0.01f;

	UPROPERTY(EditAnywhere, Category = "Needs|Hunger", meta = (ClampMin = "0", ClampMax = "1"))
	float HungryThreshold = 0.65f;

	UPROPERTY(EditAnywhere, Category = "Needs|Energy", meta = (ClampMin = "0", ClampMax = "1"))
	float ExhaustedThreshold = 0.35f;

	/** Optional passive recovery while near home/food (data update only; no movement). */
	UPROPERTY(EditAnywhere, Category = "Needs|Recovery")
	float RecoveryRadius = 250.f;

	UPROPERTY(EditAnywhere, Category = "Needs|Recovery")
	float HungerRecoveryPerSecondAtFood = 0.15f;

	UPROPERTY(EditAnywhere, Category = "Needs|Recovery")
	float EnergyRecoveryPerSecondAtHome = 0.12f;

	/** When idle at home and exhausted, energy drains at this fraction of normal (rest). */
	UPROPERTY(EditAnywhere, Category = "Needs|Rest", meta = (ClampMin = "0", ClampMax = "1"))
	float RestEnergyDrainScale = 0.2f;

	/** While resting at home, hunger rises at this fraction of normal. */
	UPROPERTY(EditAnywhere, Category = "Needs|Rest", meta = (ClampMin = "0", ClampMax = "1"))
	float RestHungerIncreaseScale = 0.45f;

	/** Multiplier on at-home energy recovery while resting (idle + exhausted + near home). */
	UPROPERTY(EditAnywhere, Category = "Needs|Rest", meta = (ClampMin = "1"))
	float RestEnergyRecoveryMultiplier = 2.25f;

	void AdvanceNeeds(float DeltaTime, const FVector& AgentWorldLocation, const FVector& FoodLocation, const FVector& HomeLocation, bool bFoodValid, bool bHomeValid, bool bRestingAtHome);

	UFUNCTION(BlueprintCallable, Category = "Needs")
	float GetRecoveryRadius() const { return RecoveryRadius; }

	UFUNCTION(BlueprintCallable, Category = "Needs")
	bool IsHungry() const { return HungerLevel >= HungryThreshold; }

	UFUNCTION(BlueprintCallable, Category = "Needs")
	bool IsExhausted() const { return EnergyLevel <= ExhaustedThreshold; }

	UFUNCTION(BlueprintCallable, Category = "Needs")
	float GetHungerLevel() const { return HungerLevel; }

	UFUNCTION(BlueprintCallable, Category = "Needs")
	float GetEnergyLevel() const { return EnergyLevel; }

protected:
	void ClampLevels();
};
