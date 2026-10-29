#pragma once

#include "CoreMinimal.h"
#include "AgentArea.h"
#include "AgentSuperStation.generated.h"

/** Area grouping up to three stations; agents pick any available slot. */
UCLASS()
class THESIS_API AAgentSuperStation : public AAgentArea
{
    GENERATED_BODY()

public:
    AAgentSuperStation();

    static constexpr int32 MaxStationCount = 3;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
    void EnforceStationLimit();
};
