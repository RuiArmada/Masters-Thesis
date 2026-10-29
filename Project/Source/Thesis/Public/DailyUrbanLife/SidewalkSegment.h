#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/BoxComponent.h"
#include "Simulation/SimulationTypes.h"
#include "SidewalkSegment.generated.h"

/**
 * Path segment marker for utilization analytics.
 * Place along pedestrian paths; does not affect routing.
 */
UCLASS()
class THESIS_API ASidewalkSegment : public AActor
{
	GENERATED_BODY()

public:
	ASidewalkSegment();

	UFUNCTION(BlueprintCallable, Category = "Sidewalk")
	FString GetSegmentName() const { return SegmentName.IsEmpty() ? GetName() : SegmentName; }

	UFUNCTION(BlueprintCallable, Category = "Sidewalk")
	EPathSegmentCategory GetSegmentCategory() const { return SegmentCategory; }

	UFUNCTION(BlueprintCallable, Category = "Sidewalk")
	void GetSegmentOverlappingAgents(TArray<AActor*>& OutAgents) const;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sidewalk")
	TObjectPtr<UBoxComponent> SegmentVolume;

	UPROPERTY(EditAnywhere, Category = "Sidewalk")
	FString SegmentName;

	UPROPERTY(EditAnywhere, Category = "Sidewalk")
	EPathSegmentCategory SegmentCategory = EPathSegmentCategory::Sidewalk;

	UFUNCTION()
	void OnSegmentOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	                      UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
	                      bool bFromSweep, const FHitResult& SweepResult);
};
