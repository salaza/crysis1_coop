#include <StdAfx.h>
#include "CoopCutsceneSystem.h"
#include "CoopAdminMenu.h"
#include <IGameFramework.h>
#include <IEntitySystem.h>
#include <IActorSystem.h>
#include "Game.h"
#include "GameRules.h"
#include "HUD/HUD.h"
#include <StringUtils.h>
#include "SoundMoods.h"
 
CCoopCutsceneSystem CCoopCutsceneSystem::s_instance = CCoopCutsceneSystem();
 
CCoopCutsceneSystem::CCoopCutsceneSystem() : m_bPlayingCutscene(0),
    m_bPlayingCutsceneLastFrame(0),
    m_bCutsceneBlending(0),
    m_pCameraEntity(0),
    m_fCameraFOV(0),
    m_seqPollTimer(0.0f),
    m_pActiveSequence(NULL),
    m_bHostCamActive(false),
    m_vHostCamTargetPos(ZERO),
    m_qHostCamTargetRot(IDENTITY),
    m_fHostCamTargetFOV(0.0f),
    m_vHostCamCurrentPos(ZERO),
    m_qHostCamCurrentRot(IDENTITY),
    m_fHostCamCurrentFOV(0.0f),
    m_fLastCamPacketTime(0.0f),
    m_camSyncTimer(0.0f),
    m_wasCutsceneActiveOnServer(false),
    m_fCutsceneEndedTimer(999.0f)
{
 
}
 
CCoopCutsceneSystem::~CCoopCutsceneSystem()
{
 
}
 
bool CCoopCutsceneSystem::SequenceHasCamera(IAnimSequence* pSeq)
{
    if (!pSeq)
        return false;

    int nodeCount = pSeq->GetNodeCount();
    for (int i = 0; i < nodeCount; ++i)
    {
        IAnimNode* pNode = pSeq->GetNode(i);
        if (!pNode)
            continue;

        if (pNode->GetType() == ANODE_CAMERA)
            return true;

        if (pNode->GetType() == ANODE_SCENE)
        {
            IAnimTrack* pCamTrack = pNode->GetTrack(APARAM_CAMERA);
            if (pCamTrack && pCamTrack->GetNumKeys() > 0)
                return true;
        }

        const char* pNodeName = pNode->GetName();
        if (pNodeName)
        {
            if (CryStringUtils::stristr(pNodeName, "cam") || CryStringUtils::stristr(pNodeName, "camera"))
                return true;
        }
    }

    return false;
}

bool CCoopCutsceneSystem::IsCutsceneSequence(IAnimSequence* pSeq)
{
    if (!pSeq)
        return false;

    const char* sName = pSeq->GetName();
    if (sName)
    {
        // Explicitly exclude non-cutscene ambient, utility, idle, or letterbox sequences
        if (CryStringUtils::stristr(sName, "black_bars") ||
            CryStringUtils::stristr(sName, "blackbars") ||
            CryStringUtils::stristr(sName, "letterbox") ||
            CryStringUtils::stristr(sName, "cinematic_bars") ||
            CryStringUtils::stristr(sName, "parachute") ||
            CryStringUtils::stristr(sName, "extro") ||
            CryStringUtils::stristr(sName, "ambient") ||
            CryStringUtils::stristr(sName, "ambience") ||
            CryStringUtils::stristr(sName, "flyby") ||
            CryStringUtils::stristr(sName, "fly_by") ||
            CryStringUtils::stristr(sName, "idle") ||
            CryStringUtils::stristr(sName, "_post") ||
            CryStringUtils::stristr(sName, "_pre") ||
            CryStringUtils::stristr(sName, "dummy") ||
            CryStringUtils::stristr(sName, "sound") ||
            CryStringUtils::stristr(sName, "tornado") ||
            CryStringUtils::stristr(sName, "bars"))
        {
            return false;
        }
    }

    // A cutscene sequence MUST contain a camera track
    if (!SequenceHasCamera(pSeq))
        return false;

    // A true cutscene has duration >= 6.0 seconds
    float duration = pSeq->GetTimeRange().end - pSeq->GetTimeRange().start;
    if (duration < 6.0f)
        return false;

    int flags = pSeq->GetFlags();
    if (flags & (IAnimSequence::CUT_SCENE | IAnimSequence::NO_PLAYER | IAnimSequence::IS_16TO9))
        return true;

    if (sName)
    {
        if (strnicmp(sName, "cine_", 5) == 0 || strnicmp(sName, "cine.", 5) == 0)
            return true;
    }

    return false;
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

void CCoopCutsceneSystem::OnHostCameraSync(const Vec3& pos, const Quat& rot, float fov, bool active)
{
    if (!active)
    {
        m_bHostCamActive = false;
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
        return;
    }

    m_vHostCamTargetPos = pos;
    m_qHostCamTargetRot = rot;
    m_fHostCamTargetFOV = fov;
    m_fLastCamPacketTime = gEnv->pTimer ? gEnv->pTimer->GetAsyncTime().GetSeconds() : 0.0f;

    if (!m_bHostCamActive)
    {
        m_bHostCamActive = true;
        m_vHostCamCurrentPos = pos;
        m_qHostCamCurrentRot = rot;
        m_fHostCamCurrentFOV = fov;
    }
}

void CCoopCutsceneSystem::Update(float fFrameTime)
{
    if (m_bPlayingCutscene)
        m_fCutsceneEndedTimer = 0.0f;
    else
        m_fCutsceneEndedTimer += fFrameTime;

    // Client: smooth interpolation of host camera during cutscene
    if (!gEnv->bServer && m_bHostCamActive)
    {
        float now = gEnv->pTimer ? gEnv->pTimer->GetAsyncTime().GetSeconds() : 0.0f;
        if (now - m_fLastCamPacketTime > 0.75f)
        {
            m_bHostCamActive = false;
        }
        else
        {
            float t = clamp(fFrameTime * 35.0f, 0.0f, 1.0f);
            m_vHostCamCurrentPos += (m_vHostCamTargetPos - m_vHostCamCurrentPos) * t;
            m_qHostCamCurrentRot = Quat::CreateSlerp(m_qHostCamCurrentRot, m_qHostCamTargetRot, t);
            if (m_fHostCamTargetFOV > 0.0f)
                m_fHostCamCurrentFOV += (m_fHostCamTargetFOV - m_fHostCamCurrentFOV) * t;

            if (gEnv->pGame && !gEnv->bEditor)
            {
                IActor* pClientActor = gEnv->pGame->GetIGameFramework()->GetClientActor();
                if (pClientActor && pClientActor->GetLinkedVehicle())
                {
                    // Never override or fight local camera when client is in a vehicle
                    m_bHostCamActive = false;
                }
                else
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
                                newParams.position = m_vHostCamCurrentPos;
                                newParams.rotation = m_qHostCamCurrentRot;
                                if (m_fHostCamTargetFOV > 0.0f)
                                    newParams.fov = m_fHostCamCurrentFOV;
                                newParams.blend = false;
                                pView->SetCurrentParams(newParams);
                            }
                        }
                    }
                }
        }
    }

    }
    if (m_bPlayingCutscene && (!m_bHostCamActive || gEnv->bServer))
    {
        if (gEnv->pMovieSystem)
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

    // Server: Authoritative cutscene camera broadcast to all clients
    if (gEnv->bServer && gEnv->bMultiplayer && !gEnv->bEditor)
    {
        IActor* pHostActor = gEnv->pGame ? gEnv->pGame->GetIGameFramework()->GetClientActor() : NULL;
        bool bHostInVehicle = (pHostActor && pHostActor->GetLinkedVehicle() != NULL);
        bool bHostInCutscene = m_bPlayingCutscene && !bHostInVehicle;
        if (!bHostInCutscene && !bHostInVehicle && gEnv->pMovieSystem)
        {
            ISequenceIt* pIt = gEnv->pMovieSystem->GetSequences(true, false);
            if (pIt)
            {
                for (IAnimSequence* pSeq = pIt->first(); pSeq; pSeq = pIt->next())
                {
                    if (gEnv->pMovieSystem->IsPlaying(pSeq) && IsCutsceneSequence(pSeq))
                    {
                        bHostInCutscene = true;
                        break;
                    }
                }
                pIt->Release();
            }
        }

        if (bHostInCutscene)
        {
            m_camSyncTimer += fFrameTime;
            if (m_camSyncTimer >= 0.02f) // 50 Hz sync
            {
                m_camSyncTimer = 0.0f;
                Vec3 camPos(ZERO);
                Quat camRot(IDENTITY);
                float camFOV = 60.0f * (gf_PI / 180.0f);

                IViewSystem* pVS = gEnv->pGame ? gEnv->pGame->GetIGameFramework()->GetIViewSystem() : NULL;
                IView* pView = pVS ? pVS->GetActiveView() : NULL;
                const SViewParams* curr = pView ? pView->GetCurrentParams() : NULL;
                if (curr && curr->position.GetLengthSquared() > 0.01f)
                {
                    camPos = curr->position;
                    camRot = curr->rotation;
                    camFOV = curr->fov;
                }
                else
                {
                    const CCamera& viewCam = gEnv->pSystem->GetViewCamera();
                    camPos = viewCam.GetPosition();
                    camRot = Quat(Matrix33(viewCam.GetMatrix()));
                    camFOV = viewCam.GetFov();
                }

                CGameRules* pGameRules = g_pGame ? g_pGame->GetGameRules() : NULL;
                if (pGameRules && pGameRules->GetGameObject())
                {
                    CGameRules::SCutsceneCameraParams camParams(camPos, camRot, camFOV, true);
                    pGameRules->GetGameObject()->InvokeRMI(CGameRules::ClCutsceneCameraSync(), camParams, eRMI_ToAllClients | eRMI_NoLocalCalls);
                }
                m_wasCutsceneActiveOnServer = true;
            }
        }
        else if (m_wasCutsceneActiveOnServer)
        {
            m_wasCutsceneActiveOnServer = false;
            m_camSyncTimer = 0.0f;
            CGameRules* pGameRules = g_pGame ? g_pGame->GetGameRules() : NULL;
            if (pGameRules && pGameRules->GetGameObject())
            {
                CGameRules::SCutsceneCameraParams camParams(ZERO, IDENTITY, 0.0f, false);
                pGameRules->GetGameObject()->InvokeRMI(CGameRules::ClCutsceneCameraSync(), camParams, eRMI_ToAllClients | eRMI_NoLocalCalls);
            }
        }
    }

    // Auto-detect when a cutscene ends in movie system if abort sequence was not received (on both server & client)
    if (m_bPlayingCutscene && gEnv->pMovieSystem && !gEnv->bEditor)
    {
        bool anyCutscenePlaying = false;
        ISequenceIt* pIt = gEnv->pMovieSystem->GetSequences(true, false);
        if (pIt)
        {
            for (IAnimSequence* pSeq = pIt->first(); pSeq; pSeq = pIt->next())
            {
                if (gEnv->pMovieSystem->IsPlaying(pSeq) && IsCutsceneSequence(pSeq))
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
        if (m_seqPollTimer >= 0.05f)
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

                        if (IsCutsceneSequence(pSeq))
                        {
                            CryLogAlways("[CCoopCutsceneSystem] Detected cutscene sequence '%s' started on server", it->c_str());
                            OnBeginCutScene(pSeq, !m_bPlayingCutscene);
                            if (g_pGame && g_pGame->GetHUD())
                            {
                                g_pGame->GetHUD()->OnBeginCutScene(pSeq, !m_bPlayingCutscene);
                            }
                        }
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
    if (pSeq && !IsCutsceneSequence(pSeq))
        return true;

    m_bPlayingCutscene = true;
    m_pActiveSequence = pSeq;

    if (gEnv->pSoundSystem)
    {
        gEnv->pSoundSystem->Pause(false, true);
    }
    if (g_pGame && g_pGame->GetSoundMoods())
    {
        g_pGame->GetSoundMoods()->ClearAllSoundMoods();
    }

    if (gEnv->pMovieSystem)
    {
        EntityId camId = gEnv->pMovieSystem->GetCameraParams().cameraEntityId;
        if (camId)
        {
            m_pCameraEntity = gEnv->pEntitySystem->GetEntity(camId);
            m_fCameraFOV = gEnv->pMovieSystem->GetCameraParams().fFOV;
        }
    }

    // Search for camera node in sequence if not yet set
    if (!m_pCameraEntity && pSeq)
    {
        int nodeCount = pSeq->GetNodeCount();
        for (int i = 0; i < nodeCount; ++i)
        {
            IAnimNode* pNode = pSeq->GetNode(i);
            if (pNode)
            {
                if (pNode->GetType() == ANODE_CAMERA || (pNode->GetName() && (strstr(pNode->GetName(), "cam") || strstr(pNode->GetName(), "Cam") || strstr(pNode->GetName(), "Camera"))))
                {
                    IEntity* pEnt = pNode->GetEntity();
                    if (!pEnt && pNode->GetName() && gEnv->pEntitySystem)
                    {
                        pEnt = gEnv->pEntitySystem->FindEntityByName(pNode->GetName());
                    }
                    if (pEnt)
                    {
                        m_pCameraEntity = pEnt;
                        break;
                    }
                }
            }
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
    if (pSeq && !IsCutsceneSequence(pSeq))
        return true;

    if (g_pGame && g_pGame->GetSoundMoods())
    {
        g_pGame->GetSoundMoods()->ClearAllSoundMoods();
    }

    // If another cutscene sequence is still playing in MovieSystem, do not end cutscene mode yet!
    if (gEnv->pMovieSystem && !gEnv->bEditor)
    {
        ISequenceIt* pIt = gEnv->pMovieSystem->GetSequences(true, false);
        if (pIt)
        {
            for (IAnimSequence* pOtherSeq = pIt->first(); pOtherSeq; pOtherSeq = pIt->next())
            {
                if (pOtherSeq != pSeq && gEnv->pMovieSystem->IsPlaying(pOtherSeq) && IsCutsceneSequence(pOtherSeq))
                {
                    CryLogAlways("[CCoopCutsceneSystem] Sequence '%s' ended, but '%s' is still playing - keeping cutscene active",
                        pSeq ? pSeq->GetName() : "null", pOtherSeq->GetName());
                    pIt->Release();
                    m_pActiveSequence = pOtherSeq;
                    m_pCameraEntity = NULL;
                    m_fCameraFOV = 0.0f;
                    int nodeCount = pOtherSeq->GetNodeCount();
                    for (int i = 0; i < nodeCount; ++i)
                    {
                        IAnimNode* pNode = pOtherSeq->GetNode(i);
                        if (pNode && (pNode->GetType() == ANODE_CAMERA || (pNode->GetName() && CryStringUtils::stristr(pNode->GetName(), "cam"))))
                        {
                            IEntity* pEnt = pNode->GetEntity();
                            if (!pEnt && pNode->GetName() && gEnv->pEntitySystem)
                                pEnt = gEnv->pEntitySystem->FindEntityByName(pNode->GetName());
                            if (pEnt)
                            {
                                m_pCameraEntity = pEnt;
                                break;
                            }
                        }
                    }
                    return true;
                }
            }
            pIt->Release();
        }
    }

    m_bPlayingCutscene = false;
    m_pCameraEntity = NULL;
    m_fCameraFOV = 0.0f;
    IAnimSequence* pEnded = pSeq ? pSeq : m_pActiveSequence;
    m_pActiveSequence = NULL;

    // Reset sound listener VisArea and ensure audio is fully restored after cutscene
    if (gEnv->pSoundSystem)
    {
        IListener* pListener = gEnv->pSoundSystem->GetListener(LISTENERID_STANDARD);
        if (pListener)
        {
            pListener->SetVisArea(NULL);
            pListener->SetUnderwater(0.0f);
        }
        gEnv->pSoundSystem->Pause(false, true);
    }
    if (g_pGame && g_pGame->GetSoundMoods())
    {
        g_pGame->GetSoundMoods()->ClearAllSoundMoods();
    }

    if (gEnv->pMovieSystem)
    {
        SCameraParams emptyParams;
        gEnv->pMovieSystem->SetCameraParams(emptyParams);
    }

    if (gEnv->bServer && gEnv->bMultiplayer)
    {
        m_wasCutsceneActiveOnServer = false;
        m_camSyncTimer = 0.0f;
        CGameRules* pGameRules = g_pGame ? g_pGame->GetGameRules() : NULL;
        if (pGameRules && pGameRules->GetGameObject())
        {
            CGameRules::SCutsceneCameraParams camParams(ZERO, IDENTITY, 0.0f, false);
            pGameRules->GetGameObject()->InvokeRMI(CGameRules::ClCutsceneCameraSync(), camParams, eRMI_ToAllClients | eRMI_NoLocalCalls);
        }
    }

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

            // Safety check: Never align clients if host is high up in the air (e.g. falling or flying in plane)
            // On island, ground level at beach is ~95m. Anything above 200m is in the sky.
            bool bHostSafeOnGround = (hostPos.z < 200.0f);

            if (bHostSafeOnGround)
            {
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
    }

    return true;
}

void CCoopCutsceneSystem::OnPlayCutSceneSound(IAnimSequence* pSeq, ISound* pSound)
{
    if (pSound && CCoopAdminMenu::GetInstance().IsLobbyWaiting())
    {
        pSound->SetPaused(true);
    }
}

bool CCoopCutsceneSystem::OnCameraChange(const SCameraParams& cameraParams)
{
    m_pCameraEntity = gEnv->pEntitySystem->GetEntity(cameraParams.cameraEntityId);
    m_fCameraFOV = cameraParams.fFOV;
 
    return true;
}
