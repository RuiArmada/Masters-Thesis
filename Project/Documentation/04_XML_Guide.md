# XML Guide

Schedules are plain XML files read by `AScheduleManager`. Only the actions and attributes below are actually parsed. Simulation feature toggles live in `SimulationConfig.xml`, not in these schedule files.

## Where the files are

| Path | Used for |
|------|----------|
| `Schedules/DailySchedule.xml` | Older / generic day |
| `Schedules/DailyUrbanLifeSchedule.xml` | UC2 |
| `Schedules/InfrastructurePlanningSchedule_MallA.xml` | UC3 Mall A |
| `Schedules/InfrastructurePlanningSchedule_MallB.xml` | UC3 Mall B |
| `Schedules/MultiStageStationExample.xml` | Multi-stage UseStation sample |
| `Schedules/Validation/*.xml` | Validation maps and left/right spawner templates |

ScheduleManager’s `RelativeSchedulePath` (or a manager override) chooses the file. Under PIE you often end up on the copy in `Saved/Schedules/`.

## Time

Times are `HH:MM` on the schedule clock. UC2 comments treat one schedule minute as about one real second. Rules can be dropped as stale after the retry window (default 45 schedule minutes).

## Skeleton

```xml
<?xml version="1.0" encoding="UTF-8"?>
<schedule>
  <agent id="Agent_0" role="Civilian">
    <!-- rules and/or tasks -->
  </agent>
</schedule>
```

`id` must match the registered agent. Optional `role` (`Police` / `Robber` / `Civilian`) is applied on register when cops-and-robbers is enabled.

## Actions

| `action` | Behaviour |
|----------|-----------|
| `MoveTo` | Path to first actor with the given area/tag |
| `Idle` | Idle for `duration` seconds |
| `Trigger` | `OnScheduleTrigger` with the `id` string |
| `UseStation` | Occupy a station (one step or a pipeline) |
| `None` | No-op |

There is no `Break` action. UC2 breaks look like:

```xml
<then action="Trigger" id="Break" />
```

`Break` / `Pause` are special-cased so station blocking does not trap them the same way as other rules.

## Rules (preferred)

```xml
<agent id="Agent_0">
  <rule time="00:22" prob="1.0">
    <if condition="location == Work"/>
    <then action="UseStation" area="Work0" stationType="A0" duration="15"/>
    <else action="MoveTo" area="Work0"/>
  </rule>

  <rule time="00:40">
    <then action="MoveTo" area="Home1"/>
  </rule>
</agent>
```

Optional `prob` on the rule. Nested `<else><then …/></else>` is accepted.

## Older task tags

Still loaded if present:

```xml
<task time="00:30" action="MoveTo" target="ShopA" id="" prob="1.0" duration="-1" stationType=""/>
```

Prefer `<rule>` for new work.

## Conditions

Only:

- `emergency == true` / `emergency == false`
- `location == Home` / `Work` / `Food` (within `LocationCheckRadius`, default 200 cm)

Anything else evaluates false and logs a warning.

## MoveTo, Idle, Trigger

```xml
<then action="MoveTo" area="Exit_Right"/>
<then action="Idle" duration="8"/>
<then action="Trigger" id="Break"/>
```

## UseStation

Single step on the `<then>`:

```xml
<then action="UseStation" area="ShopA" stationType="ShopA_Desk" duration="10"/>
```

`area` is the area tag. `stationType` matches `StationIdentifier` / station tag. `duration` in seconds (−1 → station default). Optional `station` on a **child** stage binds an explicit actor name.

Multi-stage:

```xml
<then action="UseStation">
  <stage area="FoodCourt" stationType="FoodCounter" duration="10"/>
  <stage area="FoodCourt" stationType="FoodTable" duration="14"/>
</then>
```

Child tags may be `stage`, `step`, or `interaction`. See `MultiStageStationExample.xml`.

## Practical notes

Agents must arrive at the station (XY and height checks) before interaction time runs. Full stations queue. Closed areas affect availability. Mall food flows rely heavily on multi-stage UseStation.
