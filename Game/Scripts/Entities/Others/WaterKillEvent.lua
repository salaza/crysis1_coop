--------------------------------------------------------------------------
--	Crytek Source File.
-- 	Copyright (C), Crytek Studios, 2001-2006.
--------------------------------------------------------------------------
--	Description: To be used for kill events (Co-op Multiplayer enhanced)
--------------------------------------------------------------------------

WaterKillEvent = {
	Client = {},
	Server = {},
	Properties = {
	},
	Editor={
		Model="Editor/Objects/T.cgf",
		Icon="Trigger.bmp",
	},
	CheckShark = function(self)
		if(self.Shark and self.Shark.dead==false)then
			Script.SetTimerForFunction(5000,"WaterKillEvent.CheckShark",self);
		elseif(self.Shark)then
			Script.SetTimerForFunction(10000,"WaterKillEvent.SpawnNextShark",self);
		end;
	end,
	SpawnNextShark=function(self)
		self:Event_SpawnShark();	
	end,
	Shark = nil,
}

WKE_SPAWN_SHARK_TIMER = 1;
	
function WaterKillEvent:OnPropertyChange()
	self:OnReset();
end;

function WaterKillEvent:OnSave(tbl)
	tbl.Shark = self.Shark;
	tbl.bInitialized = self.bInitialized
end

function WaterKillEvent:OnLoad(tbl)
	self.Shark = tbl.Shark;
	self.bInitialized = tbl.bInitialized;
end

function WaterKillEvent:OnReset()
	if(self.Shark) then 
		self.Shark:Event_Disable();
		self.Shark:OnReset();
	end
end;

----------------------------------------------------------------------------------------------------
function WaterKillEvent.Server:OnInit()
	if(not self.bInitialized)then
		self:OnReset();
		self.bInitialized=1;
	end;
	self:LoadShark();
	if(self.Shark) then
		self.Shark:Event_Disable();
	end
end;

----------------------------------------------------------------------------------------------------
function WaterKillEvent.Client:OnInit()
	if(not self.bInitialized)then
		self:OnReset();
		self.bInitialized=1;
	end;
	self:LoadShark();
	if(self.Shark) then
		self.Shark:Event_Disable();
	end
end;

----------------------------------------------------------------------------------------------------
function WaterKillEvent:LoadShark()
	if (self.Shark) then
		return;
	end

	local spawnParams = {};
	spawnParams.class = "Shark";
	spawnParams.position = self:GetPos();
	spawnParams.name = self:GetName().."_Shark";
	spawnParams.flags = 0;
	spawnParams.scale = nil;
	
	self.Shark = System.SpawnEntity(spawnParams);
end;

----------------------------------------------------------------------------------------------------
function WaterKillEvent:GetTargetPlayer()
	local target = nil;
	local minDist = 999999.0;

	if (g_gameRules and g_gameRules.game and g_gameRules.game.GetPlayers) then
		local players = g_gameRules.game:GetPlayers();
		if (players) then
			for _, player in pairs(players) do
				if (player and player.actor and player.actor:GetHealth() > 0) then
					local dist = self:GetDistance(player.id);
					if (dist < minDist) then
						minDist = dist;
						target = player;
					end
				end
			end
		end
	end

	if (not target) then
		target = g_localActor;
	end
	return target;
end;

----------------------------------------------------------------------------------------------------
function WaterKillEvent:Event_SpawnShark()
	if (not self.Shark) then
		self:LoadShark();
	end

	local shark = self.Shark;
	if(not shark) then
		return;
	end
	
	local target = self:GetTargetPlayer();
	if(not target) then
		System.Warning("WaterKillEvent: Cannot find target player");
		return;
	end
	
	if (shark.Chase) then
		shark:Chase(target);
	end

	shark:Event_Spawn();
	shark.WaterKillEvent = self;
	
	Script.SetTimerForFunction(5000,"WaterKillEvent.CheckShark",self);
	BroadcastEvent(self, "SpawnShark");
end;

function WaterKillEvent:Event_RemoveShark()
	if(self.Shark)then
		self.Shark:Event_Remove();
		BroadcastEvent(self, "RemoveShark");
	end;
end;

function WaterKillEvent.Client:OnTimer(timerId)
	if(timerId == WKE_SPAWN_SHARK_TIMER) then
		self:LoadShark();
		self:Event_SpawnShark();
	end
end

WaterKillEvent.FlowEvents =
{
	Inputs =
	{
		SpawnShark = { WaterKillEvent.Event_SpawnShark, "bool" },
		RemoveShark = { WaterKillEvent.Event_RemoveShark, "bool" },
	},
	Outputs =
	{
		SpawnShark = "bool",
		RemoveShark = "bool",
	},
}
