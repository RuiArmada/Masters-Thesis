# Known Limitations

Things that are genuinely limited or awkward in the current codebase—not a wish list.

## Content and maps

Binary `.umap` / `.uasset` graphs are not fully visible from source. Actor placement and widget bindings have to be inspected in the editor. There is also no separate requirements PDF in the repo; intent is taken from maps, schedule comments, and `SimulationConfig.xml`.

## Schedules

`MoveTo` picks the **first** actor with the requested tag. Duplicate tags are not load-balanced. Missing tags tend to surface as warnings or fallbacks at runtime, not as a preflight schema check. Rules older than the retry window (default 45 sim minutes) can be skipped as stale. Conditions are only `emergency == true|false` and `location == Home|Work|Food`. Break is a Trigger id (`Break`), not its own action enum value.

## Legacy leftovers

`ARuleParser` still points at hard-coded absolute paths from an older B.A.R.S layout and is not the schedule path used by the use cases. Predator spawner/boid types are unused by UC1–UC3. Some flock fields (for example `AntiProgressDamp`) remain for editor compatibility while MoveTo blend is lateral-only on progress.

## Navigation

If there is no sidewalk-connected route, the sidewalk filter simply fails to produce a usable path. Multi-floor travel needs a continuous mesh (stairs/ramps). Character Movement will not invent vertical travel; gap-closing to a station root does not teleport between floors. Off-mesh XY snap keeps the agent’s current Z, which can be wrong if the projected poly is on another floor.

## Stations

Interaction time starts after arrival notification. An empty waypoint list must not start interaction. Pose and animation quality depend on InteractionPoint setup and the AnimBP in content.

## Per use case

**UC2** expects the Home/Work/desk tagging scheme described in the schedule comments. Missing plaza tag falls back to pausing in place for breaks. Daily-life CSV is not auto-exported by the manager.

**UC3** assumes the mall tags referenced in the schedules (shops, food courts, service points, exits, and so on). V&V comparison needs both layout CSVs on disk; checklist lines are not CI gates.

**UC1** is not map-name gated. A map with the feature on but without managers/exits will not evacuate properly. Non-evacuation crowd-routing modes are mostly distance fallbacks.

## Validation

Only V01–V06 maps ship. Name patterns for V07–V09 exist in code but unused. There is no Unreal AutomationTest suite. `AgentsFailed` and average path length show as n/a. There is no validation CSV. Separate “crosswalk / merge / split” maps were never added.

## Defaults that surprise people

No startup or game default map is set, so open a use-case map from the Content Browser. Local config edits belong in `Saved/Config/SimulationConfig.xml`—editing only the project Config copy and wondering why nothing changed is a common footgun.

## Performance

Many agents repathing at once (evac switches, stuck retries) can hitch because pathfinding is synchronous. Dense flocks with high sonar counts are expensive. The validation panel redraws debug text every tick while shown. Fifty UC2 agents sharing desks is intentional queue pressure, but it does load nav and stations harder.
