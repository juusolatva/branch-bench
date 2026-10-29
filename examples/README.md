# Example output

`i5-8350U-standard.md` and `i5-8350U-standard.jsonl` are one run of all
six tests with the standard profile, kept to show the report and export
formats of benchmark revision 4 (schema 2). They demonstrate the format;
they are not a reference result, and RESULTS.md stays the place for
results.

- Machine: Intel Core i5-8350U laptop on AC power, Fedora 44.
- Command: `./run.sh --hard -n 6 --cpu 2 -o examples/i5-8350U-standard.md --json examples/i5-8350U-standard.jsonl`
- Commit: shown in the report's header (`Commit:`) and the export's
  `meta` record.

Check that the two files agree:

```sh
python3 .github/scripts/check_export.py examples/i5-8350U-standard.jsonl \
    --report examples/i5-8350U-standard.md
```
