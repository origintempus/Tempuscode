/* ************************************************************************
*   File: act.social.c                                  Part of CircleMUD *
*  Usage: Functions to handle socials                                     *
*                                                                         *
*  All rights reserved.  See license.doc for complete information.        *
*                                                                         *
*  Copyright (C) 1993, 94 by the Trustees of the Johns Hopkins University *
*  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
************************************************************************ */

//
// File: act.social.c                      -- Part of TempusMUD
//
// All modifications and additions are
// Copyright 1998 by John Watson, all rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "structs.h"
#include "utils.h"
#include "comm.h"
#include "interpreter.h"
#include "handler.h"
#include "db.h"
#include "spells.h"

/* extern variables */
extern struct room_data *world;
extern struct descriptor_data *descriptor_list;
extern struct room_data *world;

/* extern functions */
char *fread_action(FILE * fl, int nr);

/* local globals */
static int list_top = -1;

struct social_messg {
	int act_nr;
	int hide;
	int min_victim_position;	/* Position of victim */

	/* No argument was supplied */
	char *char_no_arg;
	char *others_no_arg;

	/* An argument was there, and a victim was found */
	char *char_found;			/* if NULL, read no further, ignore args */
	char *others_found;
	char *vict_found;

	/* An argument was there, but no victim was found */
	char *not_found;

	/* The victim turned out to be the character */
	char *char_auto;
	char *others_auto;
} *soc_mess_list = NULL;



int
find_action(int cmd)
{
	int bot, top, mid;

	bot = 0;
	top = list_top;

	if (top < 0)
		return (-1);

	for (;;) {
		mid = (bot + top) >> 1;

		if (soc_mess_list[mid].act_nr == cmd)
			return (mid);
		if (bot >= top)
			return (-1);

		if (soc_mess_list[mid].act_nr > cmd)
			top = --mid;
		else
			bot = ++mid;
	}
}

ACMD(do_mood)
{
	skip_spaces(&argument);
	GET_MOOD(ch) = cmd_info[cmd].command;
	command_interpreter(ch, argument);
	GET_MOOD(ch) = NULL;
}

ACMD(do_action)
{
	int act_nr;
	struct social_messg *action;
	struct Creature *vict = NULL;
	struct obj_data *obj = NULL;

	/*struct obj_data *weap = GET_EQ(ch, WEAR_WIELD); */

	if ((act_nr = find_action(cmd)) < 0) {
		send_to_char(ch, "That action is not supported.\r\n");
		return;
	}
	action = &soc_mess_list[act_nr];

	if (action->char_found) {
		one_argument(argument, buf);
	} else {
		*buf = '\0';
	}
	if (!*buf) {
		send_to_char(ch, action->char_no_arg);
		send_to_char(ch, "\r\n");
		act(action->others_no_arg, action->hide, ch, 0, 0, TO_ROOM);
		return;
	}
	if (!(vict = get_char_room_vis(ch, buf))) {
		if (!(obj = get_obj_in_list_vis(ch, buf, ch->in_room->contents)) &&
			!(obj = get_obj_in_list_vis(ch, buf, ch->carrying))) {
			act(action->not_found, action->hide, ch, 0, vict, TO_CHAR);
			return;
		} else {
			// Convert messages from creature-oriented to object-oriented
			// messages (no pun intended).  This is seriously hacky, but
			// it sure beats reworking act().  Someday later, maybe...
			char *char_found_msg, *others_found_msg;

			char_found_msg = action->char_found;
			others_found_msg = action->others_found;

			char_found_msg = tmp_gsub(action->char_found, "$N", "$p");
			char_found_msg = tmp_gsub(char_found_msg, "$M", "it");
			char_found_msg = tmp_gsub(char_found_msg, "$S", "its");
			char_found_msg = tmp_gsub(char_found_msg, "$E", "it");

			others_found_msg = tmp_gsub(action->others_found, "$N", "$p");
			others_found_msg = tmp_gsub(others_found_msg, "$M", "it");
			others_found_msg = tmp_gsub(others_found_msg, "$S", "its");
			others_found_msg = tmp_gsub(others_found_msg, "$E", "it");

			act(char_found_msg, 0, ch, obj, 0, TO_CHAR | TO_SLEEP);
			act(others_found_msg, action->hide, ch, obj, 0, TO_ROOM);
			return;
		}
	}
	if (vict == ch) {
		send_to_char(ch, action->char_auto);
		send_to_char(ch, "\r\n");
		act(action->others_auto, action->hide, ch, 0, 0, TO_ROOM);
	} else {
		if (vict->getPosition() < action->min_victim_position)
			act("$N is not in a proper position for that.",
				FALSE, ch, 0, vict, TO_CHAR | TO_SLEEP);
		else {
			act(action->char_found, 0, ch, 0, vict, TO_CHAR | TO_SLEEP);
			act(action->others_found, action->hide, ch, 0, vict, TO_NOTVICT);
			act(action->vict_found, action->hide, ch, 0, vict, TO_VICT);
		}
	}
}

ACMD(do_point)
{
	struct Creature *vict = NULL;
	struct obj_data *obj = NULL;
	int dir, i;

	skip_spaces(&argument);
	if (!*argument) {
		send_to_char(ch, "You point whereto?\r\n");
		act("$n points in all directions, seemingly confused.",
			TRUE, ch, 0, 0, TO_ROOM);
		return;
	}

	if ((dir = search_block(argument, dirs, FALSE)) >= 0) {
		sprintf(buf, "$n points %sward.", dirs[dir]);
		act(buf, TRUE, ch, 0, 0, TO_ROOM);
		sprintf(buf, "You point %sward.", dirs[dir]);
		act(buf, FALSE, ch, 0, 0, TO_CHAR);
		return;
	}

	if ((vict = get_char_room_vis(ch, argument))) {
		if (vict == ch) {
			send_to_char(ch, "You point at yourself.\r\n");
			act("$n points at $mself.", TRUE, ch, 0, 0, TO_ROOM);
			return;
		}
		act("You point at $M.", FALSE, ch, 0, vict, TO_CHAR);
		act("$n points at $N.", TRUE, ch, 0, vict, TO_NOTVICT);
		act("$n points at you.", TRUE, ch, 0, vict, TO_VICT);
		return;
	}

	if ((obj = get_object_in_equip_vis(ch, argument, ch->equipment, &i)) ||
		(obj = get_obj_in_list_vis(ch, argument, ch->carrying)) ||
		(obj = get_obj_in_list_vis(ch, argument, ch->in_room->contents))) {
		act("You point at $p.", FALSE, ch, obj, 0, TO_CHAR);
		act("$n points at $p.", TRUE, ch, obj, 0, TO_ROOM);
		return;
	}

	send_to_char(ch, "You don't see any '%s' here.\r\n", argument);

}


ACMD(do_insult)
{
	struct Creature *victim;

	one_argument(argument, arg);

	if (*arg) {
		if (!(victim = get_char_room_vis(ch, arg)))
			send_to_char(ch, "Can't hear you!\r\n");
		else {
			if (victim != ch) {
				send_to_char(ch, "You insult %s.\r\n", GET_NAME(victim));

				switch (number(0, 2)) {
				case 0:
					if (GET_SEX(ch) == SEX_MALE) {
						if (GET_SEX(victim) == SEX_MALE)
							act("$n accuses you of fighting like a woman!",
								FALSE, ch, 0, victim, TO_VICT);
						else
							act("$n says that women can't fight.", FALSE, ch,
								0, victim, TO_VICT);
					} else {	/* Ch == Woman */
						if (GET_SEX(victim) == SEX_MALE)
							act("$n accuses you of having the smallest... (brain?)", FALSE, ch, 0, victim, TO_VICT);
						else
							act("$n tells you that you'd lose a beauty contest against a troll.", FALSE, ch, 0, victim, TO_VICT);
					}
					break;
				case 1:
					act("$n calls your mother a bitch!", FALSE, ch, 0, victim,
						TO_VICT);
					break;
				default:
					act("$n tells you to get lost!", FALSE, ch, 0, victim,
						TO_VICT);
					break;
				}				/* end switch */

				act("$n insults $N.", TRUE, ch, 0, victim, TO_NOTVICT);
			} else {			/* ch == victim */
				send_to_char(ch, "You feel insulted.\r\n");
			}
		}
	} else
		send_to_char(ch, "I'm sure you don't want to insult *everybody*...\r\n");
}


char *
fread_action(FILE * fl, int nr)
{
	char buf[MAX_STRING_LENGTH], *rslt;

	fgets(buf, MAX_STRING_LENGTH, fl);
	if (feof(fl)) {
		fprintf(stderr, "fread_action - unexpected EOF near action #%d", nr);
		safe_exit(1);
	}
	if (*buf == '#')
		return (NULL);
	else {
		*(buf + strlen(buf) - 1) = '\0';
		CREATE(rslt, char, strlen(buf) + 1);
		strcpy(rslt, buf);
		return (rslt);
	}
}


#define MAX_SOCIALS 500

void
boot_social_messages(void)
{
	FILE *fl;
	int nr, i, hide, min_pos, idx;
	int social_count = 0;
	char next_soc[100];
	struct social_messg temp;
	struct social_messg tmp_soc_mess_list[MAX_SOCIALS];

	memset(tmp_soc_mess_list, 0, sizeof(tmp_soc_mess_list));

	/* open social file */
	if (!(fl = fopen(SOCMESS_FILE, "r"))) {
		sprintf(buf, "Can't open socials file '%s'", SOCMESS_FILE);
		perror(buf);
		safe_exit(1);
	}
	/* count socials & allocate space */
	for (nr = 0; *cmd_info[nr].command != '\n'; nr++)
		if (cmd_info[nr].command_pointer == do_action)
			list_top++;

	/* now read 'em */
	for (;;) {
		fscanf(fl, " %s ", next_soc);
		if (*next_soc == '$')
			break;
		if ((nr = find_command(next_soc)) < 0) {
			slog("Unknown social '%s' in social file", next_soc);
		}
		if (fscanf(fl, " %d %d \n", &hide, &min_pos) != 2) {
			fprintf(stderr, "Format error in social file near social '%s'\n",
				next_soc);
			safe_exit(1);
		}

		/* read the stuff */
		tmp_soc_mess_list[social_count].act_nr = nr;
		tmp_soc_mess_list[social_count].hide = hide;
		tmp_soc_mess_list[social_count].min_victim_position = min_pos;

		tmp_soc_mess_list[social_count].char_no_arg = fread_action(fl, nr);
		tmp_soc_mess_list[social_count].others_no_arg = fread_action(fl, nr);
		tmp_soc_mess_list[social_count].char_found = fread_action(fl, nr);

		/* if no char_found, the rest is to be ignored */
		if (!tmp_soc_mess_list[social_count].char_found) {
			tmp_soc_mess_list[social_count].others_found = NULL;
			tmp_soc_mess_list[social_count].vict_found = NULL;
			tmp_soc_mess_list[social_count].not_found = NULL;
			tmp_soc_mess_list[social_count].others_auto = NULL;
			social_count++;
			continue;
		}

		tmp_soc_mess_list[social_count].others_found = fread_action(fl, nr);
		tmp_soc_mess_list[social_count].vict_found = fread_action(fl, nr);
		tmp_soc_mess_list[social_count].not_found = fread_action(fl, nr);
		if ((tmp_soc_mess_list[social_count].char_auto = fread_action(fl, nr)))
			tmp_soc_mess_list[social_count].others_auto = fread_action(fl, nr);
		else
			tmp_soc_mess_list[social_count].others_auto = NULL;
		social_count++;

		if (social_count >= MAX_SOCIALS) {
			slog("Too many socials.  Increase MAX_SOCIALS in act.social.c");
			safe_exit(1);
		}
	}

	/* close file & set top */
	fclose(fl);

	CREATE(soc_mess_list, struct social_messg, list_top + 1);

	for (idx = 0, i = 0; idx < social_count && i < list_top;
		idx++)
		if (tmp_soc_mess_list[idx].act_nr >= 0) {
			soc_mess_list[i] = tmp_soc_mess_list[idx];
			i++;
		}

	/* now, sort 'em */
	for (idx = 0; idx < list_top; idx++) {
		min_pos = idx;
		for (i = idx + 1; i < list_top; i++)
			if (soc_mess_list[i].act_nr < soc_mess_list[min_pos].act_nr)
				min_pos = i;
		if (idx != min_pos) {
			temp = soc_mess_list[idx];
			soc_mess_list[idx] = soc_mess_list[min_pos];
			soc_mess_list[min_pos] = temp;
		}
	}

	/* Check to make sure that all social commands are defined */
	for (nr = 0; *cmd_info[nr].command != '\n'; nr++)
		if (cmd_info[nr].command_pointer == do_action && find_action(nr) < 0)
			slog("SYSERR: Social '%s' is not defined in socials file",
				cmd_info[nr].command);

}


void
show_social_messages(struct Creature *ch, char *arg)
{
	int i, j, l;
	struct social_messg *action;

	if (!*arg)
		send_to_char(ch, "What social?\r\n");
	else {
		for (l = strlen(arg), i = 0; *cmd_info[i].command != '\n'; i++) {
			if (!strncmp(cmd_info[i].command, arg, l)) {
				if (GET_LEVEL(ch) >= cmd_info[i].minimum_level) {
					break;
				}
			}
		}
		if (*cmd_info[i].command == '\n')
			send_to_char(ch, "No such social.\r\n");
		else if ((j = find_action(i)) < 0)
			send_to_char(ch, "That action is not supported.\r\n");
		else {
			action = &soc_mess_list[j];

			sprintf(buf, "Action '%s', Hide-invis : %s, Min Vict Pos: %d\r\n",
				cmd_info[i].command, YESNO(action->hide),
				action->min_victim_position);
			sprintf(buf, "%schar_no_arg  : %s\r\n", buf, action->char_no_arg);
			sprintf(buf, "%sothers_no_arg: %s\r\n", buf,
				action->others_no_arg);
			sprintf(buf, "%schar_found   : %s\r\n", buf, action->char_found);
			if (action->others_found) {
				sprintf(buf, "%sothers_found : %s\r\n", buf,
					action->others_found);
				sprintf(buf, "%svict_found   : %s\r\n", buf,
					action->vict_found);
				sprintf(buf, "%snot_found    : %s\r\n", buf,
					action->not_found);
				sprintf(buf, "%schar_auto    : %s\r\n", buf,
					action->char_auto);
				sprintf(buf, "%sothers_auto  : %s\r\n", buf,
					action->others_auto);
			}
			send_to_char(ch, "%s", buf);

			return;
		}
	}
}
void
free_socials(void)
{
	int i;

	for (i = 0; i <= list_top; i++) {
		if (soc_mess_list[i].char_no_arg)
			free(soc_mess_list[i].char_no_arg);
		if (soc_mess_list[i].others_no_arg)
			free(soc_mess_list[i].others_no_arg);
		if (soc_mess_list[i].char_found)
			free(soc_mess_list[i].char_found);
		if (soc_mess_list[i].others_found)
			free(soc_mess_list[i].others_found);
		if (soc_mess_list[i].vict_found)
			free(soc_mess_list[i].vict_found);
		if (soc_mess_list[i].not_found)
			free(soc_mess_list[i].not_found);
		if (soc_mess_list[i].char_auto)
			free(soc_mess_list[i].char_auto);
		if (soc_mess_list[i].others_auto)
			free(soc_mess_list[i].others_auto);
	}
	free(soc_mess_list);
}
