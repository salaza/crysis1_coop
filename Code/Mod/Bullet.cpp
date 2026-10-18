/*************************************************************************
Crytek Source File.
Copyright (C), Crytek Studios, 2001-2004.
-------------------------------------------------------------------------
$Id$
$DateTime$

-------------------------------------------------------------------------
History:
- 18:10:2005   14:14 : Created by Marcio Martins

*************************************************************************/
#include "StdAfx.h"
#include "Game.h"
#include "Bullet.h"
#include "GameRules.h"
#include <IEntitySystem.h>
#include <IGameTokens.h>
#include "AmmoParams.h"


int CBullet::m_waterMaterialId = -1;
IEntityClass* CBullet::EntityClass = 0;

//------------------------------------------------------------------------
CBullet::CBullet()
{
}

//------------------------------------------------------------------------
CBullet::~CBullet()
{
}

//------------------------------------------------------------------------
void CBullet::HandleEvent(const SGameObjectEvent &event)
{
	FUNCTION_PROFILER(GetISystem(), PROFILE_GAME);

	CProjectile::HandleEvent(event);

	if (event.event == eGFE_OnCollision)
	{
		if (m_destroying)
			return;

		EventPhysCollision *pCollision = reinterpret_cast<EventPhysCollision *>(event.ptr);
		if (!pCollision)
			return;
        
		IEntity *pTarget = pCollision->iForeignData[1]==PHYS_FOREIGN_ID_ENTITY ? (IEntity*)pCollision->pForeignData[1]:0;

		Vec3 dir(0, 0, 0);
		if (pCollision->vloc[0].GetLengthSquared() > 1e-6f)
			dir = pCollision->vloc[0].GetNormalized();

		// Physical collision registration for cutting/breaking trees, vegetation and pushing props
		if (pCollision->pEntity[1])
		{
			pe_action_register_coll_event collision;
			collision.collMass = 0.005f;
			collision.partid[1] = pCollision->partid[1];
			collision.partid[0] = -2;
			ISurfaceTypeManager *pSTM = gEnv->p3DEngine->GetMaterialManager()->GetSurfaceTypeManager();
			int invId = pSTM ? pSTM->GetSurfaceTypeByName("mat_invulnerable")->GetId() : 0;
			collision.idmat[1] = pCollision->idmat[1];
			collision.idmat[0] = invId;
			collision.n = pCollision->n;
			collision.pt = pCollision->pt;
			float speed = cry_sqrtf(4000.0f / (80.0f * 0.5f));
			collision.vSelf = dir * speed;
			collision.v = Vec3(0, 0, 0);
			collision.pCollider = NULL;
			pCollision->pEntity[1]->Action(&collision);

			if (pCollision->pEntity[1]->GetType() == PE_RIGID)
			{
				pe_action_impulse ai;
				ai.partid = pCollision->partid[1];
				ai.point = pCollision->pt;
				ai.impulse = dir * (m_damage > 0.0f ? m_damage * 10.0f : 250.0f);
				pCollision->pEntity[1]->Action(&ai);
			}
		}

		CGameRules *pGameRules = g_pGame->GetGameRules();
		IActor* pActor = g_pGame->GetIGameFramework()->GetIActorSystem()->GetActor(m_ownerId);

		bool ok = true;
		if(pTarget && !gEnv->bMultiplayer && pActor && pActor->IsPlayer())
		{
			IActor* pAITarget = g_pGame->GetIGameFramework()->GetIActorSystem()->GetActor(pTarget->GetId());
			if(pAITarget && pTarget->GetAI() && !pTarget->GetAI()->IsHostile(pActor->GetEntity()->GetAI(),false))
			{
				if (pGameRules)
					pGameRules->SetEntityToIgnore(pTarget->GetId());
				ok = false;
			}
		}

		if(ok && pGameRules)
		{
			HitInfo hitInfo(m_ownerId, pTarget ? pTarget->GetId() : 0, m_weaponId,
				m_fmId, 0.0f, pGameRules->GetHitMaterialIdFromSurfaceId(pCollision->idmat[1]), pCollision->partid[1],
				m_hitTypeId, pCollision->pt, dir, pCollision->n);

			hitInfo.remote = IsRemote();
			hitInfo.projectileId = GetEntityId();
			hitInfo.bulletType = m_pAmmoParams->bulletType;
			if (!hitInfo.remote)
				hitInfo.seq=m_seq;
			hitInfo.damage = m_damage;

			if (m_weaponId)
			{
				CWeapon *pWeapon=GetWeapon();
				if (pWeapon && pWeapon->GetForcedHitMaterial() != -1)
					hitInfo.material=pGameRules->GetHitMaterialIdFromSurfaceId(pWeapon->GetForcedHitMaterial());
			}

			pGameRules->ClientHit(hitInfo);

			// Notify AI
			if (gEnv->pAISystem && !gEnv->bMultiplayer)
			{
				IEntity* pShooter = gEnv->pEntitySystem->GetEntity(m_ownerId);
				if (pShooter)
				{
					static int htMelee = pGameRules->GetHitTypeId("melee");
					if (pShooter && m_hitTypeId != htMelee)
					{
						ISurfaceType *pSurfaceType = pGameRules->GetHitMaterial(hitInfo.material);
						const ISurfaceType::SSurfaceTypeAIParams* pParams = pSurfaceType ? pSurfaceType->GetAIParams() : 0;
						const float radius = pParams ? pParams->fImpactRadius : 2.5f;
						const float soundRadius = pParams ? pParams->fImpactSoundRadius : 20.0f;
						gEnv->pAISystem->BulletHitEvent(pCollision->pt, radius, soundRadius, pShooter->GetAI());
					}
				}
			}
		}


		if (pCollision->pEntity[0]->GetType() == PE_PARTICLE)
		{
			float bouncy, friction;
			uint	pierceabilityMat;
			gEnv->pPhysicalWorld->GetSurfaceParameters(pCollision->idmat[1], bouncy, friction, pierceabilityMat);
			pierceabilityMat&=sf_pierceable_mask;
			
			pe_params_particle params;
			if(pCollision->pEntity[0]->GetParams(&params)==0)
				SetDefaultParticleParams(&params);

			//Under water trail
			Vec3 pos=pCollision->pt;
			if ((pCollision->idmat[1] == CBullet::m_waterMaterialId) && 
					(pCollision->pEntity[1]!=gEnv->pPhysicalWorld->AddGlobalArea() || !gEnv->p3DEngine->GetVisAreaFromPos(pos)))
			{
				//Reduce drastically bullet velocity (to be able to see the trail effect)
				//pe_params_particle pparams;
				//if(m_pPhysicalEntity->GetParams(&pparams)==0)
					//SetDefaultParticleParams(&pparams);
				//pparams.velocity = 25.0f;

				//m_pPhysicalEntity->SetParams(&pparams);

				if(m_trailUnderWaterId<0)
				{
					//Check terrain/against water level
					float terrainHeight = gEnv->p3DEngine->GetTerrainElevation(pCollision->pt.x,pCollision->pt.y);
					float waterLevel = gEnv->p3DEngine->GetWaterLevel(&(pCollision->pt));
					if(waterLevel>terrainHeight)
					{
						TrailEffect(true,true);
						return;
					}
				}
			}

			if (pierceabilityMat<=params.iPierceability || pCollision->idCollider==-1) //Do not destroy if collides water
				Destroy();
		}
	}
}

void CBullet::SetWaterMaterialId()
{
	m_waterMaterialId = gEnv->p3DEngine->GetMaterialManager()->GetSurfaceTypeManager()->GetSurfaceTypeByName("mat_water")->GetId();
}


