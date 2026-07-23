# Runtime suite branch context

## Purpose

HairSystem experiments are no longer tied to a hard-coded branch such as
`Novelty` or `prenovelty-ready`.  The formal batch entry point detects the branch
that is currently checked out, pins its exact commit SHA, and materializes a
resolved suite configuration before the runner starts.

This supports future implementation branches while preserving reproducibility.
A result is identified by both:

- the detected branch name;
- the exact 40-character commit SHA.

The commit SHA remains the authoritative source identity.  The branch name is
recorded so a human can understand where the result came from.

## Required entry point

Run suites through:

```bat
tools\final_stage\Run_Final_Runtime_Suite.bat ^
  docs\sample_assets\prenovelty_runtime_suite.json ^
  prenovelty-smoke
```

The mode is mandatory.  When the output directory is omitted, the batch file
creates a branch- and commit-specific directory below the config's
`outputs_dir`:

```text
<outputs_dir>/<sanitized-branch>/<commit-short-sha>/
```

An explicit output directory is also supported:

```bat
tools\final_stage\Run_Final_Runtime_Suite.bat ^
  docs\sample_assets\prenovelty_runtime_suite.json ^
  prenovelty-smoke ^
  out\my_prenovelty_run
```

## Detection order

`hairgpu_branch_context.py` resolves the source branch in this order:

1. `git branch --show-current`;
2. `git symbolic-ref --short HEAD`;
3. `GITHUB_HEAD_REF`;
4. `GITHUB_REF_NAME`.

A detached HEAD is rejected for formal execution by default.  Diagnostic tools
may opt in to a detached commit context explicitly, but those results must not
be mixed with named-branch formal evidence.

## Generated context files

The output root contains:

```text
suite_branch_context.json
.suite_context/
  <branch>/
    <commit>/
      resolved_suite_config.json
      timelines/
        <materialized scenario files>
```

`suite_branch_context.json` records:

- branch;
- commit SHA;
- branch-detection source;
- upstream branch when available;
- origin URL when available;
- original config path and SHA-256;
- resolved config path;
- output directory.

The resolved suite configuration replaces `source.required_branch` with the
branch detected at runtime.  The existing strict preflight therefore still
rejects accidental branch changes; the branch name is simply resolved instead
of being hard-coded.

## Input immutability

Original suite JSON and scenario timeline files are never modified.  Timelines
are copied into `.suite_context/.../timelines/` and any compatibility assertions
are applied only to those run-specific copies.

This avoids:

- dirtying the Git worktree during a test;
- changing source timeline hashes between runs;
- leaking baseline-specific assertions into another method;
- making Resume depend on a previously mutated input file.

After a suite finishes, the batch entry point verifies that tracked source files
are unchanged.

## Cross-branch output protection

If an explicit output directory already contains `suite_branch_context.json`
for another branch or commit, the resolver stops before the suite starts.
Choose a new output directory instead of mixing evidence.

This is intentional.  Results from two commits must never share one formal
result directory even when their suite names and run names are identical.

## Startup stability

The `startup20` mode now launches the real Viewer window and render loop through
`hairgpu_startup_self_test.py`, posts `WM_CLOSE`, and requires a clean exit for
all 20 repetitions.  Native and shader self-tests remain separate checks; they
are not substitutes for the real startup/shutdown path.

## Files involved

```text
tools/final_stage/Run_Final_Runtime_Suite.bat
tools/final_stage/hairgpu_branch_context.py
tests/final_stage/test_hairgpu_branch_context.py
docs/RUNTIME_BRANCH_CONTEXT.md
```
