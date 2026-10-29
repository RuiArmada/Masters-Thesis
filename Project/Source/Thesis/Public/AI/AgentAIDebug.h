#pragma once

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"

/**
 * Agent AI / schedule diagnostic logging.
 * Default OFF to keep PIE Output Log readable for thesis demos.
 * Enable at runtime: thesis.AgentAIDebug 1
 *                  thesis.VerboseSchedule 1
 */
#if UE_BUILD_SHIPPING
#define THESIS_AGENT_AI_LOG(Category, Verbosity, Format, ...)
#define THESIS_SCHEDULE_VERBOSE_LOG(Category, Verbosity, Format, ...)
#else

inline TAutoConsoleVariable<int32>& ThesisCVarAgentAIDebug()
{
	static TAutoConsoleVariable<int32> CVar(
		TEXT("thesis.AgentAIDebug"),
		0,
		TEXT("1 = enable periodic AgentAI priority-layer logs"),
		ECVF_Default);
	return CVar;
}

inline TAutoConsoleVariable<int32>& ThesisCVarVerboseSchedule()
{
	static TAutoConsoleVariable<int32> CVar(
		TEXT("thesis.VerboseSchedule"),
		0,
		TEXT("1 = enable verbose Schedule/Rule/Condition diagnostic logs"),
		ECVF_Default);
	return CVar;
}

#define THESIS_AGENT_AI_LOG(Category, Verbosity, Format, ...) \
	do \
	{ \
		if (ThesisCVarAgentAIDebug().GetValueOnGameThread() != 0) \
		{ \
			UE_LOG(Category, Verbosity, Format, ##__VA_ARGS__); \
		} \
	} while (0)

#define THESIS_SCHEDULE_VERBOSE_LOG(Category, Verbosity, Format, ...) \
	do \
	{ \
		if (ThesisCVarVerboseSchedule().GetValueOnGameThread() != 0) \
		{ \
			UE_LOG(Category, Verbosity, Format, ##__VA_ARGS__); \
		} \
	} while (0)

#endif
