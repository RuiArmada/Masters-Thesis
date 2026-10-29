#include "AgentArea.h"
#include "AgentStation.h"
#include "UObject/ConstructorHelpers.h"

AAgentArea::AAgentArea()
{
    PrimaryActorTick.bCanEverTick = false;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    RootComponent = SceneRoot;

#if WITH_EDITORONLY_DATA
    EditorBillboard = CreateDefaultSubobject<UBillboardComponent>(TEXT("EditorBillboard"));
    if (EditorBillboard)
    {
        EditorBillboard->SetupAttachment(RootComponent);
        EditorBillboard->SetHiddenInGame(true);

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

AAgentStation* AAgentArea::FindAvailableStation() const
{
    for (AAgentStation* Station : Stations)
    {
        if (Station && Station->HasAvailableSlot())
        {
            return Station;
        }
    }
    return nullptr;
}

AAgentStation* AAgentArea::FindStationByIdentifier(const FString& Identifier) const
{
    if (Identifier.IsEmpty())
    {
        return nullptr;
    }

    AAgentStation* FirstMatch = nullptr;
    for (AAgentStation* Station : Stations)
    {
        if (!Station || !Station->MatchesIdentifier(Identifier))
        {
            continue;
        }

        if (Station->HasAvailableSlot())
        {
            return Station;
        }

        if (!FirstMatch)
        {
            FirstMatch = Station;
        }
    }

    return FirstMatch;
}
