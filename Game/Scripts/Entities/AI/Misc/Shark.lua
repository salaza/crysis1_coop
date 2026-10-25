--------------------------------------------------------------------------
--	Crytek Source File.
-- 	Copyright (C), Crytek Studios, 2001-2006.
--------------------------------------------------------------------------
--	Description: Shark class to be used for kill events (Co-op Multiplayer enhanced)
--------------------------------------------------------------------------
Script.ReloadScript( "SCRIPTS/Entities/AI/Misc/Shark_x.lua");
-----------------------------------------------------------------------------------------------------

CreateActor(Shark_x);
Shark = CreateAI(Shark_x);
Shark:Expose();

----------------------------------------------------------------------------------------------------
function Shark:GetTargetActor()
	local targetActor = nil;
	local minDist = 999999.0;

	if (g_gameRules and g_gameRules.game and g_gameRules.game.GetPlayers) then
		local players = g_gameRules.game:GetPlayers();
		if (players) then
			for _, player in pairs(players) do
				if (player and player.actor and player.actor:GetHealth() > 0) then
					local dist = self:GetDistance(player.id);
					if (dist < minDist) then
						minDist = dist;
						targetActor = player;
					end
				end
			end
		end
	end

	if (not targetActor) then
		targetActor = g_localActor;
	end
	return targetActor;
end;

----------------------------------------------------------------------------------------------------
function Shark:Event_Spawn()
	local targetActor = self.AI.target;
	if (not targetActor) then
		targetActor = self:GetTargetActor();
	end

	self:Event_Enable();
	self.AI.target = targetActor;
	if (targetActor and targetActor.id) then
		self.actor:SetParams({selectTarget = targetActor.id, spawned = self.AI.spawned});
	end
	
	self:Hide(0);
	self:Activate(1);
	
	if(not self.AI.spawned) then
		self.AI.spawned = true;
	end
	
	BroadcastEvent(self, "SpawnShark");
	AI.ModifySmartObjectStates(self.id,"-Escaping");
end;

function Shark:Event_Remove()
	self.actor:SetParams({goAway = 1});
	AI.ModifySmartObjectStates(self.id,"Escaping-StickToPlayer");
end

----------------------------------------------------------------------------------------------------
function Shark:Chase(target)
	if (target and target.id) then
		self.actor:SetParams({targetId = target.id, selectTarget = target.id});
		self.AI.target = target;
	end
end

-------------------------------------------------------
function Shark:GetDamageAbsorption()
	return 1;
end

-------------------------------------------------------
function Shark:Event_Disable()
	self:TriggerEvent(AIEVENT_DISABLE);
	self.bNowEnabled = 0;
	BroadcastEvent(self, "Disable");
end

-------------------------------------------------------
function Shark:Event_Enable()
	self:TriggerEvent(AIEVENT_ENABLE);
	self.bNowEnabled = 1;
	BroadcastEvent(self, "Enable");
end

Shark.FlowEvents =
{
	Inputs =
	{
		SpawnShark = { Shark.Event_Spawn, "bool" },
		RemoveShark = { Shark.Event_Remove, "bool" },
		DisableShark = { Shark.Event_Disable, "bool" },
		EnableShark = { Shark.Event_Enable, "bool" },
	},
	Outputs =
	{
		Spawn = "bool",
		Remove = "bool",
		Disable = "bool",
		Enable = "bool",
	},
}
