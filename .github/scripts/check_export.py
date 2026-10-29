#!/usr/bin/env python3
"""Checks a branch_bench --json export (schema 1) against its own trials.

Rebuilds every pair summary from the trial records, independently of the
C code, and compares it with the pair records; with --report, also checks
that the Markdown report shows the same medians, ranges, ratios and
effective costs.  Standard library only.

Usage: check_export.py EXPORT [--report REPORT.md] [--kernel-counts FILE]
                       [--same-results OTHER --test N]
                       [--different-results OTHER --test N]

Prints "ok   <check>" lines and exits 0, or "FAIL <reason>" and exits 1.
"""
import argparse
import json
import math
import sys

COST_MIN_MISSES_PER_PAIR = 1000.0
COST_LABEL = "**Effective extra cycles per additional miss:**"


class Failure(Exception):
    pass


def ok(msg):
    print("ok   " + msg)


def check(cond, msg):
    if not cond:
        raise Failure(msg)


def close(a, b):
    if a is None or b is None:
        return a is None and b is None
    return math.isclose(a, b, rel_tol=1e-12, abs_tol=1e-12)


def load(path):
    records = []
    with open(path, encoding="utf-8") as f:
        for n, line in enumerate(f, 1):
            try:
                records.append(json.loads(line))
            except json.JSONDecodeError as e:
                raise Failure(f"{path}:{n}: not JSON ({e})")
    check(records, f"{path}: empty export")
    return records


def by_type(records, kind):
    return [r for r in records if r.get("type") == kind]


def check_structure(records):
    meta = records[0]
    check(meta.get("type") == "meta", "first record is not meta")
    check(meta.get("schema") == 1, f"schema {meta.get('schema')}, want 1")
    check(meta.get("revision") == 2, f"revision {meta.get('revision')}, want 2")
    kernels = {k["id"] for k in by_type(records, "kernel")}
    variants = {(v["test"], v["variant"]): v for v in by_type(records, "variant")}
    for v in variants.values():
        check(v["kernel"] in kernels, f"variant {v['test']}/{v['variant']}: no kernel {v['kernel']}")
    for t in by_type(records, "trial"):
        check((t["test"], t["variant"]) in variants,
              f"trial seq {t['seq']}: no variant {t['test']}/{t['variant']}")
    ok("structure: meta first, schema 1, revision 2, every trial has a variant and kernel")
    return variants


def valid_counts(trial):
    """Counter values of a trial that read its counters, else None."""
    if trial["perf_status"] != "ok":
        return None
    return {k: v for k, v in trial["counts"].items() if v is not None}


def rebuild_pair(pair, trials):
    """Pair summary from its trials: matched by trial index within the pair."""
    mine = [t for t in trials if t["pair"] == pair["pair"]]
    a = {t["trial"]: t for t in mine if t["variant"] == pair["a"]}
    b = {t["trial"]: t for t in mine if t["variant"] == pair["b"]}
    pairs = valid = 0
    ms_a = ms_b = 0.0
    delta = {}
    open_names = None
    k = 0
    while k in a and k in b:
        pairs += 1
        ms_a += a[k]["time_ms"]
        ms_b += b[k]["time_ms"]
        ca, cb = valid_counts(a[k]), valid_counts(b[k])
        if ca is not None and cb is not None:
            valid += 1
            names = set(ca) & set(cb)
            open_names = names if open_names is None else open_names & names
            for name in names:
                delta[name] = delta.get(name, 0.0) + (float(cb[name]) - float(ca[name]))
        k += 1
    open_names = open_names or set()
    delta = {name: (delta[name] if name in open_names else None)
             for name in pair["delta"]}
    if not valid or not {"cycles", "misses"} <= open_names:
        status, cost = "unavailable", None
    elif delta["misses"] < COST_MIN_MISSES_PER_PAIR * valid:
        status, cost = "inconclusive", None
    else:
        status, cost = "resolved", delta["cycles"] / delta["misses"]
    slowdown = ms_b / ms_a if ms_a else None
    return {"pairs": pairs, "valid": valid, "time_ms_a": ms_a, "time_ms_b": ms_b,
            "slowdown": slowdown, "delta": delta, "effective_cost": cost,
            "cost_status": status}


def check_pairs(records):
    trials = by_type(records, "trial")
    pairs = by_type(records, "pair")
    for p in pairs:
        want = rebuild_pair(p, trials)
        where = f"pair {p['pair']} (test {p['test']})"
        for field in ("pairs", "valid", "cost_status"):
            check(p[field] == want[field], f"{where}: {field} {p[field]}, rebuilt {want[field]}")
        for field in ("time_ms_a", "time_ms_b", "slowdown", "effective_cost"):
            check(close(p[field], want[field]), f"{where}: {field} {p[field]}, rebuilt {want[field]}")
        for name, value in want["delta"].items():
            check(close(p["delta"][name], value),
                  f"{where}: delta {name} {p['delta'][name]}, rebuilt {value}")
    ok(f"{len(pairs)} pair records rebuilt from {len(trials)} trials")


def report_sections(path):
    """Report text per test number, from each "### Test N " heading on."""
    sections = {}
    current = None
    with open(path, encoding="utf-8") as f:
        for line in f:
            if line.startswith("### "):
                current = None
                words = line.split()
                if len(words) > 2 and words[1] == "Test" and words[2].isdigit():
                    current = int(words[2])
                    sections[current] = []
            if current is not None:
                sections[current].append(line)
    return {n: "".join(lines) for n, lines in sections.items()}


def representative(trials):
    """Lower-middle trial by time, as median_trial() picks it (stable sort)."""
    ordered = sorted(trials, key=lambda t: t["time_ms"])
    return ordered[(len(ordered) - 1) // 2], ordered[0], ordered[-1]


def table_row(section, label):
    for line in section.splitlines():
        if line.startswith(f"| {label} |"):
            return [c.strip() for c in line.strip().strip("|").split("|")]
    return None


def check_report(records, variants, path):
    sections = report_sections(path)
    trials = by_type(records, "trial")
    several = records[0]["trials"] > 1
    rows = 0
    for (test, variant), v in variants.items():
        mine = [t for t in trials if t["test"] == test and t["variant"] == variant]
        if not mine:
            continue
        check(test in sections, f"report lacks a Test {test} section")
        row = table_row(sections[test], v["label"])
        check(row is not None, f"Test {test}: no table row for {v['label']!r}")
        med, lo, hi = representative(mine)
        check(row[1] == f"{med['time_ms']:.1f}",
              f"Test {test} {v['label']!r}: time {row[1]}, median of trials {med['time_ms']:.1f}")
        if several:
            want = f"{lo['time_ms']:.1f}–{hi['time_ms']:.1f}"
            check(row[2] == want, f"Test {test} {v['label']!r}: range {row[2]}, trials {want}")
        rows += 1
    lines = 0
    for p in by_type(records, "pair"):
        section = sections.get(p["test"], "")
        if p["ratio_label"] and p["slowdown"] is not None:
            want = f"**{p['ratio_label']}:** {p['slowdown']:.2f}×"
            check(want in section, f"Test {p['test']}: report lacks {want!r}")
            lines += 1
        if p["cost_shown"]:
            want = f"{COST_LABEL} {p['effective_cost']:.1f} cycles"
            check(want in section, f"Test {p['test']}: report lacks {want!r}")
            lines += 1
    ok(f"report matches the export: {rows} table rows, {lines} summary lines")


def check_kernel_counts(records, path):
    kernels = {k["id"]: k for k in by_type(records, "kernel")}
    fields = ("data_branches", "indirect_calls", "inner_loop_branches")
    n = 0
    with open(path, encoding="utf-8") as f:
        for line in f:
            words = line.split()
            if not words:
                continue
            check(len(words) == 4, f"{path}: bad line {line.strip()!r}")
            check(words[0] in kernels, f"{path}: no kernel record for {words[0]}")
            k = kernels[words[0]]
            for field, measured in zip(fields, words[1:]):
                if k[field] is None:
                    continue
                check(k[field] == int(measured),
                      f"kernel {words[0]}: declared {field} {k[field]}, binary has {measured}")
            n += 1
    check(n, f"{path}: no kernel counts")
    ok(f"{n} kernel descriptors match the binary")


def results(records, test):
    trials = [t for t in by_type(records, "trial") if t["test"] == test]
    return [(t["variant"], t["result"]) for t in sorted(trials, key=lambda t: (t["variant"], t["seq"]))]


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("export")
    ap.add_argument("--report")
    ap.add_argument("--kernel-counts")
    ap.add_argument("--same-results")
    ap.add_argument("--different-results")
    ap.add_argument("--test", type=int)
    args = ap.parse_args()
    try:
        records = load(args.export)
        variants = check_structure(records)
        check_pairs(records)
        if args.report:
            check_report(records, variants, args.report)
        if args.kernel_counts:
            check_kernel_counts(records, args.kernel_counts)
        for other, same in ((args.same_results, True), (args.different_results, False)):
            if not other:
                continue
            check(args.test is not None, "--same-results/--different-results need --test")
            mine, theirs = results(records, args.test), results(load(other), args.test)
            check(mine, f"no Test {args.test} trials in {args.export}")
            check((mine == theirs) == same,
                  f"Test {args.test} results {'differ' if same else 'are equal'}: {mine} vs {theirs}")
            ok(f"Test {args.test} results {'equal' if same else 'differ'}")
    except Failure as e:
        print(f"FAIL {e}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
