"""Analyze a multi-seed board sweep from the testbench.

Input: JSON file — a list of bench.run / bench.runBlocks results
(each carries .tag, .seed, .pct or .blocks[], optional .stats).
Output: per-tag mean +- sd tables and paired V2-V1 deltas per seed.

Usage: py tools/analyze_board.py results/board-YYYY-MM-DD.json
"""
import json, math, sys
from collections import defaultdict


def mean(xs):
    return sum(xs) / len(xs)


def sd(xs):
    if len(xs) < 2:
        return 0.0
    m = mean(xs)
    return math.sqrt(sum((x - m) ** 2 for x in xs) / (len(xs) - 1))


def fmt(xs):
    return f"{mean(xs):6.1f} +- {sd(xs):4.1f}  [{min(xs):5.1f}, {max(xs):5.1f}]  n={len(xs)}"


def winrate(pair):
    w, l = pair
    return 100.0 * w / max(w + l, 1)


def final_pct(r):
    """Scalar endpoint metric for a result: pct of last block (runBlocks) or pct (run)."""
    if "blocks" in r:
        return r["blocks"][-1]["pct"]
    return r["pct"]


def block_winrates(r, key):
    """Per-block win rates from cumulative stats snapshots."""
    outs = []
    prev = (0, 0)
    for b in r["blocks"]:
        cur = tuple(b["stats"][key])
        dw, dl = cur[0] - prev[0], cur[1] - prev[1]
        outs.append(100.0 * dw / max(dw + dl, 1))
        prev = cur
    return outs


def main(path):
    results = json.load(open(path, encoding="utf-8"))
    bytag = defaultdict(dict)  # tag -> seed -> result
    for r in results:
        if "error" in r:
            print("JOB ERROR:", r["error"], r.get("job"))
            continue
        bytag[r["tag"]][r["seed"]] = r

    print("== endpoint pct by tag (brain error as % of plain twin; lower better) ==")
    for tag in sorted(bytag):
        vals = [final_pct(r) for r in bytag[tag].values()]
        print(f"  {tag:22s} {fmt(vals)}")

    print("\n== paired V2 - V1 deltas per seed (negative = V2 better) ==")
    tags = set(bytag)
    for t2 in sorted(tags):
        if "-v2" not in t2:
            continue
        t1 = t2.replace("-v2", "-v1")
        if t1 not in tags:
            continue
        seeds = sorted(set(bytag[t2]) & set(bytag[t1]))
        deltas = [final_pct(bytag[t2][s]) - final_pct(bytag[t1][s]) for s in seeds]
        wins = sum(1 for d in deltas if d < 0)
        print(f"  {t2.replace('-v2',''):18s} {fmt(deltas)}  V2 wins {wins}/{len(deltas)} seeds")

    print("\n== decision scenarios: win rates ==")
    for tag in sorted(bytag):
        sample = next(iter(bytag[tag].values()))
        if "blocks" not in sample or "stats" not in sample["blocks"][0]:
            continue
        st = sample["blocks"][0]["stats"]
        key = "wB" if "wB" in st else ("hive" if "hive" in st else "gossip")
        per_block = defaultdict(list)  # block index -> [winrate per seed]
        overall = []
        for r in bytag[tag].values():
            brs = block_winrates(r, key)
            for i, w in enumerate(brs):
                per_block[i].append(w)
            cum = r["blocks"][-1]["stats"][key]
            overall.append(winrate(cum))
        blocks_str = " | ".join(
            f"b{i}: {mean(per_block[i]):5.1f}+-{sd(per_block[i]):4.1f}" for i in sorted(per_block)
        )
        print(f"  {tag:22s} overall {mean(overall):5.1f}+-{sd(overall):4.1f}   {blocks_str}")


if __name__ == "__main__":
    main(sys.argv[1])
