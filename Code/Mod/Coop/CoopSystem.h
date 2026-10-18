#ifndef _CoopSystem_H_
#define _CoopSystem_H_

#include <ILevelSystem.h>
#include <IEntitySystem.h>
#include "CoopReadability.h"

class CDialogSystem;

class CCoopSystem 
	: public ILevelSystemListener
	, public IEntitySystemSink
{
private:
	// Static CCoopSystem class instance forward declaration.
	static CCoopSystem s_instance;

	CCoopSystem();
	~CCoopSystem();

public:
	// Summary:
	//	Gets an pointer to the static CCoopSystem instance.
	static inline CCoopSystem* GetInstance() {
		return &s_instance;
	}

	// Summary:
	//	Initializes the CCoopSystem instance.
	bool Initialize();

	// Summary:
	//	Initialize Complete
	void CompleteInit();

	// Summary:
	//	Shuts down the CCoopSystem instance.
	void Shutdown();

	// Summary:
	//	Updates the CCoopSystem instance.
	void Update(float fFrameTime);


	// ILevelSystemListener
	virtual void OnLevelNotFound(const char *levelName) { };
	virtual void OnLoadingStart(ILevelInfo *pLevel);
	virtual void OnLoadingComplete(ILevel *pLevel);
	virtual void OnLoadingError(ILevelInfo *pLevel, const char *error) { };
	virtual void OnLoadingProgress(ILevelInfo *pLevel, int progressAmount) { };
	// ~ILevelSystemListener

	// IEntitySystemSink
	virtual bool OnBeforeSpawn(SEntitySpawnParams &params);
	virtual void OnSpawn(IEntity *pEntity, SEntitySpawnParams &params) { };
	virtual bool OnRemove(IEntity *pEntity) { return true; };
	virtual void OnEvent(IEntity *pEntity, SEntityEvent &event) { };
	// ~IEntitySystemSink


	CDialogSystem* GetDialogSystem() { return m_pDialogSystem; }

	CCoopReadability* m_pReadability;

private:
	int					m_nInitialized;

	IEntityClass* m_pEntityClassPlayer;
	IEntityClass* m_pEntityClassGrunt;
	IEntityClass* m_pEntityClassAlien;
	IEntityClass* m_pEntityClassScout;
	IEntityClass* m_pEntityClassTrooper;
	IEntityClass* m_pEntityClassHunter;

private:

	CDialogSystem* m_pDialogSystem;

};

#endif // _CoopSystem_H_