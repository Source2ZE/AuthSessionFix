using System.Reflection;
using System.Runtime.InteropServices;
using CounterStrikeSharp.API;
using CounterStrikeSharp.API.Core;
using Microsoft.Extensions.Logging;
namespace AuthSessionFix;

public class AuthSessionFix : BasePlugin
{
    public override string ModuleName => "AuthSessionFix";
    public override string ModuleVersion => "1.0.0";
    public override string ModuleAuthor => "Poggu";
    public override string ModuleDescription => "Fixes the issue where the server doesn't end the auth session when a player disconnects, resulting in inaccurate player counts on the server browser.";
    
    [DllImport("steam_api")]
    public static extern IntPtr SteamInternal_CreateInterface(string name);
    
    [DllImport("steam_api", EntryPoint = "SteamGameServer_GetHSteamPipe", CallingConvention = CallingConvention.Cdecl)]
    public static extern int SteamGameServer_GetHSteamPipe();

    [DllImport("steam_api", EntryPoint = "SteamGameServer_GetHSteamUser", CallingConvention = CallingConvention.Cdecl)]
    public static extern int SteamGameServer_GetHSteamUser();
    
    [DllImport("steam_api", EntryPoint = "SteamAPI_ISteamClient_GetISteamGenericInterface", CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr ISteamClient_GetISteamGenericInterface(IntPtr instancePtr, IntPtr hSteamUser, IntPtr hSteamPipe, string pchVersion);
    
    [DllImport("steam_api", EntryPoint = "SteamAPI_ISteamGameServer_EndAuthSession", CallingConvention = CallingConvention.Cdecl)]
    public static extern void ISteamGameServer_EndAuthSession(IntPtr instancePtr, ulong steamid);
    
    private IntPtr _gGameServer = IntPtr.Zero;
    
    public override void Load(bool hotReload)
    {
        NativeLibrary.SetDllImportResolver(Assembly.GetExecutingAssembly(), DllImportResolver);
        
        RegisterListener<Listeners.OnGameServerSteamAPIActivated>(LoadSteamClient);
        
        RegisterListener<Listeners.OnGameServerSteamAPIDeactivated>(() =>
        {
            _gGameServer = IntPtr.Zero;
        });
        
        RegisterListener<Listeners.OnClientDisconnect>((int playerSlot) =>
        {
            var player = Utilities.GetPlayerFromSlot(playerSlot);
            if (player?.SteamID is not null)
            {
                ISteamGameServer_EndAuthSession(_gGameServer, player.SteamID);
            }
        });

        if (hotReload)
            LoadSteamClient();
    }
    private void LoadSteamClient()
    {
        var steamPipe = SteamGameServer_GetHSteamPipe();
        var steamUser = SteamGameServer_GetHSteamUser();

        if (steamPipe == IntPtr.Zero || steamUser == IntPtr.Zero)
        {
            Logger.LogError("Steam API failed to load");
            return;
        }
        
        var steamClient = SteamInternal_CreateInterface("SteamClient020");
        
        if (steamClient == IntPtr.Zero)
        {
            Logger.LogError("Steam Client failed to load");
            return;
        }
  
        _gGameServer = ISteamClient_GetISteamGenericInterface(steamClient, steamUser, steamPipe, "SteamGameServer014");
        
        if(_gGameServer == IntPtr.Zero)
        {
            Logger.LogError("Failed to get SteamGameServer");
            return;
        }
        
        Logger.LogInformation("Steam API loaded successfully");
    }
    
    private static IntPtr DllImportResolver(string libraryName, Assembly assembly, DllImportSearchPath? searchPath)
    {
        if (libraryName == "steam_api")
            return NativeLibrary.Load(RuntimeInformation.IsOSPlatform(OSPlatform.Windows) ? "steam_api64" : "libsteam_api", assembly, searchPath);
        
        return IntPtr.Zero;
    }
}