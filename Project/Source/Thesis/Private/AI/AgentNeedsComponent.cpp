#include "AI/AgentNeedsComponent.h"

UAgentNeedsComponent::UAgentNeedsComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UAgentNeedsComponent::ClampLevels()
{
	HungerLevel = FMath::Clamp(HungerLevel, 0.f, 1.f);
	EnergyLevel = FMath::Clamp(EnergyLevel, 0.f, 1.f);
}

void UAgentNeedsComponent::AdvanceNeeds(float DeltaTime, const FVector& AgentWorldLocation, const FVector& FoodLocation, const FVector& HomeLocation, bool bFoodValid, bool bHomeValid, bool bRestingAtHome)
{
	if (DeltaTime <= 0.f)
	{
		return;
	}

	float HungerRate = HungerIncreasePerSecond;
	float EnergyDrain = EnergyDrainPerSecond;
	if (bRestingAtHome)
	{
		HungerRate *= RestHungerIncreaseScale;
		EnergyDrain *= RestEnergyDrainScale;
	}

	HungerLevel += HungerRate * DeltaTime;
	EnergyLevel -= EnergyDrain * DeltaTime;

	if (bFoodValid)
	{
		const float D = FVector::Dist(AgentWorldLocation, FoodLocation);
		if (D <= RecoveryRadius)
		{
			HungerLevel -= HungerRecoveryPerSecondAtFood * DeltaTime;
		}
	}

	if (bHomeValid)
	{
		const float D = FVector::Dist(AgentWorldLocation, HomeLocation);
		if (D <= RecoveryRadius)
		{
			float Recovery = EnergyRecoveryPerSecondAtHome;
			if (bRestingAtHome)
			{
				Recovery *= RestEnergyRecoveryMultiplier;
			}
			EnergyLevel += Recovery * DeltaTime;
		}
	}

	ClampLevels();
}
