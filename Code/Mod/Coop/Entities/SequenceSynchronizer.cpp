#include <StdAfx.h>
#include "SequenceSynchronizer.h"

#include <ICryAnimation.h>
#include <IViewSystem.h>
#include <IMovieSystem.h>
#include "Coop/CoopCutsceneSystem.h"
#include "Game.h"
#include "GameRules.h"
#include "HUD/HUD.h"

CSequenceSynchronizer* CSequenceSynchronizer::s_pInstance = NULL;

CSequenceSynchronizer::CSequenceSynchronizer() :
	m_bInitialized(false)
{
	s_pInstance = this;
}

CSequenceSynchronizer::~CSequenceSynchronizer()
{
	if (s_pInstance == this)
		s_pInstance = NULL;
}

bool CSequenceSynchronizer::Init(IGameObject *pGameObject)
{
	SetGameObject(pGameObject);

	if (!GetGameObject()->BindToNetwork())
		return false;

	s_pInstance = this;
	return true;
}

void CSequenceSynchronizer::PostInit(IGameObject *pGameObject)
{
	m_bInitialized = true;
	s_pInstance = this;
}

void CSequenceSynchronizer::PostInitClient(int channelId)
{
	if (!gEnv->bServer || !gEnv->pMovieSystem)
		return;

	ISequenceIt* pIt = gEnv->pMovieSystem->GetSequences(true, false);
	if (pIt)
	{
		IAnimSequence* pSeq = pIt->first();
		while (pSeq)
		{
			if (gEnv->pMovieSystem->IsPlaying(pSeq))
			{
				float playTime = gEnv->pMovieSystem->GetPlayingTime(pSeq);
				CryLogAlways("[CSequenceSynchronizer] Syncing active cutscene '%s' at %.2fs to client channel %d", 
					pSeq->GetName(), playTime, channelId);

				GetGameObject()->InvokeRMI(ClTrackviewSequence(), 
					STrackviewSeqParams(true, pSeq->GetName(), playTime, false), 
					eRMI_ToClientChannel, channelId);
			}
			pSeq = pIt->next();
		}
		pIt->Release();
	}
}

void CSequenceSynchronizer::Release()
{
	if (s_pInstance == this)
		s_pInstance = NULL;
	delete this;
}

void CSequenceSynchronizer::PlaySequence(string sSequence, float fStartTime, bool bBreakOnStop)
{
	if (!m_bInitialized || !gEnv->bServer) return;

	float curTime = gEnv->pTimer ? gEnv->pTimer->GetAsyncTime().GetSeconds() : 0.0f;
	std::map<string, float>::iterator it = m_sequenceStartTimes.find(sSequence);
	if (it != m_sequenceStartTimes.end() && (curTime - it->second) < 1.0f)
		return;

	m_sequenceStartTimes[sSequence] = curTime;

	CryLogAlways("[CSequenceSynchronizer::PlaySequence] %s at %.2fs", sSequence.c_str(), fStartTime);

	if (g_pGame && g_pGame->GetGameRules())
	{
		CGameRules::SSyncCutsceneParams grParams(true, sSequence, fStartTime, bBreakOnStop);
		g_pGame->GetGameRules()->GetGameObject()->InvokeRMI(CGameRules::ClSyncCutscene(), grParams, eRMI_ToAllClients | eRMI_NoLocalCalls);
	}
}

void CSequenceSynchronizer::StopSeqeunce(string sSequence, float fStartTime, bool bBreakOnStop)
{
	if (!m_bInitialized || !gEnv->bServer) return;

	float curTime = gEnv->pTimer ? gEnv->pTimer->GetAsyncTime().GetSeconds() : 0.0f;
	std::map<string, float>::iterator it = m_sequenceStartTimes.find(sSequence);
	if (it != m_sequenceStartTimes.end())
	{
		float elapsed = curTime - it->second;
		if (elapsed < 2.0f)
		{
			CryLogAlways("[CSequenceSynchronizer::StopSequence] Ignored premature stop for '%s' (started %.2fs ago)",
				sSequence.c_str(), elapsed);
			return;
		}
	}

	// Server authoritative check: if movie system is still playing this sequence on server, DO NOT stop on clients!
	if (gEnv->pMovieSystem)
	{
		IAnimSequence* pServerSeq = gEnv->pMovieSystem->FindSequence(sSequence.c_str());
		if (pServerSeq && gEnv->pMovieSystem->IsPlaying(pServerSeq))
		{
			CryLogAlways("[CSequenceSynchronizer::StopSequence] Sequence '%s' is still playing on server - abort cancelled",
				sSequence.c_str());
			return;
		}
	}

	m_sequenceStartTimes.erase(sSequence);

	CryLogAlways("[CSequenceSynchronizer::StopSequence] %s", sSequence.c_str());

	if (g_pGame && g_pGame->GetGameRules())
	{
		CGameRules::SSyncCutsceneParams grParams(false, sSequence, fStartTime, bBreakOnStop);
		g_pGame->GetGameRules()->GetGameObject()->InvokeRMI(CGameRules::ClSyncCutscene(), grParams, eRMI_ToAllClients | eRMI_NoLocalCalls);
	}
}

IMPLEMENT_RMI(CSequenceSynchronizer, ClTrackviewSequence)
{
	bool bStart = params.bStart;
	string sSequence = params.sSequence;
	float fStartTime = params.fStartTime;
	bool bLeaveTime = params.bBreakOnStop;

	IMovieSystem *pMovieSystem = gEnv->pMovieSystem;
	if (!pMovieSystem)
		return true;

	if (bStart)
	{
		IAnimSequence* pSeq = pMovieSystem->FindSequence(sSequence);

		if (pSeq)
		{
			CryLogAlways("[CSequenceSynchronizer] Client Started Sequence: '%s' at %.2fs", sSequence.c_str(), fStartTime);
			pMovieSystem->PlaySequence(pSeq, true);

			if (fStartTime < pSeq->GetTimeRange().start)
				fStartTime = pSeq->GetTimeRange().start;
			else if (fStartTime > pSeq->GetTimeRange().end)
				fStartTime = pSeq->GetTimeRange().end;

			pMovieSystem->SetPlayingTime(pSeq, fStartTime);

			bool isCutscene = CCoopCutsceneSystem::IsCutsceneSequence(pSeq);
			if (isCutscene)
			{
				CCoopCutsceneSystem::GetInstance()->OnBeginCutScene(pSeq, true);
				if (g_pGame && g_pGame->GetHUD())
				{
					g_pGame->GetHUD()->OnBeginCutScene(pSeq, true);
				}
			}
		}
		else
		{
			CryLogAlways("[CSequenceSynchronizer] Client failed to find sequence: '%s'", sSequence.c_str());
		}
	}
	else
	{
		IAnimSequence* pSeq = pMovieSystem->FindSequence(sSequence);

		if (pSeq)
		{
			if (pMovieSystem->IsPlaying(pSeq))
			{
				CryLogAlways("[CSequenceSynchronizer] Client Stopped Sequence: '%s'", sSequence.c_str());
				pMovieSystem->AbortSequence(pSeq, bLeaveTime);
			}

			bool isCutscene = CCoopCutsceneSystem::IsCutsceneSequence(pSeq);
			if (isCutscene)
			{
				CCoopCutsceneSystem::GetInstance()->OnEndCutScene(pSeq);
				if (g_pGame && g_pGame->GetHUD())
				{
					g_pGame->GetHUD()->OnEndCutScene(pSeq);
				}
			}
		}
	}

	return true;
}