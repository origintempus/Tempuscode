//
// File: quantum_rift.spec                     -- Part of TempusMUD
//
// Copyright 1999 by John Watson & John Rothe, all rights reserved.
//


SPECIAL(quantum_rift)
{
	struct obj_data *rift = (struct obj_data *) me;
	char arg1[MAX_INPUT_LENGTH];

	if (!CMD_IS("enter") || !CAN_SEE_OBJ(ch, rift) || !AWAKE(ch))
		return 0;
	one_argument(argument,arg1);
	if(!isname(arg1, rift->name))
		return 0;
	
	// Everytime someone enters, lower the timer by one.
    GET_OBJ_TIMER( rift ) = MAX(0,GET_OBJ_TIMER( rift ) - 1);
    return 1;
}
