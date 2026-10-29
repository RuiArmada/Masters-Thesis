#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MallEntranceGate.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UNavModifierComponent;

/**
 * Mall entrance that blocks BOTH physics and navigation when closed.
 *
 * Approach (UE Dynamic Modifiers Only — matches Nav Modifier workflow):
 *   https://www.youtube.com/watch?v=0AQzDuRtZnM
 *   - Collision never bakes into nav (CanEverAffectNavigation=false, no Dynamic Obstacle)
 *   - UNavModifierComponent owns the cut: CLOSED = NavArea_Null, OPEN = off (None)
 *   - FailsafeExtent mirrors GateVolumeExtent (bounds when collision is nav-irrelevant)
 *
 * ClosedBarrier is visual-only. Requires RecastNavMesh RuntimeGeneration = DynamicModifiersOnly.
 * Sidewalk must already be baked through the opening (rebuild Paths with gate OPEN once).
 */
UCLASS()
class THESIS_API AMallEntranceGate : public AActor
{
	GENERATED_BODY()

public:
	AMallEntranceGate();

	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif

	UFUNCTION(BlueprintCallable, Category = "Entrance")
	void Open();

	UFUNCTION(BlueprintCallable, Category = "Entrance")
	void Close();

	UFUNCTION(BlueprintCallable, Category = "Entrance")
	bool IsOpen() const { return bIsOpen; }

	/** World-space bounds of the blocked corridor (for path/debug queries). */
	UFUNCTION(BlueprintCallable, Category = "Entrance")
	FBox GetGateBounds() const;

	UFUNCTION(BlueprintCallable, Category = "Entrance")
	FString GetGateDebugLabel() const;

protected:
	void ApplyGateState();
	void ApplyGateDimensions();
	void EnsureDynamicModifiersRuntime();
	void SyncNavModifierExtent();
	void RefreshNavigationObstacle();

	/**
	 * Root box = physical barrier when closed.
	 * Does NOT paint nav (NavModifier owns that — avoids collision overriding the modifier).
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Entrance")
	TObjectPtr<UBoxComponent> GateVolume;

	/** Visual barrier when closed. Does not drive navigation. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Entrance|Visual")
	TObjectPtr<UStaticMeshComponent> ClosedBarrier;

	/**
	 * Dynamic nav cut (video approach).
	 * Closed → NavArea_Null; Open → relevancy off / AreaClass None.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Entrance|Navigation")
	TObjectPtr<UNavModifierComponent> NavModifier;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entrance|Dimensions", meta = (ClampMin = "10.0"))
	FVector GateVolumeExtent = FVector(200.f, 200.f, 200.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entrance|Visual")
	FVector ClosedBarrierScale = FVector(4.f, 0.2f, 3.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entrance|Visual")
	FVector ClosedBarrierRelativeLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entrance")
	bool bStartOpen = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Entrance")
	bool bIsOpen = false;
};
