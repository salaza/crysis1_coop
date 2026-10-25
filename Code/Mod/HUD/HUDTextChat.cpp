/*************************************************************************
Crytek Source File.
Copyright (C), Crytek Studios, 2001-2006.
-------------------------------------------------------------------------
$Id$
$DateTime$
Description: HUD text chat - receives text input and broadcasts it to other players

-------------------------------------------------------------------------
History:
- 07:03:2006: Created by Jan Muller

*************************************************************************/
#include "StdAfx.h"
#include "HUDTextChat.h"
#include "IGameRulesSystem.h"
#include "IActorSystem.h"
#include "IUIDraw.h"
#include "HUD.h"
#include "GameActions.h"
#include "GameRules.h"
#include "Voting.h"

#include "GameFlashAnimation.h"
#include "GameFlashLogic.h"

CHUDTextChat::CHUDTextChat() : m_flashChat(NULL), m_isListening(false), m_repeatTimer(0.0f), m_chatHead(0), m_cursor(0),
		m_anyCurrentText(false), m_teamChat(false), m_showVirtualKeyboard(false), m_textInputActive(false), m_lastFrameUpdated(-1)
{
}

CHUDTextChat::~CHUDTextChat()
{
	if(m_isListening)
		gEnv->pInput->SetExclusiveListener(NULL);
}

void CHUDTextChat::Init(CGameFlashAnimation *pFlashChat)
{
	m_flashChat = pFlashChat;
	if(m_flashChat && m_flashChat->GetFlashPlayer())
		m_flashChat->GetFlashPlayer()->SetFSCommandHandler(this);

	m_opFuncMap["/vote"] = &CHUDTextChat::Vote;
	m_opFuncMap["/yes"] = &CHUDTextChat::VoteYes;
	m_opFuncMap["/no"] = &CHUDTextChat::VoteNo;
	m_opFuncMap["/lowtec"] = &CHUDTextChat::Lowtec;
	m_opFuncMap["/quarantine"] = &CHUDTextChat::Quarantine;
}

void CHUDTextChat::Update(float fDeltaTime)
{
	int curFrame = gEnv->pRenderer ? gEnv->pRenderer->GetFrameID() : 0;
	if (m_lastFrameUpdated == curFrame)
		return;
	m_lastFrameUpdated = curFrame;

	float now = gEnv->pTimer ? gEnv->pTimer->GetAsyncTime().GetSeconds() : 0.0f;

	if (m_isListening)
	{
		float nowMs = gEnv->pTimer ? gEnv->pTimer->GetAsyncTime().GetMilliSeconds() : 0.0f;

		if (m_repeatEvent.keyId != eKI_Unknown)
		{
			float repeatSpeed = 40.0f;
			float nextTimer = (1000.0f / repeatSpeed);

			if (nowMs - m_repeatTimer > nextTimer)
			{
				ProcessInput(m_repeatEvent);
				m_repeatTimer = nowMs + nextTimer;
			}
		}

		if (m_flashChat && stricmp(m_lastInputText.c_str(), m_inputText.c_str()))
		{
			m_flashChat->Invoke("setInputText", m_inputText.c_str());
			m_lastInputText = m_inputText;
		}
	}

	// Only render 2D label fallback if Flash HUD chat animation is not loaded
	if (!m_flashChat && gEnv->pRenderer)
	{
		float y = 280.0f;
		for (size_t i = 0; i < m_history.size(); ++i)
		{
			const SChatEntry& entry = m_history[i];
			float age = now - entry.timestamp;
			if (age < 12.0f)
			{
				float alpha = 1.0f;
				if (age > 8.0f)
					alpha = 1.0f - (age - 8.0f) / 4.0f;

				float col[4] = { entry.isTeam ? 0.35f : 0.45f, entry.isTeam ? 0.95f : 0.85f, 1.0f, alpha };
				gEnv->pRenderer->Draw2dLabel(25.0f, y, 1.35f, col, false, "%s%s: %s",
					entry.isTeam ? "(Team) " : "", entry.sender.c_str(), entry.message.c_str());
				y += 18.0f;
			}
		}

		if (m_isListening)
		{
			bool blink = (int(now * 3.0f) % 2 == 0);
			float colInput[4] = { 1.0f, 0.88f, 0.2f, 1.0f };
			gEnv->pRenderer->Draw2dLabel(25.0f, y + 4.0f, 1.45f, colInput, false,
				"%s: %s%s", m_teamChat ? "Say (Team)" : "Say", m_inputText.c_str(), blink ? "_" : " ");
		}
	}
}

bool CHUDTextChat::OnInputEvent(const SInputEvent &event )
{
	if(gEnv->pConsole->IsOpened() || gEnv->pConsole->GetStatus())
		return false;

	if (event.deviceId != eDI_Keyboard)
		return false;

	if (event.state == eIS_Released)
		m_repeatEvent.keyId = eKI_Unknown;

	if (event.state != eIS_Pressed)
		return true;

	m_repeatEvent = event;

	float repeatDelay = 200.0f;
	float now = gEnv->pTimer ? gEnv->pTimer->GetAsyncTime().GetMilliSeconds() : 0.0f;

	m_repeatTimer = now + repeatDelay;

	bool isClose = (event.keyId == eKI_Enter || event.keyId == eKI_NP_Enter);

	if (isClose)
	{
		Flush();
		return true;
	}
	else if (event.keyId == eKI_Escape)
	{
		m_inputText.clear();
		Flush();
		return true;
	}

	ProcessInput(event);

	return true;
}

bool CHUDTextChat::OnInputEventUI(const SInputEvent &event)
{
	if(gEnv->pConsole->IsOpened())
		return false;

	const char* keyName = event.keyName;

	if (*keyName == 0 || ! ((unsigned)(*keyName) < 0x80) )
		return true;

	Insert(keyName);

	if (m_inputText.length() > 120)
		Flush(false);

	return true;
}

void CHUDTextChat::Delete()
{
	if (!m_inputText.empty())
	{
		if(m_cursor < (int)m_inputText.length())
			m_inputText.erase(m_cursor, 1);
	}
}

void CHUDTextChat::Backspace()
{
	if(!m_inputText.empty())
	{
		if (m_cursor>0)
		{
			m_inputText.erase(m_cursor-1,1);
			m_cursor--;
		}
	}
}

void CHUDTextChat::Left()
{
	if(m_cursor)
		m_cursor--;
}

void CHUDTextChat::Right()
{
	if(m_cursor < (int)m_inputText.length())
		m_cursor++;
}

void CHUDTextChat::Insert(const char *key)
{
	if (key)
	{
		if(strlen(key)!=1)
			return;

		if(m_cursor < (int)m_inputText.length())
			m_inputText.insert(m_cursor, 1, key[0]);
		else
			m_inputText+=key[0];
		m_cursor++;
	}
}

void CHUDTextChat::Flush(bool close)
{
	gEnv->pInput->ClearKeyState();
	m_cursor = 0;

	if(m_inputText.size() > 0)
	{
	//broadcast text / send to receivers ...
		EChatMessageType type = eChatToAll;
		if(m_teamChat)
			type = eChatToTeam;

		if(!ProcessCommands(m_inputText))
		{
			IGame* pGame = gEnv->pGame;
			if(pGame)
			{
				IGameFramework* pGameFramework = pGame->GetIGameFramework();
				if(pGameFramework)
				{
					IGameRulesSystem* pGameRulesSystem = pGameFramework->GetIGameRulesSystem();
					if(pGameRulesSystem)
					{
						IGameRules* pGameRules = pGameRulesSystem->GetCurrentGameRules();
						IActor* pClientActor = pGameFramework->GetClientActor();
						EntityId id = 0;
						if(pClientActor)
							id = pClientActor->GetEntityId();
						if(pGameRules)
							pGameRules->SendChatMessage(type, id, 0, m_inputText.c_str());
					}
				}
			}
		}
		

		m_inputText.clear();
	}

	if (close)
	{
		m_isListening = false;
		gEnv->pInput->SetExclusiveListener(NULL);
		if (m_flashChat)
		{
			m_flashChat->Invoke("setVisibleChatBox", 0);
			m_flashChat->Invoke("GamepadAvailable", false);
		}
		m_textInputActive = false;
	}
}

bool CHUDTextChat::ProcessCommands(const string& inText)
{
	int sep = -1;

	string text = inText;
	string command = inText;
	string param1 = "";
	string param2 = "";

	sep = text.find_first_of(" ");
	if(sep>=0)
	{
		command = text.substr(0,sep);
		text = text.substr(sep+1, text.length()-(sep+1));
	}

	sep = text.find_first_of(" ");
	if(sep>=0)
	{
		param1 = text.substr(0,sep);
		text = text.substr(sep+1, text.length()-(sep+1));
	}
	else
	{
		param1 = text;
		text = "";
	}

	sep = text.find_first_of(" ");
	if(sep>=0)
	{
		param2 = text.substr(0,sep);
	}
	else
	{
		param2 = text;
		text = "";
	}

	TOpFuncMapIt it = m_opFuncMap.find(command);
	if(it!=m_opFuncMap.end())
	{
		return (this->*(it->second))(param1.c_str(), param2.c_str());
	}


	return false;
}

void CHUDTextChat::ProcessInput(const SInputEvent &event)
{
	if(gEnv->pConsole->GetStatus())
		return;

	if (event.keyId == eKI_Backspace)
		Backspace();
	else if (event.keyId == eKI_Delete)
		Delete();
	else if (event.keyId == eKI_Left)
		Left();
	else if (event.keyId == eKI_Right)
		Right();
	else if (event.keyId == eKI_Home)
	{
		m_cursor=0;
	}
	else if (event.keyId == eKI_End)
	{
		m_cursor=(int)m_inputText.length();
	}
}

void CHUDTextChat::AddChatMessage(const char* nick, const wchar_t* msg, int teamFaction, bool teamChat)
{
	string msgNarrow;
	if (msg)
	{
		char buf[512];
		size_t converted = 0;
		wcstombs_s(&converted, buf, sizeof(buf), msg, _TRUNCATE);
		msgNarrow = buf;
	}

	CryLogAlways("[Chat] %s%s: %s", teamChat ? "(Team) " : "", nick ? nick : "Player", msgNarrow.c_str());

	SChatEntry entry;
	entry.sender = nick ? nick : "Player";
	entry.message = msgNarrow;
	entry.timestamp = gEnv->pTimer ? gEnv->pTimer->GetAsyncTime().GetSeconds() : 0.0f;
	entry.isTeam = teamChat;
	if (m_history.size() >= 10)
		m_history.erase(m_history.begin());
	m_history.push_back(entry);

	if (m_flashChat)
	{
		if(teamChat)
		{
			wstring nameAndTarget = g_pGame->GetHUD()->LocalizeWithParams("@ui_chat_team", true, nick);
			SFlashVarValue args[3] = {nameAndTarget.c_str(), msg, teamFaction};
			m_flashChat->Invoke("setChatText", args, 3);
		}
		else
		{
			SFlashVarValue args[3] = {nick, msg, teamFaction};
			m_flashChat->Invoke("setChatText", args, 3);
		}
	}

	m_chatHead++;
	if(m_chatHead > CHAT_LENGTH-1)
		m_chatHead = 0;
	m_anyCurrentText = true;  
}

void CHUDTextChat::AddChatMessage(EntityId sourceId, const wchar_t* msg, int teamFaction, bool teamChat)
{
	string sourceName;
	if(IEntity *pSource = gEnv->pEntitySystem->GetEntity(sourceId))
		sourceName = string(pSource->GetName());
	AddChatMessage(sourceName.c_str(),msg,teamFaction,teamChat);
}


void CHUDTextChat::AddChatMessage(EntityId sourceId, const char* msg, int teamFaction, bool teamChat)
{
	string sourceName;
	if(IEntity *pSource = gEnv->pEntitySystem->GetEntity(sourceId))
		sourceName = string(pSource->GetName());
  AddChatMessage(sourceName.c_str(),msg,teamFaction,teamChat);
}

void CHUDTextChat::AddChatMessage(const char* nick, const char* msg, int teamFaction, bool teamChat)
{
	string message(msg ? msg : "");
	int idx = message.find("@mp_vote_initialized_kick:#:");
	if(idx!=-1)
	{
		message = message.substr(idx+strlen("@mp_vote_initialized_kick:#:"));
		wstring localizedString;
		if(g_pGame->GetHUD())
			localizedString = g_pGame->GetHUD()->LocalizeWithParams("@mp_vote_initialized_kick", true, message.c_str());
		AddChatMessage(nick, localizedString.c_str(), teamFaction,teamChat);
		return;
	}

	CryLogAlways("[Chat] %s%s: %s", teamChat ? "(Team) " : "", nick ? nick : "Player", msg ? msg : "");

	SChatEntry entry;
	entry.sender = nick ? nick : "Player";
	entry.message = msg ? msg : "";
	entry.timestamp = gEnv->pTimer ? gEnv->pTimer->GetAsyncTime().GetSeconds() : 0.0f;
	entry.isTeam = teamChat;
	if (m_history.size() >= 10)
		m_history.erase(m_history.begin());
	m_history.push_back(entry);

	m_chatStrings[m_chatHead] = string(msg ? msg : "");
	m_chatSpawnTime[m_chatHead] = gEnv->pTimer ? gEnv->pTimer->GetAsyncTime().GetMilliSeconds() : 0.0f;

	if (m_flashChat)
	{
		if(teamChat)
		{
			wstring nameAndTarget = g_pGame->GetHUD()->LocalizeWithParams("@ui_chat_team", true, nick);
			SFlashVarValue args[3] = {nameAndTarget.c_str(), msg, teamFaction};
			m_flashChat->Invoke("setChatText", args, 3);
		}
		else
		{
			SFlashVarValue args[3] = {nick, msg, teamFaction};
			m_flashChat->Invoke("setChatText", args, 3);
		}
	}

	m_chatHead++;
	if(m_chatHead > CHAT_LENGTH-1)
		m_chatHead = 0;
	m_anyCurrentText = true;  
}

void CHUDTextChat::GetMemoryStatistics(ICrySizer * s)
{
	s->Add(*this);
	s->Add(m_inputText);
	s->Add(m_lastInputText);
	for (int i=0; i<CHAT_LENGTH; i++)
		s->Add(m_chatStrings[i]);
}

void CHUDTextChat::HandleFSCommand(const char *pCommand, const char *pArgs)
{
	if(!stricmp(pCommand, "sendChatText"))
	{
		size_t len = strlen(pArgs);
		char buffer[2] = {0};
		for(int i = 0; i < len; ++i)
		{
			buffer[0] = pArgs[i];
			Insert(buffer);
		}

		Flush();
	}
}

void CHUDTextChat::VirtualKeyboardInput(const char* direction)
{
	if(m_flashChat && m_textInputActive)
		m_flashChat->Invoke("moveCursor", direction);
}

void CHUDTextChat::OpenChat(int type)
{
	if(!m_isListening)
	{
		gEnv->pInput->ClearKeyState();
		gEnv->pInput->SetExclusiveListener(this);
		m_isListening = true;
		m_teamChat = (type == 2);

		if (m_flashChat)
		{
			m_flashChat->Invoke("setVisibleChatBox", 1);
			m_flashChat->Invoke("GamepadAvailable", m_showVirtualKeyboard);
			if (m_teamChat)
				m_flashChat->Invoke("setShowTeamChat");
			else
				m_flashChat->Invoke("setShowGlobalChat");
		}
		m_textInputActive = true;

		m_repeatEvent = SInputEvent();
		m_inputText.clear();
		m_cursor = 0;
	}
}

bool CHUDTextChat::Vote(const char* type, const char* params)
{
	CActor *pActor = static_cast<CActor*>(gEnv->pGame->GetIGameFramework()->GetClientActor());
	if(!pActor)
		return true;
	
	CGameRules *gameRules = g_pGame->GetGameRules();
	if(!gameRules)
		return true;

	EVotingState vote = eVS_none;
	EntityId id = 0;
	if(type && type[0])
	{
		if(!stricmp(type,"kick"))
		{
			vote = eVS_kick;
		}
		else if(!stricmp(type,"nextmap"))
		{
			vote = eVS_nextMap;
		}
	}
	if(vote == eVS_kick && params && params[0])
	{
		IEntity* pEntity = gEnv->pEntitySystem->FindEntityByName(params);
		if(pEntity)
		{
			id = pEntity->GetId();
		}
	}

	if(gameRules->GetVotingSystem() && gameRules->GetVotingSystem()->IsInProgress())
	{
		AddChatMessage("", "@mp_vote_in_progress", 0, false);
		return true;
	}

	gameRules->StartVoting(pActor, vote,id,params);
	return true;
}

bool CHUDTextChat::VoteYes(const char* param1, const char* param2)
{
	CActor *pActor = static_cast<CActor*>(gEnv->pGame->GetIGameFramework()->GetClientActor());
	if(!pActor)
		return true;

	CGameRules *gameRules = g_pGame->GetGameRules();
	if(!gameRules)
		return true;

	gameRules->Vote(pActor, true);
	return true;
}

bool CHUDTextChat::VoteNo(const char* param1, const char* param2)
{
	CActor *pActor = static_cast<CActor*>(gEnv->pGame->GetIGameFramework()->GetClientActor());
	if(!pActor)
		return true;

	CGameRules *gameRules = g_pGame->GetGameRules();
	if(!gameRules)
		return true;

	gameRules->Vote(pActor, false);
	return true;
}

bool CHUDTextChat::Lowtec(const char* param1, const char* param2)
{
	SAFE_HUD_FUNC(StartInterference(30.0, 100.0, 100.0, 10.0));
	AddChatMessage("", "Funky!", 0, false);
	return true;
}

bool CHUDTextChat::Quarantine(const char* param1, const char* param2)
{
	SAFE_HUD_FUNC(StartInterference(30.0, 100.0, 100.0, 5.0));
	AddChatMessage("", "LEGEN -wait for it !- DARY", 0, false);
	return true;
}