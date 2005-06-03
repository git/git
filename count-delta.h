/*
 * Copyright (C) 2005 Junio C Hamano
 */
#ifndef COUNT_DELTA_H
#define COUNT_DELTA_H

int count_delta(void *, unsigned long,
		unsigned long *src_copied, unsigned long *literal_added);

#endif
