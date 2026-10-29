"""Frame-step check: the same configurations at 1 simulated second per frame and at 0.25 (prefix T_)."""
from analyze import run_dirs, analyse_run, authored_rule_count

pairs = []
for cap in (1, 3):
    for seed in range(1, 6):
        pairs.append((f"FoodCounter cap{cap}", f"F_FoodCounter_cap{cap}_s{seed}", f"T_FoodCounter_cap{cap}_s{seed}", None))
for seed in (1, 2):
    pairs.append(("Daily life", f"D_dul_acc_s{seed}", f"T_dul_s{seed}", authored_rule_count("DailyUrbanLifeSchedule.xml")))

dirs = {}
for p in ("F_", "T_", "D_dul_acc_"):
    dirs.update(run_dirs(p))

groups = {}
for group, one, quarter, authored in pairs:
    if one not in dirs or quarter not in dirs:
        continue
    for step, label in ((1.0, one), (0.25, quarter)):
        r = analyse_run(dirs[label], authored)
        g = groups.setdefault((group, step), [])
        g.append(r)
        print(f"{label:28s} step={step:<5} wait={r['wait_per_visit']:.2f} queued={r['queued']}/{r['visits']} "
              f"completed={r['completed']} open={r['open']} abandoned={r['abandoned']} "
              f"dispatched={r['dispatched']} stale={r['stale']} trips={r['trips_arrived']}/{r['trips']}")

print()
for (group, step), rs in sorted(groups.items()):
    n = len(rs)
    print(f"{group:16s} step={step:<5} runs={n} wait={sum(r['wait_per_visit'] for r in rs) / n:.2f} "
          f"completed={sum(r['completed'] for r in rs)} open={sum(r['open'] for r in rs)} "
          f"dispatched={sum(r['dispatched'] for r in rs)} stale={sum(r['stale'] for r in rs)} "
          f"trips={sum(r['trips_arrived'] for r in rs)}/{sum(r['trips'] for r in rs)}")
