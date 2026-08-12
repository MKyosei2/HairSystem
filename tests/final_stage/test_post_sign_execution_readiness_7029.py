from __future__ import annotations

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
SIGN_SCRIPT = ROOT / "tools" / "signing" / "Sign-HairSystemBuild.ps1"


class PostSignExecutionReadiness7029Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = SIGN_SCRIPT.read_text(encoding="utf-8")

    def test_7029_retries_only_transient_access_denied(self) -> None:
        source = self.source
        self.assertIn("function Wait-HairSystemPostSignExecutionReadiness", source)
        self.assertIn("[int]$RetryWindowSeconds = 90", source)
        self.assertIn("[int]$RetryDelaySeconds = 5", source)
        self.assertIn("[int]$AttemptTimeoutSeconds = 30", source)
        self.assertIn("$nativeCode -eq 5", source)
        self.assertIn("$nativeCode -eq 4551", source)
        self.assertIn("$message -match '(?i)access.*denied'", source)
        self.assertIn("Start-Sleep -Seconds $RetryDelaySeconds", source)
        self.assertIn("remained Access Denied for the full $RetryWindowSeconds-second readiness window", source)

    def test_7029_freezes_signed_bytes_and_signer_during_retry(self) -> None:
        source = self.source
        self.assertIn("Get-FileHash -LiteralPath $Executable -Algorithm SHA256", source)
        self.assertIn("Assert-SignatureValid", source)
        self.assertIn("HairSystem.exe changed during post-sign execution readiness", source)
        self.assertIn("-ExpectedThumbprint $ExpectedThumbprint", source)

    def test_7029_uses_real_native_self_test_before_returning_to_formal_runner(self) -> None:
        source = self.source
        self.assertIn("-ArgumentList @('--native-self-test')", source)
        self.assertIn("Post-sign native self-test failed. ExitCode=$($process.ExitCode)", source)
        self.assertIn("Post-sign native self-test timed out after $AttemptTimeoutSeconds seconds", source)
        self.assertIn("Post-sign execution readiness PASS on attempt $attempt", source)

    def test_7029_does_not_disable_windows_security(self) -> None:
        source = self.source
        forbidden = (
            "Set-MpPreference",
            "Set-ProcessMitigation",
            "Disable-WindowsOptionalFeature",
            "VerifiedAndReputablePolicyState",
            "CiTool.exe -r",
            "AppLockerPolicy",
        )
        for token in forbidden:
            self.assertNotIn(token, source)

    def test_7029_readiness_runs_only_for_real_signed_execution(self) -> None:
        source = self.source
        self.assertIn("if (-not $WhatIf)", source)
        self.assertIn("$files | Where-Object { $_.Name -ieq 'HairSystem.exe' }", source)
        self.assertIn("Wait-HairSystemPostSignExecutionReadiness", source)
        self.assertIn("-RetryWindowSeconds 90", source)
        self.assertIn("-RetryDelaySeconds 5", source)
        self.assertIn("-AttemptTimeoutSeconds 30", source)


if __name__ == "__main__":
    unittest.main()
