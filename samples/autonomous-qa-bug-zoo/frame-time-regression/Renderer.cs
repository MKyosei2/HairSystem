using System.Threading;
public static class RendererRegression
{
    public static void RenderFrame() => Thread.Sleep(40); // intentional frame-time regression
}
