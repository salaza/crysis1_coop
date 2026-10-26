#include "StdAfx.h"
#include "CoopAdminMenu.h"
#include "CoopNetPlatform.h"
#include <IConsole.h>
#include <IRenderer.h>
#include <I3DEngine.h>
#include <IGameFramework.h>
#include <ILevelSystem.h>
#include <IActorSystem.h>
#include <IMovieSystem.h>
#include <IMusicSystem.h>
#include <ISound.h>
#include <algorithm>
#include "Game.h"
#include "GameRules.h"
#include "SynchedStorage.h"
#include "Entities/SequenceSynchronizer.h"
#include "Player.h"
#include "Actor.h"
#include "GameCVars.h"

static const TSynchedKey COOP_LOBBY_WAITING_KEY = 950;
static const TSynchedKey COOP_LOBBY_ROSTER_KEY  = 951;
static const TSynchedKey COOP_LOBBY_LOADING_KEY = 952;
static const TSynchedKey COOP_LOBBY_READY_KEY   = 953;

struct SMenuItem
{
	const char* name;
	const char* desc;
};

static const SMenuItem s_maps[] = {
	{ "island",  "Map 01: Contact (Island)" },
	{ "village", "Map 02: Recovery (Village)" },
	{ "rescue",  "Map 03: Relic (Rescue)" },
	{ "harbor",  "Map 04: Assault (Harbor)" },
	{ "tank",    "Map 05: Onslaught (Tank)" },
	{ "mine",    "Map 06: Awakening (Mine)" },
	{ "core",    "Map 07: Core (Alien Ship)" },
	{ "ice",     "Map 08: Paradise Lost (Ice)" },
	{ "sphere",  "Map 09: Exodus (Sphere)" },
	{ "ascent",  "Map 10: Ascension (VTOL Ascent)" },
	{ "fleet",   "Map 11: Reckoning (Aircraft Carrier Fleet)" },
};

static const SMenuItem s_cheats[] = {
	{ "Restart Current Map",      "Reload and restart the current mission" },
	{ "Toggle Fly / Noclip",      "Toggle fly / noclip mode (p_fly_mode)" },
	{ "Toggle God Mode",          "Toggle player invulnerability (g_godMode)" },
	{ "Toggle Unlimited Ammo",    "Toggle infinite ammunition (i_unlimitedammo)" },
	{ "Give All Weapons & Items", "Give all SP/MP weapons and suit items" },
	{ "Give Max Ammo",            "Refill ammo pools to maximum" },
	{ "Toggle AI Ignore Player",  "Toggle AI blindness / ignoring players" },
	{ "Kill All Enemies",         "Eliminate all active enemy AI entities" },
	{ "Clear Map Drawings",       "Clear strategic drawings on PDA map" },
};

static const SMenuItem s_timeOfDay[] = {
	{ "Morning (06:00)",      "Set time of day to sunrise / early morning" },
	{ "Noon (12:00)",         "Set time of day to midday sun" },
	{ "Sunset (18:30)",       "Set time of day to dusk / sunset" },
	{ "Night (00:00)",        "Set time of day to midnight" },
	{ "Advance +2 Hours",     "Fast forward current time by 2 hours" },
	{ "Rewind -2 Hours",      "Rewind current time by 2 hours" },
	{ "Toggle Pause Time",    "Freeze or unfreeze time progression" },
};

struct SSandboxItem
{
	const char* name;
	const char* desc;
	const char* archetype;
	const char* className;
	const char* cgfModel;
	bool bStatic;
	float offsetZ;
};

static const SSandboxItem s_npcs[] = {
	{ "KPA Rifleman",        "Standard NK infantry soldier", "Asian_new.Cover\\Camp.Heavy_Rifle", "CoopGrunt", "objects/characters/human/asian/nk_soldier/nk_soldier_camp_cover_heavy_01.cdf", false, 0.0f },
	{ "KPA Shotgunner",      "Close quarters NK combatant", "Asian_new.Cover\\Camp.Heavy_Shotgun", "CoopGrunt", "objects/characters/human/asian/nk_soldier/nk_soldier_camp_cover_heavy_shotgun_01.cdf", false, 0.0f },
	{ "KPA Sniper",          "Long range marksman with scope", "Asian_new.Special\\Sniper.Sniper_SniperScope", "CoopGrunt", "objects/characters/human/asian/nk_soldier/nk_soldier_jungle_flanker_light_01.cdf", false, 0.0f },
	{ "KPA Grenadier",       "Heavy grenade launcher specialist", "Asian_new.Cover\\Camp.Heavy_Rifle_Gren", "CoopGrunt", "objects/characters/human/asian/nk_soldier/nk_soldier_camp_cover_heavy_gren_01.cdf", false, 0.0f },
	{ "KPA Nanosuit Soldier","Elite Korean nanosuit operative", "Asian_new.Special\\Suit.Suit_Sniper", "CoopGrunt", "objects/characters/human/asian/nanosuit/nanosuit_asian.cdf", false, 0.0f },
	{ "US Marine Rifleman",  "Allied US Marine with SCAR", "US.Grunt/Cover.US_Rifleman_1", "CoopGrunt", "objects/characters/human/us/marine/marine_01.cdf", false, 0.0f },
	{ "US Marine Gauss",     "US Marine armed with Gauss sniper", "US.Grunt/Sniper.US_Gauss", "CoopGrunt", "objects/characters/human/us/marine/marine_01.cdf", false, 0.0f },
	{ "Squadmate Psycho",    "Sergeant Michael Sykes", "Squad_mates.Squadmate_Sykes", "CoopGrunt", "objects/characters/human/story/michael_sykes/michael_sykes.cdf", false, 0.0f },
	{ "Squadmate Prophet",   "Major Laurence Barnes", "Squad_mates.Squadmate_Barnes", "CoopGrunt", "objects/characters/human/story/laurence_barnes/laurence_barnes.cdf", false, 0.0f },
	{ "Alien Scout",         "Flying Ceph assault bio-mechanoid", "Aliens.Scouts.Scout_MOAC", "CoopScout", "Objects/Characters/Alien/Scout/Scout_base.cdf", false, 2.0f },
	{ "Alien Trooper",       "Ceph ground trooper with MOAC", "Aliens.Troopers.Trooper_MOAC", "Trooper", "objects/characters/alien/trooper/trooper_base.chr", false, 0.0f },
	{ "Alien Hunter",        "Massive Ceph walking combat tripod", "Aliens.Hunters.Hunter", "Hunter", "objects/characters/alien/hunter/hunter.cdf", false, 0.0f },
	{ "Great White Shark",   "Lethal oceanic predator", NULL, "Shark", "Objects/characters/animals/whiteshark/greatwhiteshark.cdf", false, -1.0f },
	{ "Hostage Civilian",    "Unarmed civilian survivor", "US.Scientist.US_Scientist_02", "Civilian", "objects/characters/human/us/scientist/scientist_02.cdf", false, 0.0f },
};

static const SSandboxItem s_props[] = {
	{ "US LTV Humvee",       "Armored transport with gun turret", "Vehicles.Land.US_LTV", "US_ltv", NULL, false, 0.5f },
	{ "Asian LTV Truck",     "NK combat light tactical vehicle", "Vehicles.Land.Asian_LTV", "Asian_ltv", NULL, false, 0.5f },
	{ "US Battle Tank",      "Heavy main battle tank", "Vehicles.Land.US_Tank", "US_tank", NULL, false, 0.5f },
	{ "Asian Battle Tank",   "NK armored heavy tank", "Vehicles.Land.Asian_Tank", "Asian_tank", NULL, false, 0.5f },
	{ "Asian Patrol Boat",   "Armed high speed patrol boat", "Vehicles.Sea.Asian_Patrolboat", "Asian_patrolboat", NULL, false, 0.0f },
	{ "Civilian Speedboat",  "Fast civilian motorboat", "Vehicles.Sea.Asian_Speedboat", "Civ_speedboat", NULL, false, 0.0f },
	{ "Civilian Sedan Car",  "Drivable four-door sedan", "Vehicles.Land.Civ_Car", "Civ_car1", NULL, false, 0.5f },
	{ "Explosive Fuel Barrel","Volatile pressurized fuel drum", NULL, "BasicEntity", "Objects/Library/Storage/Barrels/Barrel_explosive_red.cgf", false, 0.0f },
	{ "Military Ammo Crate", "Heavy wooden military storage box", NULL, "BasicEntity", "Objects/Library/Storage/military/military_shipping_box_wooden_100_17.cgf", false, 0.0f },
	{ "Concrete Barrier",    "Heavy fortified roadblock barrier", NULL, "BasicEntity", "Objects/Library/Barriers/Roadblock/concrete_barrier.cgf", true, 0.0f },
};

static const SSandboxItem s_buildings[] = {
	{ "Asian Guard Tower",   "Tall military lookout watchtower", NULL, "BasicEntity", "Objects/Library/Architecture/Watchtowers/watchtower_asian.cgf", true, 0.0f },
	{ "Concrete Tower",      "Fortified concrete observation post", NULL, "BasicEntity", "Objects/Library/Architecture/Watchtowers/watchtower_concrete.cgf", true, 0.0f },
	{ "Military Bunker",     "Reinforced personal bunker shelter", NULL, "BasicEntity", "Objects/Library/Architecture/Village/personal_bunker.cgf", true, 0.0f },
	{ "Command Container",   "Deployable military operations barrack", NULL, "BasicEntity", "Objects/Library/Architecture/Mobile_Camp_Structures/Chinese_Barrack_Container/chinese_barrack_container_basic.cgf", true, 0.0f },
	{ "Hospital Container",  "Medical hospital shelter container", NULL, "BasicEntity", "Objects/Library/Architecture/Container/Military_Hospital_Container/military_hospital_container.cgf", true, 0.0f },
	{ "Military Tent",       "Large camouflage command shelter tent", NULL, "BasicEntity", "Objects/Library/Architecture/Tents/Asian/tent_asian_middleA.cgf", true, 0.0f },
	{ "Mobile Radar Dish",   "Long-range rotating radar installation", NULL, "BasicEntity", "Objects/Library/Vehicles/Mobile_Radar_Dish/mobile_radar_dish.cgf", true, 0.0f },
	{ "Industrial Water Tower","High-capacity water storage tower", NULL, "BasicEntity", "Objects/Library/Architecture/Watchtowers/water_tower.cgf", true, 0.0f },
};

CCoopAdminMenu& CCoopAdminMenu::GetInstance()
{
	static CCoopAdminMenu s_instance;
	return s_instance;
}

CCoopAdminMenu::CCoopAdminMenu()
	: m_bOpen(false)
	, m_currentCategory(0)
	, m_statusTimer(0.0f)
	, m_bLobbyWaiting(false)
	, m_bWasLobbyWaiting(false)
	, m_lobbySyncTimer(0.0f)
	, m_lobbyTimeSinceLoad(0.0f)
	, m_lobbyInitialTOD(-1.0f)
	, m_bGodMode(false)
	, m_bUnlimitedAmmo(false)
	, m_bAiIgnorePlayer(false)
{
	for (int i = 0; i < ECAT_COUNT; ++i)
		m_selectedItem[i] = 0;
}

CCoopAdminMenu::~CCoopAdminMenu()
{
}

void CCoopAdminMenu::Init()
{
	if (gEnv->pConsole)
	{
		gEnv->pConsole->AddCommand("admin_menu", CmdToggleAdminMenu, 0, "Toggle the Co-op In-Game Admin Menu");
		gEnv->pConsole->AddCommand("coop_start", CmdStartCampaign, 0, "Start the Co-op Campaign from the Pre-Game Lobby");
		gEnv->pConsole->AddCommand("coop_spawn", CmdSpawnItem, 0, "Spawn sandbox item: coop_spawn <category: 3=NPC, 4=Prop, 5=Building> <index>");
	}
}

void CCoopAdminMenu::Shutdown()
{
	if (gEnv->pConsole)
	{
		gEnv->pConsole->RemoveCommand("admin_menu");
		gEnv->pConsole->RemoveCommand("coop_start");
		gEnv->pConsole->RemoveCommand("coop_spawn");
	}
}

void CCoopAdminMenu::CmdToggleAdminMenu(IConsoleCmdArgs* pArgs)
{
	CCoopAdminMenu::GetInstance().Toggle();
}

void CCoopAdminMenu::CmdStartCampaign(IConsoleCmdArgs* pArgs)
{
	CCoopAdminMenu::GetInstance().StartCampaignFromLobby();
}

void CCoopAdminMenu::CmdSpawnItem(IConsoleCmdArgs* pArgs)
{
	if (pArgs->GetArgCount() < 3)
	{
		CryLogAlways("Usage: coop_spawn <category: 3=NPC, 4=Prop, 5=Building> <index>");
		return;
	}
	int cat = atoi(pArgs->GetArg(1));
	int idx = atoi(pArgs->GetArg(2));
	CCoopAdminMenu::GetInstance().SpawnSandboxItem(cat, idx);
}

void CCoopAdminMenu::OnLoadingStart()
{
	m_deferredSequences.clear();
	m_lobbyTimeSinceLoad = 0.0f;
	m_lobbyInitialTOD = -1.0f;
	if (gEnv->bServer && gEnv->bMultiplayer)
	{
		m_bLobbyWaiting = true;
		for (std::map<int, SLobbyPlayerInfo>::iterator it = m_lobbyPlayers.begin(); it != m_lobbyPlayers.end(); ++it)
		{
			it->second.bLoaded = false;
		}
	}
	else
	{
		m_bLobbyWaiting = false;
	}
}

void CCoopAdminMenu::OnLoadingComplete()
{
	m_lobbyTimeSinceLoad = 0.0f;
	m_lobbyInitialTOD = -1.0f;

	if (gEnv->bServer && gEnv->bMultiplayer)
	{
		m_bLobbyWaiting = true;
		m_bWasLobbyWaiting = true;
		if (gEnv->p3DEngine && gEnv->p3DEngine->GetTimeOfDay())
			gEnv->p3DEngine->GetTimeOfDay()->SetPaused(true);
		if (gEnv->pMusicSystem)
			gEnv->pMusicSystem->Pause(true);
		SyncLobbyToClients();
	}
}

void CCoopAdminMenu::OnPlayerConnecting(int channelId, const char* playerName)
{
	if (!gEnv->bServer)
		return;

	SLobbyPlayerInfo& info = m_lobbyPlayers[channelId];
	if (playerName && playerName[0])
		info.name = playerName;
	else if (info.name.empty())
		info.name.Format("Player #%d", channelId);

	info.bLoaded = false;
	SyncLobbyToClients();
}

void CCoopAdminMenu::OnPlayerEnteredGame(int channelId, const char* playerName)
{
	if (!gEnv->bServer)
		return;

	SLobbyPlayerInfo& info = m_lobbyPlayers[channelId];
	if (playerName && playerName[0])
		info.name = playerName;

	if (g_pGame && g_pGame->GetGameRules())
	{
		CActor* pActor = g_pGame->GetGameRules()->GetActorByChannelId(channelId);
		if (pActor)
		{
			if (pActor->GetEntity() && pActor->GetEntity()->GetName()[0])
				info.name = pActor->GetEntity()->GetName();
			info.bIsHost = pActor->IsClient();
		}
	}

	if (info.name.empty())
		info.name.Format("Player #%d", channelId);

	info.bLoaded = true;
	SyncLobbyToClients();
}

void CCoopAdminMenu::OnPlayerDisconnected(int channelId)
{
	if (!gEnv->bServer)
		return;

	m_lobbyPlayers.erase(channelId);
	SyncLobbyToClients();
}

bool CCoopAdminMenu::IsLobbyWaiting() const
{
	if (!gEnv->bMultiplayer)
		return false;

	if (gEnv->bServer)
		return m_bLobbyWaiting;

	if (g_pGame && g_pGame->GetSynchedStorage())
	{
		bool bWaiting = false;
		if (g_pGame->GetSynchedStorage()->GetGlobalValue(COOP_LOBBY_WAITING_KEY, bWaiting))
			return bWaiting;
	}
	return false;
}

void CCoopAdminMenu::RegisterDeferredSequence(ICoopDeferredSequence* pSeq)
{
	if (!pSeq)
		return;
	if (std::find(m_deferredSequences.begin(), m_deferredSequences.end(), pSeq) == m_deferredSequences.end())
	{
		m_deferredSequences.push_back(pSeq);
		CryLogAlways("[CoopLobby] Held sequence '%s' until squad is ready.", pSeq->GetSequenceName());
	}
}

void CCoopAdminMenu::UnregisterDeferredSequence(ICoopDeferredSequence* pSeq)
{
	std::vector<ICoopDeferredSequence*>::iterator it = std::find(m_deferredSequences.begin(), m_deferredSequences.end(), pSeq);
	if (it != m_deferredSequences.end())
		m_deferredSequences.erase(it);
}

void CCoopAdminMenu::SyncLobbyToClients()
{
	if (!gEnv->bServer || !g_pGame || !g_pGame->GetSynchedStorage())
		return;

	int loadingCount = 0;
	int readyCount = 0;
	string rosterStr;

	// Ensure host actor is represented even if map just loaded
	if (IActor* pClientActor = gEnv->pGame->GetIGameFramework()->GetClientActor())
	{
		int hostChan = pClientActor->GetChannelId();
		if (hostChan > 0)
		{
			SLobbyPlayerInfo& hostInfo = m_lobbyPlayers[hostChan];
			if (pClientActor->GetEntity() && pClientActor->GetEntity()->GetName()[0])
				hostInfo.name = pClientActor->GetEntity()->GetName();
			else if (hostInfo.name.empty())
				hostInfo.name = "Host";
			hostInfo.bLoaded = true;
			hostInfo.bIsHost = true;
		}
	}

	int idx = 1;
	for (std::map<int, SLobbyPlayerInfo>::const_iterator it = m_lobbyPlayers.begin(); it != m_lobbyPlayers.end(); ++it, ++idx)
	{
		const SLobbyPlayerInfo& info = it->second;
		if (info.bLoaded)
			readyCount++;
		else
			loadingCount++;

		string entry;
		if (info.bIsHost)
			entry.Format("%d. %-20s [HOST - READY]", idx, info.name.c_str());
		else if (info.bLoaded)
			entry.Format("%d. %-20s [CLIENT - MAP LOADED & READY]", idx, info.name.c_str());
		else
			entry.Format("%d. %-20s [CONNECTING / LOADING MAP...]", idx, info.name.c_str());

		if (!rosterStr.empty())
			rosterStr += "\n";
		rosterStr += entry;
	}

	g_pGame->GetSynchedStorage()->SetGlobalValue(COOP_LOBBY_WAITING_KEY, m_bLobbyWaiting);
	g_pGame->GetSynchedStorage()->SetGlobalValue(COOP_LOBBY_ROSTER_KEY, rosterStr);
	g_pGame->GetSynchedStorage()->SetGlobalValue(COOP_LOBBY_LOADING_KEY, loadingCount);
	g_pGame->GetSynchedStorage()->SetGlobalValue(COOP_LOBBY_READY_KEY, readyCount);
}

void CCoopAdminMenu::StartCampaignFromLobby()
{
	if (!gEnv->bServer || !m_bLobbyWaiting)
		return;

	m_bLobbyWaiting = false;
	m_bWasLobbyWaiting = false;
	SyncLobbyToClients();

	if (gEnv->pMusicSystem)
		gEnv->pMusicSystem->Pause(false);
	if (gEnv->pSoundSystem)
		gEnv->pSoundSystem->Pause(false);
	if (gEnv->p3DEngine && gEnv->p3DEngine->GetTimeOfDay())
	{
		ITimeOfDay* pTOD = gEnv->p3DEngine->GetTimeOfDay();
		pTOD->SetPaused(false);
	}

	CryLogAlways("[CoopLobby] Starting Co-op Campaign! Releasing %d held sequences...", (int)m_deferredSequences.size());

	// Align all joined clients with the Host / SP start point before starting the cutscene
	if (gEnv->pScriptSystem)
	{
		const char* alignLua = "if g_gameRules and g_gameRules.AlignPlayersAfterCutscene then g_gameRules:AlignPlayersAfterCutscene() end";
		gEnv->pScriptSystem->ExecuteBuffer(alignLua, strlen(alignLua));
	}

	// Trigger all deferred FlowGraph PlaySequence nodes at 0.00s now that everyone is loaded
	std::vector<ICoopDeferredSequence*> toStart = m_deferredSequences;
	m_deferredSequences.clear();
	std::set<string> startedNames;
	for (size_t i = 0; i < toStart.size(); ++i)
	{
		if (toStart[i])
		{
			startedNames.insert(toStart[i]->GetSequenceName());
			toStart[i]->OnCoopLobbyStart();
		}
	}

	// Also broadcast any MovieSystem sequences that were held at 0.0f during the lobby
	if (gEnv->pMovieSystem)
	{
		CSequenceSynchronizer* pSync = GetSequenceSynchronizer();
		ISequenceIt* pIt = gEnv->pMovieSystem->GetSequences(true, false);
		if (pIt)
		{
			for (IAnimSequence* pSeq = pIt->first(); pSeq; pSeq = pIt->next())
			{
				if (startedNames.find(pSeq->GetName()) == startedNames.end() && gEnv->pMovieSystem->IsPlaying(pSeq))
				{
					if (pSync)
						pSync->PlaySequence(pSeq->GetName(), 0.0f, false);
				}
			}
			pIt->Release();
		}
	}

	SetStatus("Campaign Started! All connected players synchronized.");
}

void CCoopAdminMenu::Toggle()
{
	SetOpen(!m_bOpen);
}

void CCoopAdminMenu::SetOpen(bool bOpen)
{
	m_bOpen = bOpen;
	if (m_bOpen)
	{
		SetStatus("Admin Menu Opened. [F2/ESC] Close | [Arrows] Navigate | [ENTER] Select");
	}
}

void CCoopAdminMenu::SetStatus(const char* msg)
{
	m_statusMessage = msg ? msg : "";
	m_statusTimer = 4.0f;
}

void CCoopAdminMenu::Update(float frameTime)
{
	if (m_statusTimer > 0.0f)
	{
		m_statusTimer -= frameTime;
		if (m_statusTimer <= 0.0f)
			m_statusMessage.clear();
	}

	// Execute any deferred map switch outside of input event dispatch
	if (!m_pendingMapChange.empty())
	{
		string nextMap = m_pendingMapChange;
		m_pendingMapChange.clear();

		if (gEnv->pConsole)
		{
			if (ICVar* pRules = gEnv->pConsole->GetCVar("sv_gamerules"))
				pRules->ForceSet("Coop");
		}

		IGameFramework* pFW = gEnv->pGame ? gEnv->pGame->GetIGameFramework() : NULL;
		bool bSwitched = false;
		if (pFW && gEnv->bServer && gEnv->bMultiplayer && pFW->StartedGameContext() && pFW->GetServerNetNub() != NULL)
		{
			SGameContextParams ctx;
			ctx.levelName = nextMap.c_str();
			ctx.gameRules = "Coop";
			CryLogAlways("[CoopAdmin] Switching server game context to map '%s' (Coop)...", nextMap.c_str());
			bSwitched = pFW->ChangeGameContext(&ctx);
		}

		if (!bSwitched && pFW)
		{
			string cmd;
			cmd.Format("map %s s x", nextMap.c_str());
			CryLogAlways("[CoopAdmin] Executing map command next frame: %s", cmd.c_str());
			pFW->ExecuteCommandNextFrame(cmd.c_str());
		}
		return;
	}

	bool bWaitingNow = IsLobbyWaiting();
	if (bWaitingNow)
	{
		m_bWasLobbyWaiting = true;
		m_lobbyTimeSinceLoad += frameTime;

		// Ensure all players have full health and suit energy during lobby wait (God Mode)
		if (gEnv->pGame)
		{
			IGameFramework* pGF = gEnv->pGame->GetIGameFramework();
			if (pGF && pGF->GetIActorSystem())
			{
				IActorIteratorPtr pIt = pGF->GetIActorSystem()->CreateActorIterator();
				while (IActor* pActor = pIt->Next())
				{
					if (pActor && pActor->IsPlayer())
					{
						if (pActor->GetHealth() < pActor->GetMaxHealth())
							pActor->SetHealth(pActor->GetMaxHealth());

						CPlayer* pPlayer = static_cast<CPlayer*>(pActor);
						CNanoSuit* pSuit = pPlayer ? pPlayer->GetNanoSuit() : NULL;
						if (pSuit && pSuit->GetSuitEnergy() < 100.0f)
							pSuit->SetSuitEnergy(100.0f);
					}
				}
			}
		}

		// Pause cutscene music and freeze Time of Day while waiting in the pre-game lobby
		if (gEnv->pMusicSystem)
			gEnv->pMusicSystem->Pause(true);

		if (gEnv->p3DEngine && gEnv->p3DEngine->GetTimeOfDay())
		{
			ITimeOfDay* pTOD = gEnv->p3DEngine->GetTimeOfDay();
			pTOD->SetPaused(true);
		}
	}
	else if (m_bWasLobbyWaiting)
	{
		m_bWasLobbyWaiting = false;
		if (gEnv->pMusicSystem)
			gEnv->pMusicSystem->Pause(false);
		if (gEnv->pSoundSystem)
			gEnv->pSoundSystem->Pause(false);
		if (gEnv->p3DEngine && gEnv->p3DEngine->GetTimeOfDay())
		{
			ITimeOfDay* pTOD = gEnv->p3DEngine->GetTimeOfDay();
			pTOD->SetPaused(false);
		}
	}

	if (gEnv->bServer && m_bLobbyWaiting)
	{
		m_lobbySyncTimer += frameTime;
		if (m_lobbySyncTimer >= 0.5f)
		{
			m_lobbySyncTimer = 0.0f;
			SyncLobbyToClients();
		}
	}
}

bool CCoopAdminMenu::OnInputEvent(const SInputEvent &event)
{
	if (event.deviceId != eDI_Keyboard)
		return false;

	// Do not intercept keys while the tilde console is open
	if (gEnv->pConsole && gEnv->pConsole->GetStatus())
		return false;

	if (event.state == eIS_Pressed)
	{
		if (event.keyId == eKI_F2)
		{
			Toggle();
			return true;
		}

		// Handle Pre-Game Campaign Lobby Start keys on Host when Admin Menu is closed
		if (!m_bOpen && gEnv->bServer && m_bLobbyWaiting)
		{
			if (event.keyId == eKI_F3)
			{
				StartCampaignFromLobby();
				return true;
			}
			else if ((event.keyId == eKI_Enter || event.keyId == eKI_NP_Enter) && m_lobbyTimeSinceLoad >= 2.5f)
			{
				int loadingCount = 0;
				for (std::map<int, SLobbyPlayerInfo>::const_iterator it = m_lobbyPlayers.begin(); it != m_lobbyPlayers.end(); ++it)
				{
					if (!it->second.bLoaded)
						loadingCount++;
				}

				if (loadingCount == 0)
				{
					StartCampaignFromLobby();
				}
				else
				{
					SetStatus("Wait for connecting players to finish loading map, or press [F3] to Force Start!");
				}
				return true;
			}
		}

		if (!m_bOpen)
			return false;

		if (event.keyId == eKI_Escape)
		{
			SetOpen(false);
			return true;
		}

		int numItems = 0;
		if (m_currentCategory == ECAT_MAPS)
			numItems = (int)(sizeof(s_maps) / sizeof(s_maps[0]));
		else if (m_currentCategory == ECAT_CHEATS)
			numItems = (int)(sizeof(s_cheats) / sizeof(s_cheats[0]));
		else if (m_currentCategory == ECAT_TIMEOFDAY)
			numItems = (int)(sizeof(s_timeOfDay) / sizeof(s_timeOfDay[0]));
		else if (m_currentCategory == ECAT_NPCS)
			numItems = (int)(sizeof(s_npcs) / sizeof(s_npcs[0]));
		else if (m_currentCategory == ECAT_PROPS)
			numItems = (int)(sizeof(s_props) / sizeof(s_props[0]));
		else if (m_currentCategory == ECAT_BUILDINGS)
			numItems = (int)(sizeof(s_buildings) / sizeof(s_buildings[0]));

		if (event.keyId == eKI_Up || event.keyId == eKI_NP_8)
		{
			m_selectedItem[m_currentCategory]--;
			if (m_selectedItem[m_currentCategory] < 0)
				m_selectedItem[m_currentCategory] = numItems - 1;
			return true;
		}
		else if (event.keyId == eKI_Down || event.keyId == eKI_NP_2)
		{
			m_selectedItem[m_currentCategory]++;
			if (m_selectedItem[m_currentCategory] >= numItems)
				m_selectedItem[m_currentCategory] = 0;
			return true;
		}
		else if (event.keyId == eKI_Left || event.keyId == eKI_NP_4)
		{
			m_currentCategory = (m_currentCategory - 1 + ECAT_COUNT) % ECAT_COUNT;
			return true;
		}
		else if (event.keyId == eKI_Right || event.keyId == eKI_NP_6)
		{
			m_currentCategory = (m_currentCategory + 1) % ECAT_COUNT;
			return true;
		}
		else if (event.keyId == eKI_Enter || event.keyId == eKI_NP_Enter)
		{
			ExecuteSelected();
			return true;
		}

		// Consume all keyboard keys while admin menu is active
		return true;
	}

	return m_bOpen;
}

void CCoopAdminMenu::ExecuteSelected()
{
	int sel = m_selectedItem[m_currentCategory];

	if (m_currentCategory == ECAT_MAPS)
	{
		int maxMaps = (int)(sizeof(s_maps) / sizeof(s_maps[0]));
		if (sel >= 0 && sel < maxMaps)
		{
			ChangeMap(s_maps[sel].name);
		}
	}
	else if (m_currentCategory == ECAT_CHEATS)
	{
		switch (sel)
		{
		case 0: // Restart Current Map
			{
				SetStatus("Restarting current map...");
				ILevelSystem* pLevelSystem = gEnv->pGame ? gEnv->pGame->GetIGameFramework()->GetILevelSystem() : NULL;
				ILevel* pCurLevel = pLevelSystem ? pLevelSystem->GetCurrentLevel() : NULL;
				if (pCurLevel && pCurLevel->GetLevelInfo())
				{
					ChangeMap(pCurLevel->GetLevelInfo()->GetName());
				}
				else if (gEnv->pConsole)
				{
					gEnv->pConsole->ExecuteString("restart");
				}
			}
			break;
		case 1: // Toggle Fly / Noclip
			{
				ICVar* pVar = gEnv->pConsole ? gEnv->pConsole->GetCVar("p_fly_mode") : NULL;
				if (pVar)
				{
					int newVal = (pVar->GetIVal() == 0) ? 1 : 0;
					pVar->Set(newVal);
					SetStatus(newVal ? "Fly / Noclip Mode: ENABLED" : "Fly / Noclip Mode: DISABLED");
				}
			}
			break;
		case 2: // Toggle God Mode
			{
				m_bGodMode = !m_bGodMode;
				EntityId localId = (gEnv->pGame && gEnv->pGame->GetIGameFramework()) ? gEnv->pGame->GetIGameFramework()->GetClientActorId() : 0;
				if (CGameRules* pGR = g_pGame ? g_pGame->GetGameRules() : NULL)
					pGR->RequestAdminCheat(2, localId);
				SetStatus(m_bGodMode ? "God Mode: ENABLED" : "God Mode: DISABLED");
			}
			break;
		case 3: // Toggle Unlimited Ammo
			{
				m_bUnlimitedAmmo = !m_bUnlimitedAmmo;
				if (g_pGameCVars)
					g_pGameCVars->i_unlimitedammo = m_bUnlimitedAmmo ? 1 : 0;
				EntityId localId = (gEnv->pGame && gEnv->pGame->GetIGameFramework()) ? gEnv->pGame->GetIGameFramework()->GetClientActorId() : 0;
				if (CGameRules* pGR = g_pGame ? g_pGame->GetGameRules() : NULL)
					pGR->RequestAdminCheat(3, localId);
				SetStatus(m_bUnlimitedAmmo ? "Unlimited Ammo: ENABLED" : "Unlimited Ammo: DISABLED");
			}
			break;
		case 4: // Give All Weapons
			{
				EntityId localId = (gEnv->pGame && gEnv->pGame->GetIGameFramework()) ? gEnv->pGame->GetIGameFramework()->GetClientActorId() : 0;
				if (CGameRules* pGR = g_pGame ? g_pGame->GetGameRules() : NULL)
					pGR->RequestAdminCheat(4, localId);
				SetStatus("All Weapons & Items Given!");
			}
			break;
		case 5: // Give Max Ammo
			{
				EntityId localId = (gEnv->pGame && gEnv->pGame->GetIGameFramework()) ? gEnv->pGame->GetIGameFramework()->GetClientActorId() : 0;
				if (CGameRules* pGR = g_pGame ? g_pGame->GetGameRules() : NULL)
					pGR->RequestAdminCheat(5, localId);
				SetStatus("Ammunition Refilled to Maximum!");
			}
			break;
		case 6: // Toggle AI Ignore Player
			{
				m_bAiIgnorePlayer = !m_bAiIgnorePlayer;
				EntityId localId = (gEnv->pGame && gEnv->pGame->GetIGameFramework()) ? gEnv->pGame->GetIGameFramework()->GetClientActorId() : 0;
				if (CGameRules* pGR = g_pGame ? g_pGame->GetGameRules() : NULL)
					pGR->RequestAdminCheat(6, localId);
				SetStatus(m_bAiIgnorePlayer ? "AI Ignore Players: ENABLED" : "AI Ignore Players: DISABLED");
			}
			break;
		case 7: // Kill All Enemies
			{
				EntityId localId = (gEnv->pGame && gEnv->pGame->GetIGameFramework()) ? gEnv->pGame->GetIGameFramework()->GetClientActorId() : 0;
				if (CGameRules* pGR = g_pGame ? g_pGame->GetGameRules() : NULL)
					pGR->RequestAdminCheat(7, localId);
				SetStatus("All Active Enemy AI Killed!");
			}
			break;
		case 8: // Clear Map Drawings
			{
				if (CGameRules* pGR = g_pGame->GetGameRules())
					pGR->SendMapClearDrawings();
				SetStatus("Strategic PDA Map Drawings Cleared!");
			}
			break;
		}
	}
	else if (m_currentCategory == ECAT_TIMEOFDAY)
	{
		ITimeOfDay* pTOD = gEnv->p3DEngine ? gEnv->p3DEngine->GetTimeOfDay() : NULL;
		if (pTOD)
		{
			switch (sel)
			{
			case 0: // Morning
				pTOD->SetTime(6.0f, true);
				pTOD->Update(true, true);
				SetStatus("Time of Day set to 06:00 (Morning)");
				break;
			case 1: // Noon
				pTOD->SetTime(12.0f, true);
				pTOD->Update(true, true);
				SetStatus("Time of Day set to 12:00 (Noon)");
				break;
			case 2: // Sunset
				pTOD->SetTime(18.5f, true);
				pTOD->Update(true, true);
				SetStatus("Time of Day set to 18:30 (Sunset)");
				break;
			case 3: // Night
				pTOD->SetTime(0.0f, true);
				pTOD->Update(true, true);
				SetStatus("Time of Day set to 00:00 (Night)");
				break;
			case 4: // +2 Hours
				{
					float cur = pTOD->GetTime();
					float next = fmodf(cur + 2.0f, 24.0f);
					pTOD->SetTime(next, true);
					pTOD->Update(true, true);
					string msg;
					msg.Format("Time of Day advanced to %.1f:00", next);
					SetStatus(msg.c_str());
				}
				break;
			case 5: // -2 Hours
				{
					float cur = pTOD->GetTime();
					float prev = fmodf(cur - 2.0f + 24.0f, 24.0f);
					pTOD->SetTime(prev, true);
					pTOD->Update(true, true);
					string msg;
					msg.Format("Time of Day rewound to %.1f:00", prev);
					SetStatus(msg.c_str());
				}
				break;
			case 6: // Toggle Pause
				{
					static bool s_todPaused = false;
					s_todPaused = !s_todPaused;
					pTOD->SetPaused(s_todPaused);
					SetStatus(s_todPaused ? "Time of Day: PAUSED" : "Time of Day: RESUMED");
				}
				break;
			}
		}
	}
	else if (m_currentCategory == ECAT_NPCS || m_currentCategory == ECAT_PROPS || m_currentCategory == ECAT_BUILDINGS)
	{
		SpawnSandboxItem(m_currentCategory, sel);
	}
}

void CCoopAdminMenu::ChangeMap(const char* mapName)
{
	if (!mapName || !mapName[0])
		return;

	string status;
	status.Format("Switching server map to: %s...", mapName);
	SetStatus(status.c_str());
	SetOpen(false);
	m_pendingMapChange = mapName;
}

void CCoopAdminMenu::RenderLobbyOverlay()
{
	if (!gEnv->pRenderer || !IsLobbyWaiting())
		return;

	IRenderer* pR = gEnv->pRenderer;
	float scrW = (float)pR->GetWidth();
	float startX = (scrW > 800.0f) ? (scrW * 0.5f - 280.0f) : 60.0f;
	float curY = 75.0f;
	float lineHeight = 22.0f;

	float colCyan[4]   = { 0.0f, 0.95f, 1.0f, 1.0f };
	float colGold[4]   = { 1.0f, 0.85f, 0.1f, 1.0f };
	float colWhite[4]  = { 0.95f, 0.95f, 0.95f, 0.95f };
	float colGreen[4]  = { 0.25f, 1.0f, 0.35f, 1.0f };
	float colYellow[4] = { 1.0f, 0.75f, 0.15f, 1.0f };
	float colDim[4]    = { 0.65f, 0.75f, 0.85f, 0.9f };

	ILevelSystem* pLS = gEnv->pGame ? gEnv->pGame->GetIGameFramework()->GetILevelSystem() : NULL;
	ILevel* pLevel = pLS ? pLS->GetCurrentLevel() : NULL;
	const char* levelName = (pLevel && pLevel->GetLevelInfo()) ? pLevel->GetLevelInfo()->GetName() : "Unknown";

	pR->Draw2dLabel(startX, curY, 1.75f, colCyan, false, "==========================================================");
	curY += lineHeight * 1.1f;
	pR->Draw2dLabel(startX, curY, 1.8f, colGold, false,  "         CRYSIS CO-OP - CAMPAIGN SQUAD LOBBY");
	curY += lineHeight * 1.1f;
	pR->Draw2dLabel(startX, curY, 1.75f, colCyan, false, "==========================================================");
	curY += lineHeight * 1.2f;

	pR->Draw2dLabel(startX, curY, 1.35f, colWhite, false, "Mission: %s   |   Intro Cutscenes & AI: HELD UNTIL SQUAD READY", levelName);
	curY += lineHeight * 1.1f;

	if (gEnv->bServer)
	{
		std::string pubIP = CoopNet::GetPublicIP();
		std::string lanIP = CoopNet::GetLocalIP();
		if (!pubIP.empty())
			pR->Draw2dLabel(startX, curY, 1.25f, colDim, false, "Server Address: %s:64087 (Internet)   |   %s:64087 (LAN)", pubIP.c_str(), lanIP.c_str());
		else
			pR->Draw2dLabel(startX, curY, 1.25f, colDim, false, "Server Address: %s:64087 (LAN)", lanIP.c_str());
		curY += lineHeight * 1.3f;
	}
	else
	{
		curY += lineHeight * 0.4f;
	}

	string rosterStr;
	int loadingCount = 0;
	int readyCount = 0;

	if (g_pGame && g_pGame->GetSynchedStorage())
	{
		g_pGame->GetSynchedStorage()->GetGlobalValue(COOP_LOBBY_ROSTER_KEY, rosterStr);
		g_pGame->GetSynchedStorage()->GetGlobalValue(COOP_LOBBY_LOADING_KEY, loadingCount);
		g_pGame->GetSynchedStorage()->GetGlobalValue(COOP_LOBBY_READY_KEY, readyCount);
	}

	pR->Draw2dLabel(startX, curY, 1.4f, colCyan, false, "CONNECTED SQUAD MEMBERS (%d Ready, %d Loading):", readyCount, loadingCount);
	curY += lineHeight * 1.2f;

	if (!rosterStr.empty())
	{
		size_t pos = 0;
		while (pos < rosterStr.length())
		{
			size_t next = rosterStr.find('\n', pos);
			string line = (next == string::npos) ? rosterStr.substr(pos) : rosterStr.substr(pos, next - pos);
			bool isLoading = (line.find("LOADING") != string::npos);
			pR->Draw2dLabel(startX + 15.0f, curY, 1.35f, isLoading ? colYellow : colGreen, false, "%s", line.c_str());
			curY += lineHeight;
			if (next == string::npos)
				break;
			pos = next + 1;
		}
	}
	else
	{
		pR->Draw2dLabel(startX + 15.0f, curY, 1.35f, colGreen, false, "1. Host [READY]");
		curY += lineHeight;
	}

	curY += lineHeight * 0.6f;
	pR->Draw2dLabel(startX, curY, 1.5f, colCyan, false, "----------------------------------------------------------");
	curY += lineHeight * 1.2f;

	if (gEnv->bServer)
	{
		if (loadingCount > 0)
		{
			pR->Draw2dLabel(startX, curY, 1.45f, colYellow, false, ">> WAITING FOR %d PLAYER(S) TO FINISH LOADING MAP...", loadingCount);
			curY += lineHeight * 1.1f;
			pR->Draw2dLabel(startX, curY, 1.25f, colDim, false, "   (Wait until status is READY, or press [F3] to Force Start)");
		}
		else
		{
			pR->Draw2dLabel(startX, curY, 1.55f, colGreen, false, ">> ALL PLAYERS LOADED! PRESS [ENTER] OR [F3] TO START MISSION <<");
			curY += lineHeight * 1.1f;
			pR->Draw2dLabel(startX, curY, 1.25f, colDim, false, "   (Friends can join now before you press [ENTER] to start cutscenes)");
		}
	}
	else
	{
		pR->Draw2dLabel(startX, curY, 1.45f, colGreen, false, ">> YOU ARE LOADED & READY! WAITING FOR HOST TO START MISSION... <<");
	}

	if (!m_statusMessage.empty())
	{
		curY += lineHeight * 1.2f;
		pR->Draw2dLabel(startX, curY, 1.35f, colYellow, false, "STATUS: %s", m_statusMessage.c_str());
	}
}

void CCoopAdminMenu::Render()
{
	if (!gEnv->pRenderer)
		return;

	if (!m_bOpen)
	{
		RenderLobbyOverlay();
		return;
	}

	IRenderer* pR = gEnv->pRenderer;
	float scrW = (float)pR->GetWidth();
	float scrH = (float)pR->GetHeight();

	float startX = 60.0f;
	float startY = 80.0f;
	float lineHeight = 22.0f;

	float colCyan[4]   = { 0.0f, 0.95f, 1.0f, 1.0f };
	float colGold[4]   = { 1.0f, 0.85f, 0.1f, 1.0f };
	float colWhite[4]  = { 0.9f, 0.9f, 0.9f, 0.95f };
	float colDim[4]    = { 0.5f, 0.55f, 0.6f, 0.8f };
	float colGreen[4]  = { 0.3f, 1.0f, 0.3f, 1.0f };
	float colOrange[4] = { 1.0f, 0.6f, 0.1f, 1.0f };

	float curY = startY;

	pR->Draw2dLabel(startX, curY, 1.8f, colCyan, false, "==================================================");
	curY += lineHeight * 1.1f;
	pR->Draw2dLabel(startX, curY, 1.8f, colCyan, false, "        CRYSIS 1 CO-OP - IN-GAME ADMIN MENU");
	curY += lineHeight * 1.1f;
	pR->Draw2dLabel(startX, curY, 1.8f, colCyan, false, "==================================================");
	curY += lineHeight * 1.2f;

	// Navigation hint
	pR->Draw2dLabel(startX, curY, 1.2f, colDim, false, "[F2/ESC] Close   |   [UP/DOWN] Select   |   [LEFT/RIGHT] Category   |   [ENTER] Execute");
	curY += lineHeight * 1.4f;

	// Categories row
	const char* catNames[ECAT_COUNT] = { " [1. MAPS] ", " [2. CHEATS] ", " [3. TOD] ", " [4. NPCS] ", " [5. PROPS] ", " [6. BUILDINGS] " };
	string catRow = "Category: ";
	for (int c = 0; c < ECAT_COUNT; ++c)
	{
		if (c == m_currentCategory)
		{
			catRow += ">>> ";
			catRow += catNames[c];
			catRow += " <<<   ";
		}
		else
		{
			catRow += "    ";
			catRow += catNames[c];
			catRow += "       ";
		}
	}
	pR->Draw2dLabel(startX, curY, 1.35f, colGold, false, "%s", catRow.c_str());
	curY += lineHeight * 1.5f;

	// Render items
	int numItems = 0;
	if (m_currentCategory == ECAT_MAPS)
		numItems = (int)(sizeof(s_maps) / sizeof(s_maps[0]));
	else if (m_currentCategory == ECAT_CHEATS)
		numItems = (int)(sizeof(s_cheats) / sizeof(s_cheats[0]));
	else if (m_currentCategory == ECAT_TIMEOFDAY)
		numItems = (int)(sizeof(s_timeOfDay) / sizeof(s_timeOfDay[0]));
	else if (m_currentCategory == ECAT_NPCS)
		numItems = (int)(sizeof(s_npcs) / sizeof(s_npcs[0]));
	else if (m_currentCategory == ECAT_PROPS)
		numItems = (int)(sizeof(s_props) / sizeof(s_props[0]));
	else if (m_currentCategory == ECAT_BUILDINGS)
		numItems = (int)(sizeof(s_buildings) / sizeof(s_buildings[0]));

	int sel = m_selectedItem[m_currentCategory];

	for (int i = 0; i < numItems; ++i)
	{
		const char* name = "";
		const char* desc = "";
		string stateSuffix = "";

		if (m_currentCategory == ECAT_MAPS)
		{
			name = s_maps[i].name;
			desc = s_maps[i].desc;
		}
		else if (m_currentCategory == ECAT_CHEATS)
		{
			name = s_cheats[i].name;
			desc = s_cheats[i].desc;

			// Add active status to toggle cheats
			if (i == 1) // Fly mode
			{
				ICVar* pVar = gEnv->pConsole ? gEnv->pConsole->GetCVar("p_fly_mode") : NULL;
				stateSuffix = (pVar && pVar->GetIVal()) ? "  [ENABLED]" : "  [DISABLED]";
			}
			else if (i == 2) // God mode
			{
				ICVar* pVar = gEnv->pConsole ? gEnv->pConsole->GetCVar("g_godMode") : NULL;
				stateSuffix = (pVar && pVar->GetIVal()) ? "  [ENABLED]" : "  [DISABLED]";
			}
			else if (i == 3) // Unlimited ammo
			{
				ICVar* pVar = gEnv->pConsole ? gEnv->pConsole->GetCVar("i_unlimitedammo") : NULL;
				stateSuffix = (pVar && pVar->GetIVal()) ? "  [ENABLED]" : "  [DISABLED]";
			}
			else if (i == 6) // AI ignore
			{
				ICVar* pVar = gEnv->pConsole ? gEnv->pConsole->GetCVar("ai_IgnorePlayer") : NULL;
				stateSuffix = (pVar && pVar->GetIVal()) ? "  [ENABLED]" : "  [DISABLED]";
			}
		}
		else if (m_currentCategory == ECAT_TIMEOFDAY)
		{
			name = s_timeOfDay[i].name;
			desc = s_timeOfDay[i].desc;
		}
		else if (m_currentCategory == ECAT_NPCS)
		{
			name = s_npcs[i].name;
			desc = s_npcs[i].desc;
		}
		else if (m_currentCategory == ECAT_PROPS)
		{
			name = s_props[i].name;
			desc = s_props[i].desc;
		}
		else if (m_currentCategory == ECAT_BUILDINGS)
		{
			name = s_buildings[i].name;
			desc = s_buildings[i].desc;
		}

		if (i == sel)
		{
			pR->Draw2dLabel(startX, curY, 1.4f, colGold, false, "  >>  %-28s %s%s", name, desc, stateSuffix.c_str());
		}
		else
		{
			pR->Draw2dLabel(startX, curY, 1.3f, colWhite, false, "      %-28s %s%s", name, desc, stateSuffix.c_str());
		}
		curY += lineHeight;
	}

	curY += lineHeight * 0.8f;
	pR->Draw2dLabel(startX, curY, 1.2f, colCyan, false, "--------------------------------------------------");
	curY += lineHeight * 1.1f;

	if (!m_statusMessage.empty())
	{
		pR->Draw2dLabel(startX, curY, 1.35f, colOrange, false, "STATUS: %s", m_statusMessage.c_str());
	}
	else
	{
		pR->Draw2dLabel(startX, curY, 1.2f, colDim, false, "Press [ENTER] or [SPACE] to activate selected action.");
	}
}


//------------------------------------------------------------------------
// Coop: Sandbox Placement and Server Spawning
//------------------------------------------------------------------------
void CCoopAdminMenu::SpawnSandboxItem(int category, int index)
{
	IActor* pActor = gEnv->pGame ? gEnv->pGame->GetIGameFramework()->GetClientActor() : NULL;
	Vec3 eyePos = gEnv->pSystem->GetViewCamera().GetPosition();
	Vec3 viewDir = gEnv->pSystem->GetViewCamera().GetViewdir();
	if (viewDir.GetLengthSquared() < 0.01f)
		viewDir.Set(0, 1, 0);
	viewDir.Normalize();

	// Skip player entity so raycast hits ground/objects, not the player themselves
	IPhysicalEntity* pSkipEnts[10];
	int numSkip = 0;
	if (pActor && pActor->GetEntity() && pActor->GetEntity()->GetPhysics())
		pSkipEnts[numSkip++] = pActor->GetEntity()->GetPhysics();

	// Raycast forward along player view direction to find ground/wall hit point
	ray_hit hit;
	static const int rayFlags = rwi_stop_at_pierceable | rwi_colltype_any;
	Vec3 spawnPos;
	if (gEnv->pPhysicalWorld && gEnv->pPhysicalWorld->RayWorldIntersection(eyePos, viewDir * 300.0f, ent_all, rayFlags, &hit, 1, pSkipEnts, numSkip))
	{
		spawnPos = hit.pt;
	}
	else
	{
		spawnPos = eyePos + viewDir * 10.0f;
		if (gEnv->p3DEngine)
		{
			float terrZ = gEnv->p3DEngine->GetTerrainElevation(spawnPos.x, spawnPos.y);
			float waterZ = gEnv->p3DEngine->GetWaterLevel();
			spawnPos.z = max(terrZ, waterZ);
		}
	}

	Ang3 angles(0, 0, atan2f(-viewDir.x, viewDir.y));

	if (CGameRules* pGR = g_pGame ? g_pGame->GetGameRules() : NULL)
	{
		pGR->RequestSpawnItem(category, index, spawnPos, angles);
	}
	else
	{
		ServerSpawnItem(category, index, spawnPos, angles);
	}
}

void CCoopAdminMenu::ExecuteAdminCheat(int cheatId, EntityId targetId)
{
	if (!gEnv->bServer)
		return;

	IActorSystem* pActorSystem = gEnv->pGame ? gEnv->pGame->GetIGameFramework()->GetIActorSystem() : NULL;
	IActor* pTargetActor = pActorSystem ? pActorSystem->GetActor(targetId) : NULL;

	switch (cheatId)
	{
	case 2: // Toggle God Mode
		{
			m_bGodMode = !m_bGodMode;
			if (g_pGameCVars)
				g_pGameCVars->g_godMode = m_bGodMode ? 1 : 0;
			CryLogAlways("[Co-op Admin] Server executed God Mode: %s", m_bGodMode ? "ENABLED" : "DISABLED");
		}
		break;
	case 3: // Toggle Unlimited Ammo
		{
			m_bUnlimitedAmmo = !m_bUnlimitedAmmo;
			if (g_pGameCVars)
				g_pGameCVars->i_unlimitedammo = m_bUnlimitedAmmo ? 1 : 0;
			CryLogAlways("[Co-op Admin] Server executed Unlimited Ammo: %s", m_bUnlimitedAmmo ? "ENABLED" : "DISABLED");
		}
		break;
	case 4: // Give All Weapons & Items
		{
			if (pTargetActor)
			{
				IItemSystem* pItemSystem = gEnv->pGame ? gEnv->pGame->GetIGameFramework()->GetIItemSystem() : NULL;
				if (pItemSystem)
				{
					static const char* s_giveWeapons[] = {
						"SOCOM", "FY71", "SCAR", "SMG", "Shotgun", "DSG1", "GaussRifle",
						"LAW", "Minigun", "TACGun", "AlienMount",
						"Silencer", "SOCOMSilencer", "Reflex", "AssaultScope", "SniperScope",
						"LAM", "LAMFlashLight", "LAMRifleFlashLight", "GrenadeLauncher", "TacticalAttachment",
						"FragGrenade", "SmokeGrenade", "FlashbangGrenade", "EMPGrenade"
					};
					for (int i = 0; i < (int)(sizeof(s_giveWeapons)/sizeof(s_giveWeapons[0])); ++i)
					{
						pItemSystem->GiveItem(pTargetActor, s_giveWeapons[i], false, false, true);
					}
				}

				IInventory* pInv = pTargetActor->GetInventory();
				if (pInv)
				{
					static const char* s_ammoClasses[] = {
						"bullet", "fy71bullet", "tacbullet", "scargrenade",
						"explosivegrenade", "smokegrenade", "flashbang", "empgrenade",
						"shotgunshell", "sniperbullet", "gaussbullet", "rocket", "tagbullet"
					};
					for (int i = 0; i < (int)(sizeof(s_ammoClasses)/sizeof(s_ammoClasses[0])); ++i)
					{
						IEntityClass* pAmmoClass = gEnv->pEntitySystem->GetClassRegistry()->FindClass(s_ammoClasses[i]);
						if (pAmmoClass)
							pInv->SetAmmoCount(pAmmoClass, 999);
					}
				}
				CryLogAlways("[Co-op Admin] Server gave all weapons & items to '%s'", pTargetActor->GetEntity()->GetName());
			}
		}
		break;
	case 5: // Give Max Ammo
		{
			if (pTargetActor)
			{
				IInventory* pInv = pTargetActor->GetInventory();
				if (pInv)
				{
					static const char* s_ammoClasses[] = {
						"bullet", "fy71bullet", "tacbullet", "scargrenade",
						"explosivegrenade", "smokegrenade", "flashbang", "empgrenade",
						"shotgunshell", "sniperbullet", "gaussbullet", "rocket", "tagbullet"
					};
					for (int i = 0; i < (int)(sizeof(s_ammoClasses)/sizeof(s_ammoClasses[0])); ++i)
					{
						IEntityClass* pAmmoClass = gEnv->pEntitySystem->GetClassRegistry()->FindClass(s_ammoClasses[i]);
						if (pAmmoClass)
							pInv->SetAmmoCount(pAmmoClass, 999);
					}
				}
				CryLogAlways("[Co-op Admin] Server refilled ammo to maximum for '%s'", pTargetActor->GetEntity()->GetName());
			}
		}
		break;
	case 6: // Toggle AI Ignore Player
		{
			m_bAiIgnorePlayer = !m_bAiIgnorePlayer;
			ICVar* pVar = gEnv->pConsole ? gEnv->pConsole->GetCVar("ai_IgnorePlayer") : NULL;
			if (pVar)
				pVar->Set(m_bAiIgnorePlayer ? 1 : 0);
			CryLogAlways("[Co-op Admin] Server toggled AI Ignore Player: %s", m_bAiIgnorePlayer ? "ENABLED" : "DISABLED");
		}
		break;
	case 7: // Kill All Enemies
		{
			if (pActorSystem)
			{
				int killedCount = 0;
				IActorIteratorPtr it = pActorSystem->CreateActorIterator();
				while (IActor* pA = it->Next())
				{
					if (pA && !pA->IsPlayer() && pA->GetHealth() > 0)
					{
						CActor* pCA = static_cast<CActor*>(pA);
						pCA->SetHealth(0);
						pCA->Kill();
						killedCount++;
					}
				}
				CryLogAlways("[Co-op Admin] Server killed %d enemy AI entities", killedCount);
			}
		}
		break;
	}
}

void CCoopAdminMenu::ServerSpawnItem(int category, int index, const Vec3& pos, const Ang3& angles)
{
	if (!gEnv->bServer)
		return;

	const SSandboxItem* pItem = NULL;
	int count = 0;
	if (category == ECAT_NPCS)
	{
		pItem = s_npcs;
		count = (int)(sizeof(s_npcs) / sizeof(s_npcs[0]));
	}
	else if (category == ECAT_PROPS)
	{
		pItem = s_props;
		count = (int)(sizeof(s_props) / sizeof(s_props[0]));
	}
	else if (category == ECAT_BUILDINGS)
	{
		pItem = s_buildings;
		count = (int)(sizeof(s_buildings) / sizeof(s_buildings[0]));
	}

	if (!pItem || index < 0 || index >= count)
		return;

	const SSandboxItem& item = pItem[index];

	SEntitySpawnParams spawnParams;
	if (item.archetype && item.archetype[0])
	{
		spawnParams.pArchetype = gEnv->pEntitySystem->LoadEntityArchetype(item.archetype);
	}

	if (spawnParams.pArchetype)
	{
		spawnParams.pClass = spawnParams.pArchetype->GetClass();
	}
	else if (item.className && item.className[0])
	{
		spawnParams.pClass = gEnv->pEntitySystem->GetClassRegistry()->FindClass(item.className);
	}
	else
	{
		spawnParams.pClass = gEnv->pEntitySystem->GetClassRegistry()->FindClass("BasicEntity");
	}

	Vec3 finalPos = pos;
	finalPos.z += item.offsetZ;
	spawnParams.vPosition = finalPos;
	spawnParams.qRotation = Quat::CreateRotationZ(angles.z);
	spawnParams.nFlags = ENTITY_FLAG_SPAWNED | ENTITY_FLAG_NET_PRESENT | ENTITY_FLAG_CASTSHADOW;

	static int s_spawnCounter = 1;
	string name;
	name.Format("Sandbox_%s_%d", item.className ? item.className : "Entity", s_spawnCounter++);
	spawnParams.sName = name.c_str();

	IEntity* pEntity = gEnv->pEntitySystem->SpawnEntity(spawnParams);
	if (pEntity)
	{
		if (item.cgfModel && item.cgfModel[0])
		{
			const char* ext = strrchr(item.cgfModel, '.');
			if (ext && (!stricmp(ext, ".cdf") || !stricmp(ext, ".chr") || !stricmp(ext, ".cga")))
			{
				pEntity->LoadCharacter(0, item.cgfModel);
			}
			else
			{
				pEntity->LoadGeometry(0, item.cgfModel);
				SEntityPhysicalizeParams phys;
				phys.type = item.bStatic ? PE_STATIC : PE_RIGID;
				phys.mass = item.bStatic ? 0.0f : 120.0f;
				pEntity->Physicalize(phys);
			}
		}

		pEntity->Hide(false);
		pEntity->Activate(true);

		// Call OnReset on script table if available so AI and character equipment initialize
		if (IScriptTable* pScript = pEntity->GetScriptTable())
		{
			HSCRIPTFUNCTION pfnReset = 0;
			if (pScript->GetValue("OnReset", pfnReset))
			{
				Script::CallMethod(pScript, "OnReset");
				gEnv->pScriptSystem->ReleaseFunc(pfnReset);
			}
		}

		CryLogAlways("[Sandbox] Spawned '%s' (%s) at (%.1f, %.1f, %.1f)", item.name, pEntity->GetName(), finalPos.x, finalPos.y, finalPos.z);
		string msg;
		msg.Format("Spawned: %s", item.name);
		SetStatus(msg.c_str());
	}
	else
	{
		CryLogAlways("[Sandbox] Failed to spawn '%s'", item.name);
		string msg;
		msg.Format("Failed to spawn: %s", item.name);
		SetStatus(msg.c_str());
	}
}
