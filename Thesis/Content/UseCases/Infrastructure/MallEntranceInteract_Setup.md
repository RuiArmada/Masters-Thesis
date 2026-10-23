# Mall Entrance Interact — Editor Setup (Phase 1)

Demonstrates: **Agent → GoTo button → Interact → Entrance2 opens → later agents path through**.

## Actors

| Actor | Class | Notes |
|-------|--------|--------|
| Gate | `AMallEntranceGate` | Box over Entrance2 corridor; `bStartOpen = false` |
| Button | `AInteractionButton` | Tag `EntranceButton2`; `LinkedEntrance` = gate; InteractionPoint on NavMesh |
| Far destination | Empty actor | Tag `Entrance2Side` (beyond the gate) |
| Exit | Existing | Tag `Exit1` (unchanged) |

## Schedule

`Schedules/InfrastructurePlanningSchedule_MallEntranceInteract.xml`

- `Agent_0` at `00:04`: `<then action="Interact" target="EntranceButton2"/>`
- `Agent_1`–`3` at `00:10+`: `MoveTo` `Entrance2Side`

Point `AScheduleManager::RelativeSchedulePath` at that file for the test run (restore Mall A/B schedule afterward).

## Verify in PIE log

1. `[GoToAction]` / `[InteractAction]` for Agent_0  
2. `[MallEntranceGate] ... OPEN`  
3. Agent_1+ reach `Entrance2Side` (path succeeds only after open)

## Navigation check

- **Closed:** path query origin → `Entrance2Side` should fail or avoid the gate volume (`NavArea_Null`).  
- **Open:** same query succeeds through the gate (`NavArea_Sidewalk`).
