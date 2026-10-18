#include <StdAfx.h>
#include "CoopCutsceneSystem.h"
#include <IGameFramework.h>
#include <IEntitySystem.h>
 
CCoopCutsceneSystem CCoopCutsceneSystem::s_instance = CCoopCutsceneSystem();
 
CCoopCutsceneSystem::CCoopCutsceneSystem() : m_bPlayingCutscene(0),
    m_pCameraEntity(0),
    //m_pCurrentView(0),
    m_fCameraFOV(0)
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
}

bool CCoopCutsceneSystem::OnBeginCutScene(IAnimSequence* pSeq, bool bResetFX)
{
    m_bPlayingCutscene = true;
    return true;
}

bool CCoopCutsceneSystem::OnEndCutScene(IAnimSequence* pSeq)
{
    m_bPlayingCutscene = false;
    m_pCameraEntity = NULL;
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