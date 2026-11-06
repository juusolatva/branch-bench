# Security Policy

## Supported Versions

Only the latest release, or the default branch if there are no releases, receives security fixes.

## Reporting a Vulnerability

Please do not report security vulnerabilities through public issues, pull requests, or discussions.

Report them privately through GitHub's private vulnerability reporting:

1. Go to the repository's Security tab.
2. Click Report a vulnerability.

If that option is unavailable, please contact me using the email address on my GitHub profile.

Please include:

- A description of the issue and its potential impact
- Steps to reproduce, or a proof of concept
- Affected version, commit, or configuration

## What to Expect

This is a personal project maintained on a best-effort basis. I aim to acknowledge reports within 7 days and will keep you updated on progress. Once a fix is released, you will be credited in the advisory unless you prefer to remain anonymous.

## System and Scope

branch_bench is a local C microbenchmark for POSIX systems. The benchmark has no network listener or remote input service. It generates its benchmark datasets internally.

Security review covers branch_bench.c, build and test scripts, dependency configuration, and GitHub Actions workflows that build, check, or publish releases.

Assets include the invoking user's files, CI credentials, and the integrity of published binaries and their provenance.

## Threat Model and Trust Boundaries

The intended runtime is an ordinary user process. The caller selects command-line arguments and the report destination. Privileged execution or use behind a service accepting untrusted requests requires a separate assessment.

Runtime inputs include command-line arguments, filesystem paths, CPU metadata from procfs and sysfs, and Linux perf counter responses. Review parsing and bounds handling even when inputs normally originate from the local operating system.

For local attacks, identify what another actor can control, such as a shared output directory, and what authority the benchmark has that the actor lacks. Do not assume remote control of command-line arguments without a concrete integration.

Pull-request contents are untrusted code. CI credentials, publishing permissions, release artifacts, and tag verification form separate security boundaries.

## Security Invariants

- Argument parsing, allocation sizes, array indices, and counter-response handling must remain memory-safe, including malformed input and failure paths.
- Report creation must request owner-only permissions for new files. Report write or close failures must produce a failing exit status.
- Unavailable perf counters must permit wall-clock operation without the benchmark changing host security settings or elevating privileges.
- Untrusted contributions must not gain unauthorized access to CI secrets or release publishing authority.
- Published release binaries must correspond to the verified signed tag's commit. Checksums and provenance must describe the artifacts actually published.

## Reportable Findings and Severity Context

Report memory corruption, unintended disclosure or modification of files, credential exposure, and compromise of build or release integrity when supported by a reachable path and concrete impact.

Assess severity using the attacker's prerequisites and the authority crossed. Distinguish effects limited to an invocation the attacker already controls from effects on another user's files, CI credentials, or distributed binaries.

Benchmark timing differences and measurement errors need a demonstrated security consequence to qualify as security findings. No finding class is categorically excluded by this policy.

## Known Limitations

The output option intentionally overwrites an existing destination and currently follows symbolic links. Existing file permissions are retained. This behavior does not establish safety in attacker-writable directories; assess any file-write issue against the actual invocation and filesystem control.

Large caller-selected workloads intentionally consume CPU time and memory. Tests, sanitizers, static analysis, pinned build inputs, and release checks provide evidence about controls, but do not prove the absence of vulnerabilities.
