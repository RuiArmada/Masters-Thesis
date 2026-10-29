#include "AgentSuperStation.h"
#include "AgentStation.h"
#include "UObject/ConstructorHelpers.h"

#if WITH_EDITOR
#include "UObject/UnrealType.h"
#endif

AAgentSuperStation::AAgentSuperStation()
{
#if WITH_EDITORONLY_DATA
    if (EditorBillboard)
    {
        static ConstructorHelpers::FObjectFinder<UTexture2D> IconTexture(
            TEXT("/Engine/EditorResources/S_TriggerBox.S_TriggerBox")
        );
        if (IconTexture.Succeeded())
        {
            EditorBillboard->SetSprite(IconTexture.Object);
        }
    }
#endif
}

void AAgentSuperStation::EnforceStationLimit()
{
    const TArray<AAgentStation*>& StationList = GetStations();
    if (StationList.Num() > MaxStationCount)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[AgentSuperStation] '%s' supports at most %d stations (has %d)"),
            *GetName(),
            MaxStationCount,
            StationList.Num());
    }
}

#if WITH_EDITOR
void AAgentSuperStation::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    EnforceStationLimit();
}
#endif
