/*
 * This includes functions that are meant to live entirely in .rodata
 * (via objcopy tricks), to validate the non-executability of .rodata.
 */
#include "lkdtm.h"

void lkdtm_rodata_do_nothing(void)
{
	/* Does nothing. We just want an architecture agnostic "return". */
}
