using Dip.Infrastructure.QualityAutomation;
using FluentAssertions;

namespace Dip.Infrastructure.Tests.QualityAutomation;

public sealed class AutonomousQaSandboxPolicyTests
{
    [Theory]
    [InlineData("ffmpeg")]
    [InlineData("ffprobe")]
    [InlineData("k6")]
    [InlineData("renderdoccmd")]
    [InlineData("nsys")]
    [InlineData("ncu")]
    public void AutonomousQaExternalTools_AreExplicitlyAllowlisted(string tool) =>
        ProcessCommandValidator.ExternalQaTools.Should().Contain(tool);
}
