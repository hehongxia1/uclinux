/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * UserEvent application -- send manager event
 * 
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <asterisk/lock.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>
#include <asterisk/manager.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

static char *tdesc = "Custom User Event Application";

static char *app = "UserEvent";

static char *synopsis = "Send an arbitrary event to the manager interface";

static char *descrip = 
"  UserEvent(eventname[|body]): Sends an arbitrary event to the\n"
"manager interface, with an optional body representing additional\n"
"arguments.  The format of the event will be:\n"
"    Event: UserEvent<specified event name>\n"
"    Channel: <channel name>\n"
"    Uniqueid: <call uniqueid>\n"
"    [body]\n"
"If the body is not specified, only Event, Channel, and Uniqueid fields\n"
"will be present.  Returns 0.";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int userevent_exec(struct ast_channel *chan, void *data)
{
	struct localuser *u;
	char info[512];
    char eventname[512];
	char *eventbody;

	if (!data || !strlen(data)) {
		ast_log(LOG_WARNING, "UserEvent requires an argument (eventname|optional event body)\n");
		return -1;
	}

	strncpy(info, (char *)data, strlen((char *)data) + AST_MAX_EXTENSION-1);
	snprintf(eventname, sizeof(eventname), "UserEvent%s", info);
	eventbody = strchr(eventname, '|');
	if (eventbody) {
		*eventbody = '\0';
		eventbody++;
	}
	LOCAL_USER_ADD(u);

	if(eventbody) {
            ast_log(LOG_DEBUG, "Sending user event: %s, %s\n", eventname, eventbody);
            manager_event(EVENT_FLAG_CALL, eventname, 
			"Channel: %s\r\nUniqueid: %s\r\n%s\r\n",
			chan->name, chan->uniqueid, eventbody);
	} else {
            ast_log(LOG_DEBUG, "Sending user event: %s\n", eventname);
            manager_event(EVENT_FLAG_CALL, eventname, 
			"Channel: %s\r\nUniqueid: %s\r\n", chan->name, chan->uniqueid);
	}

	LOCAL_USER_REMOVE(u);
	return 0;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return ast_unregister_application(app);
}

int load_module(void)
{
	return ast_register_application(app, userevent_exec, synopsis, descrip);
}

char *description(void)
{
	return tdesc;
}

int usecount(void)
{
	int res;
	STANDARD_USECOUNT(res);
	return res;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
