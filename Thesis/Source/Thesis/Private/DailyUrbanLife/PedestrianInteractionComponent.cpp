#include "DailyUrbanLife/PedestrianInteractionComponent.h"
#include "BoidsAB.h"
#include "AI/AgentNeedsComponent.h"
#include "Simulation/SimulationAnalytics.h"
#include "Simulation/SimulationFeatureRegistry.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"

UPedestrianInteractionComponent::UPedestrianInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

static bool CanAgentEngageInCasualGreet(const ABoidsAB* Boid)
{
	if (!Boid)
	{
		return false;
	}

	return !Boid->IsMoving()
		&& !Boid->IsIdle()
		&& !Boid->IsInteractingWithStation()
		&& !Boid->IsWaitingAtStation();
}

void UPedestrianInteractionComponent::TickInteractions(float DeltaTime, ABoidsAB* OwnerBoid)
{
	if (!OwnerBoid || !GetWorld())
	{
		return;
	}

	if (bPausedForInteraction)
	{
		PauseRemainingSeconds -= DeltaTime;
		if (PauseRemainingSeconds <= 0.f)
		{
			EndBriefPause();
		}
		return;
	}

	if (USimulationFeatureRegistry* Registry = GetWorld()->GetSubsystem<USimulationFeatureRegistry>())
	{
		if (!Registry->IsFeatureEnabled(ESimulationFeature::DailyUrbanLife))
		{
			return;
		}
	}

	if (!CanAgentEngageInCasualGreet(OwnerBoid))
	{
		return;
	}

	if (OwnerBoid->GetNeedsComponent() && OwnerBoid->GetNeedsComponent()->IsHungry())
	{
		return;
	}

	if (OwnerBoid->IsNearAnyFoodLocation(450.f))
	{
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	if (Now - LastGreetWorldTime < GreetCooldownSeconds)
	{
		return;
	}

	TArray<AActor*> AllBoids;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ABoidsAB::StaticClass(), AllBoids);
	const FVector MyLoc = OwnerBoid->GetActorLocation();
	const float CasualDistSq = FMath::Square(350.f);

	for (AActor* Actor : AllBoids)
	{
		if (Actor == OwnerBoid)
		{
			continue;
		}

		ABoidsAB* Other = Cast<ABoidsAB>(Actor);
		if (!Other || !CanAgentEngageInCasualGreet(Other))
		{
			continue;
		}

		if (Other->GetNeedsComponent() && Other->GetNeedsComponent()->IsHungry())
		{
			continue;
		}

		if (Other->IsNearAnyFoodLocation(450.f))
		{
			continue;
		}

		const float* LastPartnerTime = LastGreetByPartner.Find(Other);
		if (LastPartnerTime && (Now - *LastPartnerTime) < PartnerGreetCooldownSeconds)
		{
			continue;
		}

		if (FVector::DistSquared(MyLoc, Other->GetActorLocation()) <= CasualDistSq)
		{
			TriggerCasualGreet(Other);
			break;
		}
	}
}

void UPedestrianInteractionComponent::BeginBriefPause(float DurationSeconds)
{
	if (DurationSeconds <= 0.f)
	{
		return;
	}

	bPausedForInteraction = true;
	PauseRemainingSeconds = DurationSeconds;
	bResumeMovementAfterPause = false;

	if (ABoidsAB* Owner = Cast<ABoidsAB>(GetOwner()))
	{
		bResumeMovementAfterPause = Owner->IsMoving();
		if (bResumeMovementAfterPause)
		{
			if (UCharacterMovementComponent* Move = Owner->GetCharacterMovement())
			{
				Move->StopMovementImmediately();
			}
		}
	}
}

void UPedestrianInteractionComponent::TriggerCasualGreet(ABoidsAB* Other)
{
	if (!Other || !GetOwner())
	{
		return;
	}

	ABoidsAB* Owner = Cast<ABoidsAB>(GetOwner());
	if (!Owner || !CanAgentEngageInCasualGreet(Owner) || !CanAgentEngageInCasualGreet(Other))
	{
		return;
	}

	LastGreetWorldTime = GetWorld()->GetTimeSeconds();
	LastGreetByPartner.Add(Other, LastGreetWorldTime);
	Owner->OnHelloWave();

	if (USimulationAnalytics* Analytics = GetWorld()->GetSubsystem<USimulationAnalytics>())
	{
		Analytics->RecordDailyInteraction(Owner, Other, TEXT("Greet"));
	}

	const FDailyUrbanLifeSettings& Settings =
		GetWorld()->GetSubsystem<USimulationFeatureRegistry>()->GetDailyUrbanLifeSettings();
	const float Pause = FMath::FRandRange(Settings.InteractionPauseMinSeconds, Settings.InteractionPauseMaxSeconds);
	BeginBriefPause(Pause);
}

void UPedestrianInteractionComponent::CancelBriefPause()
{
	if (bPausedForInteraction)
	{
		EndBriefPause();
	}
}

void UPedestrianInteractionComponent::EndBriefPause()
{
	const bool bShouldResumeMovement = bResumeMovementAfterPause;
	bPausedForInteraction = false;
	PauseRemainingSeconds = 0.f;
	bResumeMovementAfterPause = false;

	if (ABoidsAB* Owner = Cast<ABoidsAB>(GetOwner()))
	{
		if (bShouldResumeMovement && Owner->IsMoving() && !Owner->IsIdle())
		{
			Owner->RecomputePathToCurrentTarget();
		}
	}
}
