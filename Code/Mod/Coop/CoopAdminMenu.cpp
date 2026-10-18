#include "StdAfx.h"
#include "CoopAdminMenu.h"
#include <IConsole.h>
#include <IRenderer.h>
#include <I3DEngine.h>
#include <IGameFramework.h>
#include <ILevelSystem.h>
#include "Game.h"
#include "GameRules.h"

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

CCoopAdminMenu& CCoopAdminMenu::GetInstance()
{
	static CCoopAdminMenu s_instance;
	return s_instance;
}

CCoopAdminMenu::CCoopAdminMenu()
	: m_bOpen(false)
	, m_currentCategory(0)
	, m_statusTimer(0.0f)
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
	}
}

void CCoopAdminMenu::Shutdown()
{
	if (gEnv->pConsole)
	{
		gEnv->pConsole->RemoveCommand("admin_menu");
	}
}

void CCoopAdminMenu::CmdToggleAdminMenu(IConsoleCmdArgs* pArgs)
{
	CCoopAdminMenu::GetInstance().Toggle();
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
}

bool CCoopAdminMenu::OnInputEvent(const SInputEvent &event)
{
	if (event.deviceId != eDI_Keyboard)
		return false;

	if (event.state == eIS_Pressed)
	{
		if (event.keyId == eKI_F2)
		{
			Toggle();
			return true;
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
		else if (event.keyId == eKI_Enter || event.keyId == eKI_NP_Enter || event.keyId == eKI_Space)
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
				ICVar* pVar = gEnv->pConsole ? gEnv->pConsole->GetCVar("g_godMode") : NULL;
				if (pVar)
				{
					int newVal = (pVar->GetIVal() == 0) ? 1 : 0;
					pVar->Set(newVal);
					SetStatus(newVal ? "God Mode: ENABLED" : "God Mode: DISABLED");
				}
			}
			break;
		case 3: // Toggle Unlimited Ammo
			{
				ICVar* pVar = gEnv->pConsole ? gEnv->pConsole->GetCVar("i_unlimitedammo") : NULL;
				if (pVar)
				{
					int newVal = (pVar->GetIVal() == 0) ? 1 : 0;
					pVar->Set(newVal);
					SetStatus(newVal ? "Unlimited Ammo: ENABLED" : "Unlimited Ammo: DISABLED");
				}
			}
			break;
		case 4: // Give All Weapons
			{
				if (gEnv->pConsole)
					gEnv->pConsole->ExecuteString("i_giveallitems");
				SetStatus("All Weapons & Items Given!");
			}
			break;
		case 5: // Give Max Ammo
			{
				if (gEnv->pConsole)
					gEnv->pConsole->ExecuteString("i_giveammo 1");
				SetStatus("Ammunition Refilled to Maximum!");
			}
			break;
		case 6: // Toggle AI Ignore Player
			{
				ICVar* pVar = gEnv->pConsole ? gEnv->pConsole->GetCVar("ai_IgnorePlayer") : NULL;
				if (pVar)
				{
					int newVal = (pVar->GetIVal() == 0) ? 1 : 0;
					pVar->Set(newVal);
					SetStatus(newVal ? "AI Ignore Players: ENABLED" : "AI Ignore Players: DISABLED");
				}
			}
			break;
		case 7: // Kill All Enemies
			{
				if (gEnv->pConsole)
					gEnv->pConsole->ExecuteString("ai_killall");
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
}

void CCoopAdminMenu::ChangeMap(const char* mapName)
{
	if (!mapName || !mapName[0])
		return;

	string status;
	status.Format("Switching server map to: %s...", mapName);
	SetStatus(status.c_str());

	ILevelSystem* pLevelSystem = gEnv->pGame ? gEnv->pGame->GetIGameFramework()->GetILevelSystem() : NULL;
	if (pLevelSystem)
	{
		ILevelRotation* pRotation = pLevelSystem->GetLevelRotation();
		if (pRotation)
		{
			pRotation->Reset();
			pRotation->AddLevel(mapName, "Coop");
			pRotation->First();
			pRotation->ChangeLevel();
			return;
		}
	}

	// Fallback only if level rotation fails
	if (gEnv->pConsole)
	{
		string cmd;
		cmd.Format("map %s s", mapName);
		gEnv->pConsole->ExecuteString(cmd.c_str());
	}
}

void CCoopAdminMenu::Render()
{
	if (!m_bOpen || !gEnv->pRenderer)
		return;

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
	const char* catNames[ECAT_COUNT] = { " [1. MAPS] ", " [2. CHEATS & DEV] ", " [3. TIME OF DAY] " };
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
