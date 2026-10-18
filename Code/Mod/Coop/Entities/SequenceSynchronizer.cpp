#include <StdAfx.h>
#include "SequenceSynchronizer.h"

#include <ICryAnimation.h>
#include <IViewSystem.h>
#include <IMovieSystem.h>
#include "Coop/CoopCutsceneSystem.h"
#include "Game.h"
#include "HUD/HUD.h"

CSequenceSynchronizer* CSequenceSynchronizer::s_pInstance = NULL;

CSequenceSynchronizer::CSequenceSynchronizer() :
	m_bInitialized(false),
	m_lastSequenceTime(0.0f)
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
	if (m_lastSequence == sSequence && (curTime - m_lastSequenceTime) < 1.0f)
		return;

	m_lastSequence = sSequence;
	m_lastSequenceTime = curTime;

	CryLogAlways("[CSequenceSynchronizer::PlaySequence] %s at %.2fs", sSequence.c_str(), fStartTime);

	GetGameObject()->InvokeRMI(ClTrackviewSequence(), STrackviewSeqParams(true, sSequence, fStartTime, bBreakOnStop), eRMI_ToAllClients | eRMI_NoLocalCalls);
}

void CSequenceSynchronizer::StopSeqeunce(string sSequence, float fStartTime, bool bBreakOnStop)
{
	if (!m_bInitialized || !gEnv->bServer) return;

	if (m_lastSequence == sSequence)
		m_lastSequence.clear();

	CryLogAlways("[CSequenceSynchronizer::StopSequence] %s", sSequence.c_str());

	GetGameObject()->InvokeRMI(ClTrackviewSequence(), STrackviewSeqParams(false, sSequence, fStartTime, bBreakOnStop), eRMI_ToAllClients | eRMI_NoLocalCalls);
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

			int flags = pSeq->GetFlags();
			if (flags & (IAnimSequence::CUT_SCENE | IAnimSequence::NO_PLAYER | IAnimSequence::IS_16TO9))
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

			int flags = pSeq->GetFlags();
			if (flags & (IAnimSequence::CUT_SCENE | IAnimSequence::NO_PLAYER | IAnimSequence::IS_16TO9))
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