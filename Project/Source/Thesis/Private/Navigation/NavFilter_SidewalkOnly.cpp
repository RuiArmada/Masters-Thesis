#include "Navigation/NavFilter_SidewalkOnly.h"
#include "NavAreas/NavArea_Default.h"
#include "NavAreas/NavArea_Null.h"
#include "Navigation/NavArea_Sidewalk.h"

UNavFilter_SidewalkOnly::UNavFilter_SidewalkOnly()
{
    // Block default navigation areas (roads, generic ground)
    {
        FNavigationFilterArea Area;
        Area.AreaClass = UNavArea_Default::StaticClass();
        Area.bOverrideTravelCost = true;
        Area.TravelCostOverride = BIG_NUMBER;
        Area.bIsExcluded = false;
        Areas.Add(Area);
    }

    // Allow sidewalks
    {
        FNavigationFilterArea Area;
        Area.AreaClass = UNavArea_Sidewalk::StaticClass();
        Area.bOverrideTravelCost = true;
        Area.TravelCostOverride = 1.0f;
        Area.bIsExcluded = false;
        Areas.Add(Area);
    }

    // Explicitly block null areas
    {
        FNavigationFilterArea Area;
        Area.AreaClass = UNavArea_Null::StaticClass();
        Area.bIsExcluded = true;
        Areas.Add(Area);
    }
}
