#include <StdAfx.h>
#include "CoopCutsceneSystem.h"
#include <IGameFramework.h>
#include <IEntitySystem.h>
 
CCoopCutsceneSystem CCoopCutsceneSystem::s_instance = CCoopCutsceneSystem();
 
CCoopCutsceneSystem::CCoopCutsceneSystem() : m_bPlayingCutscene(0),
    m_pCameraEntity(0),
    m_fCameraFOV(0),
    m_seqPollTimer(0.0f)
{
 
}
 
CCoopCutsceneSystem::~CCoopCutsceneSystem()
{
 
}
 
void CCoopCutsceneSystem::Register()
{
    if (!gEnv->bEditor)
        gEnv->pGame->GetIGameFramework()->GetIViewSystem()->AddListener(this);
}
 
void CCoopCutsceneSystem::Unregister()
{
    if (!gEnv->bEditor)
        gEnv->pGame->GetIGameFramework()->GetIViewSystem()->RemoveListener(this);
}
 
#include "Entities/SequenceSynchronizer.h"

void CCoopCutsceneSystem::Update(float fFrameTime)
{
    if (m_bPlayingCutscene && m_pCameraEntity && !gEnv->bEditor)
    {
        IView* pView = gEnv->pGame->GetIGameFramework()->GetIViewSystem()->GetActiveView();
        if (pView)
        {
            const SViewParams* currParams = pView->GetCurrentParams();
            if (currParams)
            {
                SViewParams newParams = *currParams;
                newParams.position = m_pCameraEntity->GetWorldPos();
                newParams.rotation = m_pCameraEntity->GetWorldRotation();
                if (m_fCameraFOV > 0.0f)
                    newParams.fov = m_fCameraFOV;

                if (!m_bPlayingCutsceneLastFrame)
                {
                    m_bCutsceneBlending = currParams->blend;
                    newParams.blend = false;
                }
                else
                {
                    newParams.blend = m_bCutsceneBlending;
                }

                pView->SetCurrentParams(newParams);
            }
        }
    }

    m_bPlayingCutsceneLastFrame = m_bPlayingCutscene;

    // Server: Poll playing sequences to broadcast non-camera dialogues and animations (e.g. Prophet talking on beach)
    if (gEnv->bServer && gEnv->bMultiplayer && gEnv->pMovieSystem && !gEnv->bEditor)
    {
        m_seqPollTimer += fFrameTime;
        if (m_seqPollTimer >= 0.15f)
        {
            m_seqPollTimer = 0.0f;
            std::set<string> currentPlaying;
            ISequenceIt* pIt = gEnv->pMovieSystem->GetSequences(true, false);
            if (pIt)
            {
                for (IAnimSequence* pSeq = pIt->first(); pSeq; pSeq = pIt->next())
                {
                    if (gEnv->pMovieSystem->IsPlaying(pSeq))
                    {
                        currentPlaying.insert(pSeq->GetName());
                    }
                }
                pIt->Release();
            }

            CSequenceSynchronizer* pSync = GetSequenceSynchronizer();
            if (pSync)
            {
                for (std::set<string>::iterator it = currentPlaying.begin(); it != currentPlaying.end(); ++it)
                {
                    if (m_serverPlayingSequences.find(*it) == m_serverPlayingSequences.end())
                    {
                        IAnimSequence* pSeq = gEnv->pMovieSystem->FindSequence(it->c_str());
                        float playTime = pSeq ? gEnv->pMovieSystem->GetPlayingTime(pSeq) : 0.0f;
                        pSync->PlaySequence(*it, playTime, false);
                    }
                }
                for (std::set<string>::iterator it = m_serverPlayingSequences.begin(); it != m_serverPlayingSequences.end(); ++it)
                {
                    if (currentPlaying.find(*it) == currentPlaying.end())
                    {
                        pSync->StopSeqeunce(*it, 0.0f, false);
                    }
                }
            }
            m_serverPlayingSequences = currentPlaying;
        }
    }
}

bool CCoopCutsceneSystem::OnBeginCutScene(IAnimSequence* pSeq, bool bResetFX)
{
    m_bPlayingCutscene = true;

    if (gEnv->bServer && gEnv->bMultiplayer && pSeq)
    {
        CSequenceSynchronizer* pSync = GetSequenceSynchronizer();
        if (pSync)
        {
            pSync->PlaySequence(pSeq->GetName(), 0.0f, false);
        }
    }

    return true;
}

bool CCoopCutsceneSystem::OnEndCutScene(IAnimSequence* pSeq)
{
    m_bPlayingCutscene = false;
    m_pCameraEntity = NULL;

    if (gEnv->bServer && gEnv->bMultiplayer && pSeq)
    {
        CSequenceSynchronizer* pSync = GetSequenceSynchronizer();
        if (pSync)
        {
            pSync->StopSeqeunce(pSeq->GetName(), 0.0f, false);
        }
    }

    return true;
}
void CCoopCutsceneSystem::OnPlayCutSceneSound(IAnimSequence* pSeq, ISound* pSound)
{
 
}
bool CCoopCutsceneSystem::OnCameraChange(const SCameraParams& cameraParams)
{
    m_pCameraEntity = gEnv->pEntitySystem->GetEntity(cameraParams.cameraEntityId);
    m_fCameraFOV = cameraParams.fFOV;
 
    return true;
}