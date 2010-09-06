//
// File: newbie_fly.spec                     -- Part of TempusMUD
//
// Copyright 1998 by John Watson, all rights reserved.
//

SPECIAL(newbie_fly)
{
	if (spec_mode != SPECIAL_CMD && spec_mode != SPECIAL_TICK)
		return 0;
	if (cmd || ch->fighting)
		return 0;
	struct creatureList_iterator it = ch->in_room->people.begin();
	for (; it != ch->in_room->people.end(); ++it) {
		if (AFF_FLAGGED((*it), AFF_INFLIGHT) || !can_see_creature(ch, (*it)))
			continue;
		cast_spell(ch, (*it), 0, NULL, SPELL_FLY);
		return 1;
	}
	return 0;
}