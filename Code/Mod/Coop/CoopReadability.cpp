#include <StdAfx.h>
#include "CoopReadability.h"
#include "Actor.h"

CCoopReadability::CCoopReadability()
{
	if (gEnv->pSoundSystem)
		gEnv->pSoundSystem->AddEventListener(this, false);
}

CCoopReadability::~CCoopReadability()
{
	if (gEnv->pSoundSystem)
		gEnv->pSoundSystem->RemoveEventListener(this);
}

// Summary:
//	Listener for all sounds within the world, used for syncing AI sounds
void CCoopReadability::OnSoundSystemEvent( ESoundSystemCallbackEvent event,ISound *pSound )
{
	if (event == SOUNDSYSTEM_EVENT_ON_START && gEnv->bServer)
		SendSoundToClosestActor(pSound);
}

#include "GameRules.h"

bool CCoopReadability::SendSoundToClosestActor(ISound* pSound)
{
    if (!pSound)
        return false;

	ESoundSemantic sem = pSound->GetSemantic();
	const char* soundName = pSound->GetName();
	if (!soundName || !soundName[0])
		return false;

	bool isVoiceOrDialog = (sem == eSoundSemantic_Dialog ||
	                        sem == eSoundSemantic_AI_Readability ||
	                        sem == eSoundSemantic_AI_Readability_Response ||
	                        sem == eSoundSemantic_Living_Entity ||
	                        (pSound->GetFlags() & FLAG_SOUND_VOICE) != 0 ||
	                        strstr(soundName, "dialog") != NULL ||
	                        strstr(soundName, "Dialog") != NULL ||
	                        strstr(soundName, "Languages") != NULL ||
	                        strstr(soundName, "languages") != NULL ||
	                        strstr(soundName, "radio") != NULL ||
	                        strstr(soundName, "Radio") != NULL ||
	                        strstr(soundName, "voice") != NULL);

	if (!isVoiceOrDialog)
		return false;

	CActor* pClosestActor = NULL;
	float fClosestDistance = 2500.0f; // 50m search radius
	Vec3 soundPos = pSound->GetPosition();
	bool is3D = (pSound->GetFlags() & FLAG_SOUND_3D) != 0 || soundPos.GetLengthSquared() > 1.0f;

	if (is3D)
	{
		IActorIteratorPtr it = g_pGame->GetIGameFramework()->GetIActorSystem()->CreateActorIterator();
		while (CActor* pActor = static_cast<CActor*>(it->Next()))
		{
			if (pActor->GetHealth() > 0)
			{
				float fDistance = (pActor->GetEntity()->GetPos() - soundPos).GetLengthSquared();
				if (fDistance < fClosestDistance)
				{
					pClosestActor = pActor;
					fClosestDistance = fDistance;
				}
			}
		}
	}

	if (pClosestActor)
	{
		CActor::PlayReadabilitySoundParams params(soundName, (int)sem);
		pClosestActor->GetGameObject()->InvokeRMI(CActor::ClPlayReadabilitySound(), params, eRMI_ToAllClients | eRMI_NoLocalCalls);
		return true;
	}
	else
	{
		CGameRules* pGameRules = g_pGame ? g_pGame->GetGameRules() : NULL;
		if (pGameRules && pGameRules->GetGameObject())
		{
			CGameRules::SPlayVoiceSoundParams params(soundName, (int)sem);
			pGameRules->GetGameObject()->InvokeRMI(CGameRules::ClPlayVoiceSound(), params, eRMI_ToAllClients | eRMI_NoLocalCalls);
			return true;
		}
	}

    return false;
}