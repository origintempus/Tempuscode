#include <vector>

#include "actions.h"
#include "db.h"
#include "comm.h"
#include "handler.h"
#include "interpreter.h"
#include "tmpstr.h"
#include "screen.h"
#include "utils.h"

const char VENDOR_HELP[] =
"Directives:\r\n"
"    room <room vnum>\r\n"
"        if set, vendor will only work if the mob is in the specified room.\r\n"
"        leave unset for wandering vendors.\r\n"
"    produce <item vnum>\r\n"
"        (unimplemented) provides a limitless supply of the object\r\n"
"    accept <item type>|all\r\n"
"        Vendor will buy any item of the type configured by this directive.\r\n"
"    refuse <item type>|all\r\n"
"        Vendor will refuse to buy any item of the type configured by this\r\n"
"        directive\r\n"
"    refusal-msg <str to say>\r\n"
"        What is said when the player is unacceptable to the vendor.\r\n"
"    keeper-broke-msg <str to say>\r\n"
"        What is said when the vendor doesn't have enough money to buy\r\n"
"        an item from a player.\r\n"
"    buyer-broke-msg <str to say>\r\n"
"        What is said by the vendor when the player doesn't have enough\r\n"
"        money to buy the selected item.\r\n"
"    temper-cmd <cmd to execute>\r\n"
"        Command to execute after saying buyer-broke-msg\r\n"
"    buy-msg <str to say>\r\n"
"        String said by the vendor after a purchase\r\n"
"    sell-msg <str to say>\r\n"
"        String said by the vendor upon a successful sale.\r\n"
"    markup <percentage>\r\n"
"        Percent to mark price up when selling to a player\r\n"
"    markdown <percentage>\r\n"
"        Percent to mark price down when buying from a player\r\n"
"    currency gold|cash\r\n"
"        Which currency to use\r\n"
"    steal-ok yes|no\r\n"
"        If this is set, stealing from the vendor is allowed\r\n"
"    attack-ok yes|no\r\n";
"        If this is set, attacking the vendor is allowed\r\n"

// From shop.cc
int same_obj(obj_data *, obj_data *);
// From act.comm.cc
void perform_tell(struct Creature *ch, struct Creature *vict, char *arg);


struct ShopData {
	ShopData(void) : item_list(), item_types() {};

	long room;				// Room of self
	vector<int> item_list;	// list of produced items
	vector<int> item_types;	// list of types of items self deals in
	char *msg_denied;		// Message sent to those of wrong race, creed, etc
	char *msg_badobj;		// Attempt to sell invalid obj to self
	char *msg_selfbroke;	// Shop ran out of money
	char *msg_buyerbroke;	// Buyer doesn't have any money
	char *msg_buy;			// Keeper successfully bought something
	char *msg_sell;			// Keeper successfully sold something
	char *cmd_temper;		// Command to run after buyerbroke
	int markup;				// Price increase when player buying
	int markdown;			// Price decrease when player is selling
	bool currency;			// True == cash, False == gold
	bool steal_ok;
	bool attack_ok;
	Reaction reaction;
};

static int
vendor_inventory(Creature *self, obj_data *obj)
{
	obj_data *cur_obj;
	int cnt = 0;

	cur_obj = self->carrying;
	while (cur_obj && GET_OBJ_VNUM(cur_obj) != GET_OBJ_VNUM(obj))
		cur_obj = cur_obj->next_content;

	if (!cur_obj)
		return 0;

	while (same_obj(cur_obj, obj)) {
		cur_obj = cur_obj->next_content;
		cnt++;
	}

	return cnt;
}

static bool
vendor_invalid_buy(Creature *self, Creature *ch, ShopData *shop, obj_data *obj)
{
	if (GET_OBJ_COST(obj) < 1 || IS_OBJ_STAT(obj, ITEM_NOSELL) ||
			!OBJ_APPROVED(obj)) {
		do_say(self, tmp_sprintf("%s I don't buy that sort of thing.", GET_NAME(ch)),
			0, SCMD_SAY_TO, NULL);
		return true;
	}

	if (shop->item_types.size() > 0) {
		vector<int>::iterator it;
		bool accepted = false;
		for (it = shop->item_types.begin();it != shop->item_types.end();it++) {
			if ((*it & 0xFF) == GET_OBJ_TYPE(obj) || (*it & 0xFF) == 0) {
				accepted = *it >> 8;
				break;
			}
		}
		if (!accepted) {
			do_say(self, tmp_sprintf("%s %s", GET_NAME(ch),
				shop->msg_badobj), 0, SCMD_SAY_TO, NULL);
			return true;
		}
	}

	if (IS_OBJ_STAT2(obj, ITEM2_BROKEN)) {
		do_say(self, tmp_sprintf("%s I'm not buying something that's already broken.", GET_NAME(ch)),
			0, SCMD_SAY_TO, NULL);
		return true;
	}

	if (GET_OBJ_EXTRA3(obj) & ITEM3_HUNTED) {
		do_say(self, tmp_sprintf("%s This is hunted by the forces of Hell!  I'm not taking this!", GET_NAME(ch)),
			0, SCMD_SAY_TO, NULL);
		return true;
	}

	if (GET_OBJ_SIGIL_IDNUM(obj)) {
		do_say(self, tmp_sprintf("%s You'll have to remove that warding sigil before I'll bother.", GET_NAME(ch)),
			0, SCMD_SAY_TO, NULL);
		return true;
	}

	if (vendor_inventory(self, obj) >= 5) {
		do_say(self, tmp_sprintf("%s No thanks.  I've got too many of those in stock already.", GET_NAME(ch)),
			0, SCMD_SAY_TO, NULL);
		return true;
	}

	// Adjust cost for missing charges
	if (GET_OBJ_TYPE(obj) == ITEM_WAND || GET_OBJ_TYPE(obj) == ITEM_STAFF) {
		if (GET_OBJ_VAL(obj, 2) == 0) {
			do_say(self, tmp_sprintf("%s I don't buy used up wands or staves!", GET_NAME(ch)),
				0, SCMD_SAY_TO, NULL);
			return true;
		}
	}
	return false;
}

// Gets the value of an object, checking for buyability.
static long
vendor_get_value(obj_data *obj, int percent)
{
	long cost;

	// Adjust cost for wear and tear on a direct percentage basis
	if (GET_OBJ_DAM(obj) != -1 && GET_OBJ_MAX_DAM(obj) != -1)
		percent = percent *  GET_OBJ_DAM(obj) / GET_OBJ_MAX_DAM(obj);

	// Adjust cost for missing charges
	if (GET_OBJ_TYPE(obj) == ITEM_WAND || GET_OBJ_TYPE(obj) == ITEM_STAFF)
		percent = percent * GET_OBJ_VAL(obj, 2) / GET_OBJ_VAL(obj, 1);

	cost = GET_OBJ_COST(obj) * percent / 100;
	if (OBJ_REINFORCED(obj))
		cost += cost >> 2;
	if (OBJ_ENHANCED(obj))
		cost += cost >> 2;

	return cost;
}

static obj_data *
vendor_resolve_hash(Creature *self, char *obj_str)
{
	obj_data *last_obj = NULL, *cur_obj;
	int num;

	if (*obj_str != '#') {
		slog("Can't happen in vendor_resolve_hash()");
		return NULL;
	}

	num = atoi(obj_str + 1);
	if (num <= 0)
		return NULL;

	for (cur_obj = self->carrying;cur_obj;cur_obj = cur_obj->next_content) {
		if (!last_obj || !same_obj(last_obj, cur_obj))
			if (--num == 0)
				return cur_obj;
		last_obj = cur_obj;
	}

	return cur_obj;
}

static obj_data *
vendor_resolve_name(Creature *self, char *obj_str)
{
	obj_data *cur_obj;

	for (cur_obj = self->carrying;cur_obj;cur_obj = cur_obj->next_content)
		if (isname(obj_str, cur_obj->name))
			return cur_obj;

	return NULL;
}

static void
vendor_sell(Creature *ch, char *arg, Creature *self, ShopData *shop)
{
	obj_data *obj, *next_obj;
	char *obj_str, *currency_str, *msg;
	int num, cost, amt_carried;

	if (!*arg) {
		send_to_char(ch, "What do you wish to sell?\r\n");
		return;
	}

	obj_str = tmp_getword(&arg);
	if (is_number(obj_str)) {
		num = atoi(obj_str);
		if (num < 0) {
			do_say(self,
				tmp_sprintf("%s You want to buy a negative amount? Try selling.", GET_NAME(ch)), 
				0, SCMD_SAY_TO, NULL);
			return;
		} else if (num == 0) {
			do_say(self,
				tmp_sprintf("%s You wanted to buy something?", GET_NAME(ch)), 
				0, SCMD_SAY_TO, NULL);
			return;
		}

		obj_str = tmp_getword(&arg);
	} else {
		num = 1;
	}

	// Check for hash mark
	if (*obj_str == '#')
		obj = vendor_resolve_hash(self, obj_str);
	else
		obj = vendor_resolve_name(self, obj_str);

	if (!obj) {
		do_say(self,
			tmp_sprintf("%s Sorry, but I don't carry that item.",
			GET_NAME(ch)), 0, SCMD_SAY_TO, NULL);
		return;
	}

	if (num > 1) {
		int obj_cnt = vendor_inventory(self, obj);
		if (num > obj_cnt) {
			do_say(self,
				tmp_sprintf("%s I only have %d to sell to you.",
				GET_NAME(ch), obj_cnt), 0, SCMD_SAY_TO, NULL);
			num = obj_cnt;
		}
	}

	cost = vendor_get_value(obj, shop->markup);
	amt_carried = (shop->currency) ? GET_CASH(ch):GET_GOLD(ch);
	
	if (cost > amt_carried) {
		do_say(self, tmp_sprintf("%s %s.",
			GET_NAME(ch), shop->msg_buyerbroke), 0, SCMD_SAY_TO, NULL);
		if (shop->cmd_temper)
			command_interpreter(self, shop->cmd_temper);
		return;
	}
	
	if (cost * num > amt_carried) {
		num = amt_carried / cost;
		do_say(self,
			tmp_sprintf("%s You can only have enough to buy %d.",
				GET_NAME(ch), num), 0, SCMD_SAY_TO, NULL);
	}

	if (IS_CARRYING_N(ch) + 1 > CAN_CARRY_N(ch)) {
		do_say(self,
			tmp_sprintf("%s You can't carry any more items.",
				GET_NAME(ch)), 0, SCMD_SAY_TO, NULL);
		return;
	}

	if (IS_CARRYING_W(ch) + obj->getWeight() > CAN_CARRY_W(ch)) {
		switch (number(0,2)) {
		case 0:
			do_say(self,
				tmp_sprintf("%s You can't carry any more weight.",
					GET_NAME(ch)), 0, SCMD_SAY_TO, NULL);
			break;
		case 1:
			do_say(self,
				tmp_sprintf("%s You can't carry that much weight.",
					GET_NAME(ch)), 0, SCMD_SAY_TO, NULL);
			break;
		case 2:
			do_say(self,
				tmp_sprintf("%s You can carry no more weight.",
					GET_NAME(ch)), 0, SCMD_SAY_TO, NULL);
			break;
		}
		return;
	}

	if (IS_CARRYING_N(ch) + num > CAN_CARRY_N(ch) ||
			IS_CARRYING_W(ch) + obj->getWeight() * num > CAN_CARRY_W(ch)) {
		num = MIN(num, CAN_CARRY_N(ch) - IS_CARRYING_N(ch));
		num = MIN(num, (CAN_CARRY_W(ch) - IS_CARRYING_W(ch))
			/ obj->getWeight());
		do_say(self,
			tmp_sprintf("%s You can only carry %d.",
				GET_NAME(ch), num), 0, SCMD_SAY_TO, NULL);
	}

	if (shop->currency) {
		GET_CASH(ch) -= cost * num;
		GET_CASH(self) += cost * num;
		currency_str = "creds";
	} else {
		GET_GOLD(ch) -= cost * num;
		GET_GOLD(self) += cost * num;
		currency_str = "gold";
	}


	do_say(self,
		tmp_sprintf("%s %s.",
			GET_NAME(ch), shop->msg_buy), 0, SCMD_SAY_TO, NULL);
	msg = tmp_sprintf("You sell $p to $N for %d %s.",
		cost * num, currency_str);
	act((num == 1) ? msg:tmp_sprintf("%s (x%d)", msg, num),
		false, self, obj, ch, TO_CHAR);
	msg = tmp_sprintf("$n sells $p to you for %d %s.",
		cost * num, currency_str);
	act((num == 1) ? msg:tmp_sprintf("%s (x%d)", msg, num),
		false, self, obj, ch, TO_VICT);
	act("$n sells $p to $N.", false, self, obj, ch, TO_NOTVICT);

	while (num) {
		next_obj = obj->next_content;
		obj_from_char(obj);
		obj_to_char(obj, ch);
		obj = next_obj;
		num--;
	}

}

static void
vendor_buy(Creature *ch, char *arg, Creature *self, ShopData *shop)
{
	obj_data *obj;
	char *obj_str;
	long cost;
	int num = 1;

	if (!*arg) {
		send_to_char(ch, "What do you wish to sell?\r\n");
		return;
	}

	obj_str = tmp_getword(&arg);
	if (is_number(obj_str)) {
		num = atoi(obj_str);
		if (num < 0) {
			do_say(self,
				tmp_sprintf("%s You want to sell a negative amount? Try buying.", GET_NAME(ch)), 
				0, SCMD_SAY_TO, NULL);
			return;
		} else if (num == 0) {
			do_say(self,
				tmp_sprintf("%s You wanted to sell something?", GET_NAME(ch)), 
				0, SCMD_SAY_TO, NULL);
			return;
		}

		obj_str = tmp_getword(&arg);
	} else {
		num = 1;
	}

	obj = get_obj_in_list_all(ch, obj_str, ch->carrying);
	if (!obj) {
		send_to_char(ch, "You don't seem to have any '%s'.\r\n", obj_str);
		return;
	}

	if (vendor_invalid_buy(self, ch, shop, obj))
		return;

	cost = vendor_get_value(obj, shop->markdown);
	if ((shop->currency) ? GET_CASH(self):GET_GOLD(self) < cost) {
		do_say(self, tmp_sprintf("%s %s", GET_NAME(ch), shop->msg_selfbroke),
			0, SCMD_SAY_TO, NULL);
		return;
	}

	do_say(self, tmp_sprintf("%s %s", GET_NAME(ch), shop->msg_sell),
		0, SCMD_SAY_TO, NULL);

	if (shop->currency)
		perform_give_credits(self, ch, cost);
	else
		perform_give_gold(self, ch, cost);

	obj_from_char(obj);
	obj_to_char(obj, self);

	// repair object
	if (GET_OBJ_DAM(obj) != -1 && GET_OBJ_MAX_DAM(obj) != -1)
		GET_OBJ_DAM(obj) = GET_OBJ_MAX_DAM(obj);
}

char *
vendor_list_obj(Creature *ch, obj_data *obj, int cnt, int idx, int cost)
{
	char *obj_desc;

	obj_desc = obj->short_description;
	if (GET_OBJ_TYPE(obj) == ITEM_DRINKCON && GET_OBJ_VAL(obj, 1))
		obj_desc = tmp_strcat(obj_desc, " of %s", drinks[GET_OBJ_VAL(obj, 1)]);
	if (GET_OBJ_TYPE(obj) == ITEM_WAND || GET_OBJ_TYPE(obj) == ITEM_STAFF &&
			GET_OBJ_VAL(obj, 2) < GET_OBJ_VAL(obj, 1))
		obj_desc = tmp_strcat(obj_desc, " (partially used)");
	if (OBJ_REINFORCED(obj))
		obj_desc = tmp_strcat(obj_desc, " [reinforced]");
	if (OBJ_ENHANCED(obj))
		obj_desc = tmp_strcat(obj_desc, " |augmented|");
	if (IS_OBJ_STAT2(obj, ITEM2_BROKEN))
		obj_desc = tmp_strcat(obj_desc, " <broken>");
	if (IS_OBJ_STAT(obj, ITEM_HUM))
		obj_desc = tmp_strcat(obj_desc, " (humming)");
	if (IS_OBJ_STAT(obj, ITEM_GLOW))
		obj_desc = tmp_strcat(obj_desc, " (glowing)");
	if (IS_OBJ_STAT(obj, ITEM_INVISIBLE))
		obj_desc = tmp_strcat(obj_desc, " (invisible)");
	if (IS_OBJ_STAT(obj, ITEM_TRANSPARENT))
		obj_desc = tmp_strcat(obj_desc, " (transparent)");
	if (IS_AFFECTED(ch, AFF_DETECT_ALIGN)) {
		if (IS_OBJ_STAT(obj, ITEM_BLESS))	
			obj_desc = tmp_strcat(obj_desc, " (holy aura)");
		if (IS_OBJ_STAT(obj, ITEM_EVIL_BLESS))
			obj_desc = tmp_strcat(obj_desc, " (unholy aura)");
	}

	obj_desc = tmp_capitalize(obj_desc);
	return tmp_sprintf(" %2d%s)  %s%5d%s       %-48s %6d\r\n",
		idx, CCRED(ch, C_NRM), CCYEL(ch, C_NRM), cnt, CCNRM(ch, C_NRM),
		obj_desc, cost);
}

static void
vendor_list(Creature *ch, char *arg, Creature *self, ShopData *shop)
{
	obj_data *cur_obj, *last_obj;
	int idx, cnt;
	char *msg;

	if (!self->carrying) {
		do_say(self,
			tmp_sprintf("%s I'm out of stock at the moment", GET_NAME(ch)), 
			0, SCMD_SAY_TO, NULL);
		return;
	}

	msg = tmp_strcat(CCCYN(ch, C_NRM),
" ##   Available   Item                                               Cost\r\n"
"-------------------------------------------------------------------------\r\n"
		, CCNRM(ch, C_NRM), NULL);

	last_obj = NULL;
	for (cur_obj = self->carrying;cur_obj;cur_obj = cur_obj->next_content) {
		if (!last_obj)
			cnt = idx = 1;
		else if (same_obj(last_obj, cur_obj)) {
			cnt++;
		} else {
			msg = tmp_strcat(msg, vendor_list_obj(ch, last_obj, cnt, idx,
				vendor_get_value(last_obj, shop->markup)));
			cnt = 1;
			idx++;
		}
		last_obj = cur_obj;
	}
	if (last_obj)
		msg = tmp_strcat(msg, vendor_list_obj(ch, last_obj, cnt, idx,
			vendor_get_value(last_obj, shop->markup)));

	act("$n peruses the shop's wares.", false, ch, 0, 0, TO_ROOM);
	page_string(ch->desc, msg);
}

static void
vendor_value(Creature *ch, char *arg, Creature *self, ShopData *shop)
{
	obj_data *obj;
	char *obj_str;
	int cost;
	char *msg;

	if (!*arg) {
		send_to_char(ch, "What do you wish to value?\r\n");
		return;
	}

	obj_str = tmp_getword(&arg);
	obj = get_obj_in_list_all(ch, obj_str, ch->carrying);
	if (!obj) {
		send_to_char(ch, "You don't seem to have any %s.\r\n", obj_str);
		return;
	}

	if (vendor_invalid_buy(self, ch, shop, obj))
		return;

	cost = vendor_get_value(obj, shop->markdown);

	msg = tmp_sprintf("%s I'll give you %d %s for it!", GET_NAME(ch),
		cost, shop->currency ? "cash":"gold");
	do_say(self, msg, 0, SCMD_SAY_TO, NULL);
}

SPECIAL(vendor)
{
	Creature *self;
	char *config, *line, *param_key, *err = NULL;
	ShopData shop;
	int val, lineno = 0;

	if (spec_mode == SPECIAL_HELP) {
		page_string(ch->desc, VENDOR_HELP);
		return 1;
	}

	if (spec_mode != SPECIAL_CMD)
		return 0;	

	self = (Creature *)me;
	config = GET_MOB_PARAM(self);
	if (!config)
		return 0;

	if (!(CMD_IS("buy") || CMD_IS("sell") || CMD_IS("list") || CMD_IS("value") || CMD_IS("offer") || CMD_IS("steal")))
		return 0;

	// Initialize default values
	shop.room = -1;
	shop.msg_denied = "I'm not buying that.";
	shop.msg_badobj = "I don't buy that sort of thing.";
	shop.msg_selfbroke = "Sorry, but I don't have the cash.";
	shop.msg_buyerbroke = "You don't have enough money to buy this!";
	shop.msg_buy = "Here you go.";
	shop.msg_sell = "There you go.";
	shop.cmd_temper = NULL;
	shop.markdown = 70;
	shop.markup = 120;
	shop.currency = false;
	shop.steal_ok = false;
	shop.attack_ok = false;

	while ((line = tmp_getline(&config)) != NULL) {
		lineno++;
		if (shop.reaction.add_reaction(line))
			continue;

		param_key = tmp_getword(&line);
		if (!strcmp(param_key, "room")) {
			shop.room = atol(line);
		} else if (!strcmp(param_key, "produce")) {
			val = atoi(line);
			if (val <= 0 || !real_object_proto(val)) {
				err = "non-existant produced item";
				break;
			}
			shop.item_list.push_back(atoi(line));
		} else if (!strcmp(param_key, "accept")) {
			if (strcmp(line, "all")) {
				val = search_block(line, item_types, 0);
				if (val <= 0) {
					err = "an invalid accept line";
					break;
				}
			} else
				val = 0;
			shop.item_types.push_back( 1 << 8 | val);
		} else if (!strcmp(param_key, "refuse")) {
			if (strcmp(line, "all")) {
				val = search_block(line, item_types, 0);
				if (val <= 0) {
					err = "an invalid accept line";
					break;
				}
			} else
				val = 0;
			shop.item_types.push_back( 0 << 8 | val);
		} else if (!strcmp(param_key, "denied-msg")) {
			shop.msg_denied = line;
		} else if (!strcmp(param_key, "keeper-broke-msg")) {
			shop.msg_selfbroke= line;
		} else if (!strcmp(param_key, "buyer-broke-msg")) {
			shop.msg_buyerbroke = line;
		} else if (!strcmp(param_key, "buy-msg")) {
			shop.msg_buy = line;
		} else if (!strcmp(param_key, "sell-msg")) {
			shop.msg_sell = line;
		} else if (!strcmp(param_key, "temper-cmd")) {
			shop.cmd_temper = line;
		} else if (!strcmp(param_key, "markup")) {
			shop.markup= atoi(line);
		} else if (!strcmp(param_key, "markdown")) {
			shop.markdown= atoi(line);
		} else if (!strcmp(param_key, "currency")) {
			if (is_abbrev(line, "cash"))
				shop.currency = true;
			else if (is_abbrev(line, "gold"))
				shop.currency = false;
			else {
				err = "invalid currency";
				break;
			}
		} else if (!strcmp(param_key, "steal-ok")) {
			shop.steal_ok = (is_abbrev(line, "yes") || is_abbrev(line, "on") ||
				is_abbrev(line, "1") || is_abbrev(line, "true"));
		} else if (!strcmp(param_key, "attack-ok")) {
			shop.attack_ok = (is_abbrev(line, "yes") || is_abbrev(line, "on") ||
				is_abbrev(line, "1") || is_abbrev(line, "true"));
		} else {
			err = "invalid directive";
			break;
		}
	}

	if (err) {
		// Specparam error
		if (IS_PC(ch)) {
			if (IS_IMMORT(ch))
				perform_tell(self, ch, tmp_sprintf(
					"I have %s in line %d of my specparam", err, lineno));
			else {
				mudlog(LVL_IMMORT, NRM, true,
					"ERR: Mobile %d has %s in line %d of specparam",
					GET_MOB_VNUM(self), err, lineno);
				do_say(self, tmp_sprintf(
					"%s Sorry.  I'm broken, but a god has already been notified.",
					GET_NAME(ch)), 0, SCMD_SAY_TO, NULL);
			}
		}
		return true;
	}

	if (CMD_IS("steal") && !shop.steal_ok && GET_LEVEL(ch) < LVL_IMMORT) {
		do_gen_comm(self, tmp_sprintf("%s is a bloody thief!", GET_NAME(ch)),
			0, SCMD_SHOUT, 0);
		return true;
	}
	
	if (shop.room != -1 && shop.room != self->in_room->number) {
		do_say(self, tmp_sprintf("%s Catch me when I'm in my store.",
			GET_NAME(ch)), 0, SCMD_SAY_TO, 0);
		return true;
	}

	if (shop.reaction.react(ch) != ALLOW) {
		do_say(self, tmp_sprintf("%s %s", GET_NAME(ch), shop.msg_denied),
			0, SCMD_SAY_TO, 0);
		return true;
	}

	if (CMD_IS("buy"))
		vendor_sell(ch, argument, self, &shop);
	else if (CMD_IS("sell"))
		vendor_buy(ch, argument, self, &shop);
	else if (CMD_IS("list"))
		vendor_list(ch, argument, self, &shop);
	else if (CMD_IS("value") || CMD_IS("offer"))
		vendor_value(ch, argument, self, &shop);
	else
		return false;
	
	return true;
}
