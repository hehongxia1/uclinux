/*
mediastreamer2 library - modular sound and video processing and streaming
Copyright (C) 2006  Simon MORLAT (simon.morlat@linphone.org)

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#include "mediastreamer2/mssndcard.h"

static MSSndCardManager *scm=NULL;

static MSSndCardManager * create_manager(){
	MSSndCardManager *obj=ms_new(MSSndCardManager,1);
	obj->cards=NULL;
	return obj;
}

void ms_snd_card_manager_destroy(void){
	if (scm!=NULL){
		ms_list_for_each(scm->cards,(void (*)(void*))ms_snd_card_destroy);
		ms_list_free(scm->cards);
	}
	scm=NULL;
}

MSSndCardManager * ms_snd_card_manager_get(void){
	if (scm==NULL) scm=create_manager();
	return scm;
}

MSSndCard * ms_snd_card_manager_get_card(MSSndCardManager *m, const char *id){
	MSList *elem;
	for (elem=m->cards;elem!=NULL;elem=elem->next){
		MSSndCard *card=(MSSndCard*)elem->data;
		if (strcmp(ms_snd_card_get_string_id(card),id)==0)	return card;
	}
	ms_warning("no card with id %s",id);
	return NULL;
}

MSSndCard * ms_snd_card_manager_get_default_card(MSSndCardManager *m){
	if (m->cards!=NULL) return (MSSndCard*)m->cards->data;
	return NULL;
}

const MSList * ms_snd_card_manager_get_list(MSSndCardManager *m){
	return m->cards;
}

void ms_snd_card_manager_add_card(MSSndCardManager *m, MSSndCard *c){
	ms_message("Card %s added",ms_snd_card_get_string_id(c));
	m->cards=ms_list_append(m->cards,c);
}

void ms_snd_card_manager_register_desc(MSSndCardManager *m, MSSndCardDesc *desc){
	if (desc->detect!=NULL)
		desc->detect(m);
}

MSSndCard * ms_snd_card_dup(MSSndCard *card){
	MSSndCard *obj=NULL;
	if (card->desc->duplicate!=NULL)
		obj=card->desc->duplicate(card);
	return obj;
}

MSSndCard * ms_snd_card_new(MSSndCardDesc *desc){
	MSSndCard *obj=ms_new(MSSndCard,1);
	obj->desc=desc;
	obj->name=NULL;
	obj->data=NULL;
	obj->id=NULL;
	if (desc->init!=NULL)
		desc->init(obj);
	return obj;
}

const char *ms_snd_card_get_driver_type(const MSSndCard *obj){
	return obj->desc->driver_type;
}

const char *ms_snd_card_get_name(const MSSndCard *obj){
	return obj->name;
}

const char *ms_snd_card_get_string_id(MSSndCard *obj){
	if (obj->id==NULL)	obj->id=ms_strdup_printf("%s: %s",obj->desc->driver_type,obj->name);
	return obj->id;
}

void ms_snd_card_set_level(MSSndCard *obj, MSSndCardMixerElem e, int percent){
	if (obj->desc->set_level!=NULL)
		obj->desc->set_level(obj,e,percent);
	else ms_warning("ms_snd_card_set_capture: unimplemented by %s wrapper",obj->desc->driver_type);
}

int ms_snd_card_get_level(MSSndCard *obj, MSSndCardMixerElem e){
	if (obj->desc->get_level!=NULL)
		return obj->desc->get_level(obj,e);
	else {
		ms_warning("ms_snd_card_set_capture: unimplemented by %s wrapper",obj->desc->driver_type);
		return -1;
	}
}

void ms_snd_card_set_capture(MSSndCard *obj, MSSndCardCapture c){
	if (obj->desc->set_capture!=NULL)
		obj->desc->set_capture(obj,c);
	else ms_warning("ms_snd_card_set_capture: unimplemented by %s wrapper",obj->desc->driver_type);
}

struct _MSFilter * ms_snd_card_create_reader(MSSndCard *obj){
	if (obj->desc->create_reader!=NULL)
		return obj->desc->create_reader(obj);
	else ms_warning("ms_snd_card_create_reader: unimplemented by %s wrapper",obj->desc->driver_type);
	return NULL;
}

struct _MSFilter * ms_snd_card_create_writer(MSSndCard *obj){
	if (obj->desc->create_writer!=NULL)
		return obj->desc->create_writer(obj);
	else ms_warning("ms_snd_card_create_writer: unimplemented by %s wrapper",obj->desc->driver_type);
	return NULL;
}

void ms_snd_card_destroy(MSSndCard *obj){
	if (obj->desc->uninit!=NULL) obj->desc->uninit(obj);
	if (obj->name!=NULL) ms_free(obj->name);
	if (obj->id!=NULL)	ms_free(obj->id);
	ms_free(obj);
}
