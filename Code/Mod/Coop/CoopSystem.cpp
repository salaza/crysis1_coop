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
	// 1. CGameContext::SetImmersive(bool): RVA 0x2b395e (83 F8 03 74 08 -> 83 F8 03 EB 08)
	uint8* pSetImmersive = base + 0x2b395e;
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
	// 3. Client DX10 'X' capability string flag: RVA 0x2b6871 (83 F8 03 0F 85 DD 00 00 00 -> NOP 6 bytes at +3)
	uint8* pClientCapX = base + 0x2b6871;
	if (pClientCapX[0] == 0x83 && pClientCapX[1] == 0xF8 && pClientCapX[2] == 0x03 && pClientCapX[3] == 0x0F && pClientCapX[4] == 0x85)
	{
		DWORD oldProt = 0;
		if (VirtualProtect(pClientCapX, 12, PAGE_EXECUTE_READWRITE, &oldProt))
		{
			memset(pClientCapX + 3, 0x90, 6);
			VirtualProtect(pClientCapX, 12, oldProt, &oldProt);
		}
	}
	// 4. CGameContext::ChangeContext SupportsGameType / DevMode check: RVA 0x2b9860 (85 C0 0F 8E 8F 00 00 00 -> 85 C0 90 E9 8F 00 00 00)
	uint8* pSupportsGameType = base + 0x2b9860;
	if (pSupportsGameType[0] == 0x85 && pSupportsGameType[1] == 0xC0 && pSupportsGameType[2] == 0x0F && pSupportsGameType[3] == 0x8E)
	{
		DWORD oldProt = 0;
		if (VirtualProtect(pSupportsGameType, 8, PAGE_EXECUTE_READWRITE, &oldProt))
		{
			pSupportsGameType[2] = 0x90;
			pSupportsGameType[3] = 0xE9;
			VirtualProtect(pSupportsGameType, 8, oldProt, &oldProt);
		}
	}
#else
	// 1. CGameContext::SetImmersive(bool): RVA 0x1d6999 (83 F8 03 74 06 -> 83 F8 03 EB 06)
	uint8* pSetImmersive = base + 0x1d6999;
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
	// 3. Client DX10 'X' capability string flag: RVA 0x1d8a0c (83 F8 03 75 19 -> NOP 2 bytes at +3)
	uint8* pClientCapX = base + 0x1d8a0c;
	if (pClientCapX[0] == 0x83 && pClientCapX[1] == 0xF8 && pClientCapX[2] == 0x03 && pClientCapX[3] == 0x75 && pClientCapX[4] == 0x19)
	{
		DWORD oldProt = 0;
		if (VirtualProtect(pClientCapX, 8, PAGE_EXECUTE_READWRITE, &oldProt))
		{
			pClientCapX[3] = 0x90;
			pClientCapX[4] = 0x90;
			VirtualProtect(pClientCapX, 8, oldProt, &oldProt);
		}
	}
	// 4. CGameContext::ChangeContext SupportsGameType / DevMode check: RVA 0x1da351 (85 C0 7E 75 -> 85 C0 EB 75)
	uint8* pSupportsGameType = base + 0x1da351;
	if (pSupportsGameType[0] == 0x85 && pSupportsGameType[1] == 0xC0 && pSupportsGameType[2] == 0x7E && pSupportsGameType[3] == 0x75)
	{
		DWORD oldProt = 0;
		if (VirtualProtect(pSupportsGameType, 8, PAGE_EXECUTE_READWRITE, &oldProt))
		{
			pSupportsGameType[2] = 0xEB;
			VirtualProtect(pSupportsGameType, 8, oldProt, &oldProt);
		}
	}
#endif
}

static void EnforceCoopMaxGraphicsSettings()
{
	if (!gEnv || !gEnv->pConsole || (gEnv->pSystem && gEnv->pSystem->IsDedicated()))
		return;

	static bool s_appliedOnce = false;

	auto SetIntCVar = [](const char* name, int val) {
		if (ICVar* pVar = gEnv->pConsole->GetCVar(name))
			pVar->ForceSet( string().Format("%d", val).c_str() );
	};
	auto SetFloatCVar = [](const char* name, float val) {
		if (ICVar* pVar = gEnv->pConsole->GetCVar(name))
			pVar->ForceSet( string().Format("%g", val).c_str() );
	};

	if (!s_appliedOnce)
	{
		s_appliedOnce = true;

		// 1. Force Very High System Specs (4) & Shader Quality (3)
		SetIntCVar("sys_spec_ObjectDetail", 4);
		SetIntCVar("sys_spec_Shading", 4);
		SetIntCVar("sys_spec_VolumetricEffects", 4);
		SetIntCVar("sys_spec_Shadows", 4);
		SetIntCVar("sys_spec_Texture", 4);
		SetIntCVar("sys_spec_Physics", 4);
		SetIntCVar("sys_spec_PostProcessing", 4);
		SetIntCVar("sys_spec_Particles", 4);
		SetIntCVar("sys_spec_Sound", 4);
		SetIntCVar("sys_spec_Water", 4);
		SetIntCVar("sys_spec_GameEffects", 4);
		SetIntCVar("sys_spec_Full", 4);
		SetIntCVar("sys_spec", 4);

		SetIntCVar("q_ShaderGeneral", 3);
		SetIntCVar("q_ShaderMetal", 3);
		SetIntCVar("q_ShaderGlass", 3);
		SetIntCVar("q_ShaderVegetation", 3);
		SetIntCVar("q_ShaderIce", 3);
		SetIntCVar("q_ShaderTerrain", 3);
		SetIntCVar("q_ShaderShadow", 3);
		SetIntCVar("q_ShaderFX", 3);
		SetIntCVar("q_ShaderPostProcess", 3);
		SetIntCVar("q_ShaderHDR", 3);
		SetIntCVar("q_ShaderSky", 3);
		SetIntCVar("q_ShaderWater", 2); // 2 is max compatible in DX9 without black-ocean shader bug
		SetIntCVar("q_Renderer", 3);

		CryLogAlways("[Co-op] Enabled Maximum & Experimental CryEngine 2 Graphics Settings");
	}

	// 2. Experimental & Extreme CryEngine 2 Renderer / 3DEngine Features
	SetIntCVar("e_obj_quality", 4);
	SetIntCVar("r_UsePOM", 1);                      // Parallax Occlusion Mapping
	SetIntCVar("r_SunShafts", 2);                   // High quality volumetric sun rays / godrays
	SetIntCVar("r_SSAO", 1);                        // Screen-Space Ambient Occlusion
	SetIntCVar("r_SSAO_quality", 3);                // Maximum SSAO quality
	SetFloatCVar("r_SSAO_radius", 2.0f);
	SetIntCVar("r_SSAO_blur", 4);
	SetIntCVar("r_UseEdgeAA", 2);                   // Maximum Foliage & Edge Anti-Aliasing
	SetIntCVar("r_HDRRendering", 2);                // Full HDR rendering pipeline
	SetIntCVar("r_PostProcessEffects", 1);
	SetIntCVar("r_ColorGrading", 1);                // Experimental CryEngine 2 color grading
	SetIntCVar("r_DepthOfField", 2);                // Bokeh / cinematic depth of field
	SetIntCVar("r_MotionBlur", 1);                  // Camera & object motion blur
	SetIntCVar("r_UseSoftParticles", 1);            // Soft depth-blended particles
	SetIntCVar("r_Beams", 3);                       // Full volumetric light beams
	SetFloatCVar("r_BeamsDistFactor", 0.05f);
	SetIntCVar("r_BeamsMaxSlices", 200);
	SetIntCVar("r_Flares", 1);
	SetIntCVar("r_Glow", 1);
	SetIntCVar("r_Coronas", 1);
	SetIntCVar("r_Reflections", 1);
	SetIntCVar("r_ReflectionsQuality", 3);
	SetIntCVar("r_Refraction", 1);

	// 3. Textures, Anisotropy & Environment Probes
	SetIntCVar("r_TexMaxAnisotropy", 16);           // 16x Anisotropic Filtering
	SetIntCVar("r_TexturesStreaming", 0);           // Disable streaming pop-in: keep full-res textures in VRAM
	SetIntCVar("r_DetailTextures", 1);
	SetIntCVar("r_DetailNumLayers", 2);
	SetFloatCVar("r_DetailDistance", 20.0f);
	SetIntCVar("r_EnvCMResolution", 2);
	SetIntCVar("r_EnvTexResolution", 3);
	SetFloatCVar("r_EnvTexUpdateInterval", 0.01f);

	// 4. Water & Experimental FFT Ocean Simulation
	SetIntCVar("e_water_ocean_fft", 1);             // Experimental FFT ocean wave simulation
	SetIntCVar("e_water_ocean_soft_particles", 1);
	SetIntCVar("e_water_tesselation_amount", 10);   // Maximum ocean mesh tessellation
	SetIntCVar("e_water_tesselation_swath_width", 12);
	SetIntCVar("r_WaterReflections", 1);
	SetIntCVar("r_WaterReflectionsQuality", 3);
	SetFloatCVar("r_WaterUpdateFactor", 0.005f);
	SetIntCVar("r_WaterCaustics", 1);
	SetIntCVar("r_WaterGodRays", 1);

	// 5. High-Resolution Cascaded Shadows & Experimental Terrain AO / RAM Maps
	SetIntCVar("e_shadows", 1);
	SetIntCVar("e_shadows_max_texture_size", 2048); // High-res 2048x2048 shadow map cascades
	SetFloatCVar("e_shadows_cast_view_dist_ratio", 1.0f);
	SetIntCVar("e_shadows_from_terrain_in_all_dirs", 0); // Balanced directional sun shadows
	SetIntCVar("e_gsm_lods_num", 5);
	SetFloatCVar("e_gsm_range", 3.0f);
	SetFloatCVar("e_gsm_range_step", 3.0f);
	SetFloatCVar("r_ShadowJittering", 2.5f);
	SetIntCVar("r_ShadowBlur", 3);
	SetIntCVar("e_ram_maps", 0);                    // Disabled to prevent CPU bottleneck
	SetIntCVar("e_terrain_ao", 1);                  // Terrain Ambient Occlusion
	SetIntCVar("e_terrain_normal_map", 1);
	SetFloatCVar("e_sky_update_rate", 1.0f);

	// 6. Balanced High-End Draw Distances & LODs (60+ FPS on RTX 3080 Ti)
	SetFloatCVar("e_view_dist_ratio", 60.0f);
	SetFloatCVar("e_view_dist_ratio_detail", 40.0f);
	SetFloatCVar("e_view_dist_ratio_vegetation", 50.0f);
	SetFloatCVar("e_lod_ratio", 6.0f);              // Reduced LOD ratio to eliminate CPU bottleneck
	SetIntCVar("e_lod_min", 0);
	SetFloatCVar("e_vegetation_sprites_distance_ratio", 1.5f);
	SetFloatCVar("e_vegetation_min_size", 0.5f);     // Cull distant sub-pixel foliage
	SetIntCVar("e_particles_quality", 4);
	SetIntCVar("e_particles_max_emitter_draw_screen", 64);
	SetIntCVar("e_particles_object_collisions", 1);
	SetIntCVar("e_decals_allow_game_decals", 1);
	SetFloatCVar("e_decals_life_time_scale", 4.0f);
	SetFloatCVar("ca_AttachmentCullingRation", 120.0f);
	SetIntCVar("e_water_tesselation_amount", 7);
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
	EnforceCoopMaxGraphicsSettings();
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
	EnforceCoopMaxGraphicsSettings();
	EnforceCoopPhysicsSettings();
	EnforceCoopLuaPhysics();

	CCoopAdminMenu::GetInstance().OnLoadingStart();

	if (!gEnv->bServer) return;

	m_nInitialized = 0;
	CryLogAlways("[CCoopSystem] Initializing AI System...");

	bool savedMP = gEnv->bMultiplayer;
	gEnv->bMultiplayer = false;
	if (!gEnv->pAISystem->Init())
		CryLogAlways("[CCoopSystem] AI System Initialization Failed");

	gEnv->pAISystem->FlushSystem();
	gEnv->pAISystem->Enable();

	gEnv->pAISystem->LoadNavigationData(pLevel->GetPath(), "mission0");
	gEnv->bMultiplayer = savedMP;

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

	// If the user started a map via 'map <name>' in console (SinglePlayer without 's x'),
	// automatically relaunch it as an Immersive Multiplayer Co-op Server so clients can connect!
	if (gEnv->bServer && (!gEnv->bMultiplayer || !gEnv->pGame->GetIGameFramework()->GetServerNetNub()))
	{
		if (pLevel && pLevel->GetLevelInfo())
		{
			if (ICVar* pRules = gEnv->pConsole->GetCVar("sv_gamerules"))
				pRules->ForceSet("Coop");
			string cmd;
			cmd.Format("map %s s x", pLevel->GetLevelInfo()->GetName());
			CryLogAlways("[Co-op] SinglePlayer map launch detected - relaunching as Co-op Multiplayer Server: %s", cmd.c_str());
			gEnv->pGame->GetIGameFramework()->ExecuteCommandNextFrame(cmd.c_str());
			return;
		}
	}

	// Crysis Co-op: internet hosting without GameSpy
	if (gEnv->bServer && gEnv->bMultiplayer)
	{
		CoopNet::StartHostServices(GetServerPort(), s_coopUPnP != 0);

		// Pre-spawn and bind the SequenceSynchronizer entity so cutscenes synchronize to clients
		GetSequenceSynchronizer();
	}

	int nInitialized = 0;
	bool savedMP = gEnv->bMultiplayer;
	gEnv->bMultiplayer = false;
	m_nInitialized = 1;
	
	gEnv->pAISystem->Reset(IAISystem::RESET_ENTER_GAME);
	gEnv->bMultiplayer = savedMP;

	EnforceCoopMaxGraphicsSettings();
	EnforceCoopPhysicsSettings();
	EnforceCoopLuaPhysics();

	CCoopAdminMenu::GetInstance().OnLoadingComplete();
}
