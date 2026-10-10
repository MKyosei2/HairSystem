using System;
public static class Combat
{
    public static int Damage() => new Random().Next(1, 100); // intentional nondeterminism for replay divergence
}
