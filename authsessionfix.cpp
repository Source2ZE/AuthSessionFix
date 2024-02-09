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
ICvar *icvar = NULL;
CSteamGameServerAPIContext g_steamAPI;
void Hook_NotifyClientDisconnect(void* steam3server, void* serverSideClient);

typedef void (*NotifyClientDisconnect_t)(void* steam3server, void* serverSideClient);
NotifyClientDisconnect_t g_pNotifyClientDisconnect = nullptr;
funchook_t* g_hook = nullptr;

PLUGIN_EXPOSE(AuthSessionFix, g_AuthSessionFix);
bool AuthSessionFix::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();

	GET_V_IFACE_CURRENT(GetEngineFactory, icvar, ICvar, CVAR_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetServerFactory, g_pSource2Server, ISource2Server, SOURCE2SERVER_INTERFACE_VERSION);

	g_SMAPI->AddListener( this, this );

	META_CONPRINTF( "Starting plugin.\n" );

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

	if (err)
	{
		META_CONPRINTF("[AuthSessionFix] Failed to find signature: %i\n", err);
		V_snprintf(error, maxlen, "[AuthSessionFix] Failed to find signature: %i\n", err);
		return false;
	}

	auto g_hook = funchook_create();
	funchook_prepare(g_hook, (void**)&g_pNotifyClientDisconnect, (void*)Hook_NotifyClientDisconnect);
	funchook_install(g_hook, 0);

	META_CONPRINTF( "AuthSessionFix started!\n" );

	g_pCVar = icvar;
	ConVar_Register( FCVAR_RELEASE | FCVAR_CLIENT_CAN_EXECUTE | FCVAR_GAMEDLL );

	return true;
}

bool AuthSessionFix::Unload(char *error, size_t maxlen)
{
	SH_REMOVE_HOOK(IServerGameDLL, GameServerSteamAPIActivated, g_pSource2Server, SH_MEMBER(this, &AuthSessionFix::Hook_GameServerSteamAPIActivated), false);
	SH_REMOVE_HOOK(IServerGameDLL, GameServerSteamAPIDeactivated, g_pSource2Server, SH_MEMBER(this, &AuthSessionFix::Hook_GameServerSteamAPIDeactivated), false);

	if (g_hook)
	{
		funchook_uninstall(g_hook, 0);
		funchook_destroy(g_hook);
	}

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
}

void AuthSessionFix::Hook_GameServerSteamAPIActivated()
{
	g_steamAPI.Init();
}

void AuthSessionFix::Hook_GameServerSteamAPIDeactivated()
{
}

void AuthSessionFix::OnLevelInit(char const* pMapName,
	char const* pMapEntities,
	char const* pOldLevel,
	char const* pLandmarkName,
	bool loadGame,
	bool background)
{
}

void AuthSessionFix::OnLevelShutdown()
{
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
