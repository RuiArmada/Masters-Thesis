#pragma once

#include "CoreMinimal.h"
#include "StationInteractionTypes.generated.h"

/**
 * One generic station interaction step (area + optional station type + duration).
 * Not bound to a concrete station actor — resolved at execution time.
 * Can be built from XML, Blueprint, or other sources.
 */
USTRUCT(BlueprintType)
struct FStationInteraction
{
	GENERATED_BODY()

	/** Area actor tag (e.g. FoodCourt, ShopA, Work0). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Station|Interaction")
	FString AreaTag;

	/**
	 * Optional station type / identifier within the area.
	 * Empty = any available station in the area (existing selection strategy).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Station|Interaction")
	FString StationTypeName;

	/** Seconds of use; negative = station default InteractionDuration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Station|Interaction")
	float Duration = -1.f;

	/**
	 * Optional: bind this step to a specific station actor name.
	 * Empty = resolve by AreaTag + StationTypeName (preferred for multi-station types).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Station|Interaction")
	FName ExplicitStationName;
};
