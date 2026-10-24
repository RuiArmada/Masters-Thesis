# Test branch: AccurateTime test harness

This branch holds the test-only code with which the thesis experiments were run in the
`AccurateTime` mode of `main`. Nothing here belongs on `main`.

To repeat the runs: `python runner/make_configs.py` writes every run configuration into `runner/cfg/`
(from `runner/base/`), then `runner/full_batch.ps1` (Accurate mode) and `runner/full_batch_vis.ps1 -Phases E,S`
(Visual-mode scaling) run the batch. The paths to the engine and the project at the top of
`runner/run_scenario.ps1` and `runner/lib.ps1` must be set for the local machine.

## Harness (test only)

- `RunRecorderSubsystem` (`-RunRecord=<label>`, `-RecordSimTime`, `-RecordPositions=<s>`, `-TraceAgent=Agent_N`):
  writes `rules.csv`, `stations.csv`, `trips.csv`, `timeline.csv` (and `positions.csv`) to `Saved/Analytics/Run_<label>/`.
- `StressTestSubsystem` (`-StressAgents=N`, `-StressWarmup`, `-StressSeconds`, `-StressQuit`, `-StressHideMeshes`).
- `-NoBlend` turns the boids blend off while following a path (corridor test).
- Trip events in `GoToAction`, station events in `AgentStation`, rule outcomes in `ScheduleManager`.
- `runner/`: PowerShell batch scripts and `make_configs.py`, which writes every configuration used.
  Batch runs: `UnrealEditor.exe Thesis.uproject <map> -game -nullrhi -RenderOffScreen -UseFixedTimeStep -FPS=30`
  (one simulated second per frame; `-FPS=120` for the 0.25 s step check). Scaling runs are rendered in Visual mode.
  Create a file `runner/PAUSE` to pause a batch between runs.

## Problems found in AccurateTime (9cb5acb) and the QA-only corrections

With one simulated second per frame an agent at 2 m/s moves 2 m per frame. That exposed:

1. **Waypoint overshoot** (`ABoidsAB::MoveAlongWaypoints`). The 1 m arrival window of a waypoint can be
   stepped over; the look-ahead refuses to turn back past a waypoint, so the agent walks on along the
   segment, in the worst case off the map. QA fix: also accept a waypoint the agent has walked past along its
   segment (dot > 0) within `100 + 2*FrameTravel` cm in XY and `28 + FrameTravel` cm in Z.
2. **Evacuation slot / safe-zone approach** (`BoidsAB.cpp`, the three `AddMovementInput` calls for the queue
   slot and the safe zone). The final approach starts only when `Waypoints.Num() == 0`, not when the path has
   been walked, and a running agent overshoots the 1.5 m slot window. QA fix: condition
   `CurrentWaypointIndex >= Waypoints.Num()` and a shortened last step (`ArrivalInputScale`).
3. **Needs scaled ×30.** Needs rates were tuned per simulated minute, but AccurateTime advances them by
   real delta × 30, i.e. per simulated second. Agents become critically hungry within minutes, the evacuation
   coordinator defers `StartEvacuation` until the rule is stale, and no evacuation starts.
   QA fix: `NeedsDelta *= AccurateSpeed / max(1, VisualClockSpeed)` (needs advance in simulated minutes).
4. **Movement substeps in real time** (`RefreshWalkSpeedForSimulationMode`). The CharacterMovementComponent
   substeps every 0.05 real s = 1.5 simulated s, so a frame is one straight 2 m move whose ledge checks miss
   ramps; agents left the map from ramps. QA fix: `MaxSimulationTimeStep = DefaultMaxSimulationTimeStep / Mult`
   (0.05 simulated s), `MaxSimulationIterations` raised to match.
5. **Recorder clock** (QA only): `RunRecorderSubsystem::NowSeconds()` returns 0 until the clock exists.
6. **Fast-run finalize** (`ScheduleManager::FinalizeFastSimulationRun`): in AccurateTime the clock is kept
   running after an early end, so the shopping-center export (which waits for its own run length) still happens.

Without 1–4, one to four agents per shopping-center run and three of twenty in a corridor walked off the
map, and no evacuation started.

## Constants still in real seconds (not changed, reported in the thesis)

- `UGoToAction::MaxWaitSeconds = 180` → 90 simulated minutes in AccurateTime. Walks that end on this
  time-out (agents just outside the arrival radius of a crowded single point) inflate walk durations
  (Daily Urban Life: work 8.2, home 5.8 simulated minutes on average) and cause most of its late minutes.
- `UAgentFallbackHandlerComponent::StationQueueTimeoutSeconds = 50` → 25 simulated minutes.
- `AExitPoint::ExitProcessDuration = 1.5` → 45 simulated seconds per agent per exit.

These would need `ScaleAuthoredDurationToRealSeconds` or an explicit simulated-time unit.
