#include "World/EvacuationSafeZone.h"

AEvacuationSafeZone::AEvacuationSafeZone()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

bool AEvacuationSafeZone::ContainsLocation(const FVector& WorldLocation) const
{
	const FVector Delta = WorldLocation - GetActorLocation();
	return FVector::DistSquared2D(FVector::ZeroVector, FVector(Delta.X, Delta.Y, 0.f))
	       <= FMath::Square(ArrivalRadius);
}
