#include "StdAfx.h"
#include "CoopScout.h"
#include "Item.h"
#include "Coop\CoopSystem.h"

#include "CompatibilityAlienMovementController.h"

CCoopScout::CCoopScout() :
	m_vLookTarget(Vec3(0,0,0)),
	m_vAimTarget(Vec3(0,0,0)),
	m_bHidden(false)
{
}

CCoopScout::~CCoopScout()
{
}

bool CCoopScout::Init(IGameObject * pGameObject)
{
	CScout::Init(pGameObject);

	return true;
}

void CCoopScout::PostInit( IGameObject * pGameObject )
{
	CScout::PostInit(pGameObject);

	pGameObject->SetAIActivation(eGOAIAM_Always);

	if (!GetEntity()->IsHidden())
		GetEntity()->SetTimer(eTIMER_WEAPONDELAY, 1000);
}

void CCoopScout::RegisterMultiplayerAI()
{
	if (GetHealth() <= 0 && GetEntity()->GetAI())
	{
		gEnv->bMultiplayer = false;

		IScriptTable* pScriptTable = GetEntity()->GetScriptTable();
		gEnv->pScriptSystem->BeginCall(pScriptTable, "UnregisterAI");
		gEnv->pScriptSystem->PushFuncParam(pScriptTable);
		gEnv->pScriptSystem->EndCall(pScriptTable);
		CryLogAlways("AI Unregistered for Scout %s", GetEntity()->GetName());

		gEnv->bMultiplayer = true;
	}
	else if (!GetEntity()->GetAI() && GetHealth() > 0)
	{
		gEnv->bMultiplayer = false;

		IScriptTable* pScriptTable = GetEntity()->GetScriptTable();
		gEnv->pScriptSystem->BeginCall(pScriptTable, "RegisterAI");
		gEnv->pScriptSystem->PushFuncParam(pScriptTable);
		gEnv->pScriptSystem->EndCall(pScriptTable);
		CryLogAlways("AI Registered for Scout %s", GetEntity()->GetName());

		gEnv->bMultiplayer = true;
	}
}

void CCoopScout::Update(SEntityUpdateContext& ctx, int updateSlot)
{
	CScout::Update(ctx, updateSlot);

	// Register AI System in MP
	if (gEnv->bServer && gEnv->bMultiplayer && !gEnv->bEditor)
		RegisterMultiplayerAI();

	// Movement reqeust stuff so proper anims play on client
	if (gEnv->bServer)
	{
		SMovementState currMovement = static_cast<CCompatibilityAlienMovementController*>(GetMovementController())->GetMovementReqState();

		m_vLookTarget = currMovement.eyePosition + currMovement.bodyDirection;
		m_vAimTarget = currMovement.eyePosition + currMovement.aimDirection;

		if (GetHealth() > 0.f)
			GetGameObject()->ChangedNetworkState(ASPECT_ALIVE);
	}
	else
	{
		UpdateMovementState();
	}

}

void CCoopScout::UpdateMovementState()
{
	CMovementRequest request;
	request.SetBodyTarget(m_vLookTarget);
	request.SetLookTarget(m_vLookTarget);
	request.SetAimTarget(m_vAimTarget);


	SMovementRequestParams params(request);
	SetActorMovement(params);
}

void CCoopScout::ProcessEvent(SEntityEvent& event)
{
	CScout::ProcessEvent(event);

	switch(event.event)
	{
		case ENTITY_EVENT_HIDE:
		{
			if (gEnv->bServer)
			{
				m_bHidden = true;
				GetGameObject()->ChangedNetworkState(ASPECT_HIDE);
			}

			break;
		}
		case ENTITY_EVENT_UNHIDE:
		{
			m_bHidden = false;
			if (gEnv->bServer)
			{
				GetGameObject()->ChangedNetworkState(ASPECT_HIDE);
			}

			if (!GetInventory() || GetInventory()->GetCount() == 0)
				GetEntity()->SetTimer(eTIMER_WEAPONDELAY, 500);

			break;
		}
	case ENTITY_EVENT_TIMER:
		{
			switch(event.nParam[0])
			{
			case eTIMER_WEAPONDELAY:
				if (m_bHidden || GetEntity()->IsHidden())
					break;
				if (GetInventory() && GetInventory()->GetCount() > 0)
					break;

				IScriptTable* pScriptTable = GetEntity()->GetScriptTable();
				SmartScriptTable props;
				if(pScriptTable && pScriptTable->GetValue("Properties", props))
				{
					const char* equip = nullptr;
					if (props->GetValue("equip_EquipmentPack", equip))
						gEnv->pGame->GetIGameFramework()->GetIItemSystem()->GetIEquipmentManager()->GiveEquipmentPack(this, equip, true, true);
				}

				break;
			}
		}
		break;
	}
}

bool CCoopScout::NetSerialize( TSerialize ser, EEntityAspects aspect, uint8 profile, int flags )
{
	if (!CScout::NetSerialize(ser, aspect, profile, flags))
		return false;

	bool bReading = ser.IsReading();

	switch (aspect)
	{
		case ASPECT_ALIVE:
		{
			ser.Value("vLookTarget", m_vLookTarget, 'wrld');
			ser.Value("vAimTarget", m_vAimTarget, 'wrld');

			// Active weapon synchronization
			string sCurrentWeapon = "";
			if (!bReading)
			{
				IItem* pItem = GetCurrentItem();
				if (pItem && pItem->GetEntity() && pItem->GetEntity()->GetClass())
					sCurrentWeapon = pItem->GetEntity()->GetClass()->GetName();
			}
			ser.Value("sCurrentWeapon", sCurrentWeapon);
			if (bReading && !sCurrentWeapon.empty() && !m_bHidden && !GetEntity()->IsHidden())
			{
				IItem* pCur = GetCurrentItem();
				if (!pCur || !pCur->GetEntity() || strcmp(pCur->GetEntity()->GetClass()->GetName(), sCurrentWeapon.c_str()) != 0)
				{
					IEntityClass* pClass = gEnv->pEntitySystem->GetClassRegistry()->FindClass(sCurrentWeapon.c_str());
					if (pClass && GetInventory())
					{
						EntityId itemId = GetInventory()->GetItemByClass(pClass);
						if (itemId)
						{
							SelectItem(itemId, false);
						}
						else
						{
							gEnv->pGame->GetIGameFramework()->GetIItemSystem()->GiveItem(this, sCurrentWeapon.c_str(), false, true, false);
						}
					}
				}
			}
			break;
		}
		case ASPECT_HIDE:
		{
			ser.Value("bHide", m_bHidden, 'bool');

			if (bReading)
				GetEntity()->Hide(m_bHidden);

			break;
		}
	}

	return true;
}

