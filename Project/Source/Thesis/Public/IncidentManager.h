#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IncidentManager.generated.h"

UENUM(BlueprintType)
enum class EIncidentType : uint8
{
    None        UMETA(DisplayName = "None"),
    Accident    UMETA(DisplayName = "Accident"),
    Fire        UMETA(DisplayName = "Fire"),
    Explosion   UMETA(DisplayName = "Explosion")
};

USTRUCT(BlueprintType)
struct FActiveIncident
{
    GENERATED_BODY()

    EIncidentType Type;
    FVector Location;
    float Radius;
    int32 IncidentId;

    FActiveIncident()
        : Type(EIncidentType::None)
        , Location(FVector::ZeroVector)
        , Radius(0.0f)
        , IncidentId(-1)
    {}
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnIncidentTriggered, const FActiveIncident&, Incident);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAllIncidentsEnded);

UCLASS()
class THESIS_API AIncidentManager : public AActor
{
    GENERATED_BODY()

public:
    AIncidentManager();

    UFUNCTION(BlueprintCallable, Category = "Incident")
    void TriggerIncident(
        EIncidentType IncidentType,
        FVector IncidentLocation,
        float AffectRadius
    );

    UFUNCTION(BlueprintCallable, Category = "Incident")
    void EndIncident(int32 IncidentId);

    UFUNCTION(BlueprintCallable, Category = "Incident")
    void EndAllIncidents();

    UFUNCTION(BlueprintCallable, Category = "Incident")
    bool HasActiveIncidents() const { return ActiveIncidents.Num() > 0; }

    UFUNCTION(BlueprintCallable, Category = "Incident")
    TArray<FActiveIncident> GetActiveIncidents() const { return ActiveIncidents; }

    UPROPERTY(BlueprintAssignable, Category = "Incident|Events")
    FOnIncidentTriggered OnIncidentTriggered;

    UPROPERTY(BlueprintAssignable, Category = "Incident|Events")
    FOnAllIncidentsEnded OnAllIncidentsEnded;

protected:
    virtual void BeginPlay() override;

private:
    TArray<FActiveIncident> ActiveIncidents;
    int32 NextIncidentId = 0;

    void NotifyBoidsIncidentEnded();
};
