#include "IncidentManager.h"
#include "Kismet/GameplayStatics.h"
#include "BoidsAB.h"
#include "Engine/World.h"

AIncidentManager::AIncidentManager()
{
    PrimaryActorTick.bCanEverTick = false;
    NextIncidentId = 0;
}

void AIncidentManager::BeginPlay()
{
    Super::BeginPlay();
}

void AIncidentManager::TriggerIncident(
    EIncidentType IncidentType,
    FVector IncidentLocation,
    float AffectRadius)
{
    FActiveIncident NewIncident;
    NewIncident.Type = IncidentType;
    NewIncident.Location = IncidentLocation;
    NewIncident.Radius = AffectRadius;
    NewIncident.IncidentId = NextIncidentId++;
    
    ActiveIncidents.Add(NewIncident);
    OnIncidentTriggered.Broadcast(NewIncident);

    UE_LOG(LogTemp, Log,
        TEXT("[IncidentManager] Incident #%d triggered: %d at %s (Radius %.1f)"),
        NewIncident.IncidentId,
        static_cast<int32>(IncidentType),
        *IncidentLocation.ToString(),
        AffectRadius
    );

    TArray<AActor*> FoundBoids;
    UGameplayStatics::GetAllActorsOfClass(
        GetWorld(),
        ABoidsAB::StaticClass(),
        FoundBoids
    );

    for (AActor* Actor : FoundBoids)
    {
        ABoidsAB* Boid = Cast<ABoidsAB>(Actor);
        if (!Boid)
            continue;

        float Distance = FVector::Dist(
            Boid->GetActorLocation(),
            IncidentLocation
        );

        if (Distance <= AffectRadius)
        {
            Boid->OnIncident(IncidentType, IncidentLocation);
        }
    }
}

void AIncidentManager::EndIncident(int32 IncidentId)
{
    int32 RemovedCount = ActiveIncidents.RemoveAll(
        [IncidentId](const FActiveIncident& Incident)
        {
            return Incident.IncidentId == IncidentId;
        }
    );

    if (RemovedCount > 0)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[IncidentManager] Incident #%d ended"),
            IncidentId
        );

        if (ActiveIncidents.Num() == 0)
        {
            NotifyBoidsIncidentEnded();
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[IncidentManager] Attempted to end non-existent incident #%d"),
            IncidentId
        );
    }
}

void AIncidentManager::EndAllIncidents()
{
    int32 Count = ActiveIncidents.Num();
    ActiveIncidents.Empty();
    
    UE_LOG(LogTemp, Log,
        TEXT("[IncidentManager] Ended all %d active incidents"),
        Count
    );

    NotifyBoidsIncidentEnded();
}

void AIncidentManager::NotifyBoidsIncidentEnded()
{
    OnAllIncidentsEnded.Broadcast();

    TArray<AActor*> FoundBoids;
    UGameplayStatics::GetAllActorsOfClass(
        GetWorld(),
        ABoidsAB::StaticClass(),
        FoundBoids
    );

    for (AActor* Actor : FoundBoids)
    {
        ABoidsAB* Boid = Cast<ABoidsAB>(Actor);
        if (Boid)
        {
            Boid->OnIncidentEnded();
        }
    }

    UE_LOG(LogTemp, Log,
        TEXT("[IncidentManager] Notified %d boids that incident ended"),
        FoundBoids.Num()
    );
}
