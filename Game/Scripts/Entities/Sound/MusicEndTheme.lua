--------------------------------------------------------------------------
--	Crytek Source File.
-- 	Copyright (C), Crytek Studios, 2001-2005.
--------------------------------------------------------------------------
--	Description: Music End Theme (Co-op Multiplayer synchronized)
--------------------------------------------------------------------------

MusicEndTheme = {
	type = "MusicEndTheme",

	Editor = {
		Icon="Music.bmp",
	},

	Properties = {
		bIndoorOnly=0,
		bOutdoorOnly=0,
		bStopAtOnce=0,
		bFadeOut=1,
		bPlayEnd=0,
		bPlayEndAtFadePoint=0,
		nEndLimitInSec=10,
		bEndEverything=0,
	},
	InsideArea=0,
}

function MusicEndTheme:OnSave(stm)
  stm.InsideArea = self.InsideArea
end

function MusicEndTheme:OnLoad(stm)
  self.InsideArea = stm.InsideArea
end

function MusicEndTheme:CliSrv_OnInit()
end

function MusicEndTheme:OnShutDown()
end

function MusicEndTheme:Client_OnEnterArea( player,areaId )
	if (g_localActorId ~= player.id) then return end;	    

	local bActivate=1;
	local Indoor=System.IsPointIndoors(System.GetViewCameraPos());
	
	if ((self.Properties.bIndoorOnly==1) and (Indoor==nil)) then
		bActivate=0;
	elseif ((self.Properties.bOutdoorOnly==1) and (Indoor~=nil)) then
		bActivate=0;
	end
	
	if (bActivate==1) then
		self.InsideArea=1;
		if (self.Properties.bStopAtOnce==1) then
			Sound.EndMusicTheme(0, self.Properties.nEndLimitInSec, self.Properties.bEndEverything);
		elseif (self.Properties.bFadeOut==1) then
			Sound.EndMusicTheme(1, self.Properties.nEndLimitInSec, self.Properties.bEndEverything);
		elseif (self.Properties.bPlayEnd==1) then
			Sound.EndMusicTheme(2, self.Properties.nEndLimitInSec, self.Properties.bEndEverything);
		elseif (self.Properties.bPlayEndAtFadePoint==1) then
			Sound.EndMusicTheme(3, self.Properties.nEndLimitInSec, self.Properties.bEndEverything);
		end;
	elseif ((bActivate==0) and (self.InsideArea==1)) then
		self.InsideArea=0;
	end
end

function MusicEndTheme:Event_Stop( )
	local fadeType = 2;
	if (self.Properties.bStopAtOnce==1) then
		fadeType = 0;
	elseif (self.Properties.bFadeOut==1) then
		fadeType = 1;
	elseif (self.Properties.bPlayEnd==1) then
		fadeType = 2;
	elseif (self.Properties.bPlayEndAtFadePoint==1) then
		fadeType = 3;
	end
	Sound.EndMusicTheme(fadeType, self.Properties.nEndLimitInSec, self.Properties.bEndEverything);
	if (gEnv.bServer and g_gameRules and g_gameRules.BroadcastMusicEvent) then
		g_gameRules:BroadcastMusicEvent(4, "", "", self.Properties.bEndEverything ~= 0, false, self.Properties.nEndLimitInSec or 10, fadeType);
	end
end

MusicEndTheme.Server={
	OnInit=function(self)
		self:CliSrv_OnInit()
	end,
	OnShutDown=function(self)
	end,
	Inactive={
	},
	Active={
	},
}

MusicEndTheme.Client={
	OnInit=function(self)
		self:CliSrv_OnInit()
	end,
	OnShutDown=function(self)
	end,
	OnEnterArea=MusicEndTheme.Client_OnEnterArea,
	OnLeaveArea=MusicEndTheme.Client_OnLeaveArea,
	OnProceedFadeArea=MusicEndTheme.Client_OnProceedFadeArea,
}

MusicEndTheme.FlowEvents =
{
	Inputs =
	{
		Stop = { MusicEndTheme.Event_Stop, "bool" },
	},
	Outputs =
	{
	},
}
