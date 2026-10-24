"""Accurate-mode run configurations and generated schedules for the full re-run.

Starts from the configurations of the earlier runs (base/cfg2 and base/exp), replaces the mode with Accurate and
keeps every other setting. Run length (fastSimulation/runDurationSimMinutes) and seed are kept or set here.
Generated schedules go to Thesis/Saved/Schedules, which the game reads before the project copies.
The Visual-mode scaling configurations (base/visual/vis_*.xml) are copied into cfg/ unchanged.
"""
import os
import re
import shutil

HERE = os.path.dirname(os.path.abspath(__file__))
OLD = os.path.join(HERE, "base", "cfg2")
EXP = os.path.join(HERE, "base", "exp")
OUT = os.path.join(HERE, "cfg")
PROJ = os.path.normpath(os.path.join(HERE, "..", ".."))  # the Unreal project folder (Thesis/)
SCHED_OUT = os.path.join(PROJ, "Saved", "Schedules")
os.makedirs(OUT, exist_ok=True)
os.makedirs(SCHED_OUT, exist_ok=True)
for name in os.listdir(os.path.join(HERE, "base", "visual")):
    shutil.copy(os.path.join(HERE, "base", "visual", name), os.path.join(OUT, name))


def rd(path):
    return open(path, encoding="utf-8-sig").read()


def accurate(text, seed, run_minutes=None):
    m = re.search(r"<fastSimulation>.*?<runDurationSimMinutes value=\"(\d+)\"", text, re.S)
    minutes = run_minutes or (int(m.group(1)) if m else 230)
    text = re.sub(r"\s*<simulationMode[^>]*/>", "", text)
    text = re.sub(r"\s*<fastSimulation>.*?</fastSimulation>", "", text, flags=re.S)
    text = re.sub(r"\s*<accurateTime>.*?</accurateTime>", "", text, flags=re.S)
    block = (f'\n  <simulationMode mode="AccurateTime"/>\n  <accurateTime>\n    <simulationSpeed value="30"/>\n'
             f'    <runDurationSimMinutes value="{minutes}"/>\n    <randomSeed value="{seed}"/>\n'
             f'    <autoExportOnEnd value="true"/>\n  </accurateTime>\n</simulation>\n')
    new, n = re.subn(r"</simulation>\s*$", block, text)
    assert n == 1
    return new


def wr(name, text):
    open(os.path.join(OUT, name), "w", encoding="utf-8").write(text)


def schedule_file(text, name):
    new, k = re.subn(r'<scheduleFile value="Schedules/InfrastructurePlanningSchedule_MallA.xml"/>',
                     f'<scheduleFile value="Schedules/{name}"/>', text)
    assert k == 1
    return new


n = 0
# A, G: Mall A desks (ServicePoint) with capacity 1..3; F, G: food counters and tables. Seeds 1..30.
for cap in (1, 2, 3):
    base = rd(os.path.join(OLD, f"mallA_cap{cap}_seed1.xml"))
    for seed in range(1, 31):
        wr(f"Desks_cap{cap}_s{seed}.xml", accurate(base, seed)); n += 1
    for tag in ("FoodCounter", "FoodTable"):
        base = rd(os.path.join(OLD, f"food_{tag}_cap{cap}_seed1.xml"))
        for seed in range(1, 31):
            wr(f"{tag}_cap{cap}_s{seed}.xml", accurate(base, seed)); n += 1

# B: both layouts as authored.
base = rd(os.path.join(OLD, "mall_auto_seed1.xml"))
for seed in range(1, 6):
    wr(f"mall_auto_s{seed}.xml", accurate(base, seed)); n += 1

# D: Daily Urban Life (50 agents).
base = rd(os.path.join(OLD, "dul_fast_seed1.xml"))
for seed in range(1, 6):
    wr(f"dul_s{seed}.xml", accurate(base, seed)); n += 1

# C, E, S: evacuation and scaling (the old runs used visual.xml and dul_visual_x<N>.xml).
base = rd(os.path.join(OLD, "visual.xml"))
for seed in range(1, 4):
    wr(f"evac_s{seed}.xml", accurate(base, seed, 200)); n += 1
dul_vis = rd(os.path.join(OLD, "dul_visual_x400.xml"))
for N in (50, 100, 200, 400, 800, 1600):
    wr(f"dul_x{N}.xml", accurate(dul_vis.replace("DailyUrbanLifeSchedule_x400.xml", f"DailyUrbanLifeSchedule_x{N}.xml"), 1, 270)); n += 1

# R: lunch rush (schedule generated below).
for tag in ("FoodCounter", "FoodTable"):
    for cap in (1, 3):
        base = schedule_file(rd(os.path.join(OLD, f"food_{tag}_cap{cap}_seed1.xml")), "InfrastructurePlanningSchedule_MallA_rush.xml")
        for seed in range(1, 11):
            wr(f"rush_{tag}_cap{cap}_s{seed}.xml", accurate(base, seed)); n += 1

# J: 50 cloned agents with the authored mall schedule (Agent_k uses the rules of Agent_(k mod 10)).
base = rd(os.path.join(EXP, "H_n50_FoodCounter_cap1_s1.xml"))
for seed in range(1, 6):
    wr(f"jam_n50_s{seed}.xml", accurate(base, seed)); n += 1

# V: corridors, clock from midnight.
base = rd(os.path.join(EXP, "V_visual_midnight.xml"))
for seed in range(1, 4):
    wr(f"corridor_s{seed}.xml", accurate(base, seed, 15)); n += 1
print("configs written:", n)

# ---- schedules
mall = rd(os.path.join(PROJ, "Schedules", "InfrastructurePlanningSchedule_MallA.xml"))
blocks = {int(m.group(1)): m.group(0) for m in re.finditer(r'<agent id="Agent_(\d+)">.*?</agent>', mall, re.S)}
for N in (50,):
    parts = [re.sub(r'id="Agent_\d+"', f'id="Agent_{k}"', blocks[k % len(blocks)], count=1) for k in range(N)]
    xml = ('<?xml version="1.0" encoding="UTF-8"?>\n'
           f'<!-- Generated for contention tests: Agent_k uses the rules of Agent_(k mod {len(blocks)}) from InfrastructurePlanningSchedule_MallA.xml -->\n'
           '<schedule>\n    ' + "\n\n    ".join(parts) + '\n</schedule>\n')
    open(os.path.join(SCHED_OUT, f"InfrastructurePlanningSchedule_MallA_x{N}.xml"), "w", encoding="utf-8").write(xml)

dul = rd(os.path.join(PROJ, "Schedules", "DailyUrbanLifeSchedule.xml"))
dblocks = {int(m.group(1)): m.group(0) for m in re.finditer(r'<agent id="Agent_(\d+)">.*?</agent>', dul, re.S)}
for N in (50, 100, 200, 400, 800, 1600):
    parts = [re.sub(r'id="Agent_\d+"', f'id="Agent_{k}"', dblocks[k % len(dblocks)], count=1) for k in range(N)]
    xml = ('<?xml version="1.0" encoding="UTF-8"?>\n'
           f'<!-- Generated for scaling tests: Agent_k uses the rules of Agent_(k mod {len(dblocks)}) from DailyUrbanLifeSchedule.xml -->\n'
           '<schedule>\n    ' + "\n\n    ".join(parts) + '\n</schedule>\n')
    open(os.path.join(SCHED_OUT, f"DailyUrbanLifeSchedule_x{N}.xml"), "w", encoding="utf-8").write(xml)

# Lunch rush: every diner walks to the food court at 08:40 and asks for a meal at 08:50; exits at 09:20 or later.
def fix_block(m):
    b = m.group(0)
    b = re.sub(r'<rule time="\d\d:\d\d">(<then action="MoveTo" area="FoodCourt" />)', r'<rule time="08:40">\1', b)
    b = re.sub(r'<rule time="\d\d:\d\d">(\s*<then action="UseStation">\s*<stage superStation="FoodCourt")', r'<rule time="08:50">\1', b)

    def exit_time(e):
        h, mi = map(int, e.group(1).split(":"))
        t = max(h * 60 + mi, 9 * 60 + 20)
        return f'<rule time="{t // 60:02d}:{t % 60:02d}"><then action="MoveTo" area="Exit1"'
    return re.sub(r'<rule time="(\d\d:\d\d)"><then action="MoveTo" area="Exit1"', exit_time, b)


rush = re.sub(r'<agent id="Agent_\d+">.*?</agent>', fix_block, mall, flags=re.S)
rush = rush.replace("<schedule>", "<!-- Lunch-rush variant generated for the contention test: food court at 08:40, meal at 08:50. -->\n<schedule>", 1)
open(os.path.join(SCHED_OUT, "InfrastructurePlanningSchedule_MallA_rush.xml"), "w", encoding="utf-8").write(rush)
print("schedules written to", SCHED_OUT)
