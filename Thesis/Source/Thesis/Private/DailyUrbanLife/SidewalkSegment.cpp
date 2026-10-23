#include "DailyUrbanLife/SidewalkSegment.h"
#include "BoidsAB.h"
#include "Simulation/SimulationAnalytics.h"
#include "Simulation/SimulationFeatureRegistry.h"

ASidewalkSegment::ASidewalkSegment()
{
	PrimaryActorTick.bCanEverTick = false;

	SegmentVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("SegmentVolume"));
	RootComponent = SegmentVolume;
	SegmentVolume->SetBoxExtent(FVector(200.f, 50.f, 100.f));
	SegmentVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SegmentVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	SegmentVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	SegmentVolume->OnComponentBeginOverlap.AddDynamic(this, &ASidewalkSegment::OnSegmentOverlap);
}

void ASidewalkSegment::GetSegmentOverlappingAgents(TArray<AActor*>& OutAgents) const
{
	OutAgents.Reset();
	if (!SegmentVolume)
	{
		return;
	}

	TArray<AActor*> Overlapping;
	SegmentVolume->GetOverlappingActors(Overlapping, ABoidsAB::StaticClass());
	OutAgents = MoveTemp(Overlapping);
}

void ASidewalkSegment::OnSegmentOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
                                        UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
                                        bool bFromSweep, const FHitResult& SweepResult)
{
	if (!OtherActor || !OtherActor->IsA<ABoidsAB>())
	{
		return;
	}

	if (USimulationAnalytics* Analytics = GetWorld()->GetSubsystem<USimulationAnalytics>())
	{
		const FString Name = GetSegmentName();
		Analytics->RecordSidewalkUtilization(Name);
		Analytics->RecordPathSegmentUtilization(Name, SegmentCategory);
	}
}
