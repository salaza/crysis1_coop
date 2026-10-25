#include <StdAfx.h>
#include "CoopCutsceneSystem.h"
#include "CoopAdminMenu.h"
#include <IGameFramework.h>
#include <IEntitySystem.h>
#include <IActorSystem.h>
#include "Game.h"
#include "GameRules.h"
#include "HUD/HUD.h"
 
CCoopCutsceneSystem CCoopCutsceneSystem::s_instance = CCoopCutsceneSystem();
 
CCoopCutsceneSystem::CCoopCutsceneSystem() : m_bPlayingCutscene(0),
    m_pCameraEntity(0),
    m_fCameraFOV(0),
    m_seqPollTimer(0.0f),
    m_pActiveSequence(NULL)
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
    if (m_bPlayingCutscene)
    {
        if (!m_pCameraEntity && gEnv->pMovieSystem)
        {
            EntityId camId = gEnv->pMovieSystem->GetCameraParams().cameraEntityId;
            if (camId)
            {
                m_pCameraEntity = gEnv->pEntitySystem->GetEntity(camId);
                m_fCameraFOV = gEnv->pMovieSystem->GetCameraParams().fFOV;
            }
        }

        if (m_pCameraEntity && !gEnv->bEditor && gEnv->pGame)
        {
            IViewSystem* pVS = gEnv->pGame->GetIGameFramework()->GetIViewSystem();
            if (pVS)
            {
                IView* pView = pVS->GetActiveView();
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
        }
    }

    m_bPlayingCutsceneLastFrame = m_bPlayingCutscene;

    // Client: auto-detect when a cutscene ends in movie system if abort sequence was not received
    if (!gEnv->bServer && m_bPlayingCutscene && gEnv->pMovieSystem && !gEnv->bEditor)
    {
        bool anyCutscenePlaying = false;
        ISequenceIt* pIt = gEnv->pMovieSystem->GetSequences(true, false);
        if (pIt)
        {
            for (IAnimSequence* pSeq = pIt->first(); pSeq; pSeq = pIt->next())
            {
                if (gEnv->pMovieSystem->IsPlaying(pSeq) && (pSeq->GetFlags() & (IAnimSequence::CUT_SCENE | IAnimSequence::NO_PLAYER)))
                {
                    anyCutscenePlaying = true;
                    break;
                }
            }
            pIt->Release();
        }

        if (!anyCutscenePlaying)
        {
            IAnimSequence* pEnded = m_pActiveSequence;
            OnEndCutScene(pEnded);
            if (g_pGame && g_pGame->GetHUD() && pEnded)
            {
                g_pGame->GetHUD()->OnEndCutScene(pEnded);
            }
        }
    }

    // Server: Poll playing sequences to broadcast non-camera dialogues and animations (e.g. Prophet talking on beach)
    if (gEnv->bServer && gEnv->bMultiplayer && gEnv->pMovieSystem && !gEnv->bEditor)
    {
        if (CCoopAdminMenu::GetInstance().IsLobbyWaiting())
        {
            // Hold any auto-started sequences at 0.0f while waiting in the pre-game squad lobby
            ISequenceIt* pIt = gEnv->pMovieSystem->GetSequences(true, false);
            if (pIt)
            {
                for (IAnimSequence* pSeq = pIt->first(); pSeq; pSeq = pIt->next())
                {
                    if (gEnv->pMovieSystem->IsPlaying(pSeq))
                    {
                        gEnv->pMovieSystem->SetPlayingTime(pSeq, pSeq->GetTimeRange().start);
                    }
                }
                pIt->Release();
            }
            return;
        }

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
    m_pActiveSequence = pSeq;

    // Search for camera node in sequence if not yet set
    if (!m_pCameraEntity && pSeq)
    {
        int nodeCount = pSeq->GetNodeCount();
        for (int i = 0; i < nodeCount; ++i)
        {
            IAnimNode* pNode = pSeq->GetNode(i);
            if (pNode)
            {
                if (pNode->GetType() == ANODE_CAMERA || (pNode->GetName() && (strstr(pNode->GetName(), "cam") || strstr(pNode->GetName(), "Cam") || strstr(pNode->GetName(), "cine"))))
                {
                    IEntity* pEnt = pNode->GetEntity();
                    if (pEnt)
                    {
                        m_pCameraEntity = pEnt;
                        break;
                    }
                }
            }
        }
    }

    if (!m_pCameraEntity && gEnv->pMovieSystem)
    {
        EntityId camId = gEnv->pMovieSystem->GetCameraParams().cameraEntityId;
        if (camId)
        {
            m_pCameraEntity = gEnv->pEntitySystem->GetEntity(camId);
            m_fCameraFOV = gEnv->pMovieSystem->GetCameraParams().fFOV;
        }
    }

    if (!gEnv->bEditor && m_pCameraEntity && gEnv->pGame)
    {
        IViewSystem* pVS = gEnv->pGame->GetIGameFramework()->GetIViewSystem();
        if (pVS)
        {
            IView* pCamView = pVS->GetViewByEntityId(m_pCameraEntity->GetId(), true);
            if (pCamView)
                pVS->SetActiveView(pCamView);
        }
    }

    if (gEnv->bServer && gEnv->bMultiplayer && pSeq && !CCoopAdminMenu::GetInstance().IsLobbyWaiting())
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
    m_fCameraFOV = 0.0f;
    IAnimSequence* pEnded = pSeq ? pSeq : m_pActiveSequence;
    m_pActiveSequence = NULL;

    if (!gEnv->bEditor && gEnv->pGame)
    {
        IViewSystem* pVS = gEnv->pGame->GetIGameFramework()->GetIViewSystem();
        IActor* pClientActor = gEnv->pGame->GetIGameFramework()->GetClientActor();
        if (pVS && pClientActor)
        {
            IView* pView = pVS->GetViewByEntityId(pClientActor->GetEntityId(), true);
            if (pView)
                pVS->SetActiveView(pView);
        }
    }

    if (gEnv->bServer && gEnv->bMultiplayer)
    {
        if (pEnded)
        {
            CSequenceSynchronizer* pSync = GetSequenceSynchronizer();
            if (pSync)
            {
                pSync->StopSeqeunce(pEnded->GetName(), 0.0f, false);
            }
        }

        // Align any distant clients to host after cutscene finishes
        IActor* pHostActor = gEnv->pGame->GetIGameFramework()->GetClientActor();
        if (pHostActor)
        {
            Vec3 hostPos = pHostActor->GetEntity()->GetWorldPos();
            Ang3 hostAngles = pHostActor->GetEntity()->GetWorldAngles();

            IActorIteratorPtr pIt = gEnv->pGame->GetIGameFramework()->GetIActorSystem()->CreateActorIterator();
            while (IActor* pActor = pIt->Next())
            {
                if (pActor != pHostActor && pActor->IsPlayer())
                {
                    Vec3 pPos = pActor->GetEntity()->GetWorldPos();
                    if ((pPos - hostPos).GetLengthSquared() > 30.0f * 30.0f)
                    {
                        CryLogAlways("[CCoopCutsceneSystem] Syncing client '%s' position to host at (%.1f, %.1f, %.1f) after cutscene '%s'",
                            pActor->GetEntity()->GetName(), hostPos.x, hostPos.y, hostPos.z, pEnded ? pEnded->GetName() : "cutscene");

                        Vec3 targetPos = hostPos + Vec3(1.5f, 1.5f, 0.2f);
                        if (g_pGame && g_pGame->GetGameRules())
                        {
                            g_pGame->GetGameRules()->RevivePlayer(static_cast<CActor*>(pActor), targetPos, hostAngles, 0, false);
                        }
                        else
                        {
                            pActor->GetEntity()->SetPos(targetPos);
                            pActor->GetEntity()->SetRotation(Quat(hostAngles));
                        }
                    }
                }
            }
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
