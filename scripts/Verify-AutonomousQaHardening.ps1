param(
    [string]$RepositoryRoot = (Get-Location).Path
)
$ErrorActionPreference = 'Stop'
Set-Location $RepositoryRoot

Write-Host '=== Autonomous QA hardening verification ==='

$required = @(
    'src/Dip.Application/Features/QualityAutomation/AutonomousQaEvidenceValidator.cs',
    'src/Dip.Application/Features/QualityAutomation/AutonomousQaCapabilityPolicy.cs',
    'src/Dip.Application/Features/QualityAutomation/AutonomousQaWorkspaceContracts.cs',
    'src/Dip.Infrastructure/QualityAutomation/AutonomousQaExperimentDispatcher.cs',
    'src/Dip.Infrastructure/QualityAutomation/GitAutonomousQaWorkspaceManager.cs',
    'src/Dip.Worker/Workers/AutonomousQaJobProgressReporter.cs',
    'samples/autonomous-qa-bug-zoo/manifest.json'
)
foreach($path in $required) {
    if(-not (Test-Path $path)) { throw "Missing required hardening file: $path" }
}

if(Get-Command python -ErrorAction SilentlyContinue) {
    python .\tools\verify_autonomous_qa_hardening.py
    if($LASTEXITCODE -ne 0) { throw 'Autonomous QA hardening static guard failed.' }

    python .\tools\autonomous_qa_bug_zoo_verify.py
    if($LASTEXITCODE -ne 0) { throw 'Autonomous QA Bug Zoo contract verification failed.' }
}

if(-not (Get-Command dotnet -ErrorAction SilentlyContinue)) {
    throw 'dotnet SDK is required for final repository verification.'
}

dotnet restore .\DevelopmentIntelligencePlatform.sln
if($LASTEXITCODE -ne 0) { throw 'dotnet restore failed.' }

dotnet build .\DevelopmentIntelligencePlatform.sln -c Release --no-restore
if($LASTEXITCODE -ne 0) { throw 'Release build failed.' }

dotnet test .\tests\Dip.Application.Tests\Dip.Application.Tests.csproj -c Release --no-build
if($LASTEXITCODE -ne 0) { throw 'Dip.Application.Tests failed.' }

dotnet test .\tests\Dip.Infrastructure.Tests\Dip.Infrastructure.Tests.csproj -c Release --no-build
if($LASTEXITCODE -ne 0) { throw 'Dip.Infrastructure.Tests failed.' }

Write-Host 'AUTONOMOUS_QA_HARDENING=PASS'
