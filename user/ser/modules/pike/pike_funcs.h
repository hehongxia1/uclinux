/* 
 * $Id: pike_funcs.h,v 1.11.6.1 2004/05/12 12:41:05 bogdan Exp $
 *
 *
 * Copyright (C) 2001-2003 Fhg Fokus
 *
 * This file is part of ser, a free SIP server.
 *
 * ser is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version
 *
 * For a license to use the ser software under conditions
 * other than those described here, or to purchase support for this
 * software, please contact iptel.org by e-mail at the following addresses:
 *    info@iptel.org
 *
 * ser is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this program; if not, write to the Free Software 
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


#ifndef _PIKE_FUNCS_H
#define _PIKE_FUNCS_H

#include "../../parser/msg_parser.h"
#include "../../locking.h"


//extern int                     time_unit;
//extern int                     max_value;
//extern gen_lock_t              *timer_lock;


int  pike_check_req(struct sip_msg *msg, char *foo, char *bar);
void clean_routine(unsigned int, void*);
void swap_routine(unsigned int, void*);


#endif
