/** @file
	@brief Client time offset example code in C#.

	@copyright 2019 Aqueti

	@author ReliaSolve, working for Aqueti.
*/

using System;
using System.Runtime.InteropServices;
public class TimeWarp
{
    [DllImport("TimeWarp.dll", CallingConvention = CallingConvention.Cdecl)]
    public static unsafe extern void* atl_TimeWarpClientCreate(string hostName, int port, string cardIP);

    [DllImport("TimeWarp.dll")]
    public static unsafe extern bool atl_TimeWarpClientSetTimeOffset(void* client, Int64 offset);

    [DllImport("TimeWarp.dll")]
    public static unsafe extern bool atl_TimeWarpClientDestroy(void* client);

    public unsafe TimeWarp(string hostName, int port, string cardIP)
    {
        client = atl_TimeWarpClientCreate(hostName, port, cardIP);
    }

    public unsafe bool SetTimeOffset(Int64 offset)
    {
        if (client == null) { return false; }
        return atl_TimeWarpClientSetTimeOffset(client, offset);
    }

    unsafe ~TimeWarp()
    {
        atl_TimeWarpClientDestroy(client);
    }

    private unsafe void* client = null;
}

class main
{
    static int Main(string[] args)
    {
        if (args.Length != 3)
        {
            System.Console.WriteLine("Usage: TimeWarp_client_example_CS HOST PORT OFFSET");
            return 1;
        }
        string host = args[0];
        int port = Int32.Parse(args[1]);
        Int64 offset = Int64.Parse(args[2]);
        TimeWarp tw = new TimeWarp(host, port, "");
        if (!tw.SetTimeOffset(offset))
        {
            System.Console.WriteLine("Could not set offset");
            return 2;
        }

        System.Console.WriteLine("Success!");
        return 0;
    }
}
