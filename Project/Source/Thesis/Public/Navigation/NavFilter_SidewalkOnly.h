#pragma once

#include "NavFilters/NavigationQueryFilter.h"
#include "NavFilter_SidewalkOnly.generated.h"

UCLASS()
class THESIS_API UNavFilter_SidewalkOnly : public UNavigationQueryFilter
{
    GENERATED_BODY()

public:
    UNavFilter_SidewalkOnly();
};
