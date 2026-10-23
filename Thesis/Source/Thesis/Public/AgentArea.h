#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "Components/BillboardComponent.h"
#include "Simulation/SimulationTypes.h"
#include "AgentArea.generated.h"

class AAgentStation;

/** Editor-assigned group of stations. Does not drive movement or schedules. */
UCLASS()
class THESIS_API AAgentArea : public AActor
{
    GENERATED_BODY()

public:
    AAgentArea();

    UFUNCTION(BlueprintCallable, Category = "Area")
    AAgentStation* FindAvailableStation() const;

    /** Prefer a station with a free slot matching Identifier; otherwise first match for queueing. */
    UFUNCTION(BlueprintCallable, Category = "Area")
    AAgentStation* FindStationByIdentifier(const FString& Identifier) const;

    UFUNCTION(BlueprintCallable, Category = "Area")
    const TArray<AAgentStation*>& GetStations() const { return Stations; }

    UFUNCTION(BlueprintCallable, Category = "Area")
    int32 GetStationCount() const { return Stations.Num(); }

    /** When true, schedule UseStation will not send agents here (handled as a closed venue). */
    UFUNCTION(BlueprintCallable, Category = "Area")
    bool IsAreaClosed() const { return bAreaClosed; }

    UFUNCTION(BlueprintCallable, Category = "Area|Daily Life")
    EDailyDestinationCategory GetDestinationCategory() const { return DestinationCategory; }

protected:
    UPROPERTY(EditAnywhere, Category = "Area|Simulation")
    bool bAreaClosed = false;

    /** Used by UC2 analytics (home / work / public occupancy). */
    UPROPERTY(EditAnywhere, Category = "Area|Daily Life")
    EDailyDestinationCategory DestinationCategory = EDailyDestinationCategory::None;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area|Components")
    USceneComponent* SceneRoot;

#if WITH_EDITORONLY_DATA
    UPROPERTY(VisibleAnywhere, Category = "Area|Components")
    UBillboardComponent* EditorBillboard;
#endif

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Area")
    TArray<AAgentStation*> Stations;
};
