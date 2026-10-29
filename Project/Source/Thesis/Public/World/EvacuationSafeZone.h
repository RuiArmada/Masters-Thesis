#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EvacuationSafeZone.generated.h"

/**
 * Designated safe area for evacuated agents. Place in level and tag actor "SafeZone".
 * Reusable for future scenarios (rally points, shelters, etc.).
 */
UCLASS()
class THESIS_API AEvacuationSafeZone : public AActor
{
	GENERATED_BODY()

public:
	AEvacuationSafeZone();

	UFUNCTION(BlueprintCallable, Category = "Evacuation|SafeZone")
	FVector GetZoneLocation() const { return GetActorLocation(); }

	UFUNCTION(BlueprintCallable, Category = "Evacuation|SafeZone")
	float GetArrivalRadius() const { return ArrivalRadius; }

	/** Returns true if WorldLocation is within horizontal arrival radius. */
	UFUNCTION(BlueprintCallable, Category = "Evacuation|SafeZone")
	bool ContainsLocation(const FVector& WorldLocation) const;

protected:
	UPROPERTY(EditAnywhere, Category = "Evacuation|SafeZone")
	float ArrivalRadius = 200.f;
};
