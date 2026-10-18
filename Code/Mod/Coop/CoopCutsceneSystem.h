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

private:
    bool            m_bPlayingCutscene;
    bool            m_bPlayingCutsceneLastFrame;
    bool            m_bCutsceneBlending;
    IEntity*        m_pCameraEntity;
    float           m_fCameraFOV;
    float           m_seqPollTimer;
    IAnimSequence*  m_pActiveSequence;
    std::set<string> m_serverPlayingSequences;

public:
    // IViewSystemListener
    virtual bool OnBeginCutScene(IAnimSequence* pSeq, bool bResetFX);
    virtual bool OnEndCutScene(IAnimSequence* pSeq);
    virtual void OnPlayCutSceneSound(IAnimSequence* pSeq, ISound* pSound);
    virtual bool OnCameraChange(const SCameraParams& cameraParams);
    // ~IViewSystemListener

};
 
#endif // _CoopCutsceneSystem_H_