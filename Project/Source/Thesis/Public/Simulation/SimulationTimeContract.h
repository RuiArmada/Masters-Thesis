#pragma once

#include "CoreMinimal.h"

/**
 * Accurate Time timing contract (simulation seconds vs real seconds).
 *
 * SimulationSpeed = simulation seconds advanced per real-world second.
 * Effective simulation delta = RealDeltaSeconds * SimulationSpeed.
 * Accurate Time target: SimulationSpeed = 30.
 *
 * CharacterMovement integrates MaxWalkSpeed * real frame delta (not a scaled Tick delta).
 * To stay locked to the clock, Accurate Time sets MaxWalkSpeed = BaseUuPerSimSecond * SimulationSpeed
 * and does NOT also multiply the movement tick delta. Applying both would double-count.
 *
 * Visual and Fast do not use this movement contract (Visual moves at 1x walk speed;
 * Fast uses its own MovementSpeedMultiplier, independent of its 600x clock).
 */
namespace SimulationTimeContract
{
	/** Simulation seconds per real second for Accurate Time. */
	inline constexpr float AccurateTimeSimSecondsPerRealSecond = 30.f;

	/** Simulation seconds advanced by one real frame. Units: simulation seconds. */
	inline double EffectiveSimDelta(float RealDeltaSeconds, float SimSecondsPerRealSecond)
	{
		if (RealDeltaSeconds <= 0.f || SimSecondsPerRealSecond <= 0.f)
		{
			return 0.0;
		}
		return static_cast<double>(RealDeltaSeconds) * static_cast<double>(SimSecondsPerRealSecond);
	}

	/**
	 * CMC MaxWalkSpeed (uu per real second) so distance per real second matches
	 * BaseUuPerSimSecond * SimSecondsPerRealSecond.
	 * Do not also scale the movement component's DeltaTime.
	 */
	inline float SyncedMaxWalkSpeed(float BaseUuPerSimSecond, float SimSecondsPerRealSecond)
	{
		return BaseUuPerSimSecond * SimSecondsPerRealSecond;
	}

	/**
	 * Engine-timer seconds for a duration authored as Visual real seconds.
	 * Visual's clock turns that many real seconds into the same number of sim minutes
	 * (authored * VisualSimPerReal sim seconds). Accurate Time keeps that occupancy:
	 * wall = authored * VisualSimPerReal / AccurateSimPerReal.
	 * Example: duration 14 at Visual 60 and Accurate 30 sits for 28 real seconds
	 * and 840 sim seconds (14 sim minutes). Fast does not use this.
	 */
	inline float AccurateTimeWallSecondsForVisualDuration(
		float AuthoredRealSeconds,
		float VisualSimSecondsPerRealSecond,
		float AccurateSimSecondsPerRealSecond)
	{
		if (AuthoredRealSeconds <= 0.f
			|| VisualSimSecondsPerRealSecond <= 0.f
			|| AccurateSimSecondsPerRealSecond <= 0.f)
		{
			return AuthoredRealSeconds;
		}
		return AuthoredRealSeconds * VisualSimSecondsPerRealSecond / AccurateSimSecondsPerRealSecond;
	}
}
