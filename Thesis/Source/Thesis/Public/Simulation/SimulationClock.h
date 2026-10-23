#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "SimulationClock.generated.h"

/**
 * Authoritative simulation time-of-day clock.
 * Advances from real DeltaSeconds * SimulationSpeed (sim seconds per real second).
 *
 * Default SimulationSpeed = 60 preserves legacy ScheduleManager pace:
 * 1 real second → 60 sim seconds → 1 sim minute (HH:MM schedule step).
 *
 * StartHour/StartMinute set the time-of-day at Reset() (e.g. 08:00).
 * Schedule XML times are minutes-from-midnight compared against this clock.
 */
UCLASS(BlueprintType)
class THESIS_API USimulationClock : public UObject
{
	GENERATED_BODY()

public:
	static constexpr double SecondsPerMinute = 60.0;
	static constexpr double SecondsPerDay = 86400.0;
	static constexpr int32 MinutesPerDay = 1440;

	/** Default: 1 real second = 60 sim seconds = 1 schedule minute (legacy pace). */
	static constexpr float DefaultSimulationSpeed = 60.f;

	UFUNCTION(BlueprintCallable, Category = "Simulation|Clock")
	void Reset();

	UFUNCTION(BlueprintCallable, Category = "Simulation|Clock")
	void Tick(float RealDeltaSeconds);

	UFUNCTION(BlueprintCallable, Category = "Simulation|Clock")
	void SetSimulationSpeed(float Speed);

	UFUNCTION(BlueprintCallable, Category = "Simulation|Clock")
	float GetSimulationSpeed() const { return SimulationSpeed; }

	/** Configures the time-of-day applied by the next Reset(). */
	UFUNCTION(BlueprintCallable, Category = "Simulation|Clock")
	void SetStartTimeOfDay(int32 Hour, int32 Minute);

	UFUNCTION(BlueprintCallable, Category = "Simulation|Clock")
	int32 GetStartHour() const { return StartHour; }

	UFUNCTION(BlueprintCallable, Category = "Simulation|Clock")
	int32 GetStartMinute() const { return StartMinute; }

	UFUNCTION(BlueprintCallable, Category = "Simulation|Clock")
	double GetTotalSimSeconds() const { return TotalSimSeconds; }

	UFUNCTION(BlueprintCallable, Category = "Simulation|Clock")
	int32 GetDay() const;

	UFUNCTION(BlueprintCallable, Category = "Simulation|Clock")
	int32 GetHour() const;

	UFUNCTION(BlueprintCallable, Category = "Simulation|Clock")
	int32 GetMinute() const;

	UFUNCTION(BlueprintCallable, Category = "Simulation|Clock")
	int32 GetSecond() const;

	/** Minutes from midnight today (0..1439). */
	UFUNCTION(BlueprintCallable, Category = "Simulation|Clock")
	int32 GetCurrentDayMinute() const;

	/** Seconds from midnight today (0..86399.999). */
	UFUNCTION(BlueprintCallable, Category = "Simulation|Clock")
	double GetTimeOfDaySeconds() const;

	/** Legacy ScheduleManager field: total sim minutes elapsed (TotalSimSeconds / 60). */
	UFUNCTION(BlueprintCallable, Category = "Simulation|Clock")
	float GetAccumulatedSimMinutes() const;

	/** HH:MM:SS */
	UFUNCTION(BlueprintCallable, Category = "Simulation|Clock")
	FString ToTimeOfDayString() const;

	/** HH:MM (for debug UI / client demos). */
	UFUNCTION(BlueprintCallable, Category = "Simulation|Clock")
	FString ToTimeOfDayHHMMString() const;

	/**
	 * Advance the schedule cursor by one minute toward the clock's current time.
	 * Returns false when the cursor has caught up (no new minute to process).
	 * Used so high SimulationSpeed cannot skip HH:MM schedule steps.
	 */
	bool AdvanceScheduleCursor(int32& InOutDay, int32& InOutDayMinute) const;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Simulation|Clock")
	float SimulationSpeed = DefaultSimulationSpeed;

	/** Time-of-day hour applied by Reset() (0..23). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Simulation|Clock", meta = (ClampMin = "0", ClampMax = "23"))
	int32 StartHour = 8;

	/** Time-of-day minute applied by Reset() (0..59). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Simulation|Clock", meta = (ClampMin = "0", ClampMax = "59"))
	int32 StartMinute = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Simulation|Clock")
	double TotalSimSeconds = 0.0;
};
