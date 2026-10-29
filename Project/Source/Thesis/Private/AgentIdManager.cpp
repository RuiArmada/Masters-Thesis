#include "AgentIdManager.h"

AAgentIdManager::AAgentIdManager()
{
    PrimaryActorTick.bCanEverTick = false;
}

FName AAgentIdManager::GenerateAgentId()
{
    return FName(
        *FString::Printf(TEXT("Agent_%d"), NextAgentIndex++)
    );
}
