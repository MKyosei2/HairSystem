using System;
public static class Protocol { public static int ReadLength(byte[] b) => BitConverter.ToInt32(b, 0); }
