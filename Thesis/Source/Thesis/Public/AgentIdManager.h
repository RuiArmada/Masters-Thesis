#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AgentIdManager.generated.h"

UCLASS()
class THESIS_API AAgentIdManager : public AActor
{
    GENERATED_BODY()

public:
    AAgentIdManager();

    FName GenerateAgentId();

private:
    int32 NextAgentIndex = 0;
};
