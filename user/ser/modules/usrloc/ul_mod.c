/*
 * $Id: ul_mod.c,v 1.31.4.1.2.1 2004/07/21 10:34:45 sobomax Exp $
 *
 * Usrloc module interface
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
 *
 *
 * History:
 * ---------
 * 2003-01-27 timer activity printing #ifdef-ed to EXTRA_DEBUG (jiri)
 * 2003-03-11 New module interface (janakj)
 * 2003-03-12 added replication and state columns (nils)
 * 2003-03-16 flags export parameter added (janakj)
 * 2003-04-05: default_uri #define used (jiri)
 * 2003-04-21 failed fifo init stops init process (jiri)
 */


#include "ul_mod.h"
#include <stdio.h>
#include "../../sr_module.h"
#include "../../dprint.h"
#include "../../timer.h"     /* register_timer */
#include "../../globals.h"   /* is_main */
#include "dlist.h"           /* register_udomain */
#include "udomain.h"         /* {insert,delete,get,release}_urecord */
#include "urecord.h"         /* {insert,delete,get}_ucontact */
#include "ucontact.h"        /* update_ucontact */
#include "ul_fifo.h"
#include "notify.h"
#include "usrloc.h"

MODULE_VERSION


static int mod_init(void);                          /* Module initialization function */
static void destroy(void);                          /* Module destroy function */
static void timer(unsigned int ticks, void* param); /* Timer handler */
static int child_init(int rank);                    /* Per-child init function */

extern int bind_usrloc(usrloc_api_t* api);

/*
 * Module parameters and their default values
 */
char* user_col        = "username";       /* Name of column containing usernames */
char* domain_col      = "domain";         /* Name of column containing domains */
char* contact_col     = "contact";        /* Name of column containing contact addresses */
char* expires_col     = "expires";        /* Name of column containing expires values */
char* q_col           = "q";              /* Name of column containing q values */
char* callid_col      = "callid";         /* Name of column containing callid string */
char* cseq_col        = "cseq";           /* Name of column containing cseq values */
char* method_col      = "method";         /* Name of column containing supported method */
char* replicate_col   = "replicate";      /* Name of column containing replication mark */
char* state_col       = "state";          /* Name of column containing contact state */
char* flags_col       = "flags";          /* Name of column containing flags */
char* user_agent_col  = "user_agent";     /* Name of column containing user agent string */
char* db_url          = DEFAULT_DB_URL;   /* Database URL */
int   timer_interval  = 60;               /* Timer interval in seconds */
int   db_mode         = NO_DB;            /* Database sync scheme: 0-no db, 1-write through, 2-write back */
int   use_domain      = 0;                /* Whether usrloc should use domain part of aor */
int   desc_time_order = 0;                /* By default do not enable timestamp ordering */                  


db_con_t* db; /* Database connection handle */

/*
 * Exported functions
 */
static cmd_export_t cmds[] = {
	{"ul_register_udomain",   (cmd_function)register_udomain,   1, 0, 0},
	{"ul_insert_urecord",     (cmd_function)insert_urecord,     1, 0, 0},
	{"ul_delete_urecord",     (cmd_function)delete_urecord,     1, 0, 0},
	{"ul_get_urecord",        (cmd_function)get_urecord,        1, 0, 0},
	{"ul_lock_udomain",       (cmd_function)lock_udomain,       1, 0, 0},
	{"ul_unlock_udomain",     (cmd_function)unlock_udomain,     1, 0, 0},
	{"ul_release_urecord",    (cmd_function)release_urecord,    1, 0, 0},
	{"ul_insert_ucontact",    (cmd_function)insert_ucontact,    1, 0, 0},
	{"ul_delete_ucontact",    (cmd_function)delete_ucontact,    1, 0, 0},
	{"ul_get_ucontact",       (cmd_function)get_ucontact,       1, 0, 0},
	{"ul_get_all_ucontacts",  (cmd_function)get_all_ucontacts,  1, 0, 0},
	{"ul_update_ucontact",    (cmd_function)update_ucontact,    1, 0, 0},
	{"ul_register_watcher",   (cmd_function)register_watcher,   1, 0, 0},
	{"ul_unregister_watcher", (cmd_function)unregister_watcher, 1, 0, 0},
	{"ul_bind_usrloc",        (cmd_function)bind_usrloc,        1, 0, 0},
	{0, 0, 0, 0, 0}
};


/* 
 * Exported parameters 
 */
static param_export_t params[] = {
	{"user_column",       STR_PARAM, &user_col       },
	{"domain_column",     STR_PARAM, &domain_col     },
	{"contact_column",    STR_PARAM, &contact_col    },
	{"expires_column",    STR_PARAM, &expires_col    },
	{"q_column",          STR_PARAM, &q_col          },
	{"callid_column",     STR_PARAM, &callid_col     },
	{"cseq_column",       STR_PARAM, &cseq_col       },
	{"method_column",     STR_PARAM, &method_col     },
	{"replicate_column",  STR_PARAM, &replicate_col  },
	{"state_column",      STR_PARAM, &state_col      },
	{"flags_column",      STR_PARAM, &flags_col      },
	{"db_url",            STR_PARAM, &db_url         },
	{"timer_interval",    INT_PARAM, &timer_interval },
	{"db_mode",           INT_PARAM, &db_mode        },
	{"use_domain",        INT_PARAM, &use_domain     },
	{"desc_time_order",   INT_PARAM, &desc_time_order},
	{"user_agent_column", STR_PARAM, &user_agent_col },
	{0, 0, 0}
};


#ifdef STATIC_USRLOC
struct module_exports usrloc_exports = {
#else
struct module_exports exports = {
#endif
	"usrloc",
	cmds,       /* Exported functions */
	params,     /* Export parameters */
	mod_init,   /* Module initialization function */
	0,          /* Response function */
	destroy,    /* Destroy function */
	0,          /* OnCancel function */
	child_init  /* Child initialization function */
};


/*
 * Module initialization function
 */
static int mod_init(void)
{
	DBG("usrloc - initializing\n");

	     /* Register cache timer */
	register_timer(timer, 0, timer_interval);

	     /* Initialize fifo interface */
	if (init_ul_fifo()<0) {
		LOG(L_ERR, "ERROR: usrloc/fifo initialization failed\n");
		return -1;
	}

	     /* Shall we use database ? */
	if (db_mode != NO_DB) { /* Yes */
		if (bind_dbmod() < 0) { /* Find database module */
			LOG(L_ERR, "mod_init(): Can't bind database module\n");
			return -1;
		}
		
		     /* Open database connection in parent */
		db = db_init(db_url);
		if (!db) {
			LOG(L_ERR, "mod_init(): Error while connecting database\n");
			return -1;
		} else {
			LOG(L_INFO, "mod_init(): Database connection opened successfuly\n");
		}
	}

	return 0;
}


static int child_init(int _rank)
{
 	     /* Shall we use database ? */
	if (db_mode != NO_DB) { /* Yes */
		db_close(db); /* Close connection previously opened by parent */
		db = db_init(db_url); /* Initialize a new separate connection */
		if (!db) {
			LOG(L_ERR, "child_init(%d): Error while connecting database\n", _rank);
			return -1;
		}
	}

	return 0;
}


/*
 * Module destroy function
 */
static void destroy(void)
{
	     /* Parent only, synchronize the world
	      * and then nuke it
	      */
	if (is_main) {
		if (synchronize_all_udomains() != 0) {
			LOG(L_ERR, "timer(): Error while flushing cache\n");
		}
		free_all_udomains();
	}
	
	     /* All processes close database connection */
	if (db) db_close(db);
}


/*
 * Timer handler
 */
static void timer(unsigned int ticks, void* param)
{
#ifdef EXTRA_DEBUG
	DBG("Running timer\n");
#endif
	if (synchronize_all_udomains() != 0) {
		LOG(L_ERR, "timer(): Error while synchronizing cache\n");
	}
#ifdef EXTRA_DEBUG
	DBG("Timer done\n");
#endif
}

