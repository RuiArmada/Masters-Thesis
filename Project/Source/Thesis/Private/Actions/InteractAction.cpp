#include "Actions/InteractAction.h"
#include "Actions/GoToAction.h"
#include "Actions/Interactable.h"
#include "BoidsAB.h"
#include "Simulation/SimulationAnalytics.h"

void UInteractAction::Configure(ABoidsAB* InAgent, AActor* InTargetActor)
{
	SetAgent(InAgent);
	TargetActor = InTargetActor;
}

void UInteractAction::OnStart()
{
	ABoidsAB* Boid = GetAgent();
	AActor* Target = TargetActor.Get();
	if (!Boid || !IsValid(Target))
	{
		Fail(TEXT("Invalid interact target"));
		return;
	}

	IInteractable* Interactable = Cast<IInteractable>(Target);
	if (!Interactable)
	{
		Fail(TEXT("Target does not implement IInteractable"));
		return;
	}

	if (UWorld* World = Boid->GetWorld())
	{
		if (USimulationAnalytics* Analytics = World->GetSubsystem<USimulationAnalytics>())
		{
			Analytics->RecordFastInteractionAttempt(Target->GetName());
		}
	}

	if (!Interactable->CanInteract(Boid))
	{
		if (UWorld* World = Boid->GetWorld())
		{
			if (USimulationAnalytics* Analytics = World->GetSubsystem<USimulationAnalytics>())
			{
				Analytics->RecordFastInteractionFailure(Target->GetName());
			}
		}
		Fail(TEXT("Target rejected CanInteract"));
		return;
	}

	const FVector Approach = Interactable->GetInteractionLocation();
	GoToSubAction = NewObject<UGoToAction>(this);
	GoToSubAction->Configure(Boid, Approach, Target->GetName());
	GoToSubAction->Start();

	if (GoToSubAction->GetState() == EAgentActionState::Failed)
	{
		if (UWorld* World = Boid->GetWorld())
		{
			if (USimulationAnalytics* Analytics = World->GetSubsystem<USimulationAnalytics>())
			{
				Analytics->RecordFastInteractionFailure(Target->GetName());
			}
		}
		Fail(TEXT("GoTo to interactable failed to start"));
	}
}

void UInteractAction::OnTick(float DeltaSeconds)
{
	if (!GoToSubAction)
	{
		Fail(TEXT("Missing GoTo sub-action"));
		return;
	}

	if (!GoToSubAction->IsTerminal())
	{
		GoToSubAction->Tick(DeltaSeconds);
	}

	if (GoToSubAction->GetState() == EAgentActionState::Failed)
	{
		if (AActor* Target = TargetActor.Get())
		{
			if (ABoidsAB* Boid = GetAgent())
			{
				if (USimulationAnalytics* Analytics = Boid->GetWorld()->GetSubsystem<USimulationAnalytics>())
				{
					Analytics->RecordFastInteractionFailure(Target->GetName());
				}
			}
		}
		Fail(TEXT("Failed to reach interactable"));
		return;
	}

	if (GoToSubAction->GetState() != EAgentActionState::Completed)
	{
		return;
	}

	if (bInteractionAttempted)
	{
		return;
	}

	bInteractionAttempted = true;

	ABoidsAB* Boid = GetAgent();
	AActor* Target = TargetActor.Get();
	IInteractable* Interactable = Target ? Cast<IInteractable>(Target) : nullptr;
	if (!Boid || !Interactable)
	{
		Fail(TEXT("Interactable lost before Interact"));
		return;
	}

	USimulationAnalytics* Analytics = Boid->GetWorld()
		? Boid->GetWorld()->GetSubsystem<USimulationAnalytics>()
		: nullptr;

	if (!Interactable->CanInteract(Boid))
	{
		if (Analytics)
		{
			Analytics->RecordFastInteractionFailure(Target->GetName());
		}
		Fail(TEXT("CanInteract failed at arrival"));
		return;
	}

	if (!Interactable->Interact(Boid))
	{
		if (Analytics)
		{
			Analytics->RecordFastInteractionFailure(Target->GetName());
		}
		Fail(TEXT("Interact() returned false"));
		return;
	}

	if (Analytics)
	{
		Analytics->RecordFastInteractionSuccess(Target->GetName());
	}

	UE_LOG(LogTemp, Log, TEXT("[InteractAction] %s interacted with %s"),
		*Boid->AgentId.ToString(),
		*Target->GetName());
	Complete();
}

void UInteractAction::OnFailed(const FString& Reason)
{
	if (GoToSubAction && !GoToSubAction->IsTerminal())
	{
		GoToSubAction->Cancel();
	}
}
