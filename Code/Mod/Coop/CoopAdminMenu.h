#ifndef __COOPADMINMENU_H__
#define __COOPADMINMENU_H__

#pragma once

#include <IInput.h>

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

private:
	CCoopAdminMenu();
	~CCoopAdminMenu();

	enum ECategory
	{
		ECAT_MAPS = 0,
		ECAT_CHEATS,
		ECAT_TIMEOFDAY,
		ECAT_COUNT
	};

	bool m_bOpen;
	int m_currentCategory;
	int m_selectedItem[ECAT_COUNT];
	string m_statusMessage;
	float m_statusTimer;

	void ExecuteSelected();
	void ChangeMap(const char* mapName);
	void SetStatus(const char* msg);

	static void CmdToggleAdminMenu(IConsoleCmdArgs* pArgs);
};

#endif // __COOPADMINMENU_H__
