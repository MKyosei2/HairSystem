using System.Threading;
public static class SlowController
{
    public static string Get() { Thread.Sleep(500); return "ok"; }
}
