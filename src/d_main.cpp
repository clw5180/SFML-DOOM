/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 BFG Edition GPL Source Code ("Doom 3 BFG Edition Source Code").  

Doom 3 BFG Edition Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 BFG Edition Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 BFG Edition Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 BFG Edition Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 BFG Edition Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#include "Precompiled.hpp"
#include "globaldata.hpp"



#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "doomdef.hpp"
#include "doomstat.hpp"

#include "dstrings.hpp"
#include "sounds.hpp"


#include "z_zone.hpp"
#include "w_wad.hpp"
#include "s_sound.hpp"
#include "v_video.hpp"

#include "f_finale.hpp"
#include "f_wipe.hpp"

#include "m_argv.hpp"
#include "m_misc.hpp"
#include "m_menu.hpp"

#include "i_system.hpp"
#include "i_sound.hpp"
#include "i_video.hpp"

#include "g_game.hpp"

#include "hu_stuff.hpp"
#include "wi_stuff.hpp"
#include "st_stuff.hpp"
#include "am_map.hpp"

#include "p_setup.hpp"
#include "r_local.hpp"


#include "d_main.hpp"

//#include "../idLib/precompiled.hpp"
//#include "../Main/PlayerProfile.hpp"
//#include "../Main/PSN/PS3_Session.hpp"
//#include "d3xp/Game_local.hpp"

//
// D-DoomLoop()
// Not a globally visible function,
//  just included for source reference,
//  called by D_DoomMain, never exits.
// Manages timing and IO,
//  calls all ?_Responder, ?_Ticker, and ?_Drawer,
//  calls I_GetTime, I_StartFrame, and I_StartTic
//
void D_DoomLoop (void);

void R_ExecuteSetViewSize (void);
void D_CheckNetGame (void);
bool D_PollNetworkStart();
void D_ProcessEvents (void);
void D_DoAdvanceDemo (void);

const char*		wadfiles[MAXWADFILES] =
{
	0
};

const char*		extraWad = 0;

//
// EVENT HANDLING
//
// Events are asynchronous inputs generally generated by the game user.
// Events can be discarded if no responder claims them
//


//
// D_PostEvent
// Called by the I/O functions when input is detected
//
void D_PostEvent (event_t* ev)
{
    Globals::g->events[Globals::g->eventhead] = *ev;
    Globals::g->eventhead = (++Globals::g->eventhead)&(MAXEVENTS-1);
}


//
// D_ProcessEvents
// Send all the Globals::g->events of the given timestamp down the responder chain
//
void D_ProcessEvents (void)
{
	event_t*	ev;

	// IF STORE DEMO, DO NOT ACCEPT INPUT
	if ( ( Globals::g->gamemode == commercial )
		&& (W_CheckNumForName("map01")<0) )
		return;

	for ( ; Globals::g->eventtail != Globals::g->eventhead ; Globals::g->eventtail = (++Globals::g->eventtail)&(MAXEVENTS-1) )
	{
		ev = &Globals::g->events[Globals::g->eventtail];
		if (M_Responder (ev))
			continue;               // menu ate the event
		G_Responder (ev);
	}
}




//
// D_Display
//  draw current display, possibly wiping it from the previous
//
// Globals::g->wipegamestate can be set to -1 to force a Globals::g->wipe on the next draw
extern bool waitingForWipe;

void D_Wipe()
{
	int nowtime, tics;

	nowtime = I_GetTime();
	tics = nowtime - Globals::g->wipestart;

	if (tics != 0)
	{
		Globals::g->wipestart = nowtime;
		Globals::g->wipedone = wipe_ScreenWipe( 0, 0, SCREENWIDTH, SCREENHEIGHT, tics );

		// DHM - Nerve :: Demo recording :: Stop large hitch on first frame after the wipe
		if ( Globals::g->wipedone ) {
			Globals::g->oldtrt_entertics = nowtime / Globals::g->ticdup;
			Globals::g->gametime = nowtime;
			Globals::g->wipe = false;
			waitingForWipe = false;
		}
	}
}


void D_Display (void)
{
	qboolean			redrawsbar;

	if (Globals::g->nodrawers)
		return;                    // for comparative timing / profiling

	redrawsbar = false;

	// change the view size if needed
	if (Globals::g->setsizeneeded)
	{
		R_ExecuteSetViewSize();
		Globals::g->oldgamestate = (gamestate_t)-1;                      // force background redraw
		Globals::g->borderdrawcount = 3;
	}

	// save the current screen if about to Globals::g->wipe
	if (Globals::g->gamestate != Globals::g->wipegamestate)
	{
		Globals::g->wipe = true;
		wipe_StartScreen(0, 0, SCREENWIDTH, SCREENHEIGHT);
	}
	else
		Globals::g->wipe = false;

	if (Globals::g->gamestate == GS_LEVEL && Globals::g->gametic)
		HU_Erase();

	// do buffered drawing
	switch(Globals::g->gamestate)
	{
	case GS_LEVEL:
		if (!Globals::g->gametic)
			break;
		if (Globals::g->automapactive)
			AM_Drawer ();
		if (Globals::g->wipe ||(Globals::g->viewheight != 200 * GLOBAL_IMAGE_SCALER && Globals::g->fullscreen) )
			redrawsbar = true;
		if (Globals::g->inhelpscreensstate && !Globals::g->inhelpscreens)
			redrawsbar = true;              // just put away the help screen
		ST_Drawer ( Globals::g->viewheight == 200 * GLOBAL_IMAGE_SCALER, redrawsbar );
		Globals::g->fullscreen = Globals::g->viewheight == 200 * GLOBAL_IMAGE_SCALER;
		break;

	case GS_INTERMISSION:
		WI_Drawer ();
		break;

	case GS_FINALE:
		F_Drawer ();
		break;

	case GS_DEMOSCREEN:
		D_PageDrawer ();
		break;
	}

	// draw buffered stuff to screen
	I_UpdateNoBlit ();

	// draw the view directly
	if(Globals::g->gamestate == GS_LEVEL &&!Globals::g->automapactive && Globals::g->gametic)
		R_RenderPlayerView (&Globals::g->players[Globals::g->displayplayer]);

	if(Globals::g->gamestate == GS_LEVEL && Globals::g->gametic)
		HU_Drawer ();

	// clean up border stuff
	if(Globals::g->gamestate != Globals::g->oldgamestate && Globals::g->gamestate != GS_LEVEL)
		I_SetPalette ((unsigned char*)W_CacheLumpName ("PLAYPAL",PU_CACHE_SHARED));

	// see if the border needs to be initially drawn
	if(Globals::g->gamestate == GS_LEVEL && Globals::g->oldgamestate != GS_LEVEL)
	{
		Globals::g->viewactivestate = false;        // view was not active
		R_FillBackScreen ();    // draw the pattern into the back screen
	}

	// see if the border needs to be updated to the screen
	if(Globals::g->gamestate == GS_LEVEL &&!Globals::g->automapactive && Globals::g->scaledviewwidth != (320 * GLOBAL_IMAGE_SCALER) )
	{
		if(Globals::g->menuactive || Globals::g->menuactivestate ||!Globals::g->viewactivestate)
			Globals::g->borderdrawcount = 3;
		if(Globals::g->borderdrawcount)
		{
			R_DrawViewBorder ();    // erase old menu stuff
			Globals::g->borderdrawcount--;
		}

	}

	Globals::g->menuactivestate = Globals::g->menuactive;
	Globals::g->viewactivestate = Globals::g->viewactive;
	Globals::g->inhelpscreensstate = Globals::g->inhelpscreens;
	Globals::g->oldgamestate = Globals::g->wipegamestate = Globals::g->gamestate;

	// draw pause pic
/*
	if(Globals::g->paused)
	{
		if(Globals::g->automapactive)
			y = 4;
		else
			y = Globals::g->viewwindowy+4;
		V_DrawPatchDirect(Globals::g->viewwindowx+(ORIGINAL_WIDTH-68)/2,
			y,0,(patch_t*)W_CacheLumpName ("M_PAUSE", PU_CACHE_SHARED));
	}
*/

	// menus go directly to the screen
	M_Drawer ();          // menu is drawn even on top of everything
	NetUpdate ( );         // send out any new accumulation
	
	// normal update
	if (!Globals::g->wipe)
	{
		I_FinishUpdate ();              // page flip or blit buffer
		return;
	}

	// \ update
	wipe_EndScreen(0, 0, SCREENWIDTH, SCREENHEIGHT);

	Globals::g->wipestart = I_GetTime () - 1;

	D_Wipe(); // initialize g->wipedone
}



void D_RunFrame( bool Sounds )
{
	if (Sounds)	{
		// move positional sounds
		S_UpdateSounds(Globals::g->players[Globals::g->consoleplayer].mo);
	}

	// Update display, next frame, with current state.
	D_Display ();

	if (Sounds)	{
		// Update sound output.
		I_SubmitSound();
	}
}



//
//  D_DoomLoop
//
void D_DoomLoop (void)
{
	// DHM - Not used
/*
	if (M_CheckParm ("-debugfile"))
	{
		char    filename[20];
		sprintf (filename,"debug%i.txt",Globals::g->consoleplayer);
		I_Printf ("debug output to: %s\n",filename);
		Globals::g->debugfile = f o p e n(filename,"w");
	}

	I_InitGraphics ();

	while (1)
	{
		TryRunTics();
		D_RunFrame( true );
	}
*/
}



//
//  DEMO LOOP
//


//
// D_PageTicker
// Handles timing for warped Globals::g->projection
//
void D_PageTicker (void)
{
	if (--Globals::g->pagetic < 0)
		D_AdvanceDemo ();
}



//
// D_PageDrawer
//
void D_PageDrawer (void)
{
	V_DrawPatch (0,0, 0, (patch_t*)W_CacheLumpName(Globals::g->pagename, PU_CACHE_SHARED));
}


//
// D_AdvanceDemo
// Called after each demo or intro Globals::g->demosequence finishes
//
void D_AdvanceDemo (void)
{
	Globals::g->advancedemo = true;
}


//
// This cycles through the demo sequences.
// FIXME - version dependend demo numbers?
//
void D_DoAdvanceDemo (void)
{
	Globals::g->players[Globals::g->consoleplayer].playerstate = PST_LIVE;  // not reborn
	Globals::g->advancedemo = false;
	Globals::g->usergame = false;               // no save / end game here
	Globals::g->paused = false;
	Globals::g->gameaction = ga_nothing;

	if ( Globals::g->gamemode == retail )
		Globals::g->demosequence =(Globals::g->demosequence+1)%8;
	else
		Globals::g->demosequence =(Globals::g->demosequence+1)%6;

	switch(Globals::g->demosequence)
	{
	case 0:
		if ( Globals::g->gamemode == commercial )
			Globals::g->pagetic = 35 * 11;
		else
			Globals::g->pagetic = 8 * TICRATE;

		Globals::g->gamestate = GS_DEMOSCREEN;
		Globals::g->pagename = "INTERPIC";

		if ( Globals::g->gamemode == commercial )
			S_StartMusic(mus_dm2ttl);
		else
			S_StartMusic (mus_intro);

		break;
	case 1:
		G_DeferedPlayDemo ("demo1");
		break;
	case 2:
		Globals::g->pagetic = 3 * TICRATE;
		Globals::g->gamestate = GS_DEMOSCREEN;
		Globals::g->pagename = "INTERPIC";
		break;
	case 3:
		G_DeferedPlayDemo ("demo2");
		break;
	case 4:
		Globals::g->pagetic = 3 * TICRATE;
		Globals::g->gamestate = GS_DEMOSCREEN;
		Globals::g->pagename = "INTERPIC";
		break;
	case 5:
		G_DeferedPlayDemo ("demo3");
		break;
		// THE DEFINITIVE DOOM Special Edition demo
	case 6:
		Globals::g->pagetic = 3 * TICRATE;
		Globals::g->gamestate = GS_DEMOSCREEN;
		Globals::g->pagename = "INTERPIC";
		break;
	case 7:
		G_DeferedPlayDemo ("demo4");
		break;
	}
}



//
// D_StartTitle
//
void D_StartTitle (void)
{
	Globals::g->gameaction = ga_nothing;
	Globals::g->demosequence = -1;
	D_AdvanceDemo ();
}




//      print Globals::g->title for every printed line

//
// D_AddExtraWadFile
//
void D_SetExtraWadFile( const char *file ) {
	extraWad = file;
}

//
// D_AddFile
//
void D_AddFile (const char *file)
{
	int     numwadfiles;

	for (numwadfiles = 0 ; wadfiles[numwadfiles] ; numwadfiles++)
		if (file == wadfiles[numwadfiles])
			return;
		;
	wadfiles[numwadfiles] = file;
}

//
// IdentifyVersion
// Checks availability of IWAD files by name,
// to determine whether registered/commercial features
// should be executed (notably loading PWAD's).
//

void IdentifyVersion (void)
{
	W_FreeWadFiles();

	const ExpansionData * expansion =  DoomLib::GetCurrentExpansion();
	Globals::g->gamemode = expansion->gameMode;
	Globals::g->gamemission = expansion->pack_type;


	if( expansion->type == ExpansionData::PWAD ) {
		D_AddFile( expansion->iWadFilename );
		D_AddFile( expansion->pWadFilename );

	} else {
		D_AddFile( expansion->iWadFilename );
	}
	
}


//
// Find a Response File
//
void FindResponseFile (void)
{
}


//
// D_DoomMain
//

void D_DoomMain (void)
{
	int             p;
	char                    file[256];


	FindResponseFile ();

	IdentifyVersion ();

	setbuf (stdout, NULL);
	Globals::g->modifiedgame = false;

	// TODO: Networking
	//const bool isDeathmatch = gameLocal->GetMatchParms().GetGameType() == GAME_TYPE_PVP;
	const bool isDeathmatch = false;

	Globals::g->nomonsters = M_CheckParm ("-nomonsters") || isDeathmatch;
	Globals::g->respawnparm = M_CheckParm ("-respawn");
	Globals::g->fastparm = M_CheckParm ("-fast");
	Globals::g->devparm = M_CheckParm ("-devparm");
	if (M_CheckParm ("-altdeath") || isDeathmatch)
		Globals::g->deathmatch = 2;
	else if (M_CheckParm ("-deathmatch"))
		Globals::g->deathmatch = 1;

	switch ( Globals::g->gamemode )
	{
	case retail:
		sprintf(Globals::g->title,
			"                         "
			"The Ultimate DOOM Startup v%i.%i"
			"                           ",
			VERSION/100,VERSION%100);
		break;
	case shareware:
		sprintf(Globals::g->title,
			"                            "
			"DOOM Shareware Startup v%i.%i"
			"                           ",
			VERSION/100,VERSION%100);
		break;
	case registered:
		sprintf(Globals::g->title,
			"                            "
			"DOOM Registered Startup v%i.%i"
			"                           ",
			VERSION/100,VERSION%100);
		break;
	case commercial:
		sprintf(Globals::g->title,
			"                         "
			"DOOM 2: Hell on Earth v%i.%i"
			"                           ",
			VERSION/100,VERSION%100);
		break;
	default:
		sprintf(Globals::g->title,
			"                     "
			"Public DOOM - v%i.%i"
			"                           ",
			VERSION/100,VERSION%100);
		break;
	}

	I_Printf ("%s\n",Globals::g->title);

	if(Globals::g->devparm)
		I_Printf(D_DEVSTR);

	if (M_CheckParm("-cdrom"))
	{
		I_Printf(D_CDROM);
//c++		mkdir("c:\\doomdata",0);
		strcpy(Globals::g->basedefault,"c:/doomdata/default.cfg");
	}	

	// add any files specified on the command line with -file Globals::g->wadfile
	// to the wad list
	//
	p = M_CheckParm ("-file");
	if (p)
	{
		// the parms after p are Globals::g->wadfile/lump names,
		// until end of parms or another - preceded parm
		Globals::g->modifiedgame = true;            // homebrew levels
		while (++p != Globals::g->myargc && Globals::g->myargv[p][0] != '-')
			D_AddFile(Globals::g->myargv[p]);
	}

	p = M_CheckParm ("-playdemo");

	if (!p)
		p = M_CheckParm ("-timedemo");

	if (p && p < Globals::g->myargc-1)
	{
		sprintf (file,"d:\\%s.lmp", Globals::g->myargv[p+1]);
		D_AddFile (file);
		I_Printf("Playing demo %s.lmp.\n",Globals::g->myargv[p+1]);
	}

	// get skill / episode / map from defaults
	Globals::g->startskill = sk_medium;
	Globals::g->startepisode = 1;
	Globals::g->startmap = 1;
	Globals::g->autostart = false;

	/*if ( DoomLib::matchParms.gameEpisode != GAME_EPISODE_UNKNOWN ) {
		Globals::g->startepisode = DoomLib::matchParms.gameEpisode;
		Globals::g->autostart = 1;
	}

	if ( DoomLib::matchParms.gameMap != -1 ) {
		Globals::g->startmap = DoomLib::matchParms.gameMap;
		Globals::g->autostart = 1;
	}

	if ( DoomLib::matchParms.gameSkill != -1) {
		Globals::g->startskill = (skill_t)DoomLib::matchParms.gameSkill;
	}*/

	// get skill / episode / map from cmdline
	p = M_CheckParm ("-skill");
	if (p && p < Globals::g->myargc-1)
	{
		Globals::g->startskill = (skill_t)(Globals::g->myargv[p+1][0]-'1');
		Globals::g->autostart = true;
	}

	p = M_CheckParm ("-episode");
	if (p && p < Globals::g->myargc-1)
	{
		Globals::g->startepisode = Globals::g->myargv[p+1][0]-'0';
		Globals::g->startmap = 1;
		Globals::g->autostart = true;
	}

	/*p = M_CheckParm ("-timer");
	if (p && p < Globals::g->myargc-1 && Globals::g->deathmatch)
	{*/
	// TODO: Networking
	//const int timeLimit = gameLocal->GetMatchParms().GetTimeLimit();
	const int timeLimit = 0;
	if (timeLimit != 0 && Globals::g->deathmatch)
	{
		int     time;
		//time = atoi(Globals::g->myargv[p+1]);
		time = timeLimit;
		I_Printf("Levels will end after %d minute",time);
		if (time>1)
			I_Printf("s");
		I_Printf(".\n");
	}

	p = M_CheckParm ("-avg");
	if (p && p < Globals::g->myargc-1 && Globals::g->deathmatch)
		I_Printf("Austin Virtual Gaming: Levels will end after 20 minutes\n");

	p = M_CheckParm ("-warp");
	if (p && p < Globals::g->myargc-1)
	{
		if(Globals::g->gamemode == commercial)
			Globals::g->startmap = atoi(Globals::g->myargv[p+1]);
		else
		{
			Globals::g->startepisode = Globals::g->myargv[p+1][0]-'0';
			Globals::g->startmap = Globals::g->myargv[p+2][0]-'0';
		}
		Globals::g->autostart = true;
	}

	I_Printf ("Z_Init: Init zone memory allocation daemon. \n");
	Z_Init ();

	// init subsystems
	I_Printf ("V_Init: allocate Globals::g->screens.\n");
	V_Init ();

	I_Printf ("M_LoadDefaults: Load system defaults.\n");
	M_LoadDefaults ();              // load before initing other systems

	I_Printf ("W_Init: Init WADfiles.\n");
	W_InitMultipleFiles (wadfiles);


	// Check for -file in shareware
	if(Globals::g->modifiedgame)
	{
		// These are the lumps that will be checked in IWAD,
		// if any one is not present, execution will be aborted.
		char name[23][16]=
		{
			"e2m1","e2m2","e2m3","e2m4","e2m5","e2m6","e2m7","e2m8","e2m9",
				"e3m1","e3m3","e3m3","e3m4","e3m5","e3m6","e3m7","e3m8","e3m9",
				"dphoof","bfgga0","heada1","cybra1","spida1d1"
		};
		int i;

		if ( Globals::g->gamemode == shareware)
			I_Error("\nYou cannot -file with the shareware "
			"version. Register!");

		// Check for fake IWAD with right name,
		// but w/o all the lumps of the registered version. 
		if(Globals::g->gamemode == registered)
			for (i = 0;i < 23; i++)
				if (W_CheckNumForName(name[i])<0)
					I_Error("\nThis is not the registered version.");
	}

	// Iff additonal PWAD files are used, print modified banner
	if(Globals::g->modifiedgame)
	{
		/*m*/I_Printf (
		"===========================================================================\n"
			"ATTENTION:  This version of DOOM has been modified.  If you would like to\n"
			"get a copy of the original game, call 1-800-IDGAMES or see the readme file.\n"
			"        You will not receive technical support for modified games.\n"
			"                      press enter to continue\n"
			"===========================================================================\n"
			);
		getchar ();
	}


	// Check and print which version is executed.
	switch ( Globals::g->gamemode )
	{
	case shareware:
	case indetermined:
		I_Printf (
			"===========================================================================\n"
			"                                Shareware!\n"
			"===========================================================================\n"
			);
		break;
	case registered:
	case retail:
	case commercial:
		I_Printf (
			"===========================================================================\n"
			"                 Commercial product - do not distribute!\n"
			"         Please report software piracy to the SPA: 1-800-388-PIR8\n"
			"===========================================================================\n"
			);
		break;

	default:
		// Ouch.
		break;
	}

	I_Printf ("M_Init: Init miscellaneous info.\n");
	M_Init ();

	I_Printf ("R_Init: Init DOOM refresh daemon - ");
	R_Init ();

	I_Printf ("\nP_Init: Init Playloop state.\n");
	P_Init ();

	I_Printf ("I_Init: Setting up machine state.\n");
	I_Init ();

	I_Printf ("D_CheckNetGame: Checking network game status.\n");
	D_CheckNetGame ();
}

bool D_DoomMainPoll(void)
{
	int             p;
	char                    file[256];

	if (D_PollNetworkStart() == false)
		return false;


	I_Printf( "S_Init: Setting up sound.\n" );
	//S_Init( s_volume_sound.GetInteger(), s_volume_midi.GetInteger() );

	I_Printf ("HU_Init: Setting up heads up display.\n");
	HU_Init ();

	I_Printf ("ST_Init: Init status bar.\n");
	ST_Init ();

	// start the apropriate game based on parms
	p = M_CheckParm ("-record");

	if (p && p < Globals::g->myargc-1)
	{
		G_RecordDemo(Globals::g->myargv[p+1]);
		Globals::g->autostart = true;
	}

	p = M_CheckParm ("-playdemo");
	if (p && p < Globals::g->myargc-1)
	{
		//Globals::g->singledemo = true;              // quit after one demo
		G_DeferedPlayDemo(Globals::g->myargv[p+1]);
		//D_DoomLoop ();  // never returns
	}

	p = M_CheckParm ("-timedemo");
	if (p && p < Globals::g->myargc-1)
	{
		G_TimeDemo ("nukage1");//Globals::g->myargv[p+1]);
		D_DoomLoop ();  // never returns
	}

	p = M_CheckParm ("-loadgame");
	if (p && p < Globals::g->myargc-1)
	{
		if (M_CheckParm("-cdrom"))
            sprintf(file, (std::string("c:\\doomdata\\") + SAVEGAMENAME + "%c.dsg").c_str(),Globals::g->myargv[p+1][0]);
		else
			sprintf(file, SAVEGAMENAME"%c.dsg",Globals::g->myargv[p+1][0]);
		G_LoadGame (file);
	}


	if ( Globals::g->gameaction != ga_loadgame && Globals::g->gameaction != ga_playdemo )
	{
		if(Globals::g->autostart || Globals::g->netgame ) {
			G_InitNew(Globals::g->startskill, Globals::g->startepisode, Globals::g->startmap );
		} else if(  Globals::g->gameaction != ga_newgame) {
			D_StartTitle ();                // start up intro loop
		}
	}
	return true;
}


