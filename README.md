# HairSystem Thesis Practical Evaluation Patch

Target repository:

- `MKyosei2/HairSystem`
- `main`
- base commit: `05346c8505cf08367002c7c7af03c3bc9a91255e`

This ZIP is an **overlay package**. Extract it anywhere, then run:

```powershell
Set-Location "<extracted folder>"
.\Apply-ThesisPracticalEvaluation.ps1 -RepoRoot "C:\path\to\HairSystem"
```

The patcher:

1. checks the expected HairSystem source layout,
2. checks the base commit when Git is available,
3. creates `.thesis_practical_backup_<timestamp>`,
4. copies all new files into the existing repository paths,
5. patches the existing source files using fail-closed anchors,
6. runs static contract validation.

It does not create or switch branches and does not push to GitHub.

For a local repository based on a different commit, `-Force` bypasses the
commit check, but source anchors still must match.

## Main code changes

- real local Guide edit stimulus for S11
- proposal-only SG Block invalidation instead of unconditional all-dirty
- fail-closed fallback to the existing full-dynamic path
- `*_thesis_edits.csv`
- T00-T07 practical workload matrix
- report script joining edit events with existing frame/Novelty01 telemetry
- no change to the existing Novelty01 64-counter ABI

See `payload/docs/THESIS_PRACTICAL_EVALUATION.md`.
