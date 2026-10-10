// Purpose: Describes deterministic QA capabilities per language/technology so autonomous QA can prove the requested check actually ran.
using Dip.Application.Features.QualityAutomation;
using Dip.Domain.ValueObjects;

namespace Dip.Infrastructure.QualityAutomation;

public sealed class QaCapabilityPlanner : IQaCapabilityPlanner
{
    public QaLanguagePack Describe(LanguageId l, QaTechnologyProfile profile, string workspace)
    {
        var c = new List<QaCapabilityDescriptor>
        {
            new(QaCapability.AstAnalysis, "dip-semantic", "Language-neutral structural analysis"),
            new(QaCapability.DataFlowAnalysis, "dip-semantic", "Bounded source-to-sink data-flow analysis"),
            new(QaCapability.Security, "dip-semantic", "Cross-language dangerous sink and secret-flow checks"),
            new(QaCapability.DeadlockDetection, "dip-semantic/runtime", "Static deadlock-risk rules plus runtime hang/deadlock correlation"),
            new(QaCapability.LspDiagnostics, "dip-ide-bridge", "Normalized IDE/LSP diagnostic ingestion", true)
        };

        void Add(QaCapability capability, string tool, string detail, bool config = false) =>
            c.Add(new(capability, tool, detail, config));

        if (l == LanguageId.JavaScript)
        {
            Add(QaCapability.Compile, "node", "Syntax");
            Add(QaCapability.StaticAnalysis, "eslint", "Lint", true);
            Add(QaCapability.UnitTest, "npm", "Project tests", true);
            Add(QaCapability.PropertyTest, "fast-check", "Property tests", true);
            Add(QaCapability.Fuzz, "jazzer.js", "Fuzz target", true);
        }
        else if (l == LanguageId.TypeScript)
        {
            Add(QaCapability.TypeCheck, "tsc", "TypeScript type check");
            Add(QaCapability.StaticAnalysis, "eslint", "Lint", true);
            Add(QaCapability.UnitTest, "npm", "Project tests", true);
            Add(QaCapability.PropertyTest, "fast-check", "Property tests", true);
            Add(QaCapability.Fuzz, "jazzer.js", "Fuzz target", true);
        }
        else if (l == LanguageId.Go)
        {
            Add(QaCapability.Compile, "go test", "Compile/tests");
            Add(QaCapability.StaticAnalysis, "go vet", "Static analysis");
            Add(QaCapability.RaceDetection, "go test -race", "Race detector");
            Add(QaCapability.Fuzz, "go test -fuzz", "Native fuzz targets", true);
            Add(QaCapability.PropertyTest, "testing/quick", "Property tests", true);
        }
        else if (l == LanguageId.Rust)
        {
            Add(QaCapability.Compile, "cargo check", "Compile");
            Add(QaCapability.StaticAnalysis, "clippy", "Static analysis");
            Add(QaCapability.UnitTest, "cargo test", "Tests");
            Add(QaCapability.Fuzz, "cargo-fuzz", "Fuzz targets", true);
            Add(QaCapability.RaceDetection, "miri", "Race/undefined behavior checks", true);
            Add(QaCapability.MemorySafety, "miri", "Memory/undefined behavior checks", true);
            Add(QaCapability.PropertyTest, "proptest/quickcheck", "Property tests", true);
        }
        else if (l == LanguageId.Java || l == LanguageId.Kotlin)
        {
            Add(QaCapability.Compile, l == LanguageId.Java ? "javac" : "kotlinc", "Compile");
            Add(QaCapability.StaticAnalysis, "SpotBugs/ErrorProne", "Static analysis", true);
            Add(QaCapability.UnitTest, "Maven/Gradle", "Tests", true);
            Add(QaCapability.PropertyTest, "jqwik/Kotest", "Property tests", true);
            Add(QaCapability.Fuzz, "Jazzer", "Fuzz target", true);
        }
        else if (l == LanguageId.Ruby)
        {
            Add(QaCapability.Compile, "ruby -c", "Syntax");
            Add(QaCapability.StaticAnalysis, "rubocop", "Static analysis", true);
            Add(QaCapability.UnitTest, "rspec", "Tests", true);
            Add(QaCapability.PropertyTest, "rantly", "Property tests", true);
        }
        else if (l == LanguageId.Php)
        {
            Add(QaCapability.Compile, "php -l", "Syntax");
            Add(QaCapability.StaticAnalysis, "phpstan/psalm", "Static analysis", true);
            Add(QaCapability.UnitTest, "phpunit", "Tests", true);
            Add(QaCapability.PropertyTest, "eris", "Property tests", true);
        }
        else if (l == LanguageId.Bash)
        {
            Add(QaCapability.Compile, "bash -n", "Syntax");
            Add(QaCapability.StaticAnalysis, "shellcheck", "Static analysis", true);
            Add(QaCapability.UnitTest, "bats", "Tests", true);
        }
        else if (l == LanguageId.PowerShell)
        {
            Add(QaCapability.Compile, "PowerShell parser", "Syntax");
            Add(QaCapability.StaticAnalysis, "PSScriptAnalyzer", "Static analysis", true);
            Add(QaCapability.UnitTest, "Pester", "Tests", true);
        }
        else if (l == LanguageId.Lua)
        {
            Add(QaCapability.Compile, "luac -p", "Syntax");
            Add(QaCapability.UnitTest, "busted", "Tests", true);
            Add(QaCapability.PropertyTest, "lua-quickcheck", "Property tests", true);
        }
        else if (l == LanguageId.Swift)
        {
            Add(QaCapability.TypeCheck, "swiftc", "Type check");
            Add(QaCapability.UnitTest, "swift test", "Tests", true);
            Add(QaCapability.Fuzz, "libFuzzer", "Fuzz target", true);
        }
        else if (l == LanguageId.C || l == LanguageId.Cpp || l == LanguageId.ObjectiveC)
        {
            Add(QaCapability.Compile, "clang", "Compile diagnostics");
            Add(QaCapability.StaticAnalysis, "clang-tidy/cppcheck", "Static analysis", true);
            Add(QaCapability.Fuzz, "libFuzzer/AFL++", "Fuzz target", true);
            Add(QaCapability.RaceDetection, "ThreadSanitizer", "Race detection", true);
            Add(QaCapability.MemorySafety, "ASan/UBSan", "Memory safety and undefined-behavior checks", true);
            Add(QaCapability.Runtime, "CTest/runtime", "Runtime execution/reproduction", true);
        }
        else if (l == LanguageId.Sql)
        {
            Add(QaCapability.StaticAnalysis, "sqlfluff", "SQL lint");
            Add(QaCapability.Database, "DIP DB QA", "Migration/query contract checks", true);
        }
        else if (l == LanguageId.Glsl)
        {
            Add(QaCapability.Compile, "glslangValidator", "Shader compilation");
            Add(QaCapability.ShaderValidation, "glslangValidator", "GLSL shader validation");
            Add(QaCapability.GameEngine, "DIP Game QA", "Shader/engine integration", true);
        }
        else if (l == LanguageId.Hlsl)
        {
            Add(QaCapability.Compile, "dxc/fxc", "Shader compilation", true);
            Add(QaCapability.ShaderValidation, "dxc/fxc", "HLSL shader validation", true);
            Add(QaCapability.GameEngine, "DIP Game QA", "Shader/engine integration", true);
        }
        else if (l == LanguageId.Gdscript)
        {
            Add(QaCapability.Compile, "Godot headless", "Project/script validation", true);
            Add(QaCapability.GameEngine, "Godot headless", "Engine validation", true);
        }
        else
        {
            Add(QaCapability.Compile, ".dip/qa.json", "Custom compile command", true);
            Add(QaCapability.UnitTest, ".dip/qa.json", "Custom test command", true);
            Add(QaCapability.Fuzz, ".dip/qa.json", "Custom fuzz command", true);
        }

        // Every language, including proprietary IDs, can fill dynamic capabilities
        // through the no-shell manifest contract. Dedicated high-risk capabilities
        // are deliberately not fabricated here: they must be explicitly configured.
        if (!c.Any(x => x.Capability == QaCapability.PropertyTest))
            Add(QaCapability.PropertyTest, ".dip/qa.json", "Language/project-specific property-test command", true);
        if (!c.Any(x => x.Capability == QaCapability.Fuzz))
            Add(QaCapability.Fuzz, ".dip/qa.json", "Language/project-specific fuzz command", true);
        if (!c.Any(x => x.Capability == QaCapability.RaceDetection))
            Add(QaCapability.RaceDetection, ".dip/qa.json", "Language/project-specific race/concurrency command", true);
        if (!c.Any(x => x.Capability == QaCapability.MemorySafety))
            Add(QaCapability.MemorySafety, ".dip/qa.json", "Language/project-specific memory-safety command", true);
        if (profile.GameEngines.Count > 0)
            Add(QaCapability.GameEngine, string.Join('/', profile.GameEngines), "Engine-aware QA");

        Add(QaCapability.Runtime, "DIP Runtime Defect Intelligence", "Crash/hang/runtime correlation", true);
        Add(QaCapability.ApiContract, "DIP API QA", "HTTP/OpenAPI contract checks", true);
        Add(QaCapability.DistributedSystem, "DIP Distributed QA", "Retry/timeout/idempotency/health checks", true);
        return new(l, c.DistinctBy(x => (x.Capability, x.Tool)).ToArray());
    }
}
