#ifndef _CoopCutsceneSystem_H_
#define _CoopCutsceneSystem_H_
 
#include <IViewSystem.h>
#include <IMovieSystem.h>
 
class CCoopCutsceneSystem : public IViewSystemListener
{
private:
    static CCoopCutsceneSystem s_instance;
    CCoopCutsceneSystem();
    ~CCoopCutsceneSystem();
 
public:
    // Gets the static instance
    static inline CCoopCutsceneSystem* GetInstance() {
        return &s_instance;
    }
 
public:
    // Summary:
    //  Registers the system to listen to IViewSystem.
    void Register();
    // Summary:
    //  Unregisters the system from listening to IViewSystem.
    void Unregister();
 
    // Summary:
    //  Updates the camera view.
    void Update(float fFrameTime);

    bool IsPlayingCutscene() const { return m_bPlayingCutscene; }
    IEntity* GetCameraEntity() const { return m_pCameraEntity; }
    float GetCameraFOV() const { return m_fCameraFOV; }
    void SetCameraEntity(IEntity* pEnt, float fov) { m_pCameraEntity = pEnt; m_fCameraFOV = fov; }

    bool HasHostCutsceneCamera() const { return m_bHostCamActive; }
    Vec3 GetHostCamPos() const { return m_vHostCamCurrentPos; }
    Quat GetHostCamRot() const { return m_qHostCamCurrentRot; }
    float GetHostCamFOV() const { return m_fHostCamCurrentFOV; }
    void OnHostCameraSync(const Vec3& pos, const Quat& rot, float fov, bool active);

private:
    bool            m_bPlayingCutscene;
    bool            m_bPlayingCutsceneLastFrame;
    bool            m_bCutsceneBlending;
    IEntity*        m_pCameraEntity;
    float           m_fCameraFOV;
    float           m_seqPollTimer;
    IAnimSequence*  m_pActiveSequence;
    std::set<string> m_serverPlayingSequences;

    bool            m_bHostCamActive;
    Vec3            m_vHostCamTargetPos;
    Quat            m_qHostCamTargetRot;
    float           m_fHostCamTargetFOV;
    Vec3            m_vHostCamCurrentPos;
    Quat            m_qHostCamCurrentRot;
    float           m_fHostCamCurrentFOV;
    float           m_fLastCamPacketTime;
    float           m_camSyncTimer;
    bool            m_wasCutsceneActiveOnServer;

public:
    // IViewSystemListener
    virtual bool OnBeginCutScene(IAnimSequence* pSeq, bool bResetFX);
    virtual bool OnEndCutScene(IAnimSequence* pSeq);
    virtual void OnPlayCutSceneSound(IAnimSequence* pSeq, ISound* pSound);
    virtual bool OnCameraChange(const SCameraParams& cameraParams);
    // ~IViewSystemListener

};
 
#endif // _CoopCutsceneSystem_H_