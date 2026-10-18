#include <StdAfx.h>
#include "SequenceSynchronizer.h"

#include <ICryAnimation.h>
#include <IViewSystem.h>
#include <IMovieSystem.h>

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

void CSequenceSynchronizer::Release()
{
	if (s_pInstance == this)
		s_pInstance = NULL;
	delete this;
}

void CSequenceSynchronizer::PlaySequence(string sSequence, float fStartTime, bool bBreakOnStop)
{
	if (!m_bInitialized || !gEnv->bServer) return;

	CryLogAlways("[CSequenceSynchronizer::PlaySequence] %s", sSequence);

	GetGameObject()->InvokeRMI(ClTrackviewSequence(), STrackviewSeqParams(true, sSequence, fStartTime, bBreakOnStop), eRMI_ToAllClients | eRMI_NoLocalCalls);
}

void CSequenceSynchronizer::StopSeqeunce(string sSequence, float fStartTime, bool bBreakOnStop)
{
	if (!m_bInitialized || !gEnv->bServer) return;

	CryLogAlways("[CSequenceSynchronizer::StopSequence] %s", sSequence);

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
		}
	}

	return true;
}