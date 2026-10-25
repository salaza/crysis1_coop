#include <StdAfx.h>
#include <IAISystem.h>
#include <IGame.h>
#include <IGameFramework.h>
#include <IActorSystem.h>
#include <IAgent.h>
#include <IItemSystem.h>
#include "CoopSystem.h"
#include "CoopCutsceneSystem.h"
#include "Entities/SequenceSynchronizer.h"

#include "Coop/DialogSystem/DialogSystem.h"

#include <IVehicleSystem.h>

#include "Game.h"
#include "GameRules.h"
#include "HUD/HUD.h"
#include "CoopNetPlatform.h"
#include "CoopAdminMenu.h"
#include <windows.h>

// Static CCoopSystem class instance forward declaration.
CCoopSystem CCoopSystem::s_instance = CCoopSystem();

//------------------------------------------------------------------------
// Crysis Co-op: GameSpy-free internet play
//------------------------------------------------------------------------
static int s_coopUPnP = 1;
static const int COOP_DEFAULT_PORT = 64087;

static int GetServerPort()
{
	ICVar* pPort = gEnv->pConsole->GetCVar("sv_port");
	int port = pPort ? pPort->GetIVal() : COOP_DEFAULT_PORT;
	return (port > 0 && port < 65536) ? port : COOP_DEFAULT_PORT;
}

static void CoopHudMessage(const char* msg)
{
	CryLogAlways("$3[Co-op] %s", msg);
	if (g_pGame && g_pGame->GetHUD())
		g_pGame->GetHUD()->TextMessage(msg);
}

static void PrintHostAddress()
{
	const int port = GetServerPort();
	string msg;

	switch (CoopNet::GetUPnPStatus())
	{
	case CoopNet::eS_Ok:
		msg.Format("UPnP: UDP port %d forwarded automatically", port);
		CoopHudMessage(msg.c_str());
		break;
	case CoopNet::eS_Failed:
		msg.Format("UPnP failed: %s. Forward UDP port %d to %s manually.", CoopNet::GetUPnPError().c_str(), port, CoopNet::GetLocalIP().c_str());
		CoopHudMessage(msg.c_str());
		break;
	case CoopNet::eS_Pending:
		CoopHudMessage("UPnP: configuring router...");
		break;
	default:
		break;
	}

	if (CoopNet::GetPublicIPStatus() == CoopNet::eS_Ok)
	{
		string address;
		address.Format("%s:%d", CoopNet::GetPublicIP().c_str(), port);
		bool copied = CoopNet::CopyToClipboard(address.c_str());
		msg.Format("Internet address: %s%s  (LAN: %s:%d)", address.c_str(), copied ? " - copied to clipboard" : "", CoopNet::GetLocalIP().c_str(), port);
		CoopHudMessage(msg.c_str());
	}
	else if (CoopNet::GetPublicIPStatus() == CoopNet::eS_Failed)
	{
		msg.Format("Could not detect public IP. LAN address: %s:%d", CoopNet::GetLocalIP().c_str(), port);
		CoopHudMessage(msg.c_str());
	}

	const std::string vpn = CoopNet::GetVPNName();
	if (!vpn.empty())
	{
		msg.Format("WARNING: VPN '%s' is active! Internet players cannot reach you. Disable the VPN (or exclude Crysis via split tunneling) and restart hosting.", vpn.c_str());
		CoopHudMessage(msg.c_str());
	}

	if (CoopNet::IsDoubleNAT())
	{
		msg.Format("WARNING: your router's internet address is private (%s) - double NAT or carrier-grade NAT. Ask your ISP for a public IPv4 or use ZeroTier/Radmin VPN.", CoopNet::GetRouterExternalIP().c_str());
		CoopHudMessage(msg.c_str());
	}
}

static void CmdCoopConnect(IConsoleCmdArgs* pArgs)
{
	if (pArgs->GetArgCount() < 2)
	{
		CryLogAlways("Usage: coop_connect <ip or hostname>[:port]   (default port %d)", COOP_DEFAULT_PORT);
		return;
	}

	string address = pArgs->GetArg(1);
	if (pArgs->GetArgCount() >= 3)
		address = address + ":" + pArgs->GetArg(2);

	CoopNet::SServerEntry entry;
	if (!CoopNet::ParseAndResolve(address.c_str(), COOP_DEFAULT_PORT, entry))
	{
		CryLogAlways("$4[Co-op] Could not resolve '%s'", address.c_str());
		return;
	}

	// remember it so it shows up in the in-game server list next time
	CoopNet::AddToServerList(address.c_str());

	if (ICVar* pLan = gEnv->pConsole->GetCVar("sv_lanonly"))
		pLan->Set(1); // no GameSpy CD-key/master checks

	string cmd;
	cmd.Format("connect %s:%d", entry.ip.c_str(), entry.port);
	CryLogAlways("$3[Co-op] Connecting to %s (%s:%d)...", entry.host.c_str(), entry.ip.c_str(), entry.port);
	gEnv->pConsole->ExecuteString(cmd.c_str());
}

static void CmdCoopAddServer(IConsoleCmdArgs* pArgs)
{
	if (pArgs->GetArgCount() < 2)
	{
		CryLogAlways("Usage: coop_addserver <ip or hostname>[:port]");
		return;
	}

	string address = pArgs->GetArg(1);
	if (pArgs->GetArgCount() >= 3)
		address = address + ":" + pArgs->GetArg(2);

	if (CoopNet::AddToServerList(address.c_str()))
		CryLogAlways("$3[Co-op] Added '%s' to Mods/CrysisCoop/coop_servers.txt (Multiplayer -> LAN -> Refresh)", address.c_str());
	else
		CryLogAlways("$4[Co-op] Could not write Mods/CrysisCoop/coop_servers.txt");
}

static void CmdCoopAddress(IConsoleCmdArgs* pArgs)
{
	if (gEnv->bServer && gEnv->bMultiplayer && CoopNet::GetPublicIPStatus() != CoopNet::eS_Idle)
		PrintHostAddress();
	else
		CryLogAlways("[Co-op] Local IP: %s (start hosting to forward the port and detect the public IP)", CoopNet::GetLocalIP().c_str());
}

static void CmdCoopClearMap(IConsoleCmdArgs* pArgs)
{
	if (CGameRules* pGameRules = g_pGame->GetGameRules())
	{
		pGameRules->SendMapClearDrawings();
		CryLogAlways("[Co-op] Strategic map drawings cleared.");
	}
}

static void UnlockCryActionImmersiveMP()
{
	static bool s_patched = false;
	if (s_patched)
		return;
	s_patched = true;

	HMODULE hCryAction = GetModuleHandleA("CryAction.dll");
	if (!hCryAction)
		return;

	uint8* base = (uint8*)hCryAction;
#ifdef WIN64
	// 1. CGameContext::SetImmersive(bool): RVA 0x2b395c (83 F8 03 74 08 -> 83 F8 03 EB 08)
	uint8* pSetImmersive = base + 0x2b395c;
	if (pSetImmersive[0] == 0x83 && pSetImmersive[1] == 0xF8 && pSetImmersive[2] == 0x03 && pSetImmersive[3] == 0x74 && pSetImmersive[4] == 0x08)
	{
		DWORD oldProt = 0;
		if (VirtualProtect(pSetImmersive, 8, PAGE_EXECUTE_READWRITE, &oldProt))
		{
			pSetImmersive[3] = 0xEB;
			VirtualProtect(pSetImmersive, 8, oldProt, &oldProt);
			CryLogAlways("[Co-op] Unlocked DX9 Immersive Multiplayer Physics in 64-bit CryAction.dll (SetImmersive)");
		}
	}
	// 2. Server DX10 client capability check: RVA 0x2b90fd (0F 84 C0 00 00 00 -> 90 E9 C0 00 00 00)
	uint8* pClientCheck = base + 0x2b90fd;
	if (pClientCheck[0] == 0x0F && pClientCheck[1] == 0x84 && pClientCheck[2] == 0xC0 && pClientCheck[3] == 0x00)
	{
		DWORD oldProt = 0;
		if (VirtualProtect(pClientCheck, 8, PAGE_EXECUTE_READWRITE, &oldProt))
		{
			pClientCheck[0] = 0x90;
			pClientCheck[1] = 0xE9;
			VirtualProtect(pClientCheck, 8, oldProt, &oldProt);
			CryLogAlways("[Co-op] Bypassed DX10 client capability check in 64-bit CryAction.dll");
		}
	}
#else
	// 1. CGameContext::SetImmersive(bool): RVA 0x1d699b (83 F8 03 74 06 -> 83 F8 03 EB 06)
	uint8* pSetImmersive = base + 0x1d699b;
	if (pSetImmersive[0] == 0x83 && pSetImmersive[1] == 0xF8 && pSetImmersive[2] == 0x03 && pSetImmersive[3] == 0x74 && pSetImmersive[4] == 0x06)
	{
		DWORD oldProt = 0;
		if (VirtualProtect(pSetImmersive, 8, PAGE_EXECUTE_READWRITE, &oldProt))
		{
			pSetImmersive[3] = 0xEB;
			VirtualProtect(pSetImmersive, 8, oldProt, &oldProt);
			CryLogAlways("[Co-op] Unlocked DX9 Immersive Multiplayer Physics in 32-bit CryAction.dll (SetImmersive)");
		}
	}
	// 2. Server DX10 client capability check: RVA 0x1da0fc (74 27 -> EB 27)
	uint8* pClientCheck = base + 0x1da0fc;
	if (pClientCheck[0] == 0x74 && pClientCheck[1] == 0x27)
	{
		DWORD oldProt = 0;
		if (VirtualProtect(pClientCheck, 4, PAGE_EXECUTE_READWRITE, &oldProt))
		{
			pClientCheck[0] = 0xEB;
			VirtualProtect(pClientCheck, 4, oldProt, &oldProt);
			CryLogAlways("[Co-op] Bypassed DX10 client capability check in 32-bit CryAction.dll");
		}
	}
#endif
}

static void EnforceCoopLuaPhysics()
{
	if (!gEnv || !gEnv->pScriptSystem)
		return;

	const char* luaPatch =
		"if CryAction then CryAction.IsImmersivenessEnabled = function() return 1 end end "
		"local function _CoopPatchUsable(t) "
		"  if t and t.IsUsable and not t._coopUsablePatched then "
		"    local orig = t.IsUsable "
		"    t._coopUsablePatched = true "
		"    t.IsUsable = function(self, user, idx) "
		"      local oldMP = System.IsMultiplayer "
		"      System.IsMultiplayer = function() return nil end "
		"      local ok, res = pcall(orig, self, user, idx) "
		"      System.IsMultiplayer = oldMP "
		"      if ok and res then return res end "
		"      return 0 "
		"    end "
		"  end "
		"end "
		"_CoopPatchUsable(BasicEntity) "
		"_CoopPatchUsable(InteractiveEntity) "
		"_CoopPatchUsable(ExplosiveObject) "
		"_CoopPatchUsable(PressurizedObject) "
		"_CoopPatchUsable(DestroyableObject)";
	gEnv->pScriptSystem->ExecuteBuffer(luaPatch, strlen(luaPatch));
}

static void EnforceCoopPhysicsSettings()
{
	UnlockCryActionImmersiveMP();

	if (gEnv->pGame && gEnv->pGame->GetIGameFramework())
	{
		uintptr_t pFW = (uintptr_t)gEnv->pGame->GetIGameFramework();
#ifdef WIN64
		uintptr_t pActionGame = *(uintptr_t*)(pFW + 0x50);
		if (pActionGame)
		{
			uint8* pBreakFlags = (uint8*)(pActionGame + 0x18);
			*pBreakFlags = (*pBreakFlags & ~0x08) | 0x01;
			uintptr_t pGameContext = *(uintptr_t*)(pActionGame + 0x50);
			if (pGameContext)
			{
				uint32* pCtxFlags = (uint32*)(pGameContext + 0x140);
				*pCtxFlags |= 0x1000; // eGSF_ImmersiveMultiplayer
			}
		}
#else
		uintptr_t pActionGame = *(uintptr_t*)(pFW + 0x28);
		if (pActionGame)
		{
			uint8* pBreakFlags = (uint8*)(pActionGame + 0x0C);
			*pBreakFlags = (*pBreakFlags & ~0x08) | 0x01;
			uintptr_t pGameContext = *(uintptr_t*)(pActionGame + 0x28);
			if (pGameContext)
			{
				uint32* pCtxFlags = (uint32*)(pGameContext + 0xA8);
				*pCtxFlags |= 0x1000; // eGSF_ImmersiveMultiplayer
			}
		}
#endif
	}

	if (gEnv->pPhysicalWorld)
	{
		if (PhysicsVars *pPV = gEnv->pPhysicalWorld->GetPhysVars())
		{
			pPV->bMultiplayer = 0;
			pPV->bPlayersCanBreak = 1;
			pPV->breakImpulseScale = 1.0f;
		}
	}

	if (ICVar* pCVar = gEnv->pConsole->GetCVar("p_players_can_break")) pCVar->Set(1);
	if (ICVar* pCVar = gEnv->pConsole->GetCVar("g_procedural_breaking")) pCVar->Set(1);
	if (ICVar* pCVar = gEnv->pConsole->GetCVar("g_joint_breaking")) pCVar->Set(1);
	if (ICVar* pCVar = gEnv->pConsole->GetCVar("g_tree_cut_reuse_dist")) pCVar->Set(0.0f);
	if (ICVar* pCVar = gEnv->pConsole->GetCVar("g_breakImpulseScale")) pCVar->Set(1.0f);
	if (ICVar* pCVar = gEnv->pConsole->GetCVar("e_deformable_objects")) pCVar->Set(1);
	if (ICVar* pCVar = gEnv->pConsole->GetCVar("e_joint_strength_scale")) pCVar->Set(1.0f);
	if (ICVar* pCVar = gEnv->pConsole->GetCVar("e_terrain_deformations")) pCVar->Set(1);
	if (ICVar* pCVar = gEnv->pConsole->GetCVar("e_cull_veg_activation")) pCVar->Set(50);
	if (ICVar* pCVar = gEnv->pConsole->GetCVar("e_phys_foliage")) pCVar->Set(2);
	if (ICVar* pCVar = gEnv->pConsole->GetCVar("e_vegetation_bending")) pCVar->Set(2);
	if (ICVar* pCVar = gEnv->pConsole->GetCVar("es_MaxPhysDist")) pCVar->Set(200.0f);
	if (ICVar* pCVar = gEnv->pConsole->GetCVar("es_MaxPhysDistInvisible")) pCVar->Set(25.0f);
	if (ICVar* pCVar = gEnv->pConsole->GetCVar("e_foliage_wind_activation_dist")) pCVar->Set(25.0f);
	if (ICVar* pCVar = gEnv->pConsole->GetCVar("g_breakage_particles_limit")) pCVar->Set(250);
	if (ICVar* pCVar = gEnv->pConsole->GetCVar("ca_aimIKFadeout")) pCVar->Set(1);
}
//~Crysis Co-op

CCoopSystem::CCoopSystem() :
	m_nInitialized(0),
	m_pReadability(NULL)
{
}

CCoopSystem::~CCoopSystem()
{
}

// Summary:
//	Initializes the CCoopSystem instance.
bool CCoopSystem::Initialize()
{
	gEnv->pGame->GetIGameFramework()->GetILevelSystem()->AddListener(this);
	if (gEnv->pEntitySystem)
		gEnv->pEntitySystem->AddSink(this);

	m_pReadability = new CCoopReadability();
	
	CCoopCutsceneSystem::GetInstance()->Register();

	IScriptSystem *pSS = gEnv->pScriptSystem;
	if (pSS->ExecuteFile("Scripts/Coop/AI.lua", true, true))
	{
		pSS->BeginCall("Init");
		pSS->EndCall();
	}

	ICVar* pAIUpdateAlways = gEnv->pConsole->GetCVar("ai_UpdateAllAlways");
	ICVar* pCheatCvar = gEnv->pConsole->GetCVar("sv_cheatprotection");
	ICVar* pGameRules = gEnv->pConsole->GetCVar("sv_gamerules");
	ICVar* pObjQuality = gEnv->pConsole->GetCVar("e_obj_quality");

	if (pAIUpdateAlways)
		pAIUpdateAlways->ForceSet("1");

	if (pCheatCvar)
		pCheatCvar->ForceSet("0");

	if (pGameRules)
		pGameRules->ForceSet("coop");

	if (pObjQuality)
		pObjQuality->ForceSet("4");

	// Enable full SinglePlayer physics simulation, destruction, tree cutting and foliage bending in Co-op
	EnforceCoopPhysicsSettings();
	EnforceCoopLuaPhysics();

	// Crysis Co-op: GameSpy is offline - use direct connections only
	if (ICVar* pLan = gEnv->pConsole->GetCVar("sv_lanonly"))
		pLan->Set(1);

	gEnv->pConsole->Register("coop_upnp", &s_coopUPnP, 1, 0, "Automatically forward the server UDP port on the router via UPnP when hosting (0/1)");
	gEnv->pConsole->AddCommand("coop_connect", CmdCoopConnect, 0, "Connect to a co-op server over the internet: coop_connect <ip or hostname>[:port]");
	gEnv->pConsole->AddCommand("coop_addserver", CmdCoopAddServer, 0, "Add a server to the in-game list (Multiplayer -> LAN): coop_addserver <ip or hostname>[:port]");
	gEnv->pConsole->AddCommand("coop_address", CmdCoopAddress, 0, "Show the address your friends need to join your co-op server");
	gEnv->pConsole->AddCommand("coop_clearmap", CmdCoopClearMap, 0, "Clear all drawings on the strategic map for all co-op players");

	m_pDialogSystem = new CDialogSystem();
	m_pDialogSystem->Init();

	CCoopAdminMenu::GetInstance().Init();

	return true;
}

void CCoopSystem::CompleteInit()
{
	gEnv->pSystem->SetIDialogSystem(m_pDialogSystem);
}

// Summary:
//	Shuts down the CCoopSystem instance.
void CCoopSystem::Shutdown()
{
	CCoopAdminMenu::GetInstance().Shutdown();

	IScriptSystem *pSS = gEnv->pScriptSystem;
	if (pSS->ExecuteFile("Scripts/Coop/AI.lua", true, true))
	{
		pSS->BeginCall("Shutdown");
		pSS->EndCall();
	}

	CCoopCutsceneSystem::GetInstance()->Unregister();

	CoopNet::StopHostServices();
	gEnv->pConsole->RemoveCommand("coop_connect");
	gEnv->pConsole->RemoveCommand("coop_addserver");
	gEnv->pConsole->RemoveCommand("coop_address");
	gEnv->pConsole->UnregisterVariable("coop_upnp", true);

	if (gEnv && gEnv->pEntitySystem)
		gEnv->pEntitySystem->RemoveSink(this);

	gEnv->pGame->GetIGameFramework()->GetILevelSystem()->RemoveListener(this);
	SAFE_DELETE(m_pReadability);

	if (m_pDialogSystem)
		m_pDialogSystem->Shutdown();

	SAFE_DELETE(m_pDialogSystem);
}

bool bReinited = false;


// Summary:
//	Updates the CCoopSystem instance.
void CCoopSystem::Update(float fFrameTime)
{
	EnforceCoopPhysicsSettings();

	CCoopAdminMenu::GetInstance().Update(fFrameTime);

	// Ensure server AI ticks animations, dialogues and goal pipes (paused while waiting in pre-game lobby)
	if (gEnv->bServer && gEnv->pAISystem && !CCoopAdminMenu::GetInstance().IsLobbyWaiting())
	{
		bool savedMP = gEnv->bMultiplayer;
		gEnv->bMultiplayer = false;
		gEnv->pAISystem->Update(gEnv->pTimer ? gEnv->pTimer->GetFrameStartTime() : CTimeValue(), fFrameTime);
		gEnv->bMultiplayer = savedMP;
	}

	if (m_pDialogSystem)
		m_pDialogSystem->Update(fFrameTime);

	if (CoopNet::PollHostServicesFinished())
		PrintHostAddress();

	// Registers vehicles into the AI system
	if (gEnv->bServer)
	{
		IVehicleIteratorPtr iter = gEnv->pGame->GetIGameFramework()->GetIVehicleSystem()->CreateVehicleIterator();
		while (IVehicle* pVehicle = iter->Next())
		{
			if(IEntity *pEntity = pVehicle->GetEntity())
			{
				if (!pEntity->GetAI())
				{
					gEnv->bMultiplayer = false;
										
					HSCRIPTFUNCTION scriptFunction(0);
					IScriptSystem*	pIScriptSystem = gEnv->pScriptSystem;
					if (IScriptTable* pScriptTable = pEntity->GetScriptTable())
					{
						if (pScriptTable->GetValue("ForceCoopAI", scriptFunction))
						{
							Script::Call(pIScriptSystem, scriptFunction, pScriptTable, false);
							pIScriptSystem->ReleaseFunc(scriptFunction);
						}
					}


					gEnv->bMultiplayer = true;
				}
			}
		}
	}
	CCoopCutsceneSystem::GetInstance()->Update(fFrameTime);
}

void CCoopSystem::OnLoadingStart(ILevelInfo *pLevel)
{
	if (gEnv->bEditor) return;

	// Ensure both Host and Client spawn all MinSpec entities from mission_mission0.xml
	// regardless of graphics settings so static network entity bindings match.
	if (ICVar* pObjQuality = gEnv->pConsole->GetCVar("e_obj_quality"))
		pObjQuality->ForceSet("4");

	// Ensure physics destruction and Lua immersive mode are fully enabled on both Host and Client before entities load
	EnforceCoopPhysicsSettings();
	EnforceCoopLuaPhysics();

	CCoopAdminMenu::GetInstance().OnLoadingStart();

	if (!gEnv->bServer) return;

	m_nInitialized = 0;
	CryLogAlways("[CCoopSystem] Initializing AI System...");

	gEnv->bMultiplayer = false;
	if (!gEnv->pAISystem->Init())
		CryLogAlways("[CCoopSystem] AI System Initialization Failed");

	gEnv->pAISystem->FlushSystem();
	gEnv->pAISystem->Enable();

	gEnv->pAISystem->LoadNavigationData(pLevel->GetPath(), "mission0");
	gEnv->bMultiplayer = true;

	ICVar* pSystemUpdate = gEnv->pConsole->GetCVar("ai_systemupdate");
	if (gEnv->bServer)
		pSystemUpdate->Set(1);
	else
		pSystemUpdate->Set(0);
}

bool CCoopSystem::OnBeforeSpawn(SEntitySpawnParams &params)
{
	if (ICVar* pObjQuality = gEnv->pConsole->GetCVar("e_obj_quality"))
	{
		if (pObjQuality->GetIVal() != 4)
			pObjQuality->ForceSet("4");
	}

	if (params.id == 0)
	{
		// Dynamically spawned entities (not from mission_mission0.xml) must never be
		// bound as static network objects (BeginBindStaticObject) during level load.
		params.nFlags |= ENTITY_FLAG_NEVER_NETWORK_STATIC;

		const char* className = params.pClass ? params.pClass->GetName() : "";
		const char* entName = params.sName ? params.sName : "";

		// Helper entities spawned by Lua OnInit/OnReset on both Server and Client during LoadEntities
		if (strcmp(className, "Shark") == 0 || strstr(entName, "_switch") != NULL)
		{
			params.nFlags |= ENTITY_FLAG_CLIENT_ONLY;
		}
		else if (strcmp(className, "AIAnchor") == 0 || strcmp(className, "Dummy") == 0)
		{
			params.nFlags |= ENTITY_FLAG_SERVER_ONLY;
		}

		// Allocate non-networked local entities from the static tail of CSaltBufferArray
		// so they never collide with dynamic EntityIds allocated by the Server.
		if (params.nFlags & (ENTITY_FLAG_CLIENT_ONLY | ENTITY_FLAG_SERVER_ONLY))
		{
			params.bStaticEntityId = true;
		}
	}

	return true;
}

void CCoopSystem::OnLoadingComplete(ILevel *pLevel)
{
	m_pDialogSystem->Reset();
	
	if (CDialogSystem::sAutoReloadScripts != 0)
		m_pDialogSystem->ReloadScripts();

	if (gEnv->bEditor) return;

	// Crysis Co-op: internet hosting without GameSpy
	if (gEnv->bServer && gEnv->bMultiplayer)
	{
		CoopNet::StartHostServices(GetServerPort(), s_coopUPnP != 0);

		// Pre-spawn and bind the SequenceSynchronizer entity so cutscenes synchronize to clients
		GetSequenceSynchronizer();
	}

	int nInitialized = 0;
	gEnv->bMultiplayer = false;
	m_nInitialized = 1;
	
	gEnv->pAISystem->Reset(IAISystem::RESET_ENTER_GAME);
	gEnv->bMultiplayer = true;

	EnforceCoopPhysicsSettings();
	EnforceCoopLuaPhysics();

	CCoopAdminMenu::GetInstance().OnLoadingComplete();
}
