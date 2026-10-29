#include "Simulation/SimulationClock.h"
#include "Simulation/SimulationTimeContract.h"

void USimulationClock::Reset()
{
	const int32 H = FMath::Clamp(StartHour, 0, 23);
	const int32 M = FMath::Clamp(StartMinute, 0, 59);
	TotalSimSeconds = static_cast<double>(H) * 3600.0 + static_cast<double>(M) * SecondsPerMinute;
}

void USimulationClock::Tick(float RealDeltaSeconds)
{
	if (RealDeltaSeconds <= 0.f || SimulationSpeed <= 0.f)
	{
		return;
	}

	// RealDeltaSeconds is the frame delta. SimulationSpeed is sim seconds per real second.
	// Accurate Time sets SimulationSpeed to 30 and does not scale this delta a second time.
	TotalSimSeconds += SimulationTimeContract::EffectiveSimDelta(RealDeltaSeconds, SimulationSpeed);
}

void USimulationClock::SetSimulationSpeed(float Speed)
{
	SimulationSpeed = FMath::Max(0.f, Speed);
}

void USimulationClock::SetStartTimeOfDay(int32 Hour, int32 Minute)
{
	StartHour = FMath::Clamp(Hour, 0, 23);
	StartMinute = FMath::Clamp(Minute, 0, 59);
}

int32 USimulationClock::GetDay() const
{
	return static_cast<int32>(FMath::FloorToDouble(TotalSimSeconds / SecondsPerDay));
}

double USimulationClock::GetTimeOfDaySeconds() const
{
	double Tod = FMath::Fmod(TotalSimSeconds, SecondsPerDay);
	if (Tod < 0.0)
	{
		Tod += SecondsPerDay;
	}
	return Tod;
}

int32 USimulationClock::GetHour() const
{
	return static_cast<int32>(GetTimeOfDaySeconds() / 3600.0);
}

int32 USimulationClock::GetMinute() const
{
	return static_cast<int32>(FMath::Fmod(GetTimeOfDaySeconds(), 3600.0) / SecondsPerMinute);
}

int32 USimulationClock::GetSecond() const
{
	return static_cast<int32>(FMath::Fmod(GetTimeOfDaySeconds(), SecondsPerMinute));
}

int32 USimulationClock::GetCurrentDayMinute() const
{
	return static_cast<int32>(GetTimeOfDaySeconds() / SecondsPerMinute);
}

float USimulationClock::GetAccumulatedSimMinutes() const
{
	return static_cast<float>(TotalSimSeconds / SecondsPerMinute);
}

FString USimulationClock::ToTimeOfDayString() const
{
	return FString::Printf(TEXT("%02d:%02d:%02d"), GetHour(), GetMinute(), GetSecond());
}

FString USimulationClock::ToTimeOfDayHHMMString() const
{
	return FString::Printf(TEXT("%02d:%02d"), GetHour(), GetMinute());
}

bool USimulationClock::AdvanceScheduleCursor(int32& InOutDay, int32& InOutDayMinute) const
{
	const int32 TargetDay = GetDay();
	const int32 TargetMinute = GetCurrentDayMinute();

	const auto IsBefore = [](int32 DayA, int32 MinA, int32 DayB, int32 MinB)
	{
		return DayA < DayB || (DayA == DayB && MinA < MinB);
	};
	const auto IsBeforeOrEqual = [](int32 DayA, int32 MinA, int32 DayB, int32 MinB)
	{
		return DayA < DayB || (DayA == DayB && MinA <= MinB);
	};

	if (InOutDayMinute < 0)
	{
		// Uninitialized cursor: jump to midnight of day 0 (legacy). Prefer InitScheduleCursorAtClock.
		InOutDay = 0;
		InOutDayMinute = 0;
		return IsBeforeOrEqual(0, 0, TargetDay, TargetMinute);
	}

	if (!IsBefore(InOutDay, InOutDayMinute, TargetDay, TargetMinute))
	{
		return false;
	}

	++InOutDayMinute;
	if (InOutDayMinute >= MinutesPerDay)
	{
		InOutDayMinute = 0;
		++InOutDay;
	}

	return IsBeforeOrEqual(InOutDay, InOutDayMinute, TargetDay, TargetMinute);
}
