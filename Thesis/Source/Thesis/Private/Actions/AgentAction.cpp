#include "Actions/AgentAction.h"
#include "BoidsAB.h"

void UAgentAction::SetAgent(ABoidsAB* InAgent)
{
	Agent = InAgent;
}

void UAgentAction::Start()
{
	if (State != EAgentActionState::NotStarted)
	{
		return;
	}

	if (!Agent.IsValid())
	{
		Fail(TEXT("Invalid agent"));
		return;
	}

	State = EAgentActionState::Running;
	OnStart();
}

void UAgentAction::Tick(float DeltaSeconds)
{
	if (State != EAgentActionState::Running)
	{
		return;
	}

	if (!Agent.IsValid())
	{
		Fail(TEXT("Agent became invalid"));
		return;
	}

	OnTick(DeltaSeconds);
}

void UAgentAction::Cancel()
{
	if (State == EAgentActionState::Running)
	{
		Fail(TEXT("Cancelled"));
	}
}

void UAgentAction::Complete()
{
	if (State != EAgentActionState::Running)
	{
		return;
	}

	State = EAgentActionState::Completed;
	OnCompleted();
}

void UAgentAction::Fail(const FString& Reason)
{
	if (State == EAgentActionState::Completed || State == EAgentActionState::Failed)
	{
		return;
	}

	State = EAgentActionState::Failed;
	OnFailed(Reason);

	if (!Reason.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[AgentAction] %s failed: %s"),
			*GetClass()->GetName(), *Reason);
	}
}
