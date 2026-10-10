using Dip.Application.Features.QualityAutomation;
using Dip.Domain.ValueObjects;
using Dip.Infrastructure.QualityAutomation;
using FluentAssertions;

namespace Dip.Infrastructure.Tests.QualityAutomation;

public sealed class AutonomousQaCapabilityPlannerTests
{
    [Fact]
    public void Cpp_ExposesDedicatedMemorySafetyCapability()
    {
        var profile = new QaTechnologyProfile(".", [LanguageId.Cpp], ["CMake"], [], [], []);
        var pack = new QaCapabilityPlanner().Describe(LanguageId.Cpp, profile, ".");

        pack.Capabilities.Should().Contain(x =>
            x.Capability == QaCapability.MemorySafety
            && x.Tool.Contains("ASan", StringComparison.OrdinalIgnoreCase));
    }

    [Theory]
    [InlineData("glsl")]
    [InlineData("hlsl")]
    public void ShaderLanguages_ExposeDedicatedShaderValidation(string language)
    {
        var id = LanguageId.Parse(language);
        var profile = new QaTechnologyProfile(".", [id], [], [], [], []);
        var pack = new QaCapabilityPlanner().Describe(id, profile, ".");

        pack.Capabilities.Should().Contain(x => x.Capability == QaCapability.ShaderValidation);
    }
}
