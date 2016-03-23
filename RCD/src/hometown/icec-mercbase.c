/* $Id: icec-mercbase.c,v 1.16 2000/02/26 17:01:52 deaz Exp $ */
/*
 * IMC2 - an inter-mud communications protocol
 *
 * icec-mercbase.c: IMC-channel-extensions (ICE) Merc-specific client code
 *
 * Copyright (C) 1999 Peter Keeler <Scion@usa.net> and Gregor Moody <moodyg@erols.com>
 * Copyright (C) 1997 Oliver Jowett <oliver@jowett.manawatu.planet.co.nz>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program (see the file COPYING); if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#if defined(macintosh)
#include <types.h>
#else
#include <sys/types.h>
#include <sys/time.h>
#endif
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

#define IN_IMC

#include "imc.h"
#include "imc-mercbase.h"
#include "icec.h"
#include "icec-mercbase.h"

static ice_channel *saved_channel_list;
#ifdef SMAUG14
char *act_string(const char *format, CHAR_DATA *to, CHAR_DATA *ch,
                 const void *arg1, const void *arg2, int flags);
#elif defined(SMAUG)
char *act_string(const char *format, CHAR_DATA *to, CHAR_DATA *ch,
                 const void *arg1, const void *arg2);
#endif
void icec_save_channels(void)
{
  ice_channel *c;
  FILE *fp;
  char name[MAX_STRING_LENGTH];

  strcpy(name, imc_prefix);
  strcat(name, "icec");

  fp=fopen(name, "w");
  if (!fp)
  {
    imc_logerror("Can't write to %s", name);
    return;
  }
  
  for (c=saved_channel_list; c; c=c->next)
  {
    /* update */
    ice_channel *current=icec_findlchannel(c->local->name);
    if (current)
    {
      imc_strfree(c->name);
      imc_strfree(c->local->format1);
      imc_strfree(c->local->format2);

      c->name=imc_strdup(current->name);
      c->local->format1=imc_strdup(current->local->format1);
      c->local->format2=imc_strdup(current->local->format2);
      c->local->level=current->local->level;
    }

    /* save */
    fprintf(fp,
	    "%s %s %d\n"
	    "%s\n"
	    "%s\n",
	    c->name, c->local->name, c->local->level,
	    c->local->format1,
	    c->local->format2);
  }

  fclose(fp);
}

void icec_load_channels(void)
{
  FILE *fp;
  char name[MAX_STRING_LENGTH];
  char buf1[MAX_STRING_LENGTH];
  char buf2[MAX_STRING_LENGTH];
  char buf3[MAX_STRING_LENGTH];
  char buf4[MAX_STRING_LENGTH];
  int l;

  strcpy(name, imc_prefix);
  strcat(name, "icec");

  fp=fopen(name, "r");
  if (!fp)
  {
    imc_logerror("Can't open %s", name);
    return;
  }

  while (fscanf(fp,
		"%s %s %d\n"
		"%[^\n]\n"
		"%[^\n]\n", buf1, buf2, &l, buf3, buf4) == 5)
  {
    ice_channel *c=imc_malloc(sizeof(*c));

    c->local=imc_malloc(sizeof(*c->local));
    
    c->name=imc_strdup(buf1);
    c->local->name=imc_strdup(buf2);
    c->local->format1=imc_strdup(buf3);
    c->local->format2=imc_strdup(buf4);
    c->local->level=l;
    
    c->next=saved_channel_list;
    saved_channel_list=c;

    imc_logstring("ICEc: configured %s as %s",
		  c->name, c->local->name);
  }

  fclose(fp);
}

ice_channel *icec_findlchannel(const char *name)
{
  ice_channel *c;

  for (c=icec_channel_list; c; c=c->next)
#ifdef CIRCLE
    if (c->local && !str_cmp(c->local->name, (char *)name))
#else
    if (c->local && !str_cmp(c->local->name, name))
#endif
      return c;

  return NULL;
}

void icec_localfree(ice_channel *c)
{
  if (c->local)
  {
    imc_strfree(c->local->name);
    imc_strfree(c->local->format1);
    imc_strfree(c->local->format2);
    imc_free(c->local, sizeof(icec_lchannel));

    c->local=NULL;
  }
}

/* need exactly 2 %s's, and no other format specifiers */
static bool verify_format(const char *fmt)
{
  const char *c;
  int i=0;

  c=fmt;
  while((c=strchr(c, '%'))!=NULL)
  {
    if (*(c+1)=='%')  /* %% */
    {
      c+=2;
      continue;
    }
    
    if (*(c+1)!='s')  /* not %s */
      return FALSE;

    c++;
    i++;
  }

  if (i!=2)
    return FALSE;

  return TRUE;
}

DEFINE_DO_FUN(do_icommand)
{
#ifdef CIRCLE
  skip_spaces(&argument);
#endif

#ifdef CIRCLE
  imc_to_char((char *)icec_command(GET_NAME(ch), argument), ch);
#else
  imc_to_char(icec_command(GET_NAME(ch), argument), ch);
#endif

  imc_to_char("\n\r", ch);
}

DEFINE_DO_FUN(do_isetup)
{
    char imccmd[IMC_NAME_LENGTH];
    char chan[IMC_NAME_LENGTH];
    char arg1[IMC_DATA_LENGTH];
    ice_channel *c;
    const char *a, *a1;
    ice_channel *saved, **last;
    /* flag for its there but might be inactive - shogar 2/22/2000 */
    int found_in_saved=0; 
  
#ifdef CIRCLE
    skip_spaces(&argument);
#endif

    a=imc_getarg(argument, imccmd, IMC_NAME_LENGTH);
    a=a1=imc_getarg(a, chan, IMC_NAME_LENGTH);
    a=imc_getarg(a, arg1, IMC_DATA_LENGTH);

  if (!imccmd[0] || !chan[0])
  {
      imc_to_char("Syntax: isetup <command> <channel> [<data..>]\n\r", ch);
      return;
  }
  if (!strcasecmp(imccmd, "delete"))
  {

      for (last=&saved_channel_list, saved=*last; saved; saved=*last)
      {
          if (!strcasecmp(saved->local->name, chan))
          {
              icec_localfree(saved);
              imc_strfree(saved->name);
              *last=saved->next;
              imc_free(saved, sizeof(*saved));
      /* added break so saved would work in next if - shogar 2/25/200 */
	      found_in_saved=1;
	      break; 
          }
          else
              last=&saved->next;
      }

      c=icec_findchannel(chan);
      if (!c)
      {
          c=icec_findlchannel(chan);

          if (!c)
          {
              if(found_in_saved)
                  imc_to_char("Channel [dead] removed.\n\r", ch);
/* added braces to else so if saved fall thru and save file - shogar 2/25/2000*/
              else
	      {
                  imc_to_char("Unknown channel.\n\r", ch);
              	  return;
	      }
          }
      }

      if(c) icec_localfree(c);

      imc_to_char("Channel is no longer locally configured.\n\r", ch);
      icec_save_channels();
      return;
  }


  c=icec_findchannel(chan);
  if (!c)
  {
      c=icec_findlchannel(chan);

      if (!c)
      {
          imc_to_char("Unknown channel.\n\r", ch);
          return;
      }
  }

  if (!strcasecmp(imccmd, "add"))
  {
      char buf[IMC_DATA_LENGTH];
      ice_channel *newc;

      if (c->local)
      {
          imc_to_char("Channel is already active.\n\r", ch);
          return;
      }

      if (!arg1[0])
      {
          imc_to_char("Syntax: isetup add <channel> <local name>\n\r", ch);
          return;
      }

      c->local=imc_malloc(sizeof(*c->local));
      c->local->name=imc_strdup(arg1);
      c->local->level=LEVEL_IMMORTAL;

      sprintf(buf, "[%s] ~c%%s: ~W%%s~!", arg1);
      c->local->format1=imc_strdup(color_itom(buf));
      sprintf(buf, "[%s] ~W%%s %%s~!", arg1);
      c->local->format2=imc_strdup(color_itom(buf));

      imc_addname(&ICE_LISTEN(ch), c->local->name);

      newc=imc_malloc(sizeof(*newc));
      newc->name=imc_strdup(c->name);
      newc->local=imc_malloc(sizeof(*newc->local));
      newc->local->name=imc_strdup(arg1);
      newc->local->level=LEVEL_IMMORTAL;

      newc->local->format1=imc_strdup(c->local->format1);
      newc->local->format2=imc_strdup(c->local->format2);

      newc->next=saved_channel_list;
      saved_channel_list=newc;

      imc_to_char("Channel added.\n\r", ch);
      icec_save_channels();
      return;
  }

  if (!c->local)
  {
      imc_to_char("Channel is not locally configured, use 'isetup add' first.\n\r", ch);
      return;
  }

  if (!strcasecmp(imccmd, "rename"))
  {
      if (!arg1[0])
      {
          imc_to_char("Syntax: isetup <channel> rename <newname>\n\r", ch);
          return;
      }

      if (icec_findlchannel(arg1))
      {
          imc_to_char("New channel name already exists.\n\r", ch);
          return;
      }

      imc_strfree(c->local->name);
      c->local->name=imc_strdup(arg1);
      imc_to_char("Renamed ok.\n\r", ch);

      icec_save_channels();
      return;
  }

  if (!strcasecmp(imccmd, "format1"))
  {
      if (!a1[0])
      {
          imc_to_char("Syntax: isetup <channel> format1 <string>\n\r", ch);
          return;
      }

      if (!verify_format(a1))
      {
          imc_to_char("Bad format - must contain exactly 2 %s's.\n\r", ch);
          return;
      }

      imc_strfree(c->local->format1);
      c->local->format1=imc_strdup(a1);
      imc_to_char("Format1 changed.\n\r", ch);

      icec_save_channels();
      return;
  }

  if (!strcasecmp(imccmd, "format2"))
  {
      if (!a1[0])
      {
          imc_to_char("Syntax: isetup <channel> format2 <string>\n\r", ch);
          return;
      }

      if (!verify_format(a1))
      {
          imc_to_char("Bad format - must contain exactly 2 %s's.\n\r", ch);
          return;
      }

      imc_strfree(c->local->format2);
      c->local->format2=imc_strdup(a1);
      imc_to_char("Format2 changed.\n\r", ch);

      icec_save_channels();
      return;
  }

  if (!strcasecmp(imccmd, "level"))
  {
      if (!arg1[0])
      {
          imc_to_char("Syntax: isetup <channel> level <level>\n\r", ch);
          return;
      }

      if (atoi(arg1)<=0)
      {
          imc_to_char("Positive level, please.\n\r", ch);
          return;
      }

      c->local->level=atoi(arg1);

      imc_to_char("Level changed.\n\r", ch);
      icec_save_channels();
      return;
  }

  imc_to_char("Unknown command.\n\r"
              "Available commands: add delete rename format1 format2 level.\n\r", ch);
}

DEFINE_DO_FUN(do_ilist)
{
    char buf[MAX_STRING_LENGTH];
    char *polly;
    ice_channel *c;

#ifdef SMAUG
    set_char_color(AT_GREY,ch); /* pager color fix - shogar */
#endif

    if (argument[0])
    {
        c=icec_findlchannel(argument);
        if (!c)
            c=icec_findchannel(argument);

        if (!c)
        {
            imc_to_char("No such channel.\n\r", ch);
            return;
        }
	/* for compress policy - shogar - 1/29/2000 */
	switch(c->policy)
	{
		case ICE_OPEN:
			polly = "~gOpen -- Open to all muds~!";
			break;
		case ICE_COPEN:
			polly = "~GCOpen -- Compressed Open channel~!";
			break;
		case ICE_CLOSED:
			polly = "~RClosed -- Sent to all muds but only invited people can talk or listen~!";
			break;
		case ICE_PRIVATE:
			polly = "~yPrivate -- Only invited people can talk or listen~!";
			break;
		case ICE_CPRIVATE:
			polly = "~YCPrivate -- Compressed Private channel~!";
			break;
		default:
			polly = "~CUnknown -- We don't know what this is~!";
			break;
	}
        sprintf(buf,
                "~WChannel ~!%s:\n\r"
                "~c  Local name: ~!%s\n\r"
                "~c  Format 1  : ~!%s\n\r"
                "~c  Format 2  : ~!%s\n\r"
                "~c  Level     : ~!%d\n\r"
                "\n\r"
                "~c  Policy    : ~!%s\n\r"
                "~c  Owner     : ~!%s\n\r"
                "~c  Operators : ~!%s\n\r"
                "~c  Invited   : ~!%s\n\r"
                "~c  Excluded  : ~!%s\n\r",

                c->name,

                c->local ? c->local->name : "",
                c->local ? c->local->format1 : "",
                c->local ? c->local->format2 : "",
                c->local ? c->local->level : 0,
		polly,
                c->owner,
                c->operators,
                c->invited,
                c->excluded);
        imc_to_char(color_itom(buf), ch);
        return;
    }

    /* added level to the display for ilist with no arguments for 2.00
     - shogar - /1/26/2000 */
    sprintf(buf, "~c%-15s ~C%-15s ~B%-15s ~b%-7s ~!%s\n\r", "Name", "Local name", "Owner",
            "Lvl2Use","Policy");

	/* for compress policy - shogar - 1/29/2000 */
    for (c=icec_channel_list; c; c=c->next)
    {
	switch(c->policy)
	{
		case ICE_OPEN:
			polly = "~gopen~!";
			break;
		case ICE_COPEN:
			polly = "~Gcopen~!";
			break;
		case ICE_CLOSED:
			polly = "~Rclosed~!";
			break;
		case ICE_PRIVATE:
			polly = "~yprivate~!";
			break;
		case ICE_CPRIVATE:
			polly = "~Ycprivate~!";
			break;
		default:
			polly = "~CUnknown~!";
			break;
	}
        sprintf(buf+strlen(buf),
                "~c%-15.15s ~C%-*.*s ~B%-15.15s ~b%7d ~!%s\n\r",
                c->name,
                c->local ? 15 : 17,
                c->local ? 15 : 17,
                c->local ? c->local->name : "~Y(not local)  ",
                c->owner,
                c->local ? c->local->level : 0,
		polly);
    }

    for (c=saved_channel_list; c; c=c->next)
    {
        if (!icec_findchannel(c->name))
        {
            sprintf(buf+strlen(buf),
                    "~c%-15.15s ~C%-15.15s ~B%-15.15s ~!%-7s ~g(inactive)~!\n\r",
                    c->name,
                    c->local ? c->local->name : "~Y(not local)",
                    "", "");
        }
    }

    imc_to_pager(color_itom(buf), ch);
}

DEFINE_DO_FUN(do_ichannels)
{
    char arg[MAX_STRING_LENGTH];

  argument=one_argument(argument, arg);
  if (!arg[0])
  {
    imc_to_char("Currently tuned into:\n\r", ch);
    imc_to_char(ICE_LISTEN(ch), ch);
    imc_to_char("\n\r", ch);
    return;
  }

#ifndef CIRCLE
  smash_tilde(arg);
#endif
  
  if (imc_hasname(ICE_LISTEN(ch), arg))
  {
    imc_removename(&ICE_LISTEN(ch), arg);
    imc_to_char("Removed.\n\r", ch);
  }
  else
  {
    if (!icec_findlchannel(arg))
    {
      imc_to_char("No such channel configured locally.\n\r", ch);
      return;
    }
	
    imc_addname(&ICE_LISTEN(ch), arg);
    imc_to_char("Added.\n\r", ch);
  }
}

void icec_showchannel(ice_channel *c, const char *from, const char *txt, int emote)
{
  DESCRIPTOR_DATA *d, *dnext;
  CHAR_DATA *ch;
  char buf[MAX_STRING_LENGTH];

  if (!c->local)
    return;
  
  sprintf(buf, emote ? c->local->format2 : c->local->format1, from, color_itom(txt));
  strcat( buf, "\n\r" );

  for (d=descriptor_list; d; d=dnext)
  {
    dnext=d->next;

    ch=d->original ? d->original : d->character;

    if (!ch ||
	IS_NPC(ch) ||
	get_trust(ch) < c->local->level ||
	!ice_audible(c, imc_makename(GET_NAME(ch), imc_name)) ||
	!imc_hasname(ICE_LISTEN(ch), c->local->name))
      continue;

    imc_to_char(buf, ch);
  }
}

/* check for icec channels, return TRUE to stop command processing, FALSE otherwise */
#ifdef CIRCLE
bool icec_command_hook(CHAR_DATA *ch, const char *command, char *argument)
#else
bool icec_command_hook(CHAR_DATA *ch, const char *command, char *argument)
#endif
{
    ice_channel *c;
    int emote=0;

    if (IS_NPC(ch))
        return FALSE;

#ifdef CIRCLE
    skip_spaces(&argument);
#endif

    c=icec_findlchannel(command);
    if (!c || !c->local)
        return FALSE;

    if (c->local->level > get_trust(ch))
        return FALSE;

    if (!imc_hasname(ICE_LISTEN(ch), c->local->name))
        return FALSE;

    if (!ice_audible(c, imc_makename(GET_NAME(ch), imc_name)))
    {
        imc_to_char("You cannot use that channel.\n\r", ch);
        return TRUE;
    }

    if( *argument == '\0' )
    {
        imc_to_char( "Use ichan to toggle the channel or provide some text.\n\r", ch );
        return TRUE;
    }

    if( *argument == ',' )
    {
        emote=1;
        argument++;
    }
#ifdef SMAUG
    if(*argument == '@')
    {
        CHAR_DATA *victim;
        SOCIALTYPE *social;
        char cmd[MAX_STRING_LENGTH];
        char arg[MAX_STRING_LENGTH];
        char buf2[MAX_STRING_LENGTH];

        argument++;
        argument=one_argument(argument,cmd);
        if ( (social=find_social(cmd)) == NULL )
        {
            /* Forgot a \n\r here -- Kratas */
            ch_printf(ch,"Couldn't find %s\n\r",cmd);
            return FALSE;
        }
        one_argument( argument, arg );
        CREATE( victim, CHAR_DATA, 1 );
        victim->name=STRALLOC(arg);
        victim->short_descr=victim->name;
        victim->in_room=ch->in_room;
        if ( arg[0] == '\0' && social->others_no_arg)
        {
#ifdef SMAUG14
            strcpy(buf2,(char *) act_string(social->others_no_arg, NULL, ch, NULL, victim,0));
#else
            strcpy(buf2,(char *) act_string(social->others_no_arg, NULL, ch, NULL, victim));
#endif
            buf2[strlen(buf2) - 2]=0;
            argument = buf2;
        }
        /* someday, when i have more time, do something like this, combined with
         rwhois - shogar
         else if ( ( victim = get_char_world( ch, arg ) ) == NULL )
         {
         imc_to_char( "They aren't in the world.\n\r", ch );
        	STRFREE(victim->name);
        	DISPOSE(victim);
		return;
    	}
        */
        else if(victim==ch && social->others_auto)
        {
#ifdef SMAUG14
            strcpy(buf2,(char *) act_string(social->others_no_arg, NULL, ch, NULL, victim,0));
#else
            strcpy(buf2,(char *) act_string(social->others_auto, victim, ch, NULL, victim));
#endif
            buf2[strlen(buf2) - 2]=0;
            argument = buf2;
        }
        else if(social->others_found)
        {
#ifdef SMAUG14
            strcpy(buf2,(char *) act_string(social->others_no_arg, NULL, ch, NULL, victim,0));
#else
            strcpy(buf2,(char *) act_string(social->others_found, victim, ch, NULL, victim));
#endif
            buf2[strlen(buf2) - 2]=0;
            argument = buf2;
        }
        STRFREE(victim->name);
        DISPOSE(victim);
        argument=one_argument( argument, arg );
        icec_sendmessage(c, ch->name, color_mtoi(color_itom(argument)), emote);
        return TRUE;
    }
#endif
    icec_sendmessage(c, GET_NAME(ch), color_mtoi(argument), emote);
    return TRUE;
}

void icec_notify_update(ice_channel *c)
{
  if (!c->local)
  {
    /* saved channel? */
    ice_channel *saved;

    for (saved=saved_channel_list; saved; saved=saved->next)
      if (!strcasecmp(saved->name, c->name))
      {
	c->local=imc_malloc(sizeof(icec_lchannel));
	c->local->name=imc_strdup(saved->local->name);
	c->local->format1=imc_strdup(saved->local->format1);
	c->local->format2=imc_strdup(saved->local->format2);
	c->local->level=saved->local->level;

	return;
      }
  }
}

/* call markfn for every string allocated by ICE */
/* Added by DRUIDS Realm, this is NEEDED for Ack!'s */

void icec_markstrings(void (*markfn)(char *))
{

  ice_channel *  channel;

  markfn(imc_siteinfo.name);
  markfn(imc_siteinfo.host);
  markfn(imc_siteinfo.email);
  markfn(imc_siteinfo.imail);
  markfn(imc_siteinfo.www);
  markfn(imc_siteinfo.details);
  markfn(imc_siteinfo.flags);

  for ( channel= icec_channel_list; channel; channel= channel->next )
  {
    markfn( channel->name );
    markfn( channel->owner );
    markfn( channel->operators );
    markfn( channel->invited );
    markfn( channel->excluded );
    markfn( channel->active );
    if ( channel->local != NULL )
    {
      markfn( channel->local->name );
      markfn( channel->local->format1 );
      markfn( channel->local->format2 );
    }
  }
  for ( channel= saved_channel_list; channel; channel= channel->next )
  {
    markfn( channel->name );
    markfn( channel->owner );
    markfn( channel->operators );
    markfn( channel->invited );
    markfn( channel->excluded );
    markfn( channel->active );
    if ( channel->local != NULL )
    {
      markfn( channel->local->name );
      markfn( channel->local->format1 );
      markfn( channel->local->format2 );
    }
  }
}

