#include "Navigation/NavArea_Sidewalk.h"

UNavArea_Sidewalk::UNavArea_Sidewalk()
{
    DefaultCost = 1.f;
    FixedAreaEnteringCost = 0.f;
    DrawColor = FColor::Green;
}
