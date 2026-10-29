#!/usr/bin/env python3
"""Checks a branch_bench --json export (schema 1) against its own trials.

Rebuilds every pair summary from the trial records, independently of the
C code, and compares it with the pair records, and checks each trial's
scaled counts against its raw counts and scheduling times.  With --report,
also checks that the Markdown report shows the same medians, ranges,
counter columns, Miss %, misses per item, ratios and effective-cost lines
(resolved and inconclusive) and Test 6's "Correlation used" lines.  Every
pair must compare equal work and inputs.  Every check must have something
to compare: an empty export or report fails.  Standard library only.  The
schema, revision and the constants below must change together with
branch_bench.c (JSON_SCHEMA, BENCH_REVISION, COST_MIN_MISSES_PER_PAIR,
D6_MIN_SAMPLE).

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
D6_MIN_SAMPLE = 65536
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
    trials = by_type(records, "trial")
    check(trials, "export has no trial records")
    for t in trials:
        check((t["test"], t["variant"]) in variants,
              f"trial seq {t['seq']}: no variant {t['test']}/{t['variant']}")
    used = {t["pair"] for t in trials if t["pair"] is not None}
    recorded = {p["pair"] for p in by_type(records, "pair")}
    check(used == recorded, f"trial pairs {sorted(used)} differ from pair records {sorted(recorded)}")
    ok("structure: meta first, schema 1, revision 2, every trial has a variant and kernel, "
       "every pair has a record")
    return variants


def check_trials(records):
    """Valid trials: scaled counts follow raw counts and scheduling times;
    other trials have no counts at all."""
    n = 0
    for t in by_type(records, "trial"):
        where = f"trial seq {t['seq']}"
        if t["perf_status"] != "ok":
            check(all(v is None for v in t["counts"].values()) and
                  all(v is None for v in t["raw_counts"].values()),
                  f"{where}: status {t['perf_status']} but has counts")
            continue
        enabled, running = t["enabled_ns"], t["running_ns"]
        check(0 < running <= enabled, f"{where}: running {running} ns, enabled {enabled} ns")
        for name, raw in t["raw_counts"].items():
            scaled = t["counts"][name]
            check((raw is None) == (scaled is None), f"{where}: {name} raw/scaled mismatch")
            if raw is None:
                continue
            want = raw if running == enabled else int(float(raw) * (enabled / running))
            check(abs(scaled - want) <= 1, f"{where}: {name} scaled {scaled}, raw {raw} gives {want}")
        n += 1
    ok(f"{n} counted trials consistent with their raw counts and scheduling times")


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


def check_pair_inputs(records, variants):
    """Both sides of a pair do the same work on inputs of the same size and,
    where recorded, the same number of condition-true items."""
    n = 0
    for p in by_type(records, "pair"):
        a, b = variants[(p["test"], p["a"])], variants[(p["test"], p["b"])]
        where = f"pair {p['pair']} (test {p['test']})"
        check(a["work"] == b["work"], f"{where}: work {a['work']} vs {b['work']}")
        for field in ("pass_items", "cond_true"):
            if a[field] is not None and b[field] is not None:
                check(a[field] == b[field], f"{where}: {field} {a[field]} vs {b[field]}")
        n += 1
    ok(f"{n} pairs compare equal work and inputs")


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


def cells(line):
    return [c.strip() for c in line.strip().strip("|").split("|")]


def table_row(section, label):
    """The header cells and the row cells for label, or (None, None)."""
    header = None
    for line in section.splitlines():
        if line.startswith("| Variant |"):
            header = cells(line)
        elif line.startswith(f"| {label} |"):
            return header, cells(line)
    return None, None


def miss_rate_valid(meta, counts):
    """Miss % is shown only for counted branches and misses of one known
    population (branch_bench.c's miss_rate_valid())."""
    sem = {c["name"]: c["semantics"] for c in meta["counters"]}
    b, m = counts.get("branches"), counts.get("misses")
    return (b is not None and m is not None and b > 0 and
            sem.get("branches") == sem.get("misses") and
            sem.get("branches") not in (None, "unknown"))


def expected_cells(meta, trial, work, unit):
    """Counter, Miss % and misses-per-item cells as report_table() prints them."""
    counts = trial["counts"]
    want = {}
    for header, name in (("Cycles", "cycles"), ("Branches", "branches"), ("Misses", "misses")):
        want[header] = "–" if counts[name] is None else str(counts[name])
    if miss_rate_valid(meta, counts):
        want["Miss %"] = f"{100.0 * counts['misses'] / counts['branches']:.1f}%"
    else:
        want["Miss %"] = "–"
    misses = counts["misses"]
    if misses is None:
        want[f"Misses/{unit}"] = "–"
    else:
        want[f"Misses/{unit}"] = f"{misses / work:.3f}" if work else "0.000"
    return want


def cost_line(p):
    """The effective-cost line print_pair_cost() writes for a pair."""
    if p["cost_status"] == "inconclusive":
        return (f"{COST_LABEL} inconclusive (Δcycles {p['delta']['cycles']:+.0f}, "
                f"Δmisses {p['delta']['misses']:+.0f} over {p['valid']} valid pair(s))")
    text = f"{COST_LABEL} {p['effective_cost']:.1f} cycles"
    if p["valid"] < p["pairs"]:
        text += f" ({p['valid']} of {p['pairs']} pairs)"
    return text


def correlation_lines(records, variants):
    """The lines print_correlation_used() writes for Test 6, or None when no
    pair has miss counts."""
    entries = []
    counted = short_na = evaluated = 0
    best = 0
    for p in sorted((p for p in by_type(records, "pair") if p["test"] == 6),
                    key=lambda p: p["pair"]):
        a = variants[(6, p["a"])]
        d = int(a["label"].split("=")[1].split()[0])
        has_misses = p["valid"] > 0 and p["delta"]["misses"] is not None
        counted += has_misses
        if not has_misses or a["pass_items"] < D6_MIN_SAMPLE:
            short_na += has_misses
            entries.append(f"d={d} n/a")
            continue
        avoided = p["delta"]["misses"] / p["valid"]
        pct = min(100.0, max(0.0, 100.0 * avoided / (0.5 * a["work"])))
        entries.append(f"d={d} {pct:.0f}%")
        evaluated += 1
        if pct >= 90.0:
            best = d
    if not counted:
        return None
    shown = (str(best) if best else "none") if evaluated else "n/a"
    lines = [f"**Correlation used:** {', '.join(entries)}\n\n"
             f"**Largest d with ≥ 90%:** {shown}\n\n"]
    if short_na:
        lines.append(f"_n/a: fewer than {D6_MIN_SAMPLE} blocks per pass, so a predictor "
                     f"may learn the sequences._\n")
    return lines


def check_correlation(records, variants, sections):
    """Test 6's correlation lines, rebuilt from its pairs; returns how many
    lines were compared."""
    section = sections.get(6, "")
    want = correlation_lines(records, variants)
    if want is None:
        check("**Correlation used:**" not in section,
              "Test 6: report has a Correlation used line without miss counts")
        return 0
    for line in want:
        check(line in section, f"Test 6: report lacks {line.strip()!r}")
    if len(want) == 1:
        check("_n/a: fewer than" not in section, "Test 6: report has an n/a note no pair needs")
    return len(want)


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
        header, row = table_row(sections[test], v["label"])
        check(row is not None and header is not None, f"Test {test}: no table row for {v['label']!r}")
        check(len(row) == len(header), f"Test {test} {v['label']!r}: {len(row)} cells, header has {len(header)}")
        col = dict(zip(header, row))
        med, lo, hi = representative(mine)
        check(col["Time (ms)"] == f"{med['time_ms']:.1f}",
              f"Test {test} {v['label']!r}: time {col['Time (ms)']}, median of trials {med['time_ms']:.1f}")
        if several:
            want = f"{lo['time_ms']:.1f}–{hi['time_ms']:.1f}"
            check(col.get("Range (ms)") == want,
                  f"Test {test} {v['label']!r}: range {col.get('Range (ms)')}, trials {want}")
        unit = header[-1].split("/", 1)[1]
        for name, want in expected_cells(records[0], med, v["work"], unit).items():
            check(col.get(name) == want,
                  f"Test {test} {v['label']!r}: {name} {col.get(name)}, median trial gives {want}")
        rows += 1
    lines = 0
    for p in by_type(records, "pair"):
        section = sections.get(p["test"], "")
        if p["ratio_label"] and p["slowdown"] is not None:
            want = f"**{p['ratio_label']}:** {p['slowdown']:.2f}×"
            check(want in section, f"Test {p['test']}: report lacks {want!r}")
            lines += 1
        if p["cost_shown"]:
            check(p["cost_status"] != "unavailable",
                  f"pair {p['pair']}: cost line shown although unavailable")
            want = cost_line(p)
            check(want + "\n" in section, f"Test {p['test']}: report lacks {want!r}")
            lines += 1
    if any(t["test"] == 6 for t in trials):
        lines += check_correlation(records, variants, sections)
    check(rows, "report check compared no table rows")
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
    measured = set()
    with open(path, encoding="utf-8") as f:
        measured = {line.split()[0] for line in f if line.split()}
    for k in kernels.values():
        declared = any(k[field] is not None for field in fields)
        check(not declared or k["id"] in measured,
              f"kernel {k['id']}: declared counts but {path} has none")
    ok(f"{n} kernel descriptors match the binary")


def results(records, test):
    """A test's trial results and its variants' input hashes."""
    trials = [t for t in by_type(records, "trial") if t["test"] == test]
    hashes = {v["variant"]: v.get("input_hash") for v in by_type(records, "variant")
              if v["test"] == test}
    return ([(t["variant"], t["result"]) for t in sorted(trials, key=lambda t: (t["variant"], t["seq"]))],
            hashes)


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
        check_trials(records)
        check_pairs(records)
        check_pair_inputs(records, variants)
        if args.report:
            check_report(records, variants, args.report)
        if args.kernel_counts:
            check_kernel_counts(records, args.kernel_counts)
        for other, same in ((args.same_results, True), (args.different_results, False)):
            if not other:
                continue
            check(args.test is not None, "--same-results/--different-results need --test")
            mine, theirs = results(records, args.test), results(load(other), args.test)
            check(mine[0], f"no Test {args.test} trials in {args.export}")
            check(theirs[0], f"no Test {args.test} trials in {other}")
            check((mine == theirs) == same,
                  f"Test {args.test} results {'differ' if same else 'are equal'}: {mine} vs {theirs}")
            ok(f"Test {args.test} results {'equal' if same else 'differ'}")
    except Failure as e:
        print(f"FAIL {e}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
