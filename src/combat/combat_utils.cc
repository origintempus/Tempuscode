/*************************************************************************
*   File: combat_utils.c                                       Part of CircleMUD *
*  Usage: Combat system                                                   *
*                                                                         *
*  All rights reserved.  See license.doc for complete information.        *
*                                                                         *
*  Copyright ( C ) 1993, 94 by the Trustees of the Johns Hopkins University *
*  CircleMUD is based on DikuMUD, Copyright ( C ) 1990, 1991.               *
************************************************************************ */

//
// File: combat_utils.c                      -- Part of TempusMUD
//
// All modifications and additions are
// Copyright 1998 by John Watson, all rights reserved.
//

#ifdef HAS_CONFIG_H
#include "config.h"
#endif

#define __combat_code__
#define __combat_utils__

#include <errno.h>

#include "structs.h"
#include "utils.h"
#include "comm.h"
#include "handler.h"
#include "interpreter.h"
#include "db.h"
#include "spells.h"
#include "screen.h"
#include "char_class.h"
#include "vehicle.h"
#include "materials.h"
#include "flow_room.h"
#include "fight.h"
#include "bomb.h"
#include "guns.h"
#include "specs.h"
#include "security.h"
#include "house.h"
#include "quest.h"
#include "player_table.h"
#include "vendor.h"

#include <iostream>

extern int corpse_state;
/* The Fight related routines */
obj_data *get_random_uncovered_implant(Creature * ch, int type = -1);
int calculate_weapon_probability(struct Creature *ch, int prob,
	struct obj_data *weap);
int calculate_attack_probability(struct Creature *ch);

//checks for both vendors and utility mobs
bool
ok_damage_vendor(struct Creature *ch, struct Creature *victim)
{
	if (ch && GET_LEVEL(ch) > LVL_CREATOR)
		return true;
	if (victim &&
		GET_LEVEL(victim) > LVL_IMMORT &&
		(IS_NPC(victim) ||
		 !PLR_FLAGGED(victim, PLR_MORTALIZED)))
		return false;

	if (IS_NPC(victim)
        && (MOB2_FLAGGED(victim, MOB2_SELLER)
            || victim->mob_specials.shared->func == vendor)) {
        ShopData *shop = (ShopData *)victim->mob_specials.func_data;

		if (!GET_MOB_PARAM(victim))
			return false;

        if (!shop) {
            CREATE(shop, ShopData, 1);
            vendor_parse_param(victim, GET_MOB_PARAM(victim), shop, NULL);
            victim->mob_specials.func_data = shop;
        }

		return shop->attack_ok;
	}

    if (IS_NPC(victim) && MOB_FLAGGED(victim, MOB_UTILITY)) {
        //utility mobs shouldn't be attacked either
        return false;
    }

	return true;
}

int
calculate_weapon_probability(struct Creature *ch, int prob,
	struct obj_data *weap)
{
	int i, weap_weight;
	i = weap_weight = 0;

	if (!ch || !weap)
		return prob;

	// Add in weapon specialization bonus.
	if (GET_OBJ_VNUM(weap) > 0) {
		for (i = 0; i < MAX_WEAPON_SPEC; i++) {
			if (GET_WEAP_SPEC(ch, i).vnum == GET_OBJ_VNUM(weap)) {
				prob += GET_WEAP_SPEC(ch, i).level << 2;
				break;
			}
		}
	}
	// Below this check applies to WIELD and WIELD_2 only!
	if (weap->worn_on != WEAR_WIELD && weap->worn_on != WEAR_WIELD_2) {
		return prob;
	}
	// Weapon speed
	for (i = 0, weap_weight = 0; i < MAX_OBJ_AFFECT; i++) {
		if (weap->affected[i].location == APPLY_WEAPONSPEED) {
			weap_weight -= weap->affected[i].modifier;
			break;
		}
	}
	// 1/4 actual weapon weight or effective weapon weight, whichever is higher.
	weap_weight += weap->getWeight();
	weap_weight = MAX((weap->getWeight() >> 2), weap_weight);

	if (weap->worn_on == WEAR_WIELD_2) {
		prob -=
			(prob * weap_weight) /
			MAX(1, (str_app[STRENGTH_APPLY_INDEX(ch)].wield_w >> 1));
		if (affected_by_spell(ch, SKILL_NEURAL_BRIDGING)) {
			prob += CHECK_SKILL(ch, SKILL_NEURAL_BRIDGING) - 60;
		} else {
			if (CHECK_SKILL(ch, SKILL_SECOND_WEAPON) >= LEARNED(ch)) {
				prob += CHECK_SKILL(ch, SKILL_SECOND_WEAPON) - 60;
			}
		}
	} else {
		prob -=
			(prob * weap_weight) /
			(str_app[STRENGTH_APPLY_INDEX(ch)].wield_w << 1);
		if (IS_BARB(ch)) {
			prob += (LEARNED(ch) - weapon_prof(ch, weap)) >> 3;
		}
	}
	return prob;
}

void
update_pos(struct Creature *victim)
{
	// Wake them up from thier nap.
	if (GET_HIT(victim) > 0 && victim->getPosition() == POS_SLEEPING)
		victim->setPosition(POS_RESTING, 1);
	// If everything is normal and they're fighting, set them fighting
	else if (GET_HIT(victim) > 0 &&
		(victim->getPosition() == POS_STANDING
			|| victim->getPosition() == POS_FLYING) && victim->isFighting()) {
#ifdef DEBUG_POSITION
		if (victim->setPosition(POS_FIGHTING, 1))
			act("$n moves to POS_FIGHTING.(from standing or flying)",
				true, victim, 0, 0, TO_ROOM);
#endif
		victim->setPosition(POS_FIGHTING, 1);
	}
	// If they're alive, not stunned, in a fight, and not pos_fighting
	// (Making mobs stand when they get popped.
	else if ((GET_HIT(victim) > 0)
		&& (victim->getPosition() > POS_STUNNED)
		&& victim->getPosition() < POS_FIGHTING && victim->isFighting()) {
		// If they're an npc, and their wait is 0.
		if (IS_NPC(victim) && GET_MOB_WAIT(victim) <= 0) {
			if (victim->getPosition() < POS_FIGHTING) {
				if (!AFF3_FLAGGED(victim, AFF3_GRAVITY_WELL) ||
					number(1, 20) < GET_STR(victim)) {
					if (victim->setPosition(POS_FIGHTING, 1)) {
#ifdef DEBUG_POSITION
						act("$n moves to POS_FIGHTING.(A)", true, victim, 0, 0,
							TO_ROOM);
#else
						act("$n scrambles to $s feet!", true, victim, 0, 0,
							TO_ROOM);
#endif
					}
				}
				WAIT_STATE(victim, PULSE_VIOLENCE);
			} else {
				victim->setPosition(POS_FIGHTING, 1);
			}
		} else {				// PC or a mob with a wait state.
			return;
		}
	} else if (GET_HIT(victim) > 0) {
		if (IS_NPC(victim) && GET_MOB_WAIT(victim) <= 0) {
			// Flying?
			if (victim->in_room && victim->in_room->isOpenAir()
				&& !AFF3_FLAGGED(victim, AFF3_GRAVITY_WELL)
				&& victim->getPosition() != POS_FLYING)
				victim->setPosition(POS_FLYING, 1);
			else if (!AFF3_FLAGGED(victim, AFF3_GRAVITY_WELL)
				&& victim->getPosition() < POS_FIGHTING) {
				if (victim->isFighting()) {
					if (victim->setPosition(POS_FIGHTING, 1)) {
#ifdef DEBUG_POSITION
						act("$n moves to POS_FIGHTING.(B1)", true, victim, 0,
							0, TO_ROOM);
#else
						act("$n scrambles to $s feet!", true, victim, 0, 0,
							TO_ROOM);
#endif
						WAIT_STATE(victim, PULSE_VIOLENCE);
					}
				} else {
					if (victim->setPosition(POS_STANDING, 1)) {
#ifdef DEBUG_POSITION
						act("$n moves to POS_STANDING.(B2)", true, victim, 0,
							0, TO_ROOM);
#else
						act("$n stands up.", true, victim, 0, 0, TO_ROOM);
#endif
						WAIT_STATE(victim, PULSE_VIOLENCE);
					}
				}
			} else if (number(1, 20) < GET_STR(victim)
				&& victim->getPosition() < POS_FIGHTING) {
				if (victim->isFighting()) {
					if (victim->setPosition(POS_FIGHTING, 1)) {
#ifdef DEBUG_POSITION
						act("$n moves to POS_FIGHTING.(C1)", true, victim, 0,
							0, TO_ROOM);
#else
						act("$n scrambles to $s feet!", true, victim, 0, 0,
							TO_ROOM);
#endif
						WAIT_STATE(victim, PULSE_VIOLENCE);
					}
				} else {
					if (victim->setPosition(POS_STANDING, 1)) {
#ifdef DEBUG_POSITION
						act("$n moves to POS_STANDING.(C2)", true, victim, 0,
							0, TO_ROOM);
#else
						act("$n stands up.", true, victim, 0, 0, TO_ROOM);
#endif
						WAIT_STATE(victim, PULSE_VIOLENCE);
					}
				}
            }
        } else if (victim->getPosition() == POS_STUNNED) {
            victim->setPosition(POS_RESTING, 1);
#ifdef DEBUG_POSITION
            act("$n moves to POS_RESTING.(From Stunned)", true, victim, 0, 0,
                TO_ROOM);
#endif
        }
    }

	// Various stages of unhappiness
	else if (GET_HIT(victim) <= -11)
		victim->setPosition(POS_DEAD, 1);
	else if (GET_HIT(victim) <= -6)
		victim->setPosition(POS_MORTALLYW, 1);
	else if (GET_HIT(victim) <= -3)
		victim->setPosition(POS_INCAP, 1);
	else
		victim->setPosition(POS_STUNNED, 1);
	return;
}

char *
replace_string(const char *str,
               const char *weapon_singular,
               const char *weapon_plural,
               const char *location,
               const char *substance)
{
	static char buf[256];
	char *cp;
    char *prefixed_substance = NULL;
    if (substance) {
        prefixed_substance = tmp_strcat(AN(substance), " ", substance, NULL);
    }
	cp = buf;

	for (; *str; str++) {
		if (*str == '#') {
			switch (*(++str)) {
			case 'W':
				for (; *weapon_plural; *(cp++) = *(weapon_plural++));
				break;
			case 'w':
				for (; *weapon_singular; *(cp++) = *(weapon_singular++));
				break;
			case 'p':
				if (location)
					for (; *location; *(cp++) = *(location++));
				break;
            case 'S':
                if (substance)
                    for (; *substance; *(cp++) = *(substance++));
                break;
            case 's':
                if (prefixed_substance)
                    for (; *prefixed_substance; *(cp++) = *(prefixed_substance++));
                break;
			default:
				*(cp++) = '#';
				break;
			}
		} else
			*(cp++) = *str;

		*cp = 0;
	}							/* For */

	return (CAP(buf));
}

/* Calculate the raw armor including magic armor.  Lower AC is better. */
int
calculate_thaco(struct Creature *ch, struct Creature *victim,
	struct obj_data *weap)
{
	int calc_thaco, wpn_wgt, i;

	calc_thaco = (int)MIN(THACO(GET_CLASS(ch), GET_LEVEL(ch)),
		THACO(GET_REMORT_CLASS(ch), GET_LEVEL(ch)));

    if (weap && IS_ENERGY_GUN(weap))
        calc_thaco -= dex_app[GET_DEX(ch)].tohit;
    else
        calc_thaco -= str_app[STRENGTH_APPLY_INDEX(ch)].tohit;

	if (GET_HITROLL(ch) <= 5)
		calc_thaco -= GET_HITROLL(ch);
	else if (GET_HITROLL(ch) <= 50)
		calc_thaco -= 5 + (((GET_HITROLL(ch) - 5)) / 3);
	else
		calc_thaco -= 20;

	calc_thaco -= (int)((GET_INT(ch) - 12) >> 1);	/* Intelligence helps! */
	calc_thaco -= (int)((GET_WIS(ch) - 10) >> 2);	/* So does wisdom */

	if (AWAKE(victim))
		calc_thaco -= dex_app[GET_DEX(victim)].defensive;

	if (IS_DROW(ch)) {
		if (OUTSIDE(ch) && PRIME_MATERIAL_ROOM(ch->in_room)) {
			if (ch->in_room->zone->weather->sunlight == SUN_LIGHT)
				calc_thaco += 10;
			else if (ch->in_room->zone->weather->sunlight == SUN_DARK)
				calc_thaco -= 5;
		} else if (room_is_dark(ch->in_room))
			calc_thaco -= 5;
	}

	if (weap) {
		if (ch != weap->worn_by) {
			errlog("inconsistent weap->worn_by ptr in calculate_thaco.");
			slog("weap: ( %s ), ch: ( %s ), weap->worn->by: ( %s )",
				weap->name, GET_NAME(ch), weap->worn_by ?
				GET_NAME(weap->worn_by) : "NULL");
			return 0;
		}

		if (GET_OBJ_VNUM(weap) > 0) {
			for (i = 0; i < MAX_WEAPON_SPEC; i++)
				if (GET_WEAP_SPEC(ch, i).vnum == GET_OBJ_VNUM(weap)) {
					calc_thaco -= GET_WEAP_SPEC(ch, i).level;
					break;
				}
		}

        // Bonuses for bless/damn
        if (IS_EVIL(victim) && IS_OBJ_STAT(cur_weap, ITEM_BLESS))
            calc_thaco -= 1;
        if (IS_GOOD(victim) && IS_OBJ_STAT(cur_weap, ITEM_DAMNED))
            calc_thaco -= 1;

		wpn_wgt = weap->getWeight();
		if (wpn_wgt > str_app[STRENGTH_APPLY_INDEX(ch)].wield_w)
			calc_thaco += 2;
		if (IS_MAGE(ch) &&
			(wpn_wgt >
				((GET_LEVEL(ch) * str_app[STRENGTH_APPLY_INDEX(ch)].wield_w /
						100)
					+ (str_app[STRENGTH_APPLY_INDEX(ch)].wield_w >> 1))))
			calc_thaco += (wpn_wgt >> 2);
		else if (IS_THIEF(ch) && (wpn_wgt > 12 + (GET_STR(ch) >> 2)))
			calc_thaco += (wpn_wgt >> 3);

		if (IS_BARB(ch))
			calc_thaco += (LEARNED(ch) - weapon_prof(ch, weap)) / 8;

        if (IS_ENERGY_GUN(weap))
            calc_thaco += (LEARNED(ch) - GET_SKILL(ch, SKILL_ENERGY_WEAPONS)) / 8;

        if (IS_ENERGY_GUN(weap) && GET_SKILL(ch, SKILL_SHOOT) < 80)
            calc_thaco += (100-GET_SKILL(ch, SKILL_SHOOT))/20;

		if (GET_EQ(ch, WEAR_WIELD_2)) {
			// They don't know how to second wield and
			// they dont have neural bridging
			if (CHECK_SKILL(ch, SKILL_SECOND_WEAPON) < LEARNED(ch)
				&& !affected_by_spell(ch, SKILL_NEURAL_BRIDGING)) {
				if (weap == GET_EQ(ch, WEAR_WIELD_2))
					calc_thaco -=
						(LEARNED(ch) - CHECK_SKILL(ch,
							SKILL_SECOND_WEAPON)) / 5;
				else
					calc_thaco -=
						(LEARNED(ch) - CHECK_SKILL(ch,
							SKILL_SECOND_WEAPON)) / 10;
			}
		}
	}
	/* end if ( weap ) */
	if ((IS_EVIL(ch) && AFF_FLAGGED(victim, AFF_PROTECT_EVIL)) ||
		(IS_GOOD(ch) && AFF_FLAGGED(victim, AFF_PROTECT_GOOD)) ||
		(IS_UNDEAD(ch) && AFF_FLAGGED(victim, AFF2_PROTECT_UNDEAD)))
		calc_thaco += 2;

	if (IS_CARRYING_N(ch) > (CAN_CARRY_N(ch) * 0.80))
		calc_thaco += 1;

	if (CAN_CARRY_W(ch))
		calc_thaco += ((TOTAL_ENCUM(ch) << 1) / CAN_CARRY_W(ch));
	else
		calc_thaco += 10;

	if (AFF2_FLAGGED(ch, AFF2_DISPLACEMENT) &&
		!AFF2_FLAGGED(victim, AFF2_TRUE_SEEING))
		calc_thaco -= 2;
	if (AFF_FLAGGED(ch, AFF_BLUR))
		calc_thaco -= 1;
	if (!can_see_creature(victim, ch))
		calc_thaco -= 3;

	if (!can_see_creature(ch, victim))
		calc_thaco += 2;
	if (GET_COND(ch, DRUNK))
		calc_thaco += 2;
	if (IS_SICK(ch))
		calc_thaco += 2;
	if (AFF2_FLAGGED(victim, AFF2_DISPLACEMENT) &&
		!AFF2_FLAGGED(ch, AFF2_TRUE_SEEING))
		calc_thaco += 2;
	if (AFF2_FLAGGED(victim, AFF2_EVADE))
		calc_thaco +=  victim->getLevelBonus(SKILL_EVASION) / 6;

    if (room_is_watery(ch->in_room) && !IS_MOB(ch))
		calc_thaco += 4;

	calc_thaco -= MIN(5, MAX(0, (POS_FIGHTING - victim->getPosition())));

	calc_thaco -= char_class_race_hit_bonus(ch, victim);

	return calc_thaco;
}

void
add_blood_to_room(struct room_data *rm, int amount)
{
	struct obj_data *blood;
	int new_blood = false;

	if (amount <= 0)
		return;

	for (blood = rm->contents; blood; blood = blood->next_content)
		if (GET_OBJ_VNUM(blood) == BLOOD_VNUM)
			break;

	if (!blood && (new_blood = true) && !(blood = read_object(BLOOD_VNUM))) {
		errlog("Unable to load blood.");
		return;
	}

	if (GET_OBJ_TIMER(blood) > 50)
		return;

	GET_OBJ_TIMER(blood) += amount;

	if (new_blood)
		obj_to_room(blood, rm);

}

int
apply_soil_to_char(struct Creature *ch, struct obj_data *obj, int type,
	int pos)
{

	int cnt, idx;

	if (pos == WEAR_RANDOM) {
		cnt = 0;
		for (idx = 0;idx < NUM_WEARS;idx++) {
			if (ILLEGAL_SOILPOS(idx))
				continue;
			if (!GET_EQ(ch, idx) && CHAR_SOILED(ch, pos, type))
				continue;
			if (GET_EQ(ch, idx) && (OBJ_SOILED(GET_EQ(ch, idx), type) ||
					IS_OBJ_STAT2(GET_EQ(ch, idx), ITEM2_NOSOIL)))
				continue;
			if (!number(0, cnt))
				pos = idx;
			cnt++;
		}

		// A position will only be unchosen if there are no valid positions
		// with which to soil.
		if (!cnt)
			return 0;
	}

	if (ILLEGAL_SOILPOS(pos))
		return 0;
	if (GET_EQ(ch, pos) && (GET_EQ(ch, pos) == obj || !obj)) {
		if (IS_OBJ_STAT2(GET_EQ(ch, pos), ITEM2_NOSOIL) ||
				OBJ_SOILED(GET_EQ(ch, pos), type))
			return 0;

		SET_BIT(OBJ_SOILAGE(GET_EQ(ch, pos)), type);
	} else if (CHAR_SOILED(ch, pos, type)) {
		return 0;
	} else {
		SET_BIT(CHAR_SOILAGE(ch, pos), type);
	}

	if (type == SOIL_BLOOD && obj && GET_OBJ_VNUM(obj) == BLOOD_VNUM)
		GET_OBJ_TIMER(obj) = MAX(1, GET_OBJ_TIMER(obj) - 5);

	return pos;
}

int
choose_random_limb(Creature *victim)
{
	int prob;
	int i;
	static int limb_probmax = 0;

	if (!limb_probmax) {
		for (i = 0; i < NUM_WEARS; i++) {
			limb_probmax += limb_probs[i];
			if (i >= 1)
				limb_probs[i] += limb_probs[i - 1];
		}
	}

	prob = number(1, limb_probmax);

	for (i = 1; i < NUM_WEARS; i++) {
		if (prob > limb_probs[i - 1] && prob <= limb_probs[i])
			break;
	}

	if (i >= NUM_WEARS) {
		errlog("exceeded NUM_WEARS-1 in choose_random_limb.");
		return WEAR_BODY;
	}
	// shield will be the only armor check we do here, since it is a special position
	if (i == WEAR_SHIELD) {
		if (!GET_EQ(victim, WEAR_SHIELD)) {
			if (!number(0, 2))
				i = WEAR_ARMS;
			else
				i = WEAR_BODY;
		}
	}

	if (!POS_DAMAGE_OK(i)) {
		errlog("improper pos %d leaving choose_random_limb.", i);
		return WEAR_BODY;
	}

	return i;
}

obj_data *
make_corpse(struct Creature *ch, struct Creature *killer, int attacktype)
{
	struct obj_data *corpse = NULL, *head = NULL, *heart = NULL,
		*spine = NULL, *o = NULL, *next_o = NULL, *leg = NULL;
	struct obj_data *money = NULL;
	int i;
	char typebuf[256];
	char adj[256];
	char namestr[256];
	char isare[16];

	extern int max_npc_corpse_time, max_pc_corpse_time;

	adj[0] = '\0';
	typebuf[0] = '\0';
	namestr[0] = '\0';
	isare[0] = '\0';

	// The Fate's corpses are portals to the remorter.
	if (GET_MOB_SPEC(ch) == fate) {	// GetMobSpec checks IS_NPC
		struct obj_data *portal = NULL;
		extern int fate_timers[];
		if ((portal = read_object(FATE_PORTAL_VNUM))) {
			GET_OBJ_TIMER(portal) = max_npc_corpse_time;
			if (GET_MOB_VNUM(ch) == FATE_VNUM_LOW) {
				GET_OBJ_VAL(portal, 2) = 1;
				fate_timers[0] = 0;
			} else if (GET_MOB_VNUM(ch) == FATE_VNUM_MID) {
				GET_OBJ_VAL(portal, 2) = 2;
				fate_timers[1] = 0;
			} else if (GET_MOB_VNUM(ch) == FATE_VNUM_HIGH) {
				GET_OBJ_VAL(portal, 2) = 3;
				fate_timers[2] = 0;
			} else {
				GET_OBJ_VAL(portal, 2) = 12;
			}
			obj_to_room(portal, ch->in_room);
		}
	}
	// End Fate
	if (corpse_state) {
		attacktype = corpse_state;
		corpse_state = 0;
	}
	corpse = create_obj();
	corpse->shared = null_obj_shared;
	corpse->in_room = NULL;
	corpse->worn_on = -1;

	if (AFF2_FLAGGED(ch, AFF2_PETRIFIED))
		GET_OBJ_MATERIAL(corpse) = MAT_STONE;
	else if (IS_ROBOT(ch))
		GET_OBJ_MATERIAL(corpse) = MAT_FLESH;
	else if (IS_SKELETON(ch))
		GET_OBJ_MATERIAL(corpse) = MAT_BONE;
	else if (IS_PUDDING(ch))
		GET_OBJ_MATERIAL(corpse) = MAT_PUDDING;
	else if (IS_SLIME(ch))
		GET_OBJ_MATERIAL(corpse) = MAT_SLIME;
	else if (IS_PLANT(ch))
		GET_OBJ_MATERIAL(corpse) = MAT_VEGETABLE;
	else
		GET_OBJ_MATERIAL(corpse) = MAT_FLESH;

	strcpy(isare, "is");

	if (GET_RACE(ch) == RACE_ROBOT || GET_RACE(ch) == RACE_PLANT ||
		attacktype == TYPE_FALLING) {
		strcpy(isare, "are");
		strcpy(typebuf, "remains");
		strcpy(namestr, typebuf);
	} else if (attacktype == TYPE_SWALLOW) {
		strcpy(typebuf, "bones");
		strcpy(namestr, typebuf);
	} else {
		strcpy(typebuf, "corpse");
		strcpy(namestr, typebuf);
	}

	if (AFF2_FLAGGED(ch, AFF2_PETRIFIED)) {
		strcat(namestr, " stone");
		strcpy(adj, "stone ");
		strcat(adj, typebuf);
		strcpy(typebuf, adj);
		adj[0] = '\0';
	}
#ifdef DMALLOC
	malloc_verify(0);
#endif

	if ((attacktype == SKILL_BEHEAD ||
			attacktype == SKILL_PELE_KICK ||
			attacktype == SKILL_CLOTHESLINE)
		&& isname("headless", ch->player.name)) {
		attacktype = TYPE_HIT;
	}

	switch (attacktype) {

	case TYPE_HIT:
	case SKILL_BASH:
	case SKILL_PISTOLWHIP:
		sprintf(buf2, "The bruised up %s of %s %s lying here.",
			typebuf, GET_NAME(ch), isare);
		corpse->line_desc = strdup(buf2);
		strcpy(adj, "bruised");
		break;

	case TYPE_STING:
		sprintf(buf2, "The bloody, swollen %s of %s %s lying here.",
			typebuf, GET_NAME(ch), isare);
		corpse->line_desc = strdup(buf2);
		strcpy(adj, "stung");
		break;

	case TYPE_WHIP:
		sprintf(buf2, "The scarred %s of %s %s lying here.",
			typebuf, GET_NAME(ch), isare);
		corpse->line_desc = strdup(buf2);
		strcpy(adj, "scarred");
		break;

	case TYPE_SLASH:
	case TYPE_CHOP:
	case SPELL_BLADE_BARRIER:
		sprintf(buf2, "The chopped up %s of %s %s lying here.",
			typebuf, GET_NAME(ch), isare);
		corpse->line_desc = strdup(buf2);
		strcpy(adj, "chopped up");
		break;

	case SONG_WOUNDING_WHISPERS:
		sprintf(buf2, "The perforated %s of %s %s lying here.",
			typebuf, GET_NAME(ch), isare);
		corpse->line_desc = strdup(buf2);
		strcpy(adj, "perforated");
		break;

	case SKILL_HAMSTRING:
		sprintf(buf2, "The legless %s of %s %s lying here.",
			typebuf, GET_NAME(ch), isare);
		corpse->line_desc = strdup(buf2);
		strcpy(adj, "legless");

		if (IS_RACE(ch, RACE_BEHOLDER) || NON_CORPOREAL_MOB(ch))
			break;

		leg = create_obj();
		leg->shared = null_obj_shared;
		leg->in_room = NULL;
		if (AFF2_FLAGGED(ch, AFF2_PETRIFIED))
			leg->aliases = strdup("blood leg stone");
		else
			leg->aliases = strdup("leg severed");

		sprintf(buf2, "The severed %sleg of %s %s lying here.",
			AFF2_FLAGGED(ch, AFF2_PETRIFIED) ? "stone " : "", GET_NAME(ch),
			isare);
		leg->line_desc = strdup(buf2);
		sprintf(buf2, "the severed %sleg of %s",
			AFF2_FLAGGED(ch, AFF2_PETRIFIED) ? "stone " : "", GET_NAME(ch));
		leg->name = strdup(buf2);
		GET_OBJ_TYPE(leg) = ITEM_WEAPON;
		GET_OBJ_WEAR(leg) = ITEM_WEAR_TAKE + ITEM_WEAR_WIELD;
		GET_OBJ_EXTRA(leg) = ITEM_NODONATE + ITEM_NOSELL;
		GET_OBJ_EXTRA2(leg) = ITEM2_BODY_PART;
		GET_OBJ_VAL(leg, 0) = 0;
		GET_OBJ_VAL(leg, 1) = 2;
		GET_OBJ_VAL(leg, 2) = 9;
		GET_OBJ_VAL(leg, 3) = 7;
		leg->setWeight(7);
		leg->worn_on = -1;
		if (IS_NPC(ch))
			GET_OBJ_TIMER(leg) = max_npc_corpse_time;
		else {
			GET_OBJ_TIMER(leg) = max_pc_corpse_time;
		}
		obj_to_room(leg, ch->in_room);
		if (!is_arena_combat(killer, ch) &&
            !is_npk_combat(killer, ch) &&
			GET_LEVEL(ch) <= LVL_AMBASSADOR) {

			/* transfer character's leg EQ to room, if applicable */
			if (GET_EQ(ch, WEAR_LEGS))
				obj_to_room(unequip_char(ch, WEAR_LEGS, EQUIP_WORN), ch->in_room);
			if (GET_EQ(ch, WEAR_FEET))
				obj_to_room(unequip_char(ch, WEAR_FEET, EQUIP_WORN), ch->in_room);

		/** transfer implants to leg or corpse randomly**/
			if (GET_IMPLANT(ch, WEAR_LEGS) && number(0, 1)) {
				obj_to_obj(unequip_char(ch, WEAR_LEGS, EQUIP_IMPLANT), leg);
				REMOVE_BIT(GET_OBJ_WEAR(leg), ITEM_WEAR_TAKE);
			}
			if (GET_IMPLANT(ch, WEAR_FEET) && number(0, 1)) {
				obj_to_obj(unequip_char(ch, WEAR_FACE, EQUIP_IMPLANT), leg);
				REMOVE_BIT(GET_OBJ_WEAR(leg), ITEM_WEAR_TAKE);
			}
		}						// end if !arena room
		break;

	case SKILL_BITE:
	case TYPE_BITE:
		sprintf(buf2, "The chewed up looking %s of %s %s lying here.",
			typebuf, GET_NAME(ch), isare);
		corpse->line_desc = strdup(buf2);
		strcpy(adj, "chewed up");
		break;

	case SKILL_SNIPE:
		sprintf(buf2, "The sniped %s of %s %s lying here.",
			typebuf, GET_NAME(ch), isare);
		corpse->line_desc = strdup(buf2);
		strcpy(adj, "sniped");
		break;

	case TYPE_BLUDGEON:
	case TYPE_POUND:
	case TYPE_PUNCH:
		sprintf(buf2, "The battered %s of %s %s lying here.",
			typebuf, GET_NAME(ch), isare);
		corpse->line_desc = strdup(buf2);
		strcpy(adj, "battered");
		break;

	case TYPE_CRUSH:
	case SPELL_PSYCHIC_CRUSH:
		sprintf(buf2, "The crushed %s of %s %s lying here.",
			typebuf, GET_NAME(ch), isare);
		corpse->line_desc = strdup(buf2);
		strcpy(adj, "crushed");
		break;

	case TYPE_CLAW:
		sprintf(buf2, "The shredded %s of %s %s lying here.",
			typebuf, GET_NAME(ch), isare);
		corpse->line_desc = strdup(buf2);
		strcpy(adj, "shredded");
		break;

	case TYPE_MAUL:
		sprintf(buf2, "The mauled %s of %s %s lying here.",
			typebuf, GET_NAME(ch), isare);
		corpse->line_desc = strdup(buf2);
		strcpy(adj, "mauled");
		break;

	case TYPE_THRASH:
		sprintf(buf2, "The %s of %s %s lying here, badly thrashed.",
			typebuf, GET_NAME(ch), isare);
		corpse->line_desc = strdup(buf2);
		strcpy(adj, "thrashed");
		break;

	case SKILL_BACKSTAB:
		sprintf(buf2, "The backstabbed %s of %s %s lying here.",
			typebuf, GET_NAME(ch), isare);
		corpse->line_desc = strdup(buf2);
		strcpy(adj, "backstabbed");
		break;

	case TYPE_PIERCE:
	case TYPE_STAB:
    case TYPE_EGUN_PARTICLE:
		sprintf(buf2, "The bloody %s of %s %s lying here, full of holes.",
			typebuf, GET_NAME(ch), isare);
		corpse->line_desc = strdup(buf2);
		strcpy(adj, "stabbed");
		break;

	case TYPE_GORE_HORNS:
		sprintf(buf2, "The gored %s of %s %s lying here in a pool of blood.",
			typebuf, GET_NAME(ch), isare);
		corpse->line_desc = strdup(buf2);
		strcpy(adj, "gored");
		break;

	case TYPE_TRAMPLING:
		sprintf(buf2, "The trampled %s of %s %s lying here.",
			typebuf, GET_NAME(ch), isare);
		corpse->line_desc = strdup(buf2);
		strcpy(adj, "trampled");
		break;

	case TYPE_TAIL_LASH:
		sprintf(buf2, "The lashed %s of %s %s lying here.",
			typebuf, GET_NAME(ch), isare);
		corpse->line_desc = strdup(buf2);
		strcpy(adj, "lashed");
		break;

	case TYPE_SWALLOW:
		strcpy(buf2, "A bloody pile of bones is lying here.");
		corpse->line_desc = strdup(buf2);
		strcpy(adj, "bloody pile bones");
		break;

	case TYPE_BLAST:
	case SPELL_MAGIC_MISSILE:
	case SKILL_ENERGY_WEAPONS:
	case SPELL_SYMBOL_OF_PAIN:
	case SPELL_DISRUPTION:
	case SPELL_PRISMATIC_SPRAY:
	case SKILL_DISCHARGE:
    case TYPE_EGUN_LASER:
        sprintf(buf2, "The blasted %s of %s %s lying here.",
			typebuf, GET_NAME(ch), isare);
		corpse->line_desc = strdup(buf2);
		strcpy(adj, "blasted");
		break;

	case SKILL_PROJ_WEAPONS:
		sprintf(buf2, "The shot up %s of %s %s lying here.",
			typebuf, GET_NAME(ch), isare);
		corpse->line_desc = strdup(buf2);
		strcpy(adj, "shot up");
		break;

	case SKILL_ARCHERY:
		sprintf(buf2, "The pierced %s of %s %s lying here.",
			typebuf, GET_NAME(ch), isare);
		corpse->line_desc = strdup(buf2);
		strcpy(adj, "pierced");
		break;

	case SPELL_BURNING_HANDS:
	case SPELL_CALL_LIGHTNING:
	case SPELL_FIREBALL:
	case SPELL_FLAME_STRIKE:
	case SPELL_LIGHTNING_BOLT:
	case SPELL_MICROWAVE:
	case SPELL_FIRE_BREATH:
	case SPELL_LIGHTNING_BREATH:
	case SPELL_FIRE_ELEMENTAL:
	case TYPE_ABLAZE:
	case SPELL_METEOR_STORM:
	case SPELL_FIRE_SHIELD:
	case SPELL_HELL_FIRE:
	case TYPE_FLAMETHROWER:
    case SPELL_ELECTRIC_ARC:
    case TYPE_EGUN_PLASMA:
		sprintf(buf2, "The charred %s of %s %s lying here.",
			typebuf, GET_NAME(ch), isare);
		corpse->line_desc = strdup(buf2);
		strcpy(adj, "charred");
		break;

	case SKILL_ENERGY_FIELD:
	case SKILL_SELF_DESTRUCT:
    case TYPE_EGUN_ION:
		sprintf(buf2, "The smoking %s of %s %s lying here,",
			typebuf, GET_NAME(ch), isare);
		corpse->line_desc = strdup(buf2);
		strcpy(adj, "smoking");
		break;

	case TYPE_BOILING_PITCH:
		sprintf(buf2, "The scorched %s of %s %s here.", typebuf, GET_NAME(ch),
			isare);
		corpse->line_desc = strdup(buf2);
		strcpy(adj, "scorched");
		break;

	case SPELL_STEAM_BREATH:
		sprintf(buf2, "The scalded %s of %s %s here.", typebuf, GET_NAME(ch),
			isare);
		corpse->line_desc = strdup(buf2);
		strcpy(adj, "scalded");
		break;

	case JAVELIN_OF_LIGHTNING:
    case EGUN_LIGHTNING:
		sprintf(buf2, "The %s of %s %s lying here, blasted and smoking.",
			typebuf, GET_NAME(ch), isare);
		corpse->line_desc = strdup(buf2);
		strcpy(adj, "blasted");
		break;

	case SPELL_CONE_COLD:
	case SPELL_CHILL_TOUCH:
	case TYPE_FREEZING:
	case SPELL_HELL_FROST:
	case SPELL_FROST_BREATH:
		sprintf(buf2, "The frozen %s of %s %s lying here.",
			typebuf, GET_NAME(ch), isare);
		corpse->line_desc = strdup(buf2);
		strcpy(adj, "frozen");
		break;

	case SPELL_SPIRIT_HAMMER:
	case SPELL_EARTH_ELEMENTAL:
		sprintf(buf2, "The smashed %s of %s %s lying here.",
			typebuf, GET_NAME(ch), isare);
		corpse->line_desc = strdup(buf2);
		strcpy(adj, "smashed");
		break;

	case SPELL_AIR_ELEMENTAL:
	case TYPE_RIP:
		sprintf(buf2, "The ripped apart %s of %s %s lying here.",
			typebuf, GET_NAME(ch), isare);
		corpse->line_desc = strdup(buf2);
		strcpy(adj, "ripped apart");
		break;

	case SPELL_WATER_ELEMENTAL:
		sprintf(buf2, "The drenched %s of %s %s lying here.",
			typebuf, GET_NAME(ch), isare);
		corpse->line_desc = strdup(buf2);
		strcpy(adj, "drenched");
		break;

	case SPELL_GAMMA_RAY:
	case SPELL_HALFLIFE:
    case TYPE_EGUN_GAMMA:
		sprintf(buf2, "The radioactive %s of %s %s lying here.",
			typebuf, GET_NAME(ch), isare);
		corpse->line_desc = strdup(buf2);
		strcpy(adj, "radioactive");
		break;

	case SPELL_ACIDITY:
		sprintf(buf2, "The sizzling %s of %s %s lying here, dripping acid.",
			typebuf, GET_NAME(ch), isare);
		corpse->line_desc = strdup(buf2);
		strcpy(adj, "sizzling");
		break;

	case SPELL_GAS_BREATH:
		sprintf(buf2, "The %s of %s lie%s here, stinking of chlorine gas.",
			typebuf, GET_NAME(ch), ISARE(typebuf));
		corpse->line_desc = strdup(buf2);
		strcpy(adj, "chlorinated");
		break;

	case SKILL_TURN:
		sprintf(buf2, "The burned up %s of %s %s lying here, finally still.",
			typebuf, GET_NAME(ch), isare);
		corpse->line_desc = strdup(buf2);
		strcpy(adj, "burned");
		break;

	case TYPE_FALLING:
		sprintf(buf2, "The splattered %s of %s %s lying here.",
			typebuf, GET_NAME(ch), isare);
		corpse->line_desc = strdup(buf2);
		strcpy(adj, "splattered");
		break;

		// attack that tears the victim's spine out
	case SKILL_PILEDRIVE:
		sprintf(buf2, "The shattered, twisted %s of %s %s lying here.",
			typebuf, GET_NAME(ch), isare);
		corpse->line_desc = strdup(buf2);
		strcpy(adj, "shattered");
		if (GET_MOB_VNUM(ch) == 1511) {
			if ((spine = read_object(1541)))
				obj_to_room(spine, ch->in_room);
		} else {
			spine = create_obj();
			spine->shared = null_obj_shared;
			spine->in_room = NULL;
			if (AFF2_FLAGGED(ch, AFF2_PETRIFIED)) {
				spine->aliases = strdup("spine spinal column stone");
				strcpy(buf2, "A stone spinal column is lying here.");
				spine->line_desc = strdup(buf2);
				strcpy(buf2, "a stone spinal column");
				spine->name = strdup(buf2);
				GET_OBJ_VAL(spine, 1) = 4;
			} else {
				spine->aliases = strdup("spine spinal column bloody");
				strcpy(buf2, "A bloody spinal column is lying here.");
				spine->line_desc = strdup(buf2);
				strcpy(buf2, "a bloody spinal column");
				spine->name = strdup(buf2);
				GET_OBJ_VAL(spine, 1) = 2;
			}

			GET_OBJ_TYPE(spine) = ITEM_WEAPON;
			GET_OBJ_WEAR(spine) = ITEM_WEAR_TAKE + ITEM_WEAR_WIELD;
			GET_OBJ_EXTRA(spine) = ITEM_NODONATE + ITEM_NOSELL;
			GET_OBJ_EXTRA2(spine) = ITEM2_BODY_PART;
			if (GET_LEVEL(ch) > number(30, 59))
				SET_BIT(GET_OBJ_EXTRA(spine), ITEM_HUM);
			GET_OBJ_VAL(spine, 0) = 0;
			GET_OBJ_VAL(spine, 2) = 6;
			GET_OBJ_VAL(spine, 3) = 5;
			spine->setWeight(5);
			GET_OBJ_MATERIAL(spine) = MAT_BONE;
			spine->worn_on = -1;
			obj_to_room(spine, ch->in_room);
		}
		break;

	case SPELL_TAINT:
	case TYPE_TAINT_BURN:
		if (IS_RACE(ch, RACE_BEHOLDER) || NON_CORPOREAL_MOB(ch)) {
			// attack that rips the victim's head off
			sprintf(buf2, "The smoking %s of %s %s lying here.",
				typebuf, GET_NAME(ch), isare);
			corpse->line_desc = strdup(buf2);
			sprintf(adj, "smoking");
			break;
		} else {
			// attack that rips the victim's head off
			sprintf(buf2, "The headless smoking %s of %s %s lying here.",
				typebuf, GET_NAME(ch), isare);
			corpse->line_desc = strdup(buf2);
			sprintf(adj, "headless smoking");
		}

		if (!is_arena_combat(killer, ch) &&
            !is_npk_combat(killer, ch) &&
			GET_LEVEL(ch) <= LVL_AMBASSADOR) {
			obj_data *o;
			/* transfer character's head EQ to room, if applicable */
			if (GET_EQ(ch, WEAR_HEAD))
				obj_to_room(unequip_char(ch, WEAR_HEAD, EQUIP_WORN), ch->in_room);
			if (GET_EQ(ch, WEAR_FACE))
				obj_to_room(unequip_char(ch, WEAR_FACE, EQUIP_WORN), ch->in_room);
			if (GET_EQ(ch, WEAR_EAR_L))
				obj_to_room(unequip_char(ch, WEAR_EAR_L, EQUIP_WORN),
					ch->in_room);
			if (GET_EQ(ch, WEAR_EAR_R))
				obj_to_room(unequip_char(ch, WEAR_EAR_R, EQUIP_WORN),
					ch->in_room);
			if (GET_EQ(ch, WEAR_EYES))
				obj_to_room(unequip_char(ch, WEAR_EYES, EQUIP_WORN), ch->in_room);
			/** transfer implants to ground **/
			if ((o = GET_IMPLANT(ch, WEAR_HEAD))) {
				obj_to_room(unequip_char(ch, WEAR_HEAD, EQUIP_IMPLANT),
					ch->in_room);
				SET_BIT(GET_OBJ_WEAR(o), ITEM_WEAR_TAKE);
			}
			if ((o = GET_IMPLANT(ch, WEAR_FACE))) {
				obj_to_room(unequip_char(ch, WEAR_FACE, EQUIP_IMPLANT),
					ch->in_room);
				SET_BIT(GET_OBJ_WEAR(o), ITEM_WEAR_TAKE);
			}
			if ((o = GET_IMPLANT(ch, WEAR_EAR_L))) {
				obj_to_room(unequip_char(ch, WEAR_EAR_L, EQUIP_IMPLANT),
					ch->in_room);
				SET_BIT(GET_OBJ_WEAR(o), ITEM_WEAR_TAKE);
			}
			if ((o = GET_IMPLANT(ch, WEAR_EAR_R))) {
				obj_to_room(unequip_char(ch, WEAR_EAR_R, EQUIP_IMPLANT),
					ch->in_room);
				SET_BIT(GET_OBJ_WEAR(o), ITEM_WEAR_TAKE);
			}
			if ((o = GET_IMPLANT(ch, WEAR_EYES))) {
				obj_to_room(unequip_char(ch, WEAR_EYES, EQUIP_IMPLANT),
					ch->in_room);
				SET_BIT(GET_OBJ_WEAR(o), ITEM_WEAR_TAKE);
			}
		}						// end if !arena room
		break;
	case SKILL_BEHEAD:
	case SKILL_PELE_KICK:
	case SKILL_CLOTHESLINE:
		sprintf(buf2, "The headless %s of %s %s lying here.",
			typebuf, GET_NAME(ch), isare);
		corpse->line_desc = strdup(buf2);
		sprintf(adj, "headless");

		if (IS_RACE(ch, RACE_BEHOLDER) || NON_CORPOREAL_MOB(ch))
			break;

		head = create_obj();
		head->shared = null_obj_shared;
		head->in_room = NULL;
		if (AFF2_FLAGGED(ch, AFF2_PETRIFIED))
			head->aliases = strdup("blood head skull stone");
		else
			head->aliases = strdup("blood head skull");

		sprintf(buf2, "The severed %shead of %s %s lying here.",
			AFF2_FLAGGED(ch, AFF2_PETRIFIED) ? "stone " : "", GET_NAME(ch),
			isare);
		head->line_desc = strdup(buf2);
		sprintf(buf2, "the severed %shead of %s",
			AFF2_FLAGGED(ch, AFF2_PETRIFIED) ? "stone " : "", GET_NAME(ch));
		head->name = strdup(buf2);
		GET_OBJ_TYPE(head) = ITEM_DRINKCON;
		GET_OBJ_WEAR(head) = ITEM_WEAR_TAKE;
		GET_OBJ_EXTRA(head) = ITEM_NODONATE | ITEM_NOSELL;
		GET_OBJ_EXTRA2(head) = ITEM2_BODY_PART;
		GET_OBJ_VAL(head, 0) = 5;	/* Head full of blood */
		GET_OBJ_VAL(head, 1) = 5;
		GET_OBJ_VAL(head, 2) = 13;

		head->worn_on = -1;

		if (AFF2_FLAGGED(ch, AFF2_PETRIFIED)) {
			GET_OBJ_MATERIAL(head) = MAT_STONE;
			head->setWeight(25);
		} else {
			GET_OBJ_MATERIAL(head) = MAT_FLESH;
			head->setWeight(10);
		}

		if (IS_NPC(ch))
			GET_OBJ_TIMER(head) = max_npc_corpse_time;
		else {
			GET_OBJ_TIMER(head) = max_pc_corpse_time;
		}
		obj_to_room(head, ch->in_room);
		if (!is_arena_combat(killer, ch) &&
            !is_npk_combat(killer, ch) &&
			GET_LEVEL(ch) <= LVL_AMBASSADOR) {
			obj_data *o;
			/* transfer character's head EQ to room, if applicable */
			if (GET_EQ(ch, WEAR_HEAD))
				obj_to_room(unequip_char(ch, WEAR_HEAD, EQUIP_WORN), ch->in_room);
			if (GET_EQ(ch, WEAR_FACE))
				obj_to_room(unequip_char(ch, WEAR_FACE, EQUIP_WORN), ch->in_room);
			if (GET_EQ(ch, WEAR_EAR_L))
				obj_to_room(unequip_char(ch, WEAR_EAR_L, EQUIP_WORN),
					ch->in_room);
			if (GET_EQ(ch, WEAR_EAR_R))
				obj_to_room(unequip_char(ch, WEAR_EAR_R, EQUIP_WORN),
					ch->in_room);
			if (GET_EQ(ch, WEAR_EYES))
				obj_to_room(unequip_char(ch, WEAR_EYES, EQUIP_WORN), ch->in_room);
			/** transfer implants to head **/
			if ((o = GET_IMPLANT(ch, WEAR_HEAD))) {
				obj_to_obj(unequip_char(ch, WEAR_HEAD, EQUIP_IMPLANT), head);
				REMOVE_BIT(GET_OBJ_WEAR(o), ITEM_WEAR_TAKE);
			}
			if ((o = GET_IMPLANT(ch, WEAR_FACE))) {
				obj_to_obj(unequip_char(ch, WEAR_FACE, EQUIP_IMPLANT), head);
				REMOVE_BIT(GET_OBJ_WEAR(o), ITEM_WEAR_TAKE);
			}
			if ((o = GET_IMPLANT(ch, WEAR_EAR_L))) {
				obj_to_obj(unequip_char(ch, WEAR_EAR_L, EQUIP_IMPLANT), head);
				REMOVE_BIT(GET_OBJ_WEAR(o), ITEM_WEAR_TAKE);
			}
			if ((o = GET_IMPLANT(ch, WEAR_EAR_R))) {
				obj_to_obj(unequip_char(ch, WEAR_EAR_R, EQUIP_IMPLANT), head);
				REMOVE_BIT(GET_OBJ_WEAR(o), ITEM_WEAR_TAKE);
			}
			if ((o = GET_IMPLANT(ch, WEAR_EYES))) {
				obj_to_obj(unequip_char(ch, WEAR_EYES, EQUIP_IMPLANT), head);
				REMOVE_BIT(GET_OBJ_WEAR(o), ITEM_WEAR_TAKE);
			}
		}						// end if !arena room
		break;

		// attack that rips the victim's heart out
	case SKILL_LUNGE_PUNCH:
		sprintf(buf2, "The maimed %s of %s %s lying here.", typebuf,
			GET_NAME(ch), isare);
		corpse->line_desc = strdup(buf2);
		strcpy(adj, "maimed");

		heart = create_obj();
		heart->shared = null_obj_shared;
		heart->in_room = NULL;
		if (AFF2_FLAGGED(ch, AFF2_PETRIFIED)) {
			GET_OBJ_TYPE(heart) = ITEM_OTHER;
			heart->aliases = strdup("heart stone");
			sprintf(buf2, "the stone heart of %s", GET_NAME(ch));
			heart->name = strdup(buf2);
		} else {
			GET_OBJ_TYPE(heart) = ITEM_FOOD;
			heart->aliases = strdup("heart bloody");
			sprintf(buf2, "the bloody heart of %s", GET_NAME(ch));
			heart->name = strdup(buf2);
		}

		sprintf(buf2, "The %sheart of %s %s lying here.",
			AFF2_FLAGGED(ch, AFF2_PETRIFIED) ? "stone " : "", GET_NAME(ch),
			isare);
		heart->line_desc = strdup(buf2);

		GET_OBJ_WEAR(heart) = ITEM_WEAR_TAKE + ITEM_WEAR_HOLD;
		GET_OBJ_EXTRA(heart) = ITEM_NODONATE | ITEM_NOSELL;
		GET_OBJ_EXTRA2(heart) = ITEM2_BODY_PART;
		GET_OBJ_VAL(heart, 0) = 10;
		if (IS_DEVIL(ch) || IS_DEMON(ch) || IS_LICH(ch)) {

			if (GET_CLASS(ch) == CLASS_GREATER || GET_CLASS(ch) == CLASS_ARCH
				|| GET_CLASS(ch) == CLASS_DUKE
				|| GET_CLASS(ch) == CLASS_DEMON_PRINCE
				|| GET_CLASS(ch) == CLASS_DEMON_LORD
				|| GET_CLASS(ch) == CLASS_LESSER || GET_LEVEL(ch) > 30) {
				GET_OBJ_VAL(heart, 1) = GET_LEVEL(ch);
				GET_OBJ_VAL(heart, 2) = SPELL_ESSENCE_OF_EVIL;
				if (GET_CLASS(ch) == CLASS_LESSER) {
					GET_OBJ_VAL(heart, 1) >>= 1;
				}
			} else {
				GET_OBJ_VAL(heart, 1) = 0;
				GET_OBJ_VAL(heart, 2) = 0;
			}
		} else {
			GET_OBJ_VAL(heart, 1) = 0;
			GET_OBJ_VAL(heart, 2) = 0;
		}

		heart->setWeight(0);
		heart->worn_on = -1;

		if (IS_NPC(ch))
			GET_OBJ_TIMER(heart) = max_npc_corpse_time;
		else {
			GET_OBJ_TIMER(heart) = max_pc_corpse_time;
		}
		obj_to_room(heart, ch->in_room);
		break;

	case SKILL_IMPALE:
		sprintf(buf2, "The run through %s of %s %s lying here.",
			typebuf, GET_NAME(ch), isare);
		corpse->line_desc = strdup(buf2);
		strcpy(adj, "impaled");
		break;

	case TYPE_DROWNING:
        if (room_is_watery(ch->in_room))
			sprintf(buf2, "The waterlogged %s of %s %s lying here.", typebuf,
				GET_NAME(ch), ISARE(typebuf));
		else
			sprintf(buf2, "The %s of %s %s lying here.", typebuf,
				GET_NAME(ch), ISARE(typebuf));

		corpse->line_desc = strdup(buf2);
		strcpy(adj, "drowned");
		break;

	default:
		sprintf(buf2, "The %s of %s %s lying here.",
			typebuf, GET_NAME(ch), isare);
		corpse->line_desc = strdup(buf2);
		strcpy(adj, "");
		break;
	}

	//  make the short name
	if (attacktype != TYPE_SWALLOW)
		sprintf(buf2, "the %s%s%s of %s", adj, *adj ? " " : "", typebuf,
			GET_NAME(ch));
	else
		strcpy(buf2, "a bloody pile of bones");
	corpse->name = strdup(buf2);

	// make the alias list
	strcat(strcat(strcat(strcat(namestr, " "), adj), " "), ch->player.name);
	if (namestr[strlen(namestr)] == ' ')
		namestr[strlen(namestr)] = '\0';
	corpse->aliases = strdup(namestr);

	// now flesh out the other vairables on the corpse
	GET_OBJ_TYPE(corpse) = ITEM_CONTAINER;
	GET_OBJ_WEAR(corpse) = ITEM_WEAR_TAKE;
	GET_OBJ_EXTRA(corpse) = ITEM_NODONATE;
	if (ch->isTester())
		SET_BIT(GET_OBJ_EXTRA2(corpse), ITEM2_UNAPPROVED);
	GET_OBJ_VAL(corpse, 0) = 0;	/* You can't store stuff in a corpse */
	GET_OBJ_VAL(corpse, 3) = 1;	/* corpse identifier */
	corpse->setWeight(GET_WEIGHT(ch) + IS_CARRYING_W(ch));
	corpse->contains = NULL;

	if (IS_NPC(ch)) {
		GET_OBJ_TIMER(corpse) = max_npc_corpse_time;
		CORPSE_IDNUM(corpse) = -ch->mob_specials.shared->vnum;
        corpse->obj_flags.max_dam = corpse->obj_flags.damage = 100;
	} else {
		GET_OBJ_TIMER(corpse) = max_pc_corpse_time;
		CORPSE_IDNUM(corpse) = GET_IDNUM(ch);
        corpse->obj_flags.max_dam = corpse->obj_flags.damage = -1; //!break player corpses
	}

	if (killer) {
		if (IS_NPC(killer))
			CORPSE_KILLER(corpse) = -GET_MOB_VNUM(killer);
		else
			CORPSE_KILLER(corpse) = GET_IDNUM(killer);
	} else if (dam_object)
		CORPSE_KILLER(corpse) = DAM_OBJECT_IDNUM(dam_object);

	// if non-arena room, transfer eq to corpse
	bool lose_eq = (!is_arena_combat(killer, ch) || IS_MOB(ch))
		&& GET_LEVEL(ch) < LVL_AMBASSADOR;

    bool lose_implants = !(is_npk_combat(killer,ch) ||
                           is_arena_combat(killer, ch) ||
                           (IS_MOB(ch) && GET_LEVEL(ch) > LVL_AMBASSADOR));

    bool lose_tattoos = lose_eq;

	obj_data *next_obj;

	/* transfer character's inventory to the corpse */
	for (o = ch->carrying;o != NULL;o = next_obj) {
		next_obj = o->next_content;
		if (lose_eq || o->isUnrentable()) {
			obj_from_char(o);
			obj_to_obj(o, corpse);
		}
	}

	/* transfer character's equipment to the corpse */
	for (i = 0; i < NUM_WEARS; i++) {
		if (GET_EQ(ch, i) && (lose_eq || GET_EQ(ch, i)->isUnrentable()))
			obj_to_obj(unequip_char(ch, i, EQUIP_WORN, true), corpse);
		if (GET_IMPLANT(ch, i) && (lose_implants || GET_IMPLANT(ch, i)->isUnrentable())) {
			REMOVE_BIT(GET_OBJ_WEAR(GET_IMPLANT(ch, i)), ITEM_WEAR_TAKE);
			obj_to_obj(unequip_char(ch, i, EQUIP_IMPLANT, true), corpse);
		}
        // Tattoos get discarded
        if (GET_TATTOO(ch, i) && lose_tattoos)
            extract_obj(unequip_char(ch, i, EQUIP_TATTOO, true));
	}

	/* transfer gold */
	if (GET_GOLD(ch) > 0 && lose_eq) {
		/* following 'if' clause added to fix gold duplication loophole */
		if (IS_NPC(ch) || (!IS_NPC(ch) && ch->desc)) {
			if ((money = create_money(GET_GOLD(ch), 0)))
				obj_to_obj(money, corpse);
		}
		GET_GOLD(ch) = 0;
	}
	if (GET_CASH(ch) > 0 && lose_eq) {
		/* following 'if' clause added to fix gold duplication loophole */
		if (IS_NPC(ch) || (!IS_NPC(ch) && ch->desc)) {
			if ((money = create_money(GET_CASH(ch), 1)))
				obj_to_obj(money, corpse);
		}
		GET_CASH(ch) = 0;
	}
	if (lose_eq) {
		ch->carrying = NULL;
		IS_CARRYING_N(ch) = 0;
		IS_CARRYING_W(ch) = 0;
	}

	// Remove all script objects if not an immortal
	if (!IS_IMMORT(ch)) {
		for (o = corpse->contains; o; o = next_o) {
			next_o = o->next_content;
			if (OBJ_TYPE(o, ITEM_SCRIPT))
				extract_obj(o);
		}
	}

	// leave no corpse behind
	if (NON_CORPOREAL_MOB(ch) || GET_MOB_SPEC(ch) == fate) {
		while ((o = corpse->contains)) {
			obj_from_obj(o);
			obj_to_room(o, ch->in_room);
		}
		extract_obj(corpse);
        corpse = NULL;
	} else {
		obj_to_room(corpse, ch->in_room);
        if (CORPSE_IDNUM(corpse) > 0 && !is_arena_combat(killer, ch)) {
            FILE *corpse_file;
            char *fname;

            fname = get_corpse_file_path(CORPSE_IDNUM(corpse));
            if ((corpse_file = fopen(fname, "w+")) != NULL) {
                fprintf(corpse_file, "<corpse>");
                corpse->saveToXML(corpse_file);
                fprintf(corpse_file, "</corpse>");
                fclose(corpse_file);
            }
            else  {
	            errlog("Failed to open corpse file [%s] (%s)", fname,
                     strerror(errno));
            }
        }
    }

    return corpse;
}

int calculate_attack_probability(struct Creature *ch)
{
    int prob;
    struct obj_data *weap = NULL;

    if (!ch->isFighting())
        return 0;

    prob = 1 + (GET_LEVEL(ch) / 7) + (GET_DEX(ch) << 1);

    if (IS_RANGER(ch) && (!GET_EQ(ch, WEAR_BODY) ||
        !OBJ_TYPE(GET_EQ(ch, WEAR_BODY), ITEM_ARMOR) ||
        !IS_METAL_TYPE(GET_EQ(ch, WEAR_BODY))))
        prob -= (GET_LEVEL(ch) >> 2);

    if (GET_EQ(ch, WEAR_WIELD_2))
        prob = calculate_weapon_probability(ch, prob, GET_EQ(ch, WEAR_WIELD_2));

    if (GET_EQ(ch, WEAR_WIELD))
        prob = calculate_weapon_probability(ch, prob, GET_EQ(ch, WEAR_WIELD));

    if (GET_EQ(ch, WEAR_HANDS))
        prob = calculate_weapon_probability(ch, prob, GET_EQ(ch, WEAR_HANDS));

    prob += (POS_FIGHTING - (ch->findRandomCombat()->getPosition()) << 1);

    if (CHECK_SKILL(ch, SKILL_DBL_ATTACK))
        prob += (int)((CHECK_SKILL(ch, SKILL_DBL_ATTACK) * 0.15) +
                (CHECK_SKILL(ch, SKILL_TRIPLE_ATTACK) * 0.17));

    if (CHECK_SKILL(ch, SKILL_MELEE_COMBAT_TAC) &&
        affected_by_spell(ch, SKILL_MELEE_COMBAT_TAC))
        prob += (int)(CHECK_SKILL(ch, SKILL_MELEE_COMBAT_TAC) * 0.10);

    if (affected_by_spell(ch, SKILL_OFFENSIVE_POS))
        prob += (int)(CHECK_SKILL(ch, SKILL_OFFENSIVE_POS) * 0.10);
    else if (affected_by_spell(ch, SKILL_DEFENSIVE_POS))
        prob -= (int)(CHECK_SKILL(ch, SKILL_DEFENSIVE_POS) * 0.05);

    if (IS_MERC(ch) && ((((weap = GET_EQ(ch, WEAR_WIELD)) && IS_GUN(weap)) ||
                        ((weap = GET_EQ(ch, WEAR_WIELD_2)) && IS_GUN(weap))) &&
                        CHECK_SKILL(ch, SKILL_SHOOT) > 50))
        prob += (int)(CHECK_SKILL(ch, SKILL_SHOOT) * 0.18);

    if (AFF_FLAGGED(ch, AFF_ADRENALINE))
        prob = (int)(prob * 1.10);

    if (AFF2_FLAGGED(ch, AFF2_HASTE))
        prob = (int)(prob * 1.30);

    if (ch->getSpeed())
        prob += (prob * ch->getSpeed()) / 100;

    if (AFF2_FLAGGED(ch, AFF2_SLOW))
        prob = (int)(prob * 0.70);

    if (SECT(ch->in_room) == SECT_ELEMENTAL_OOZE)
        prob = (int)(prob * 0.70);

    if (AFF2_FLAGGED(ch, AFF2_BERSERK))
        prob += (GET_LEVEL(ch) + (GET_REMORT_GEN(ch) << 2)) >> 1;

    if (IS_MONK(ch))
        prob += GET_LEVEL(ch) >> 2;

    if (AFF3_FLAGGED(ch, AFF3_DIVINE_POWER))
        prob += (ch->getLevelBonus(SPELL_DIVINE_POWER) / 3);

    if (ch->desc)
        prob -= ((MAX(0, ch->desc->wait >> 1)) * prob) / 100;
    else
        prob -= ((MAX(0, GET_MOB_WAIT(ch) >> 1)) * prob) / 100;

    prob -= ((((IS_CARRYING_W(ch) + IS_WEARING_W(ch)) << 5) * prob) /
            (CAN_CARRY_W(ch) * 85));

    if (GET_COND(ch, DRUNK) > 5)
        prob -= (int)((prob * 0.15) + (prob * (GET_COND(ch, DRUNK) / 100)));

    return prob;
}

#undef __combat_code__
#undef __combat_utils__
