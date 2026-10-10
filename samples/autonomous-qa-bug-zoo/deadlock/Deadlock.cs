using System.Threading;
public static class DeadlockCase
{
    private static readonly object A = new();
    private static readonly object B = new();
    public static void Left() { lock (A) { Thread.Sleep(10); lock (B) { } } }
    public static void Right() { lock (B) { Thread.Sleep(10); lock (A) { } } }
}
