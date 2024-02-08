/**
 * =============================================================================
 * AuthSessionFix
 * Copyright (C) 2024 Poggu
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include "authsessionfix.h"
#include <iserver.h>
#include <steam/steam_gameserver.h>
#include "utils/module.h"
#include <funchook.h>
class GameSessionConfiguration_t { };

SH_DECL_HOOK3_void(IServerGameDLL, GameFrame, SH_NOATTRIB, 0, bool, bool, bool);
SH_DECL_HOOK4_void(IServerGameClients, ClientActive, SH_NOATTRIB, 0, CPlayerSlot, bool, const char *, uint64);
SH_DECL_HOOK5_void(IServerGameClients, ClientDisconnect, SH_NOATTRIB, 0, CPlayerSlot, ENetworkDisconnectionReason, const char *, uint64, const char *);
SH_DECL_HOOK4_void(IServerGameClients, ClientPutInServer, SH_NOATTRIB, 0, CPlayerSlot, char const *, int, uint64);
SH_DECL_HOOK1_void(IServerGameClients, ClientSettingsChanged, SH_NOATTRIB, 0, CPlayerSlot );
SH_DECL_HOOK6_void(IServerGameClients, OnClientConnected, SH_NOATTRIB, 0, CPlayerSlot, const char*, uint64, const char *, const char *, bool);
SH_DECL_HOOK6(IServerGameClients, ClientConnect, SH_NOATTRIB, 0, bool, CPlayerSlot, const char*, uint64, const char *, bool, CBufferString *);
SH_DECL_HOOK2(IGameEventManager2, FireEvent, SH_NOATTRIB, 0, bool, IGameEvent *, bool);
SH_DECL_HOOK3_void(INetworkServerService, StartupServer, SH_NOATTRIB, 0, const GameSessionConfiguration_t&, ISource2WorldSession*, const char*);
SH_DECL_HOOK2_void( IServerGameClients, ClientCommand, SH_NOATTRIB, 0, CPlayerSlot, const CCommand & );
SH_DECL_HOOK0_void(IServerGameDLL, GameServerSteamAPIActivated, SH_NOATTRIB, 0);
SH_DECL_HOOK0_void(IServerGameDLL, GameServerSteamAPIDeactivated, SH_NOATTRIB, 0);

#ifdef WIN32
SH_DECL_MANUALHOOK2_void(MHook_DisconnectClient, 67 + 5, 0, 0, void*, int);
#else
SH_DECL_MANUALHOOK2_void(MHook_DisconnectClient, 68, 0, 0, void*, int);
#endif

#ifdef _WIN32
#define ROOTBIN "/bin/win64/"
#define GAMEBIN "/csgo/bin/win64/"
#else
#define ROOTBIN "/bin/linuxsteamrt64/"
#define GAMEBIN "/csgo/bin/linuxsteamrt64/"
#endif


AuthSessionFix g_AuthSessionFix;
IServerGameDLL *server = NULL;
IServerGameClients *gameclients = NULL;
IVEngineServer *engine = NULL;
IGameEventManager2 *gameevents = NULL;
ICvar *icvar = NULL;
CSteamGameServerAPIContext g_steamAPI;
void Hook_NotifyClientDisconnect(void* steam3server, void* serverSideClient);

typedef void (*NotifyClientDisconnect_t)(void* steam3server, void* serverSideClient);
NotifyClientDisconnect_t g_pNotifyClientDisconnect = nullptr;
funchook_t* g_hook = nullptr;

// Should only be called within the active game loop (i e map should be loaded and active)
// otherwise that'll be nullptr!
CGlobalVars *GetGameGlobals()
{
	INetworkGameServer *server = g_pNetworkServerService->GetIGameServer();

	if(!server)
		return nullptr;

	return g_pNetworkServerService->GetIGameServer()->GetGlobals();
}

PLUGIN_EXPOSE(AuthSessionFix, g_AuthSessionFix);
bool AuthSessionFix::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();

	GET_V_IFACE_CURRENT(GetEngineFactory, engine, IVEngineServer, INTERFACEVERSION_VENGINESERVER);
	GET_V_IFACE_CURRENT(GetEngineFactory, icvar, ICvar, CVAR_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetServerFactory, server, IServerGameDLL, INTERFACEVERSION_SERVERGAMEDLL);
	GET_V_IFACE_ANY(GetServerFactory, gameclients, IServerGameClients, INTERFACEVERSION_SERVERGAMECLIENTS);
	GET_V_IFACE_ANY(GetEngineFactory, g_pNetworkServerService, INetworkServerService, NETWORKSERVERSERVICE_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetServerFactory, g_pSource2Server, ISource2Server, SOURCE2SERVER_INTERFACE_VERSION);

	// Currently doesn't work from within mm side, use GetGameGlobals() in the mean time instead
	// gpGlobals = ismm->GetCGlobals();

	// Required to get the IMetamodListener events
	g_SMAPI->AddListener( this, this );

	META_CONPRINTF( "Starting plugin.\n" );

	SH_ADD_HOOK(IServerGameDLL, GameFrame, server, SH_MEMBER(this, &AuthSessionFix::Hook_GameFrame), true);
	SH_ADD_HOOK(IServerGameClients, ClientActive, gameclients, SH_MEMBER(this, &AuthSessionFix::Hook_ClientActive), true);
	SH_ADD_HOOK(IServerGameClients, ClientDisconnect, gameclients, SH_MEMBER(this, &AuthSessionFix::Hook_ClientDisconnect), true);
	SH_ADD_HOOK(IServerGameClients, ClientPutInServer, gameclients, SH_MEMBER(this, &AuthSessionFix::Hook_ClientPutInServer), true);
	SH_ADD_HOOK(IServerGameClients, ClientSettingsChanged, gameclients, SH_MEMBER(this, &AuthSessionFix::Hook_ClientSettingsChanged), false);
	SH_ADD_HOOK(IServerGameClients, OnClientConnected, gameclients, SH_MEMBER(this, &AuthSessionFix::Hook_OnClientConnected), false);
	SH_ADD_HOOK(IServerGameClients, ClientConnect, gameclients, SH_MEMBER(this, &AuthSessionFix::Hook_ClientConnect), false);
	SH_ADD_HOOK(IServerGameClients, ClientCommand, gameclients, SH_MEMBER(this, &AuthSessionFix::Hook_ClientCommand), false);
	SH_ADD_HOOK(INetworkServerService, StartupServer, g_pNetworkServerService, SH_MEMBER(this, &AuthSessionFix::Hook_StartupServer), true);
	SH_ADD_HOOK(IServerGameDLL, GameServerSteamAPIActivated, g_pSource2Server, SH_MEMBER(this, &AuthSessionFix::Hook_GameServerSteamAPIActivated), false);
	SH_ADD_HOOK(IServerGameDLL, GameServerSteamAPIDeactivated, g_pSource2Server, SH_MEMBER(this, &AuthSessionFix::Hook_GameServerSteamAPIDeactivated), false);

	auto serverModule = new CModule(ROOTBIN, "engine2");

	int err;
#ifdef WIN32
	const byte sig[] = "\x48\x85\xD2\x0F\x84\x2A\x2A\x2A\x2A\x48\x89\x6C\x24\x20";
#else
	const byte sig[] = "\x48\x85\xF6\x0F\x84\x2A\x2A\x2A\x2A\x55\x48\x89\xE5\x41\x55\x49\x89\xFD";
#endif
	g_pNotifyClientDisconnect = (NotifyClientDisconnect_t)serverModule->FindSignature((byte*)sig, sizeof(sig)-1, err);

	auto g_hook = funchook_create();
	funchook_prepare(g_hook, (void**)&g_pNotifyClientDisconnect, (void*)Hook_NotifyClientDisconnect);
	funchook_install(g_hook, 0);

	if (err)
	{
		META_CONPRINTF("Failed to find signature: %d\n", err);
	}

	META_CONPRINTF( "All hooks started!\n" );

	g_pCVar = icvar;
	ConVar_Register( FCVAR_RELEASE | FCVAR_CLIENT_CAN_EXECUTE | FCVAR_GAMEDLL );

	return true;
}

bool AuthSessionFix::Unload(char *error, size_t maxlen)
{
	SH_REMOVE_HOOK(IServerGameDLL, GameFrame, server, SH_MEMBER(this, &AuthSessionFix::Hook_GameFrame), true);
	SH_REMOVE_HOOK(IServerGameClients, ClientActive, gameclients, SH_MEMBER(this, &AuthSessionFix::Hook_ClientActive), true);
	SH_REMOVE_HOOK(IServerGameClients, ClientDisconnect, gameclients, SH_MEMBER(this, &AuthSessionFix::Hook_ClientDisconnect), true);
	SH_REMOVE_HOOK(IServerGameClients, ClientPutInServer, gameclients, SH_MEMBER(this, &AuthSessionFix::Hook_ClientPutInServer), true);
	SH_REMOVE_HOOK(IServerGameClients, ClientSettingsChanged, gameclients, SH_MEMBER(this, &AuthSessionFix::Hook_ClientSettingsChanged), false);
	SH_REMOVE_HOOK(IServerGameClients, OnClientConnected, gameclients, SH_MEMBER(this, &AuthSessionFix::Hook_OnClientConnected), false);
	SH_REMOVE_HOOK(IServerGameClients, ClientConnect, gameclients, SH_MEMBER(this, &AuthSessionFix::Hook_ClientConnect), false);
	SH_REMOVE_HOOK(IServerGameClients, ClientCommand, gameclients, SH_MEMBER(this, &AuthSessionFix::Hook_ClientCommand), false);
	SH_REMOVE_HOOK(INetworkServerService, StartupServer, g_pNetworkServerService, SH_MEMBER(this, &AuthSessionFix::Hook_StartupServer), true);
	SH_REMOVE_HOOK(IServerGameDLL, GameServerSteamAPIActivated, g_pSource2Server, SH_MEMBER(this, &AuthSessionFix::Hook_GameServerSteamAPIActivated), false);
	SH_REMOVE_HOOK(IServerGameDLL, GameServerSteamAPIDeactivated, g_pSource2Server, SH_MEMBER(this, &AuthSessionFix::Hook_GameServerSteamAPIDeactivated), false);

	funchook_uninstall(g_hook, 0);
	funchook_destroy(g_hook);

	return true;
}


void Hook_NotifyClientDisconnect(void* steam3server, void* serverSideClient)
{
#ifdef WIN32
	int offset = 0x9B;
#else
	int offset = 0xAB;
#endif
	uint64 steamId = *(uint64*)((uint8*)serverSideClient + offset);

	if (steamId && CSteamID(steamId).GetEAccountType() != k_EAccountTypeAnonGameServer)
	{
		//ConMsg("Force client deauth %llu\n", steamId);
		g_steamAPI.SteamGameServer()->EndAuthSession(CSteamID(steamId));
	}

	g_pNotifyClientDisconnect(steam3server, serverSideClient);
}


void AuthSessionFix::AllPluginsLoaded()
{
	/* This is where we'd do stuff that relies on the mod or other plugins
	 * being initialized (for example, cvars added and events registered).
	 */
}

void AuthSessionFix::Hook_GameServerSteamAPIActivated()
{
	g_steamAPI.Init();
}

void AuthSessionFix::Hook_GameServerSteamAPIDeactivated()
{
}

void AuthSessionFix::Hook_StartupServer(const GameSessionConfiguration_t& config, ISource2WorldSession*, const char*)
{
	auto pNetworkGameServer = g_pNetworkServerService->GetIGameServer();
	ConMsg("fdsfds %p\n", pNetworkGameServer);
	//SH_ADD_MANUALHOOK(MHook_DisconnectClient, pNetworkGameServer, SH_MEMBER(this, &AuthSessionFix::Hook_DisconnectClient), true);
}

void AuthSessionFix::Hook_ClientActive( CPlayerSlot slot, bool bLoadGame, const char *pszName, uint64 xuid )
{
	META_CONPRINTF( "Hook_ClientActive(%d, %d, \"%s\", %d)\n", slot, bLoadGame, pszName, xuid );
}

void AuthSessionFix::Hook_ClientCommand( CPlayerSlot slot, const CCommand &args )
{
	META_CONPRINTF( "Hook_ClientCommand(%d, \"%s\")\n", slot, args.GetCommandString() );
}

void AuthSessionFix::Hook_ClientSettingsChanged( CPlayerSlot slot )
{
	META_CONPRINTF( "Hook_ClientSettingsChanged(%d)\n", slot );
}

void AuthSessionFix::Hook_OnClientConnected( CPlayerSlot slot, const char *pszName, uint64 xuid, const char *pszNetworkID, const char *pszAddress, bool bFakePlayer )
{
	META_CONPRINTF( "Hook_OnClientConnected(%d, \"%s\", %d, \"%s\", \"%s\", %d)\n", slot, pszName, xuid, pszNetworkID, pszAddress, bFakePlayer );
}

bool AuthSessionFix::Hook_ClientConnect( CPlayerSlot slot, const char *pszName, uint64 xuid, const char *pszNetworkID, bool unk1, CBufferString *pRejectReason )
{
	META_CONPRINTF( "Hook_ClientConnect(%d, \"%s\", %d, \"%s\", %d, \"%s\")\n", slot, pszName, xuid, pszNetworkID, unk1, pRejectReason->ToGrowable()->Get() );

	RETURN_META_VALUE(MRES_IGNORED, true);
}

void AuthSessionFix::Hook_ClientPutInServer( CPlayerSlot slot, char const *pszName, int type, uint64 xuid )
{
	META_CONPRINTF( "Hook_ClientPutInServer(%d, \"%s\", %d, %d)\n", slot, pszName, type, xuid );
}

void AuthSessionFix::Hook_ClientDisconnect( CPlayerSlot slot, ENetworkDisconnectionReason reason, const char *pszName, uint64 xuid, const char *pszNetworkID )
{
	META_CONPRINTF( "Hook_ClientDisconnect(%d, %d, \"%s\", %d, \"%s\")\n", slot, reason, pszName, xuid, pszNetworkID );
}

void AuthSessionFix::Hook_GameFrame( bool simulating, bool bFirstTick, bool bLastTick )
{
	/**
	 * simulating:
	 * ***********
	 * true  | game is ticking
	 * false | game is not ticking
	 */
}

void AuthSessionFix::OnLevelInit( char const *pMapName,
									 char const *pMapEntities,
									 char const *pOldLevel,
									 char const *pLandmarkName,
									 bool loadGame,
									 bool background )
{
	META_CONPRINTF("OnLevelInit(%s)\n", pMapName);
}

void AuthSessionFix::OnLevelShutdown()
{
	META_CONPRINTF("OnLevelShutdown()\n");
}

bool AuthSessionFix::Pause(char *error, size_t maxlen)
{
	return true;
}

bool AuthSessionFix::Unpause(char *error, size_t maxlen)
{
	return true;
}

const char *AuthSessionFix::GetLicense()
{
	return "GPLv3";
}

const char *AuthSessionFix::GetVersion()
{
	return "1.0.0.0";
}

const char *AuthSessionFix::GetDate()
{
	return __DATE__;
}

const char *AuthSessionFix::GetLogTag()
{
	return "AuthSessionFix";
}

const char *AuthSessionFix::GetAuthor()
{
	return "Poggu";
}

const char *AuthSessionFix::GetDescription()
{
	return "Fixes auth session not being ended on client disconnect";
}

const char *AuthSessionFix::GetName()
{
	return "AuthSessionFix";
}

const char *AuthSessionFix::GetURL()
{
	return "https://poggu.me";
}
