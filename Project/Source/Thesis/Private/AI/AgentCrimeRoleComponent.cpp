#include "AI/AgentCrimeRoleComponent.h"
#include "BoidsAB.h"
#include "Simulation/SimulationFeatureRegistry.h"
#include "Simulation/SimulationAnalytics.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"

namespace CrimeBoidCache
{
	static uint64 CachedFrame = MAX_uint64;
	static TArray<TWeakObjectPtr<ABoidsAB>> CachedBoids;

	static const TArray<TWeakObjectPtr<ABoidsAB>>& Get(UWorld* World)
	{
		const uint64 Frame = GFrameCounter;
		if (Frame != CachedFrame || CachedBoids.Num() == 0)
		{
			CachedFrame = Frame;
			CachedBoids.Reset();
			if (World)
			{
				TArray<AActor*> Found;
				UGameplayStatics::GetAllActorsOfClass(World, ABoidsAB::StaticClass(), Found);
				CachedBoids.Reserve(Found.Num());
				for (AActor* Actor : Found)
				{
					if (ABoidsAB* Boid = Cast<ABoidsAB>(Actor))
					{
						CachedBoids.Add(Boid);
					}
				}
			}
		}
		return CachedBoids;
	}
}

UAgentCrimeRoleComponent::UAgentCrimeRoleComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UAgentCrimeRoleComponent::SetRole(ECrimeRole NewRole)
{
	RestoreWalkSpeed(Cast<ABoidsAB>(GetOwner()));
	Role = NewRole;
	BehaviorState = ECrimeBehaviorState::None;
	ClearFocus();
	CrimeCheckTimer = 0.f;
	CrimeActiveTimer = 0.f;
	CapturedCooldown = 0.f;
	bResponseCounted = false;
}

bool UAgentCrimeRoleComponent::TryParseRoleString(const FString& RoleStr, ECrimeRole& OutRole)
{
	if (RoleStr.Equals(TEXT("Police"), ESearchCase::IgnoreCase))
	{
		OutRole = ECrimeRole::Police;
		return true;
	}
	if (RoleStr.Equals(TEXT("Robber"), ESearchCase::IgnoreCase))
	{
		OutRole = ECrimeRole::Robber;
		return true;
	}
	if (RoleStr.Equals(TEXT("Civilian"), ESearchCase::IgnoreCase))
	{
		OutRole = ECrimeRole::Civilian;
		return true;
	}
	return false;
}

void UAgentCrimeRoleComponent::ClearFocus()
{
	FocusTarget.Reset();
}

bool UAgentCrimeRoleComponent::WantsCrimeResponse() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const USimulationFeatureRegistry* Registry = World->GetSubsystem<USimulationFeatureRegistry>();
	if (!Registry || !Registry->IsFeatureEnabled(ESimulationFeature::CopsAndRobbers))
	{
		return false;
	}

	switch (BehaviorState)
	{
	case ECrimeBehaviorState::Crime:
	case ECrimeBehaviorState::Fleeing:
	case ECrimeBehaviorState::Pursuing:
	case ECrimeBehaviorState::Avoiding:
	case ECrimeBehaviorState::Captured:
		return true;
	default:
		return false;
	}
}

float UAgentCrimeRoleComponent::ConsumeUpdateTimer(float DeltaTime)
{
	UpdateTimer -= DeltaTime;
	return UpdateTimer;
}

void UAgentCrimeRoleComponent::ResetUpdateTimer(float Interval)
{
	UpdateTimer = Interval;
}

void UAgentCrimeRoleComponent::ApplyChaseRunSpeed(ABoidsAB* Self)
{
	if (!Self)
	{
		return;
	}

	UCharacterMovementComponent* MoveComp = Self->GetCharacterMovement();
	if (!MoveComp)
	{
		return;
	}

	const USimulationFeatureRegistry* Registry = Self->GetWorld()
		? Self->GetWorld()->GetSubsystem<USimulationFeatureRegistry>()
		: nullptr;
	const float RunSpeed = Registry
		? Registry->GetCopsAndRobbersSettings().ChaseRunSpeed
		: 450.f;

	if (!bUsingChaseRunSpeed)
	{
		CachedWalkSpeed = MoveComp->MaxWalkSpeed;
		bUsingChaseRunSpeed = true;
	}

	MoveComp->MaxWalkSpeed = FMath::Max(1.f, RunSpeed);
}

void UAgentCrimeRoleComponent::RestoreWalkSpeed(ABoidsAB* Self)
{
	if (!bUsingChaseRunSpeed)
	{
		return;
	}

	bUsingChaseRunSpeed = false;

	if (!Self)
	{
		CachedWalkSpeed = 0.f;
		return;
	}

	if (UCharacterMovementComponent* MoveComp = Self->GetCharacterMovement())
	{
		const float Walk = CachedWalkSpeed > 0.f ? CachedWalkSpeed : 200.f;
		MoveComp->MaxWalkSpeed = Walk;
	}
	CachedWalkSpeed = 0.f;
}

void UAgentCrimeRoleComponent::EnterCrime(ABoidsAB* Self)
{
	RestoreWalkSpeed(Self);
	BehaviorState = ECrimeBehaviorState::Crime;
	CrimeActiveTimer = 0.f;
	ClearFocus();

	if (Self && Self->GetWorld())
	{
		if (USimulationAnalytics* Analytics = Self->GetWorld()->GetSubsystem<USimulationAnalytics>())
		{
			Analytics->RecordCrimeEvent(TEXT("CrimeStarted"));
		}
	}

	UE_LOG(LogTemp, Verbose, TEXT("[CopsAndRobbers] %s entered Crime state"), Self ? *Self->GetName() : TEXT("?"));
}

void UAgentCrimeRoleComponent::ExitCrime()
{
	RestoreWalkSpeed(Cast<ABoidsAB>(GetOwner()));
	BehaviorState = ECrimeBehaviorState::None;
	CrimeActiveTimer = 0.f;
	ClearFocus();
}

void UAgentCrimeRoleComponent::BeginPursuit(ABoidsAB* Self, ABoidsAB* Robber)
{
	if (!Robber)
	{
		return;
	}

	BehaviorState = ECrimeBehaviorState::Pursuing;
	FocusTarget = Robber;
	PursuitElapsed = 0.f;
	ApplyChaseRunSpeed(Self);

	if (!bResponseCounted && Self && Self->GetWorld())
	{
		bResponseCounted = true;
		if (USimulationAnalytics* Analytics = Self->GetWorld()->GetSubsystem<USimulationAnalytics>())
		{
			Analytics->RecordCrimeEvent(TEXT("PoliceResponse"));
			Analytics->RecordCrimePursuitStarted(Self, Robber);
		}
	}

	UE_LOG(LogTemp, Verbose, TEXT("[CopsAndRobbers] %s pursuing %s"),
		Self ? *Self->GetName() : TEXT("?"),
		*Robber->GetName());
}

void UAgentCrimeRoleComponent::EndPursuit(bool bCaptured)
{
	ABoidsAB* Self = Cast<ABoidsAB>(GetOwner());

	if (bCaptured)
	{
		if (ABoidsAB* Robber = FocusTarget.Get())
		{
			if (UAgentCrimeRoleComponent* RobberCrime = Robber->GetCrimeRoleComponent())
			{
				RobberCrime->BehaviorState = ECrimeBehaviorState::Captured;
				RobberCrime->CapturedCooldown = 4.f;
				RobberCrime->ClearFocus();
				RobberCrime->RestoreWalkSpeed(Robber);
			}
		}

		if (UWorld* World = GetWorld())
		{
			if (USimulationAnalytics* Analytics = World->GetSubsystem<USimulationAnalytics>())
			{
				Analytics->RecordCrimeEvent(TEXT("Capture"));
				Analytics->RecordCrimePursuitEnded(Self, FocusTarget.Get(), PursuitElapsed, true);
			}
		}
	}
	else if (UWorld* World = GetWorld())
	{
		if (USimulationAnalytics* Analytics = World->GetSubsystem<USimulationAnalytics>())
		{
			Analytics->RecordCrimePursuitEnded(Self, FocusTarget.Get(), PursuitElapsed, false);
		}
	}

	RestoreWalkSpeed(Self);
	BehaviorState = ECrimeBehaviorState::None;
	ClearFocus();
	bResponseCounted = false;
	PursuitElapsed = 0.f;
}

void UAgentCrimeRoleComponent::BeginFlee(ABoidsAB* Self, ABoidsAB* Police)
{
	BehaviorState = ECrimeBehaviorState::Fleeing;
	FocusTarget = Police;
	ApplyChaseRunSpeed(Self);
	UE_LOG(LogTemp, Verbose, TEXT("[CopsAndRobbers] %s fleeing %s"),
		Self ? *Self->GetName() : TEXT("?"),
		Police ? *Police->GetName() : TEXT("?"));
}

void UAgentCrimeRoleComponent::BeginAvoid(ABoidsAB* Self, ABoidsAB* Robber)
{
	BehaviorState = ECrimeBehaviorState::Avoiding;
	FocusTarget = Robber;
}

ABoidsAB* UAgentCrimeRoleComponent::FindNearestRobberInCrime(ABoidsAB* Self, float Radius) const
{
	if (!Self || !Self->GetWorld())
	{
		return nullptr;
	}

	ABoidsAB* Best = nullptr;
	float BestDistSq = FMath::Square(Radius);
	const FVector SelfLoc = Self->GetActorLocation();

	for (const TWeakObjectPtr<ABoidsAB>& Ptr : CrimeBoidCache::Get(Self->GetWorld()))
	{
		ABoidsAB* Other = Ptr.Get();
		if (!Other || Other == Self)
		{
			continue;
		}

		const UAgentCrimeRoleComponent* OtherCrime = Other->GetCrimeRoleComponent();
		if (!OtherCrime || OtherCrime->GetRole() != ECrimeRole::Robber)
		{
			continue;
		}

		const ECrimeBehaviorState St = OtherCrime->GetBehaviorState();
		if (St != ECrimeBehaviorState::Crime && St != ECrimeBehaviorState::Fleeing)
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(SelfLoc, Other->GetActorLocation());
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = Other;
		}
	}
	return Best;
}

ABoidsAB* UAgentCrimeRoleComponent::FindNearestPolice(ABoidsAB* Self, float Radius) const
{
	if (!Self || !Self->GetWorld())
	{
		return nullptr;
	}

	ABoidsAB* Best = nullptr;
	float BestDistSq = FMath::Square(Radius);
	const FVector SelfLoc = Self->GetActorLocation();

	for (const TWeakObjectPtr<ABoidsAB>& Ptr : CrimeBoidCache::Get(Self->GetWorld()))
	{
		ABoidsAB* Other = Ptr.Get();
		if (!Other || Other == Self)
		{
			continue;
		}

		const UAgentCrimeRoleComponent* OtherCrime = Other->GetCrimeRoleComponent();
		if (!OtherCrime || OtherCrime->GetRole() != ECrimeRole::Police)
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(SelfLoc, Other->GetActorLocation());
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = Other;
		}
	}
	return Best;
}

ABoidsAB* UAgentCrimeRoleComponent::FindNearestThreateningRobber(ABoidsAB* Self, float Radius) const
{
	return FindNearestRobberInCrime(Self, Radius);
}

void UAgentCrimeRoleComponent::UpdateCrimeLogic(float DeltaTime, ABoidsAB* Self)
{
	if (!Self)
	{
		return;
	}

	const USimulationFeatureRegistry* Registry = Self->GetWorld()
		? Self->GetWorld()->GetSubsystem<USimulationFeatureRegistry>()
		: nullptr;
	if (!Registry || !Registry->IsFeatureEnabled(ESimulationFeature::CopsAndRobbers))
	{
		return;
	}

	if (Self->ShouldIgnoreScheduleDuringEvacuation())
	{
		return;
	}

	const FCopsAndRobbersSettings& Settings = Registry->GetCopsAndRobbersSettings();

			if (BehaviorState == ECrimeBehaviorState::Captured)
		{
			CapturedCooldown -= DeltaTime;
			if (CapturedCooldown <= 0.f)
			{
				ExitCrime();
			}
			return;
		}

	if (Role == ECrimeRole::Robber)
	{
		if (BehaviorState == ECrimeBehaviorState::None)
		{
			CrimeCheckTimer -= DeltaTime;
			if (CrimeCheckTimer <= 0.f)
			{
				CrimeCheckTimer = Settings.CrimeCheckInterval;
				if (FMath::FRand() < Settings.CrimeProbability)
				{
					EnterCrime(Self);
				}
			}
		}
		else if (BehaviorState == ECrimeBehaviorState::Crime || BehaviorState == ECrimeBehaviorState::Fleeing)
		{
			CrimeActiveTimer += DeltaTime;

			if (ABoidsAB* Police = FindNearestPolice(Self, Settings.RobberDetectionRadius))
			{
				if (BehaviorState != ECrimeBehaviorState::Fleeing || FocusTarget.Get() != Police)
				{
					BeginFlee(Self, Police);
				}
				else
				{
					ApplyChaseRunSpeed(Self);
				}
			}
			else if (BehaviorState == ECrimeBehaviorState::Fleeing)
			{
				// Police left range — remain in Crime until duration expires.
				RestoreWalkSpeed(Self);
				BehaviorState = ECrimeBehaviorState::Crime;
				ClearFocus();
			}

			if (CrimeActiveTimer >= Settings.CrimeDuration && BehaviorState == ECrimeBehaviorState::Crime)
			{
				ExitCrime();
			}
		}
		return;
	}

	if (Role == ECrimeRole::Police)
	{
		if (BehaviorState == ECrimeBehaviorState::Pursuing)
		{
			PursuitElapsed += DeltaTime;
			ABoidsAB* Robber = FocusTarget.Get();
			if (!IsValid(Robber))
			{
				EndPursuit(false);
				return;
			}

			const UAgentCrimeRoleComponent* RobberCrime = Robber->GetCrimeRoleComponent();
			if (!RobberCrime
				|| RobberCrime->GetRole() != ECrimeRole::Robber
				|| RobberCrime->GetBehaviorState() == ECrimeBehaviorState::None
				|| RobberCrime->GetBehaviorState() == ECrimeBehaviorState::Captured)
			{
				EndPursuit(false);
				return;
			}

			ApplyChaseRunSpeed(Self);

			if (FVector::Dist(Self->GetActorLocation(), Robber->GetActorLocation()) <= Settings.CaptureRadius)
			{
				EndPursuit(true);
			}
			return;
		}

		if (ABoidsAB* Robber = FindNearestRobberInCrime(Self, Settings.PoliceDetectionRadius))
		{
			BeginPursuit(Self, Robber);
		}
		return;
	}

	// Civilian
	if (Settings.bCiviliansAvoidRobbers)
	{
		if (ABoidsAB* Robber = FindNearestThreateningRobber(Self, Settings.CivilianAvoidRadius))
		{
			BeginAvoid(Self, Robber);
		}
		else if (BehaviorState == ECrimeBehaviorState::Avoiding)
		{
			ExitCrime();
		}
	}
}

bool UAgentCrimeRoleComponent::ComputeCrimeDestination(FVector& OutDestination) const
{
	ABoidsAB* Self = Cast<ABoidsAB>(GetOwner());
	if (!Self)
	{
		return false;
	}

	const USimulationFeatureRegistry* Registry = Self->GetWorld()
		? Self->GetWorld()->GetSubsystem<USimulationFeatureRegistry>()
		: nullptr;
	const FCopsAndRobbersSettings& Settings = Registry
		? Registry->GetCopsAndRobbersSettings()
		: FCopsAndRobbersSettings();

	ABoidsAB* Target = FocusTarget.Get();
	const FVector SelfLoc = Self->GetActorLocation();

	switch (BehaviorState)
	{
	case ECrimeBehaviorState::Pursuing:
		if (!IsValid(Target))
		{
			return false;
		}
		OutDestination = Target->GetActorLocation();
		return true;

	case ECrimeBehaviorState::Fleeing:
	case ECrimeBehaviorState::Avoiding:
	{
		if (!IsValid(Target))
		{
			return false;
		}
		FVector Away = SelfLoc - Target->GetActorLocation();
		Away.Z = 0.f;
		if (!Away.Normalize())
		{
			Away = Self->GetActorForwardVector();
			Away.Z = 0.f;
			Away.Normalize();
		}
		const float Dist = BehaviorState == ECrimeBehaviorState::Fleeing
			                   ? Settings.FleeDistance
			                   : Settings.FleeDistance * 0.75f;
		OutDestination = SelfLoc + Away * Dist;
		return true;
	}

	case ECrimeBehaviorState::Crime:
		// Stay near current location (crime in place); slight hold.
		OutDestination = SelfLoc;
		return true;

	case ECrimeBehaviorState::Captured:
		OutDestination = SelfLoc;
		return true;

	default:
		return false;
	}
}
