#include "Actions/GoToAction.h"
#include "BoidsAB.h"
#include "Simulation/SimulationAnalytics.h"

void UGoToAction::Configure(ABoidsAB* InAgent, const FVector& InTarget, const FString& InDestinationTag)
{
	SetAgent(InAgent);
	TargetLocation = InTarget;
	DestinationTag = InDestinationTag;
}

void UGoToAction::OnStart()
{
	ABoidsAB* Boid = GetAgent();
	if (!Boid)
	{
		Fail(TEXT("Invalid agent"));
		return;
	}

	Boid->NotifyScheduleMoveTo(TargetLocation, DestinationTag);
	bMoveIssued = true;
	ElapsedSeconds = 0.f;

	UE_LOG(LogTemp, Log, TEXT("[GoToAction] %s -> '%s' (%s)"),
		*Boid->AgentId.ToString(),
		*DestinationTag,
		*TargetLocation.ToString());
}

void UGoToAction::OnTick(float DeltaSeconds)
{
	ABoidsAB* Boid = GetAgent();
	if (!Boid || !bMoveIssued)
	{
		Fail(TEXT("GoTo lost agent"));
		return;
	}

	ElapsedSeconds += DeltaSeconds;

	if (Boid->bHasArrived || Boid->bReachedDestination)
	{
		Complete();
		return;
	}

	const float Dist2D = FVector::Dist2D(Boid->GetActorLocation(), TargetLocation);
	if (!Boid->IsMoving() && Dist2D <= ArrivalRadius2D)
	{
		Complete();
		return;
	}

	// Path generation failed: still marked moving but no waypoints and not near target.
	if (ElapsedSeconds >= 0.5f && Boid->IsMoving() && !Boid->HasComputedPath() && Dist2D > ArrivalRadius2D)
	{
		Fail(FString::Printf(TEXT("GoTo path failed (tag='%s')"), *DestinationTag));
		return;
	}

	if (ElapsedSeconds >= MaxWaitSeconds)
	{
		if (Dist2D <= ArrivalRadius2D)
		{
			Complete();
			return;
		}

		Fail(FString::Printf(TEXT("GoTo timed out (dist2D=%.1f tag='%s')"), Dist2D, *DestinationTag));
	}
}

void UGoToAction::OnFailed(const FString& Reason)
{
	ABoidsAB* Boid = GetAgent();
	if (Boid)
	{
		Boid->StopMovement();
		Boid->SetIdle(false);
	}

	if (!Boid || DestinationTag.IsEmpty())
	{
		return;
	}

	if (UWorld* World = Boid->GetWorld())
	{
		if (USimulationAnalytics* Analytics = World->GetSubsystem<USimulationAnalytics>())
		{
			Analytics->RecordFastDestinationFailed(DestinationTag);
		}
	}
}
