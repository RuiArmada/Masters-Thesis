#pragma once

#include "CoreMinimal.h"
#include "AgentStationAnimationTypes.generated.h"

/**
 * Generic station interaction animation asset type.
 * Stations assign an asset; agents play/stop it without use-case-specific APIs.
 */
UENUM(BlueprintType)
enum class EStationInteractionAnimType : uint8
{
	None UMETA(DisplayName = "None"),
	Montage UMETA(DisplayName = "Animation Montage"),
	Sequence UMETA(DisplayName = "Animation Sequence")
};

/**
 * How an agent aligns to the station interaction transform after arrival,
 * before the interaction animation starts.
 * None = legacy behavior (start interaction at arrival location).
 */
UENUM(BlueprintType)
enum class EStationInteractionPositionMode : uint8
{
	None UMETA(DisplayName = "None"),
	SnapToTransform UMETA(DisplayName = "Snap To Transform"),
	InterpolateToTransform UMETA(DisplayName = "Interpolate To Transform")
};
