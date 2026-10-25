#ifndef __COOPADMINMENU_H__
#define __COOPADMINMENU_H__

#pragma once

#include <IInput.h>
#include <map>
#include <vector>

struct ICoopDeferredSequence
{
	virtual ~ICoopDeferredSequence() {}
	virtual void OnCoopLobbyStart() = 0;
	virtual const char* GetSequenceName() const = 0;
};

class CCoopAdminMenu
{
public:
	static CCoopAdminMenu& GetInstance();

	void Init();
	void Shutdown();
	void Toggle();
	bool IsOpen() const { return m_bOpen; }
	void SetOpen(bool bOpen);

	bool OnInputEvent(const SInputEvent &event);
	void Update(float frameTime);
	void Render();

	// Pre-Game Campaign Squad Lobby
	void OnLoadingStart();
	void OnLoadingComplete();
	void OnPlayerConnecting(int channelId, const char* playerName);
	void OnPlayerEnteredGame(int channelId, const char* playerName);
	void OnPlayerDisconnected(int channelId);

	bool IsLobbyWaiting() const;
	void StartCampaignFromLobby();

	void RegisterDeferredSequence(ICoopDeferredSequence* pSeq);
	void UnregisterDeferredSequence(ICoopDeferredSequence* pSeq);

	void SpawnSandboxItem(int category, int index);
	void ServerSpawnItem(int category, int index, const Vec3& pos, const Ang3& angles);

private:
	CCoopAdminMenu();
	~CCoopAdminMenu();

	enum ECategory
	{
		ECAT_MAPS = 0,
		ECAT_CHEATS,
		ECAT_TIMEOFDAY,
		ECAT_NPCS,
		ECAT_PROPS,
		ECAT_BUILDINGS,
		ECAT_COUNT
	};

	struct SLobbyPlayerInfo
	{
		string name;
		bool bLoaded;
		bool bIsHost;
		SLobbyPlayerInfo() : bLoaded(false), bIsHost(false) {}
	};

	bool m_bOpen;
	int m_currentCategory;
	int m_selectedItem[ECAT_COUNT];
	string m_statusMessage;
	float m_statusTimer;

	// Lobby state
	bool m_bLobbyWaiting;
	bool m_bWasLobbyWaiting;
	float m_lobbySyncTimer;
	float m_lobbyTimeSinceLoad;
	float m_lobbyInitialTOD;
	string m_pendingMapChange;
	std::map<int, SLobbyPlayerInfo> m_lobbyPlayers;
	std::vector<ICoopDeferredSequence*> m_deferredSequences;

	void ExecuteSelected();
	void ChangeMap(const char* mapName);
	void SetStatus(const char* msg);
	void RenderLobbyOverlay();
	void SyncLobbyToClients();

	static void CmdToggleAdminMenu(IConsoleCmdArgs* pArgs);
	static void CmdStartCampaign(IConsoleCmdArgs* pArgs);
	static void CmdSpawnItem(IConsoleCmdArgs* pArgs);
};

#endif // __COOPADMINMENU_H__
