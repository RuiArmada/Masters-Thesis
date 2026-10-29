#include "Misc/AutomationTest.h"
#include "Simulation/SimulationTimeContract.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAccurateTimeDeltaTest,
	"Thesis.Simulation.AccurateTime.EffectiveDelta",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAccurateTimeDeltaTest::RunTest(const FString& Parameters)
{
	using namespace SimulationTimeContract;

	const auto Nearly = [this](const TCHAR* What, double Actual, double Expected)
	{
		TestTrue(What, FMath::IsNearlyEqual(Actual, Expected, 1.0e-4));
	};

	const float Speed = AccurateTimeSimSecondsPerRealSecond;
	Nearly(TEXT("30 FPS → 1.0 sim second"), EffectiveSimDelta(1.f / 30.f, Speed), 1.0);
	Nearly(TEXT("60 FPS → 0.5 sim second"), EffectiveSimDelta(1.f / 60.f, Speed), 0.5);
	Nearly(TEXT("20 FPS → 1.5 sim seconds"), EffectiveSimDelta(1.f / 20.f, Speed), 1.5);
	Nearly(TEXT("0.1 real second → 3.0 sim seconds"), EffectiveSimDelta(0.1f, Speed), 3.0);

	// CMC distance uses real delta * (base * speed). That equals base * effective sim delta.
	// Scaling both walk speed and tick delta would not match.
	const float BaseUuPerSimSecond = 200.f;
	const float RealDelta = 1.f / 30.f;
	const double ClockDelta = EffectiveSimDelta(RealDelta, Speed);
	const float SyncedSpeed = SyncedMaxWalkSpeed(BaseUuPerSimSecond, Speed);
	const double DistanceOnce = static_cast<double>(SyncedSpeed) * static_cast<double>(RealDelta);
	const double DistanceFromClock = static_cast<double>(BaseUuPerSimSecond) * ClockDelta;
	const double DistanceIfDoubled = static_cast<double>(SyncedSpeed) * ClockDelta;

	TestTrue(TEXT("single scale matches clock distance"), FMath::IsNearlyEqual(DistanceOnce, DistanceFromClock, 1.0e-3));
	TestTrue(TEXT("double scale is not the contract"), !FMath::IsNearlyEqual(DistanceIfDoubled, DistanceFromClock, 1.0));

	// duration 14 real seconds at Visual 60 occupies 840 sim seconds (14 sim minutes).
	// Accurate Time 30 keeps that occupancy: wall = 14 * 60 / 30 = 28 real seconds.
	const float FoodTableWall = AccurateTimeWallSecondsForVisualDuration(14.f, 60.f, 30.f);
	const float FoodCounterWall = AccurateTimeWallSecondsForVisualDuration(10.f, 60.f, 30.f);
	Nearly(TEXT("food table wall seconds"), FoodTableWall, 28.0);
	Nearly(TEXT("food counter wall seconds"), FoodCounterWall, 20.0);
	Nearly(TEXT("food table sim occupancy"), static_cast<double>(FoodTableWall) * 30.0, 14.0 * 60.0);
	Nearly(TEXT("zero duration stays zero"), AccurateTimeWallSecondsForVisualDuration(0.f, 60.f, 30.f), 0.0);
	return true;
}

#endif
