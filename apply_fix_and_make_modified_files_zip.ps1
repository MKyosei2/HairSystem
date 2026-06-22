$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Get-Location

python "$ScriptDir\apply_fix_and_make_modified_files_zip.py" --repo-root "$RepoRoot" --out-zip "HairSystem_Novelty_61_21_modified_files_only_FULL.zip"
