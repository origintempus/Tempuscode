
/************************************************************************
* File: act.intermud.c                                                  *
*                                                                       *
* Intermud interface code to CircleMUD                                  *
*                                                                       *
* All rights reserved.  See /doc/license.doc for more information       *
*                                                                       *
* Copyright (C) 1995, 96 by Chris Austin (Stryker@Tempus)               *
* CircleMUD (C) 1993, 94 by the trustees of Johns Hopkins University.   *
* CircleMUD is based on DikuMUD, which is Copyright (C) 1990, 1991.     *
*                                                                       *
************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/un.h>
#include <netdb.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <netinet/in.h>
#include <unistd.h>
#include "structs.h"
#include "utils.h"
#include "comm.h"
#include "db.h"
#include "interpreter.h"
#include "handler.h"
#include "intermud.h"
#include "screen.h"
#include "utils.h"

/* Structures */

struct bullet {
	int bytes;
} bullet = {
0};

/* External Variables */

extern struct descriptor_data *descriptor_list;
extern int intermud_desc;
extern int connected_to_intermud;

/* Local Variables */

char message[8000];
char message2[8000];
char *Service;
char *To;
char *From;
char *Remote_Mud;
char *Our_Mud;
char *IP_Address;
char *Mud_Port;
char *Text;
char *player;
char *KB_in;
char *KB_out;
char *MSG_in;
char *MSG_out;
char *TTL;
char *Muted;
char *Mud_Type;
char *Mud_Version;
char *Intermud_Version;
char *Services;
char *Info;

/* Function Prototypes */

extern void nonblock(int s);
int mud_recv_data(int fd, char *buf);
int mud_send_data(int fd, char *buf);
void serv_recv_info(char *serv_message);
void serv_recv_mudlistrpy(char *serv_message);
void serv_recv_intertell(char *message);
void serv_recv_intertellrpy(char *serv_message);
void serv_recv_interwhoreq(char *serv_message);
void serv_recv_interwhorpy(char *serv_message);
void serv_recv_interpage(char *serv_message);
void serv_recv_mudinfo(char *serv_message);
void serv_recv_interwiz(char *serv_message);
void serv_recv_stats(char *serv_message);



ACMD(do_mudinfo)
{

	argument = one_argument(argument, arg);

	skip_spaces(&argument);

	if (!*arg)
		send_to_char(ch, "Which mud do you want information on?\r\n");
	else {
		sprintf(message, "2050|%s|%s|", arg, GET_NAME(ch));

		strcat(message, "\0");

		mud_send_data(intermud_desc, message);
	}

}


ACMD(do_mudlist)
{

	sprintf(message, "1040|%s|", GET_NAME(ch));

	strcat(message, "\0");

	mud_send_data(intermud_desc, message);
}


ACMD(do_interpage)
{
	char *to;
	char *mud;

	argument = one_argument(argument, arg);

	skip_spaces(&argument);

	if (!*arg)
		send_to_char(ch, "Who do you want to interpage?\r\n");
	else {
		to = strtok(arg, "@");
		mud = strtok(NULL, " ");
		if (to == NULL || mud == NULL) {
			send_to_char(ch, "Must be in User@Mud format.\r\n");
			return;
		}

		sprintf(message, "\007\007*%s@%s* %s\r\n", to, mud, argument);

		send_to_char(ch, message);

		sprintf(message, "2040|%s|%s|%s|%s|%s|", to, mud, GET_NAME(ch),
			MUDNAME, (argument ? argument : "NONE"));

		strcat(message, "\0");

		mud_send_data(intermud_desc, message);
	}
}

ACMD(do_interwho)
{

	skip_spaces(&argument);

	if (!*argument)
		send_to_char(ch, "Which mud did you wish to get a who listing from?\r\n");
	else {
		sprintf(message, "2020|%s|%s|%s|", argument, GET_NAME(ch), MUDNAME);

		strcat(message, "\0");

		send_to_char(ch, "Request sent, but it might take a minute.\r\n");

		mud_send_data(intermud_desc, message);
	}
}

ACMD(do_intertel)
{
	char *to;
	char *mud;

	argument = one_argument(argument, arg);

	if (!*arg)
		send_to_char(ch, "Who do you want to intertell to?\r\n");
	else if (!*argument)
		send_to_char(ch, "And what do you want to tell them?\r\n");
	else {
		to = strtok(arg, "@");
		mud = strtok(NULL, " ");
		if (to == NULL || mud == NULL) {
			send_to_char(ch, "Must be in User@Mud format.\r\n");
			return;
		}

		skip_spaces(&argument);

		sprintf(message, "2000|%s|%s|%s|%s|%s|", to, mud, GET_NAME(ch),
			MUDNAME, argument);

		strcat(message, "\0");

		mud_send_data(intermud_desc, message);

		sprintf(message, "You tell %s@%s, '%s'\r\n", to, mud, argument);
		send_to_char(ch, CCRED(ch, C_NRM));
		send_to_char(ch, message);
		send_to_char(ch, CCNRM(ch, C_NRM));
	}
}


ACMD(do_interwiz)
{
	struct descriptor_data *d;

	skip_spaces(&argument);

	if (true) {
		send_to_char(ch, "You are currently deaf to interwiz channel!\r\n");
		return;
	}

	if (!*argument)
		send_to_char(ch, "What do you want to say to the network?\r\n");
	else if (connected_to_intermud == 0)
		send_to_char(ch, 
			"Currently not connected to intermud server, try again later.\r\b");
	else {

		sprintf(message, "3000|%s|%s|%s|", GET_NAME(ch), MUDNAME, argument);

		strcat(message, "\0");

		mud_send_data(intermud_desc, message);

		if (argument[0] == '*')
			sprintf(message, "%s@%s <--- %s\r\n", GET_NAME(ch), MUDNAME,
				argument + 1);
		else
			sprintf(message, "%s@%s: %s\r\n", GET_NAME(ch), MUDNAME, argument);

		for (d = descriptor_list; d; d = d->next) {
			if (IS_PLAYING(d) && d->creature &&
				(false) &&
				(!PLR_FLAGGED(d->creature,
						PLR_WRITING | PLR_MAILING | PLR_OLC))
				&& (GET_LEVEL(d->creature) >= LVL_AMBASSADOR)) {
				send_to_char(d->creature, CCMAG(d->creature, C_NRM));
				send_to_char(d->creature, message);
				send_to_char(d->creature, CCNRM(d->creature, C_NRM));
			}
		}
	}
}


ACMD(do_intermud)
{
	struct sockaddr_un serv_addr;
	int servlen;

	argument = one_argument(argument, arg);

	if (!*arg) {
		send_to_char(ch, "intermud usage :-\r\n\r\n"
			"    connect    : Will reconnect the mud to the intermud server\r\n"
			"    disconnect : Will drop the link to the intermud server\r\n"
			"    purge      : Will force the intermud server to purge it's DNS table\r\n"
			"    reget      : Will force intermud server to request a mudlist from bootmaster\r\n"
			"    stats      : Will return some statistics from the intermud server\r\n"
			"    debug      : Will toggle intermud debug messages on and off\r\n"
			"    mute <mud> : Will prevent incoming packets from a mud\r\n\r\n");
	} else {
		if (str_cmp(arg, "connect") == 0) {
			if (connected_to_intermud == 1)
				send_to_char(ch, "Already connected to intermud server.\r\n");
			else {
				bzero((char *)&serv_addr, sizeof(serv_addr));
				serv_addr.sun_family = AF_UNIX;
				strcpy(serv_addr.sun_path, MUDSOCK_PATH);
				servlen =
					sizeof(serv_addr.sun_family) + strlen(serv_addr.sun_path);

				if ((intermud_desc = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
					perror("Can't open intermud socket");
					safe_exit(1);
				}

				if (connect(intermud_desc, (struct sockaddr *)&serv_addr,
						servlen) == -1) {
					mudlog(LVL_DEMI, BRF, true,
						"WARNING: Could not connect to intermud server.");
					connected_to_intermud = 0;
				} else {
					mudlog(LVL_DEMI, BRF, true,
						"Established link to intermud server");
					connected_to_intermud = 1;
				}
			}
			return;
		} else if (str_cmp(arg, "disconnect") == 0) {
			if (connected_to_intermud == 0)
				send_to_char(ch, "Already disconnected from intermud server.\r\n");
			else {
				mudlog(LVL_DEMI, BRF, true,
					"WARNING: %s forced intermud server disconnect.",
					GET_NAME(ch));
				close(intermud_desc);
				connected_to_intermud = 0;
				return;
			}
		} else if (str_cmp(arg, "purge") == 0) {
			if (connected_to_intermud == 0)
				send_to_char(ch, "Not connected to intermud server.\r\n");
			else {
				sprintf(buf, "4000|%s|", GET_NAME(ch));
				mud_send_data(intermud_desc, buf);
				mudlog(LVL_DEMI, BRF, true,
					"WARNING: %s forced a mudlist purge.", GET_NAME(ch));
			}
			return;
		} else if (str_cmp(arg, "reget") == 0) {
			if (connected_to_intermud == 0)
				send_to_char(ch, "Not connected to intermud server.\r\n");
			else {
				sprintf(buf, "4020|%s|", GET_NAME(ch));
				mud_send_data(intermud_desc, buf);
				mudlog(LVL_DEMI, BRF, true,
					"WARNING: %s issued mudlist request from bootmaster",
					GET_NAME(ch));
			}
			return;
		} else if (str_cmp(arg, "stats") == 0) {
			if (connected_to_intermud == 0)
				send_to_char(ch, "Not connected to intermud server.\r\n");
			else {
				sprintf(buf, "4040|%s|", GET_NAME(ch));
				mud_send_data(intermud_desc, buf);

			}
			return;
		} else if (str_cmp(arg, "mute") == 0) {
			if (connected_to_intermud == 0)
				send_to_char(ch, "Not connected to intermud server.\r\n");
			else {
				if (!*argument)
					send_to_char(ch, 
						"You must specifiy a mud name to mute/unmute\r\n");
				else {
					skip_spaces(&argument);
					sprintf(buf, "4060|%s|", argument);
					mud_send_data(intermud_desc, buf);
					mudlog(LVL_DEMI, BRF, true,
						"WARNING: %s toggled mute for %s",
						GET_NAME(ch), argument);
				}
			}
			return;
		} else if (str_cmp(arg, "debug") == 0) {
			if (connected_to_intermud == 0)
				send_to_char(ch, "Not connected to intermud server.\r\n");
			else {
				sprintf(buf, "4070|%s|", GET_NAME(ch));
				mud_send_data(intermud_desc, buf);
				mudlog(LVL_DEMI, BRF, true,
					"INFO: %s toggled intermud debugging.\r\n",
					GET_NAME(ch));
			}
		}
	}
}


void
incoming_intermud_message(int intermud_desc)
{
	int numbytes;
	char msgnum_t[5];
	int msgnum;

	numbytes = mud_recv_data(intermud_desc, message);

	if (numbytes <= 0) {
		mudlog(LVL_DEMI, BRF, true,
			"WARNING: Link dropped to intermud server, use connect to re-establish");
		close(intermud_desc);
		connected_to_intermud = 0;
	} else
		message[numbytes] = '\0';

	strncpy(msgnum_t, message, 4);
	msgnum_t[4] = '\0';
	msgnum = atoi(msgnum_t);

	switch (msgnum) {
	case 1100:
		serv_recv_info(message);
		break;
	case 1050:
		serv_recv_mudlistrpy(message);
		break;
	case 2000:
		serv_recv_intertell(message);
		break;
	case 2010:
		serv_recv_intertellrpy(message);
		break;
	case 2020:
		serv_recv_interwhoreq(message);
		break;
	case 2030:
		serv_recv_interwhorpy(message);
		break;
	case 2040:
		serv_recv_interpage(message);
		break;
	case 2055:
		serv_recv_mudinfo(message);
		break;
	case 3000:
		serv_recv_interwiz(message);
		break;
	case 4050:
		serv_recv_stats(message);
		break;
	default:
		fprintf(stderr,
			"ERROR: Unknown message type (%d) received from server.\n",
			msgnum);
	}
}


void
serv_recv_info(char *serv_message)
{
	Service = strtok(serv_message, "|");
	Info = strtok(NULL, "|");

	mudlog(LVL_DEMI, BRF, true, Info);
}

void
serv_recv_mudlistrpy(char *serv_message)
{
	struct descriptor_data *d;

	Service = strtok(serv_message, "|");
	To = strtok(NULL, "|");
	Remote_Mud = strtok(NULL, "|");

	strcpy(message2, "MUD's currently connected to the network\r\n"
		"---------------------------------------------------------\r\n\r\n");
	for (d = descriptor_list; d; d = d->next)
		if (IS_PLAYING(d) && (str_cmp(To, GET_NAME(d->creature)) == 0)) {
			send_to_char(d->creature, message2);
			while (Remote_Mud != NULL) {
				Mud_Port = strtok(NULL, "|");
				IP_Address = strtok(NULL, "|");
				Muted = strtok(NULL, "|");
				sprintf(message2, "%-30s  %-15s  %-4s %s\r\n", Remote_Mud,
					IP_Address, Mud_Port, Muted);
				send_to_char(d->creature, message2);
				Remote_Mud = strtok(NULL, "|");
			}
			break;
		}
}


void
serv_recv_intertell(char *serv_message)
{
	struct descriptor_data *d;
	int player_found = 0;

	Service = strtok(serv_message, "|");
	To = strtok(NULL, "|");
	Our_Mud = strtok(NULL, "|");
	From = strtok(NULL, "|");
	Remote_Mud = strtok(NULL, "|");
	Text = strtok(NULL, "|");

	sprintf(message2, "%s@%s tells you, '%s'\r\n", From, Remote_Mud, Text);

	for (d = descriptor_list; d; d = d->next)
		if (IS_PLAYING(d) && d->creature &&
			(str_cmp(To, GET_NAME(d->creature)) == 0)) {
			if (GET_INVIS_LVL(d->creature) == GET_LEVEL(d->creature))
				continue;
			if (PLR_FLAGGED(d->creature, PLR_WRITING | PLR_MAILING | PLR_OLC))
				player_found = 1;
			else {
				send_to_char(d->creature, CCRED(d->creature, C_NRM));
				send_to_char(d->creature, message2);
				send_to_char(d->creature, CCNRM(d->creature, C_NRM));
				player_found = 2;
			}
			break;
		}

	if (player_found == 0) {
		sprintf(message2, "2010|%s|%s|Server|%s|%s is not currently on-line.",
			From, Remote_Mud, MUDNAME, To);
		mud_send_data(intermud_desc, message2);
	} else if (player_found == 1) {
		sprintf(message2,
			"2010|%s|%s|Server|%s|%s is currently mailing or writing", From,
			Remote_Mud, MUDNAME, To);
		mud_send_data(intermud_desc, message2);
	} else {
		sprintf(message2,
			"2010|%s|%s|Server|%s|Your message has been delivered", From,
			Remote_Mud, MUDNAME);
		mud_send_data(intermud_desc, message2);
	}
}


void
serv_recv_intertellrpy(char *serv_message)
{
	struct descriptor_data *d;
	int player_found = 0;

	Service = strtok(serv_message, "|");
	To = strtok(NULL, "|");
	Our_Mud = strtok(NULL, "|");
	From = strtok(NULL, "|");
	Remote_Mud = strtok(NULL, "|");
	Text = strtok(NULL, "|");

	sprintf(message2, "%s@%s tells you, '%s'\r\n", From, Remote_Mud, Text);

	for (d = descriptor_list; d; d = d->next)
		if (IS_PLAYING(d) && d->creature &&
			(str_cmp(To, GET_NAME(d->creature)) == 0)) {
			if (PLR_FLAGGED(d->creature, PLR_WRITING | PLR_MAILING | PLR_OLC))
				player_found = 1;
			else {
				send_to_char(d->creature, CCRED(d->creature, C_NRM));
				send_to_char(d->creature, message2);
				send_to_char(d->creature, CCNRM(d->creature, C_NRM));
				player_found = 2;
			}
			break;
		}
}


void
serv_recv_interwhoreq(char *serv_message)
{
	struct descriptor_data *d;
	struct Creature *ch = NULL;

	Service = strtok(serv_message, "|");
	Our_Mud = strtok(NULL, "|");
	From = strtok(NULL, "|");
	Remote_Mud = strtok(NULL, "|");

	sprintf(message2, "2030|%s|%s|%s|", Remote_Mud, From, MUDNAME);

	if (str_cmp(Remote_Mud, MUDNAME) == 0)
		for (d = descriptor_list; d; d = d->next)
			if (d->creature && str_cmp(GET_NAME(d->creature), From) == 0) {
				ch = d->creature;
				break;
			}

	for (d = descriptor_list; d; d = d->next)
		if (IS_PLAYING(d) && d->creature &&
			GET_LEVEL(d->creature) >= LVL_AMBASSADOR &&
			(!ch || GET_INVIS_LVL(d->creature) <= GET_LEVEL(ch))) {
			sprintf(message2, "%s[%7s] %s %s", message2,
				GET_LEVEL(d->creature) < LVL_AMBASSADOR ? " MORTAL " :
				level_abbrevs[(int)GET_LEVEL(d->creature) - LVL_AMBASSADOR],
				GET_NAME(d->creature), GET_TITLE(d->creature));
			if (PLR_FLAGGED(d->creature, PLR_WRITING))
				strcat(message2, " (writing)");
			else if (PLR_FLAGGED(d->creature, PLR_MAILING))
				strcat(message2, " (mailing)");
			else if (PLR_FLAGGED(d->creature, PLR_OLC))
				strcat(message2, " (creating)");
			if (true)
				strcat(message2, " (nointwiz)");
			else if (PLR_FLAGGED(d->creature, PLR_AFK))
				strcat(message2, " (afk)");
			strcat(message2, "|");
		}

	strcat(message2, "\0");

	mud_send_data(intermud_desc, message2);
}


void
serv_recv_interwhorpy(char *serv_message)
{
	struct descriptor_data *d;

	Service = strtok(serv_message, "|");
	Our_Mud = strtok(NULL, "|");
	To = strtok(NULL, "|");
	Remote_Mud = strtok(NULL, "|");

	for (d = descriptor_list; d; d = d->next)
		if (IS_PLAYING(d) && (str_cmp(To, GET_NAME(d->creature)) == 0) &&
			(!PLR_FLAGGED(d->creature,
					PLR_WRITING | PLR_MAILING | PLR_OLC))) {

			sprintf(message2, "Players on-line at %s\r\n"
				"----------------------------------\r\n\r\n", Remote_Mud);
			send_to_char(d->creature, message2);

			do {
				player = strtok(NULL, "|");
				send_to_char(d->creature, player);
				send_to_char(d->creature, "\r\n");
			}
			while (player != NULL);
		}
}


void
serv_recv_interpage(char *serv_message)
{
	struct descriptor_data *d;
	int player_found = 0;
	struct Creature *ch = NULL;

	Service = strtok(serv_message, "|");
	To = strtok(NULL, "|");
	Our_Mud = strtok(NULL, "|");
	From = strtok(NULL, "|");
	Remote_Mud = strtok(NULL, "|");
	Text = strtok(NULL, "|");

	if (str_cmp(Remote_Mud, MUDNAME) == 0)
		for (d = descriptor_list; d; d = d->next)
			if (d->creature && str_cmp(GET_NAME(d->creature), From) == 0) {
				ch = d->creature;
				break;
			}

	sprintf(message2, "\007\007*%s@%s* %s\r\n", From, Remote_Mud, Text);

	for (d = descriptor_list; d; d = d->next)
		if (IS_PLAYING(d) && (str_cmp(To, GET_NAME(d->creature)) == 0)) {
			if (GET_INVIS_LVL(d->creature) == GET_LEVEL(d->creature) ||
				(ch && GET_INVIS_LVL(d->creature) > GET_LEVEL(ch)))
				continue;
			if (PLR_FLAGGED(d->creature, PLR_WRITING | PLR_MAILING | PLR_OLC))
				player_found = 1;
			else {
				send_to_char(d->creature, message2);
				player_found = 2;
			}
			break;
		}

	if (player_found == 0) {
		sprintf(message2, "2010|%s|%s|Server|%s|%s is not currently on-line.",
			From, Remote_Mud, MUDNAME, To);
		mud_send_data(intermud_desc, message2);
	}
	if (player_found == 1) {
		sprintf(message2,
			"2010|%s|%s|Server|%s|%s is currently mailing or writing", From,
			Remote_Mud, MUDNAME, To);
		mud_send_data(intermud_desc, message2);
	}
}


void
serv_recv_mudinfo(char *serv_message)
{
	struct descriptor_data *d;
	char Services_Buf[100];
	char Mud[15];
	int i;

	Service = strtok(serv_message, "|");
	To = strtok(NULL, "|");
	Remote_Mud = strtok(NULL, "|");
	Mud_Type = strtok(NULL, "|");
	Mud_Version = strtok(NULL, "|");
	Intermud_Version = strtok(NULL, "|");
	Services = strtok(NULL, "|");

	if (strcmp(Mud_Type, "1") == 0)
		strcpy(Mud, "Circle");
	else if (strcmp(Mud_Type, "2") == 0)
		strcpy(Mud, "Merc");
	else if (strcmp(Mud_Type, "3") == 0)
		strcpy(Mud, "Envy");
	else
		strcpy(Mud, "Other");

	strcpy(Services_Buf, " ");

	for (i = 0; i <= 4; i++, Services++) {
		if (i == 0 && *Services == '1')
			strcat(Services_Buf, "INTERWIZ ");
		if (i == 1 && *Services == '1')
			strcat(Services_Buf, "INTERTELL ");
		if (i == 2 && *Services == '1')
			strcat(Services_Buf, "INTERPAGE ");
		if (i == 3 && *Services == '1')
			strcat(Services_Buf, "INTERWHO ");
		if (i == 4 && *Services == '1')
			strcat(Services_Buf, "INTERBOARD ");
	}

	for (d = descriptor_list; d; d = d->next)
		if (IS_PLAYING(d) && (str_cmp(To, GET_NAME(d->creature)) == 0)) {
			sprintf(message2, "MUD: %s\r\n\r\n"
				"Intermud Version: %s\r\n\r\n"
				"Mud Type: %s         Version: %s\r\n\r\n"
				"Subscribed Services:\r\n\r\n  %s\r\n\r\n"
				"Subscribed Channels: Not Implemented\r\n\r\n"
				"Subscribed Boards: Not Implemented\r\n\r\n",
				Remote_Mud, Intermud_Version, Mud, Mud_Version, Services_Buf);

			send_to_char(d->creature, message2);
			break;
		}
}


void
serv_recv_stats(char *serv_message)
{
	struct descriptor_data *d;

	Service = strtok(serv_message, "|");
	To = strtok(NULL, "|");
	Remote_Mud = strtok(NULL, "|");

	strcpy(message2,
		"Mud Name            Bytes in   Bytes out   MSG in   MSG out   TTL   Muted\r\n"
		"-------------------------------------------------------------------------\r\n\r\n");

	for (d = descriptor_list; d; d = d->next)
		if (IS_PLAYING(d) && (str_cmp(To, GET_NAME(d->creature)) == 0)) {
			send_to_char(d->creature, message2);
			while (Remote_Mud != NULL) {
				KB_in = strtok(NULL, "|");
				KB_out = strtok(NULL, "|");
				MSG_in = strtok(NULL, "|");
				MSG_out = strtok(NULL, "|");
				TTL = strtok(NULL, "|");
				Muted = strtok(NULL, "|");
				sprintf(message2,
					"%-20s%-5s      %-5s         %-5s    %-5s    %s      %s\r\n",
					Remote_Mud, KB_in, KB_out, MSG_in, MSG_out, TTL, Muted);
				send_to_char(d->creature, message2);
				Remote_Mud = strtok(NULL, "|");
			}
			break;
		}
}


void
serv_recv_interwiz(char *serv_message)
{
	struct descriptor_data *d;

	Service = strtok(serv_message, "|");
	From = strtok(NULL, "|");
	Remote_Mud = strtok(NULL, "|");
	Text = strtok(NULL, "|");


	if (!Text)
		return;

	if (Text[0] == '*')
		sprintf(message2, "%s@%s <--- %s\r\n", From, Remote_Mud, Text + 1);
	else
		sprintf(message2, "%s@%s: %s\r\n", From, Remote_Mud, Text);

	for (d = descriptor_list; d; d = d->next)
		if (IS_PLAYING(d) && (false) &&
			(!PLR_FLAGGED(d->creature, PLR_WRITING | PLR_MAILING | PLR_OLC))
			&& (GET_LEVEL(d->creature) >= LVL_AMBASSADOR)) {
			send_to_char(d->creature, CCMAG(d->creature, C_NRM));
			send_to_char(d->creature, message2);
			send_to_char(d->creature, CCNRM(d->creature, C_NRM));
		}
}


void
init_intermud_socket(void)
{
	struct sockaddr_un serv_addr;
	int servlen;

	bzero((char *)&serv_addr, sizeof(serv_addr));
	serv_addr.sun_family = AF_UNIX;
	strcpy(serv_addr.sun_path, MUDSOCK_PATH);
	servlen = sizeof(serv_addr.sun_family) + strlen(serv_addr.sun_path);

	if ((intermud_desc = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
		perror("Can't open intermud socket");
		safe_exit(1);
	}

	if (connect(intermud_desc, (struct sockaddr *)&serv_addr, servlen) == -1) {
		fprintf(stderr,
			"WARNING: Intermud server is unavailable, continuing, ERRNO = %d\n",
			errno);
		connected_to_intermud = 0;
	} else {
		fprintf(stderr, "Connected to intermud server OK.\n");
		connected_to_intermud = 1;
	}
}

int
mud_recv_data(int fd, char *buf)
{
	int buflen;
	int cc;

	cc = recv(fd, &bullet, sizeof(struct bullet), 0);
	if (cc <= 0)
		return (cc);
	else {
		buflen = bullet.bytes;
		while (buflen > 0) {
			cc = recv(fd, buf, buflen, 0);
			if (cc <= 0) {
				fprintf(stderr, "ERROR: mud_recv_data, fatal\n");
				safe_exit(1);
			}
			buf += cc;
			buflen -= cc;
		}
		return (bullet.bytes);
	}
}

int
mud_send_data(int fd, char *buf)
{
	int buflen;
	int cc;

	if (strlen(buf) <= 0) {
		fprintf(stderr,
			"ERROR: mud_send_data requested to send 0 bytes, exiting\n");
		safe_exit(0);
	}

	bullet.bytes = strlen(buf);

	cc = send(fd, &bullet, sizeof(struct bullet), 0);
	if (cc <= 0)
		return (cc);
	else {
		buflen = bullet.bytes;
		while (buflen > 0) {
			cc = send(fd, buf, buflen, 0);
			if (cc <= 0) {
				fprintf(stderr, "ERROR: mud_send_data, fatal\n");
			}
			buf += cc;
			buflen -= cc;
		}
		return (bullet.bytes);
	}
}