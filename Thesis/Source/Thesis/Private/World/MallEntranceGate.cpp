#include "World/MallEntranceGate.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "NavModifierComponent.h"
#include "NavAreas/NavArea_Null.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "ScheduleManager.h"
#include "Simulation/SimulationAnalytics.h"
#include "Simulation/SimulationClock.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UnrealType.h"
#include "Engine/CollisionProfile.h"

AMallEntranceGate::AMallEntranceGate()
{
	PrimaryActorTick.bCanEverTick = false;

	GateVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("GateVolume"));
	RootComponent = GateVolume;
	GateVolume->SetBoxExtent(GateVolumeExtent);
	GateVolume->SetHiddenInGame(true);
	GateVolume->ShapeColor = FColor::Orange;
	GateVolume->bDrawOnlyIfSelected = true;
	GateVolume->SetGenerateOverlapEvents(false);

	// Video rule: collision must NOT override / bake nav — NavModifier owns the cut.
	GateVolume->bDynamicObstacle = false;
	GateVolume->SetAreaClassOverride(nullptr);
	GateVolume->SetCanEverAffectNavigation(false);
	GateVolume->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	GateVolume->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);

	ClosedBarrier = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ClosedBarrier"));
	ClosedBarrier->SetupAttachment(GateVolume);
	ClosedBarrier->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ClosedBarrier->SetCanEverAffectNavigation(false);
	ClosedBarrier->SetCastShadow(true);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube"));
	if (CubeMesh.Succeeded())
	{
		ClosedBarrier->SetStaticMesh(CubeMesh.Object);
	}

	NavModifier = CreateDefaultSubobject<UNavModifierComponent>(TEXT("NavModifier"));
	NavModifier->SetAreaClass(UNavArea_Null::StaticClass());
	NavModifier->FailsafeExtent = GateVolumeExtent;

	ApplyGateDimensions();
	ApplyGateState();
}

void AMallEntranceGate::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyGateDimensions();
	if (!HasActorBegunPlay())
	{
		bIsOpen = bStartOpen;
	}
	ApplyGateState();
}

#if WITH_EDITOR
void AMallEntranceGate::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropName = PropertyChangedEvent.GetPropertyName();
	if (PropName == GET_MEMBER_NAME_CHECKED(AMallEntranceGate, GateVolumeExtent)
		|| PropName == GET_MEMBER_NAME_CHECKED(AMallEntranceGate, ClosedBarrierScale)
		|| PropName == GET_MEMBER_NAME_CHECKED(AMallEntranceGate, ClosedBarrierRelativeLocation)
		|| PropName == GET_MEMBER_NAME_CHECKED(AMallEntranceGate, bStartOpen)
		|| PropName == TEXT("BoxExtent"))
	{
		if (PropName == TEXT("BoxExtent") && GateVolume)
		{
			GateVolumeExtent = GateVolume->GetUnscaledBoxExtent();
		}

		ApplyGateDimensions();
		if (!HasActorBegunPlay())
		{
			bIsOpen = bStartOpen;
		}
		ApplyGateState();
	}
}

void AMallEntranceGate::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);

	const FName LeafName = PropertyChangedEvent.PropertyChain.GetActiveMemberNode()
		? PropertyChangedEvent.PropertyChain.GetActiveMemberNode()->GetValue()->GetFName()
		: NAME_None;
	const FName PropName = PropertyChangedEvent.GetPropertyName();

	if (PropName == TEXT("BoxExtent") || LeafName == TEXT("GateVolume")
		|| PropName == TEXT("RelativeScale3D") || PropName == TEXT("RelativeLocation"))
	{
		if (GateVolume && (PropName == TEXT("BoxExtent") || LeafName == TEXT("BoxExtent")))
		{
			GateVolumeExtent = GateVolume->GetUnscaledBoxExtent();
		}
		if (ClosedBarrier && (PropName == TEXT("RelativeScale3D") || PropName == TEXT("RelativeLocation")))
		{
			ClosedBarrierScale = ClosedBarrier->GetRelativeScale3D();
			ClosedBarrierRelativeLocation = ClosedBarrier->GetRelativeLocation();
		}

		ApplyGateDimensions();
		if (!HasActorBegunPlay())
		{
			bIsOpen = bStartOpen;
		}
		ApplyGateState();
	}
}
#endif

void AMallEntranceGate::BeginPlay()
{
	Super::BeginPlay();

	EnsureDynamicModifiersRuntime();
	ApplyGateDimensions();

	if (bStartOpen)
	{
		Open();
	}
	else
	{
		Close();
	}
}

void AMallEntranceGate::EnsureDynamicModifiersRuntime()
{
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!NavSys)
	{
		return;
	}

	ANavigationData* NavData = NavSys->GetDefaultNavDataInstance(FNavigationSystem::DontCreate);
	if (!NavData)
	{
		return;
	}

	const ERuntimeGenerationType Mode = NavData->GetRuntimeGenerationMode();
	if (Mode == ERuntimeGenerationType::DynamicModifiersOnly
		|| Mode == ERuntimeGenerationType::Dynamic)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[Gate] %s nav runtime OK (%s) — NavModifier Null cut supported"),
			*GetGateDebugLabel(),
			Mode == ERuntimeGenerationType::Dynamic ? TEXT("Dynamic") : TEXT("DynamicModifiersOnly"));
		return;
	}

	// Placed RecastNavMesh often serializes Static; force Modifiers Only (not full Dynamic rebuild).
	if (FEnumProperty* EnumProp = FindFProperty<FEnumProperty>(
			ANavigationData::StaticClass(), TEXT("RuntimeGeneration")))
	{
		void* ValuePtr = EnumProp->ContainerPtrToValuePtr<void>(NavData);
		if (FNumericProperty* Underlying = EnumProp->GetUnderlyingProperty())
		{
			Underlying->SetIntPropertyValue(
				ValuePtr, static_cast<int64>(ERuntimeGenerationType::DynamicModifiersOnly));
		}
	}
	else if (FByteProperty* ByteProp = FindFProperty<FByteProperty>(
			ANavigationData::StaticClass(), TEXT("RuntimeGeneration")))
	{
		ByteProp->SetPropertyValue_InContainer(
			NavData, static_cast<uint8>(ERuntimeGenerationType::DynamicModifiersOnly));
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[Gate] %s forced RecastNavMesh RuntimeGeneration → DynamicModifiersOnly. "
			 "Rebuild Paths once with this gate OPEN so Sidewalk exists under the corridor."),
		*GetGateDebugLabel());
}

void AMallEntranceGate::SyncNavModifierExtent()
{
	if (!NavModifier)
	{
		return;
	}

	// Collision is nav-irrelevant, so modifier bounds come from FailsafeExtent (video path).
	NavModifier->FailsafeExtent = GateVolumeExtent;
}

FBox AMallEntranceGate::GetGateBounds() const
{
	if (GateVolume)
	{
		return GateVolume->Bounds.GetBox();
	}
	return FBox::BuildAABB(GetActorLocation(), GateVolumeExtent);
}

FString AMallEntranceGate::GetGateDebugLabel() const
{
	if (ActorHasTag(FName(TEXT("Entrance2"))) || GetName().Contains(TEXT("Entrance2")))
	{
		return TEXT("Entrance2");
	}
	return GetName();
}

void AMallEntranceGate::RefreshNavigationObstacle()
{
	if (NavModifier)
	{
		NavModifier->RefreshNavigationModifiers();
	}

	if (UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		NavSys->UpdateActorInNavOctree(*this);
		NavSys->AddDirtyArea(
			GetGateBounds().ExpandBy(100.f),
			ENavigationDirtyFlag::All,
			FName(TEXT("MallEntranceGate")));
	}
}

void AMallEntranceGate::Open()
{
	if (bIsOpen)
	{
		return;
	}

	bIsOpen = true;
	ApplyGateState();
	UE_LOG(LogTemp, Log, TEXT("[Gate] %s OPEN — NavModifier off (baked Sidewalk traversable)"),
		*GetGateDebugLabel());

	if (UWorld* World = GetWorld())
	{
		if (USimulationAnalytics* Analytics = World->GetSubsystem<USimulationAnalytics>())
		{
			float SimMin = 0.f;
			if (AScheduleManager* SM = Cast<AScheduleManager>(
				UGameplayStatics::GetActorOfClass(World, AScheduleManager::StaticClass())))
			{
				if (USimulationClock* Clock = SM->GetSimulationClock())
				{
					SimMin = Clock->GetAccumulatedSimMinutes();
				}
			}
			Analytics->RecordFastEntranceOpened(GetName(), SimMin);
		}
	}
}

void AMallEntranceGate::Close()
{
	bIsOpen = false;
	ApplyGateState();
	UE_LOG(LogTemp, Log, TEXT("[Gate] %s CLOSED — NavModifier NavArea_Null + BlockAll"),
		*GetGateDebugLabel());
}

void AMallEntranceGate::ApplyGateDimensions()
{
	const FVector Extent(
		FMath::Max(10.f, GateVolumeExtent.X),
		FMath::Max(10.f, GateVolumeExtent.Y),
		FMath::Max(10.f, GateVolumeExtent.Z));
	GateVolumeExtent = Extent;

	if (GateVolume)
	{
		GateVolume->SetBoxExtent(Extent);
		// Keep collision nav-irrelevant so it never overrides NavModifier (video warning).
		GateVolume->bDynamicObstacle = false;
		GateVolume->SetAreaClassOverride(nullptr);
		GateVolume->SetCanEverAffectNavigation(false);
	}

	if (ClosedBarrier)
	{
		ClosedBarrier->SetRelativeLocation(ClosedBarrierRelativeLocation);
		ClosedBarrier->SetRelativeScale3D(ClosedBarrierScale);
		ClosedBarrier->SetCanEverAffectNavigation(false);
		ClosedBarrier->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	SyncNavModifierExtent();
}

void AMallEntranceGate::ApplyGateState()
{
	if (!GateVolume)
	{
		return;
	}

	const bool bBlock = !bIsOpen;

	// Physics barrier only — never Dynamic Obstacle / never affect navigation.
	GateVolume->bDynamicObstacle = false;
	GateVolume->SetAreaClassOverride(nullptr);
	GateVolume->SetCanEverAffectNavigation(false);
	GateVolume->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	GateVolume->SetCollisionEnabled(
		bBlock ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);

	if (ClosedBarrier)
	{
		ClosedBarrier->SetVisibility(bBlock);
		ClosedBarrier->SetHiddenInGame(!bBlock);
		ClosedBarrier->SetCanEverAffectNavigation(false);
		ClosedBarrier->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	SyncNavModifierExtent();

	if (NavModifier)
	{
		if (bBlock)
		{
			// Closed: Null cut (same as video Area Class = Null).
			NavModifier->SetAreaClass(UNavArea_Null::StaticClass());
			NavModifier->SetNavigationRelevancy(true);
		}
		else
		{
			// Open: remove modifier so baked Sidewalk under the corridor is used again.
			NavModifier->SetAreaClass(nullptr);
			NavModifier->SetNavigationRelevancy(false);
		}
	}

	RefreshNavigationObstacle();
}
