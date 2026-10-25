--------------------------------------------------------------------------
--	Crytek Source File.
-- 	Copyright (C), Crytek Studios, 2001-2005.
--------------------------------------------------------------------------
--	Description: Music Mood Selector (Co-op Multiplayer synchronized)
--------------------------------------------------------------------------

MusicMoodSelector = {
	type = "MusicMoodSelector",

	Editor = {
		Icon="Music.bmp",
	},

	Properties = {
		sMood = "",
		bCrossfade = 1,
	},
	InsideArea=0,
	InsideAreaRefCount=0,
}

function MusicMoodSelector:OnSave(props)
	props.bCrossfade = self.Properties.bCrossfade;
end

function MusicMoodSelector:OnLoad(props)
	self.Properties.bCrossfade = props.bCrossfade;
end

function MusicMoodSelector:OnPropertyChange()
end

function MusicMoodSelector:CliSrv_OnInit()
end

function MusicMoodSelector:OnShutDown()
end

function MusicMoodSelector:Client_OnEnterArea( player,areaId )
	if (g_localActorId ~= player.id) then return end;

	self.InsideArea=1;
	self.InsideAreaRefCount=self.InsideAreaRefCount+1;
	
	if (self.InsideAreaRefCount==1) then
		Sound.SetMusicMood(self.Properties.sMood, 0, self.Properties.bCrossfade);
	end
end

function MusicMoodSelector:Event_SetMood( )
	Sound.SetMusicMood(self.Properties.sMood, 0, self.Properties.bCrossfade);
	if (gEnv.bServer and g_gameRules and g_gameRules.BroadcastMusicEvent) then
		g_gameRules:BroadcastMusicEvent(1, self.Properties.sMood, "", false, self.Properties.bCrossfade ~= 0, 0, 0);
	end
end

function MusicMoodSelector:Client_OnLeaveArea( player,areaId )
	if (g_localActorId ~= player.id) then return end;

	self.InsideAreaRefCount=self.InsideAreaRefCount-1;
	if (self.InsideAreaRefCount<=0) then
		self.InsideAreaRefCount=0;
		self.InsideArea=0;
	end
end

function MusicMoodSelector:Client_OnProceedFadeArea( player,areaId,fFade )
end

MusicMoodSelector.Server={
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

MusicMoodSelector.Client={
	OnInit=function(self)
		self:CliSrv_OnInit()
	end,
	OnShutDown=function(self)
	end,
	OnEnterArea=MusicMoodSelector.Client_OnEnterArea,
	OnLeaveArea=MusicMoodSelector.Client_OnLeaveArea,
	OnProceedFadeArea=MusicMoodSelector.Client_OnProceedFadeArea,
}

MusicMoodSelector.FlowEvents =
{
	Inputs =
	{
		SetMood = { MusicMoodSelector.Event_SetMood, "bool" },
	},
	Outputs =
	{
		SetMood = "bool",
	},
}
