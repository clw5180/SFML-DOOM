/*

TiMidity -- Experimental MIDI to WAVE converter
Copyright (C) 1995 Tuukka Toivonen <toivonen@clinet.fi>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fstream>

//#include "SDL.h"
#include <timidity/config.h>
#include <timidity/common.h>
#include <timidity/instrum.h>
#include <timidity/playmidi.h>
#include <timidity/readmidi.h>
#include <timidity/output.h>
#include <timidity/controls.h>
#include <timidity/timidity.h>
#include "tables.hpp"


//void *Real_Tim_Malloc( int sz );
//void Real_Tim_Free( void *pt );

void (*s32tobuf)(void *dp,  int32_t *lp,  int32_t c);
int free_instruments_afterwards=0;
static std::string def_instr_name="";

int AUDIO_BUFFER_SIZE;
sample_t *resample_buffer;
 int32_t *common_buffer;

#define MAXWORDS 10

static int read_config_file(const char *name)
{
	std::ifstream  fp;
	char *w[MAXWORDS], *cp;
	ToneBank *bank=0;
	int i, j, k, line=0, words;
	static int rcf_count=0;

	if (rcf_count>50)
	{
        printf(
			"Probable source loop in configuration files");
		return (-1);
	}

    fp.open(name,std::ios::binary);
	if (!fp.good())
		return -2;
	
	char tokTmp[1024];

	while ( fp.getline( tokTmp, 1024 ) )
	{
		
		line++;
		w[words=0]=strtok(tokTmp, " \t\r\n\240");
		if (!w[0] || (*w[0]=='#')) continue;
		while (w[words] && (words < MAXWORDS))
			w[++words]=strtok(0," \t\r\n\240");
		if (!strcmp(w[0], "dir"))
		{
			if (words < 2)
			{
                printf(
					"%s: line %d: No directory given\n", name, line);
				return -3;
			}
			for (i=1; i<words; i++)
				add_to_pathlist(w[i]);
		}
		else if (!strcmp(w[0], "source"))
		{
			if (words < 2)
			{
                printf(
					"%s: line %d: No file name given\n", name, line);
				return -4;
			}
			for (i=1; i<words; i++)
			{
				rcf_count++;
				read_config_file(w[i]);
				rcf_count--;
			}
		}
		else if (!strcmp(w[0], "default"))
		{
			if (words != 2)
			{
                printf(
					"%s: line %d: Must specify exactly one patch name\n",
					name, line);
				return -5;
			}
			def_instr_name = std::string(w[1], 255);
			def_instr_name[255]='\0';
		}
		else if (!strcmp(w[0], "drumset"))
		{
			if (words < 2)
			{
                printf(
					"%s: line %d: No drum set number given\n", 
					name, line);
				return -6;
			}
			i=atoi(w[1]);
			if (i<0 || i>127)
			{
                printf(
					"%s: line %d: Drum set must be between 0 and 127\n",
					name, line);
				return -7;
			}
			if (!drumset[i])
			{
				drumset[i]=(ToneBank*)safe_malloc(sizeof(ToneBank));
				memset(drumset[i], 0, sizeof(ToneBank));
			}
			bank=drumset[i];
		}
		else if (!strcmp(w[0], "bank"))
		{
			if (words < 2)
			{
                printf(
					"%s: line %d: No bank number given\n", 
					name, line);
				return -8;
			}
			i=atoi(w[1]);
			if (i<0 || i>127)
			{
                printf(
					"%s: line %d: Tone bank must be between 0 and 127\n",
					name, line);
				return -9;
			}
			if (!tonebank[i])
			{
				tonebank[i]=(ToneBank*)safe_malloc(sizeof(ToneBank));
				memset(tonebank[i], 0, sizeof(ToneBank));
			}
			bank=tonebank[i];
		}
		else {
			if ((words < 2) || (*w[0] < '0' || *w[0] > '9'))
			{
                printf(
					"%s: line %d: syntax error\n", name, line);
				return -10;
			}
			i=atoi(w[0]);
			if (i<0 || i>127)
			{
                printf(
					"%s: line %d: Program must be between 0 and 127\n",
					name, line);
				return -11;
			}
			if (!bank)
			{
                printf(
					"%s: line %d: Must specify tone bank or drum set "
					"before assignment\n",
					name, line);
				return -12;
			}
			if (bank->tone[i].name)
				Real_Tim_Free(bank->tone[i].name);
			strcpy((bank->tone[i].name=(char*)safe_malloc(strlen(w[1])+1)),w[1]);
			bank->tone[i].note=bank->tone[i].amp=bank->tone[i].pan=
				bank->tone[i].strip_loop=bank->tone[i].strip_envelope=
				bank->tone[i].strip_tail=-1;

			for (j=2; j<words; j++)
			{
				if (!(cp=strchr(w[j], '=')))
				{
                    printf("%s: line %d: bad patch option %s\n",
						name, line, w[j]);
					return -13;
				}
				*cp++=0;
				if (!strcmp(w[j], "amp"))
				{
					k=atoi(cp);
					if ((k<0 || k>MAX_AMPLIFICATION) || (*cp < '0' || *cp > '9'))
					{
                        printf(
							"%s: line %d: amplification must be between "
							"0 and %d\n", name, line, MAX_AMPLIFICATION);
						return -14;
					}
					bank->tone[i].amp=k;
				}
				else if (!strcmp(w[j], "note"))
				{
					k=atoi(cp);
					if ((k<0 || k>127) || (*cp < '0' || *cp > '9'))
					{
                        std::cout << name <<
                            ": line " << line << ": note must be between 0 and 127\n";
						return -15;
					}
					bank->tone[i].note=k;
				}
				else if (!strcmp(w[j], "pan"))
				{
					if (!strcmp(cp, "center"))
						k=64;
					else if (!strcmp(cp, "left"))
						k=0;
					else if (!strcmp(cp, "right"))
						k=127;
					else
						k=((atoi(cp)+100) * 100) / 157;
					if ((k<0 || k>127) ||
						(k==0 && *cp!='-' && (*cp < '0' || *cp > '9')))
					{
                        std::cout << name <<
                            ": line " << line << ": panning must be left, right, " <<
                            "center, or between -100 and 100\n";
						return -16;
					}
					bank->tone[i].pan=k;
				}
				else if (!strcmp(w[j], "keep"))
				{
					if (!strcmp(cp, "env"))
						bank->tone[i].strip_envelope=0;
					else if (!strcmp(cp, "loop"))
						bank->tone[i].strip_loop=0;
					else
					{
                        std::cout << name << 
                            ": line " << line << ": keep must be env or loop\n";
						return -17;
					}
				}
				else if (!strcmp(w[j], "strip"))
				{
					if (!strcmp(cp, "env"))
						bank->tone[i].strip_envelope=1;
					else if (!strcmp(cp, "loop"))
						bank->tone[i].strip_loop=1;
					else if (!strcmp(cp, "tail"))
						bank->tone[i].strip_tail=1;
					else
					{
                        std::cout << name << 
                            ": line " << line << ": strip must be env, loop, or tail\n";
						return -18;
					}
				}
				else
				{
                    std::cout << name << ": line " << line << ": bad patch option " << w[j] << "\n";
					return -19;
				}
			}
		}
	}
	if ( !fp.good() ) //(ferror(fp))
	{
		std::cout <<  "Can't read from " << name << "\n";
        fp.close();
		//return -20;
	}
    fp.close();
	return 0;
}

int Timidity_Init(int rate, int format, int channels, int samples, const char* config)
{
	int ret;
	ret = read_config_file(config);
	if (ret < 0) {
		return(ret);
	}

	/* Set play mode parameters */
	play_mode->rate = rate;
	play_mode->encoding = 0;
	if ( (format&0xFF) == 16 ) {
		play_mode->encoding |= PE_16BIT;
	}
	if ( (format&0x8000) ) {
		play_mode->encoding |= PE_SIGNED;
	}
	if ( channels == 1 ) {
		play_mode->encoding |= PE_MONO;
	} 
	switch (format) {
	case AUDIO_S8:
		s32tobuf = s32tos8;
		break;
	case AUDIO_U8:
		s32tobuf = s32tou8;
		break;
	case AUDIO_S16LSB:
		s32tobuf = s32tos16l;
		break;
	case AUDIO_S16MSB:
		s32tobuf = s32tos16b;
		break;
	case AUDIO_U16LSB:
		s32tobuf = s32tou16l;
		break;
	case AUDIO_U16MSB:
		s32tobuf = s32tou16b;
		break;
	default:
        printf("Unsupported audio format");
		return(-1);
	}
	AUDIO_BUFFER_SIZE = samples;

	/* Allocate memory for mixing (WARNING:  Memory leak!) */
	resample_buffer = (sample_t*)safe_malloc(AUDIO_BUFFER_SIZE*sizeof(sample_t));
	common_buffer = (int32_t*)safe_malloc(AUDIO_BUFFER_SIZE*2*sizeof(int32_t));

	init_tables();

	if (ctl->open(0, 0)) {
        std::cout << "Couldn't open " << ctl->id_name << "\n";
		return(-1);
	}

	if (!control_ratio) {
		control_ratio = play_mode->rate / CONTROLS_PER_SECOND;
		if(control_ratio<1)
			control_ratio=1;
		else if (control_ratio > MAX_CONTROL_RATIO)
			control_ratio=MAX_CONTROL_RATIO;
	}
	if (!def_instr_name.empty())
		set_default_instrument(const_cast<char*>(def_instr_name.c_str()));
	return(0);
}

char timidity_error[TIMIDITY_ERROR_MAX_CHARS] = "";
char *Timidity_Error(void)
{
	return(timidity_error);
}
