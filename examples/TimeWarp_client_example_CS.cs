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
    public static extern int atl_TimeWarpClientCreate(string hostName, int port, string cardIP);

    [DllImport("TimeWarp.dll")]
    public static extern bool atl_TimeWarpClientSetTimeOffset(int client, Int64 offset);

    [DllImport("TimeWarp.dll")]
    public static extern bool atl_TimeWarpClientDestroy(int client);

    public TimeWarp(string hostName, int port, string cardIP)
    {
        client = atl_TimeWarpClientCreate(hostName, port, cardIP);
    }

    public bool SetTimeOffset(Int64 offset)
    {
        return atl_TimeWarpClientSetTimeOffset(client, offset);
    }

    ~TimeWarp()
    {
        atl_TimeWarpClientDestroy(client);
    }

    private int client = -1;
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
