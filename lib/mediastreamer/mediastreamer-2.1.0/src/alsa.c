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



#include <alsa/asoundlib.h>


#include "mediastreamer2/msfilter.h"
#include "mediastreamer2/mssndcard.h"

static MSSndCard * alsa_card_new(int id);
static MSSndCard *alsa_card_duplicate(MSSndCard *obj);
static MSFilter * ms_alsa_read_new(const char *dev);
static MSFilter * ms_alsa_write_new(const char *dev);


struct _AlsaData{
	char *pcmdev;
	char *mixdev;
};

typedef struct _AlsaData AlsaData;


static int alsa_set_params(snd_pcm_t *pcm_handle, int rw, int bits, int stereo, int rate)
{
	snd_pcm_hw_params_t *hwparams=NULL;
	snd_pcm_sw_params_t *swparams=NULL;
	int dir;
	uint exact_uvalue;
	unsigned long exact_ulvalue;
	int channels;
	int periods=8;
	int periodsize=256;
	int err;
	int format;
	
	/* Allocate the snd_pcm_hw_params_t structure on the stack. */
	snd_pcm_hw_params_alloca(&hwparams);
	
	/* Init hwparams with full configuration space */
	if (snd_pcm_hw_params_any(pcm_handle, hwparams) < 0) {
		ms_warning("alsa_set_params: Cannot configure this PCM device.\n");
		return -1;
	}
	
	if (snd_pcm_hw_params_set_access(pcm_handle, hwparams, SND_PCM_ACCESS_RW_INTERLEAVED) < 0) {
		ms_warning("alsa_set_params: Error setting access.\n");
		return -1;
	}
	/* Set sample format */
	format=SND_PCM_FORMAT_S16;
	if (snd_pcm_hw_params_set_format(pcm_handle, hwparams, format) < 0) {
		ms_warning("alsa_set_params: Error setting format.\n");
		return -1;
	}
	/* Set number of channels */
	if (stereo) channels=2;
	else channels=1;
	if (snd_pcm_hw_params_set_channels(pcm_handle, hwparams, channels) < 0) {
		ms_warning("alsa_set_params: Error setting channels.\n");
		return -1;
	}
	/* Set sample rate. If the exact rate is not supported */
	/* by the hardware, use nearest possible rate.         */ 
	exact_uvalue=rate;
	dir=0;
	if ((err=snd_pcm_hw_params_set_rate_near(pcm_handle, hwparams, &exact_uvalue, &dir))<0){
		ms_warning("alsa_set_params: Error setting rate to %i:%s",rate,snd_strerror(err));
		return -1;
	}
	if (dir != 0) {
		ms_warning("alsa_set_params: The rate %d Hz is not supported by your hardware.\n "
		"==> Using %d Hz instead.\n", rate, exact_uvalue);
	}
	/* choose greater period size when rate is high */
	periodsize=periodsize*(rate/8000);	
	
	/* Set buffer size (in frames). The resulting latency is given by */
	/* latency = periodsize * periods / (rate * bytes_per_frame)     */
	/* set period size */
	exact_ulvalue=periodsize;
	dir=0;
	if (snd_pcm_hw_params_set_period_size_near(pcm_handle, hwparams, &exact_ulvalue, &dir) < 0) {
		ms_warning("alsa_set_params: Error setting period size.\n");
		return -1;
	}
	if (dir != 0) {
		ms_warning("alsa_set_params: The period size %d is not supported by your hardware.\n "
		"==> Using %d instead.\n", periodsize, (int)exact_ulvalue);
	}
	periodsize=exact_ulvalue;
	/* Set number of periods. Periods used to be called fragments. */ 
	exact_uvalue=periods;
	dir=0;
	if (snd_pcm_hw_params_set_periods_near(pcm_handle, hwparams, &exact_uvalue, &dir) < 0) {
		ms_warning("alsa_set_params: Error setting periods.\n");
		return -1;
	}
	if (dir != 0) {
		ms_warning("alsa_set_params: The number of periods %d is not supported by your hardware.\n "
		"==> Using %d instead.\n", periods, exact_uvalue);
	}
	/* Apply HW parameter settings to */
	/* PCM device and prepare device  */
	if ((err=snd_pcm_hw_params(pcm_handle, hwparams)) < 0) {
		ms_warning("alsa_set_params: Error setting HW params:%s",snd_strerror(err));
		return -1;
	}
	/*prepare sw params */
	if (rw){
		snd_pcm_sw_params_alloca(&swparams);
		snd_pcm_sw_params_current(pcm_handle, swparams);
		if ((err=snd_pcm_sw_params_set_start_threshold(pcm_handle, swparams,periodsize*2 ))<0){
			ms_warning("alsa_set_params: Error setting start threshold:%s",snd_strerror(err));
			return -1;
		}
		if ((err=snd_pcm_sw_params(pcm_handle, swparams))<0){
			ms_warning("alsa_set_params: Error setting SW params:%s",snd_strerror(err));
			return -1;
		}
	}
	return 0;	
}

static snd_pcm_t * alsa_open_r(const char *pcmdev,int bits,int stereo,int rate)
{
	snd_pcm_t *pcm_handle;
	int err;

	if (snd_pcm_open(&pcm_handle, pcmdev,SND_PCM_STREAM_CAPTURE,SND_PCM_NONBLOCK) < 0) {
		ms_warning("alsa_card_open_r: Error opening PCM device %s\n",pcmdev );
		return NULL;
	}
	if (alsa_set_params(pcm_handle,0,bits,stereo,rate)<0){
		snd_pcm_close(pcm_handle);
		return NULL;
	}
	err=snd_pcm_start(pcm_handle);
	if (err<0){
		ms_warning("Cannot start read pcm: %s", snd_strerror(err));
	}
	return pcm_handle;
}

static snd_pcm_t * alsa_open_w(const char *pcmdev,int bits,int stereo,int rate)
{
	snd_pcm_t *pcm_handle;
	
	if (snd_pcm_open(&pcm_handle, pcmdev,SND_PCM_STREAM_PLAYBACK,SND_PCM_NONBLOCK) < 0) {
		ms_warning("alsa_card_open_r: Error opening PCM device %s\n",pcmdev );
		return NULL;
	}
	if (alsa_set_params(pcm_handle,1,bits,stereo,rate)<0){
		snd_pcm_close(pcm_handle);
		return NULL;
	}
	return pcm_handle;
}


static int alsa_read(snd_pcm_t *handle,unsigned char *buf,int nsamples)
{
	int err;
	err=snd_pcm_readi(handle,buf,nsamples);
	if (err<0) {
		if (err==-EPIPE){
			snd_pcm_prepare(handle);
			err=snd_pcm_readi(handle,buf,nsamples);
			if (err<0) ms_warning("alsa_read: snd_pcm_readi() failed:%s.",snd_strerror(err));
		}else if (err!=-EWOULDBLOCK){
			ms_warning("alsa_read: snd_pcm_readi() failed:%s.",snd_strerror(err));
		}
	}else if (err==0){
		ms_warning("alsa_read: snd_pcm_readi() returned 0");
	}
	return err;
}


static int alsa_write(snd_pcm_t *handle,unsigned char *buf,int nsamples)
{
	int err;
	if ((err=snd_pcm_writei(handle,buf,nsamples))<0){
		if (err==-EPIPE){
			snd_pcm_prepare(handle);
			err=snd_pcm_writei(handle,buf,nsamples);
			if (err<0) ms_warning("alsa_card_write: Error writing sound buffer (nsamples=%i):%s",nsamples,snd_strerror(err));
		}else if (err!=-EWOULDBLOCK){
			ms_warning("alsa_card_write: snd_pcm_writei() failed:%s.",snd_strerror(err));
		}
	}else if (err!=nsamples) {
		ms_warning("Only %i samples written instead of %i",err,nsamples);
	}
	return err;
}


static snd_mixer_t *alsa_mixer_open(const char *mixdev){
	snd_mixer_t *mixer=NULL;
	int err;
	err=snd_mixer_open(&mixer,0);
	if (err<0){
		ms_warning("Could not open alsa mixer: %s",snd_strerror(err));
		return NULL;
	}
	if ((err = snd_mixer_attach (mixer, mixdev)) < 0){
		ms_warning("Could not attach mixer to card: %s",snd_strerror(err));
		snd_mixer_close(mixer);
		return NULL;
	}
	if ((err = snd_mixer_selem_register (mixer, NULL, NULL)) < 0){
		ms_warning("snd_mixer_selem_register: %s",snd_strerror(err));
		snd_mixer_close(mixer);
		return NULL;
	}
	if ((err = snd_mixer_load (mixer)) < 0){
		ms_warning("snd_mixer_load: %s",snd_strerror(err));
		snd_mixer_close(mixer);
		return NULL;
	}
	return mixer;
}

static void alsa_mixer_close(snd_mixer_t *mix){
	snd_mixer_close(mix);
}

typedef enum {CAPTURE, PLAYBACK, CAPTURE_SWITCH, PLAYBACK_SWITCH} MixerAction;

static int get_mixer_element(snd_mixer_t *mixer,const char *name, MixerAction action){
	long value=0;
	const char *elemname;
	snd_mixer_elem_t *elem;
	int err;
	long sndMixerPMin=0;
	long sndMixerPMax=0;
	long newvol=0;
	elem=snd_mixer_first_elem(mixer);
	while (elem!=NULL){
		elemname=snd_mixer_selem_get_name(elem);
		//g_message("Found alsa mixer element %s.",elemname);
		if (strcmp(elemname,name)==0){
			switch (action){
				case CAPTURE:
				if (snd_mixer_selem_has_capture_volume(elem)){
					snd_mixer_selem_get_capture_volume_range(elem, &sndMixerPMin, &sndMixerPMax);
					err=snd_mixer_selem_get_capture_volume(elem,SND_MIXER_SCHN_UNKNOWN,&newvol);
					newvol-=sndMixerPMin;
					value=(100*newvol)/(sndMixerPMax-sndMixerPMin);
					if (err<0) ms_warning("Could not get capture volume for %s:%s",name,snd_strerror(err));
					//else ms_message("Succesfully get capture level for %s.",elemname);
					break;
				}
				break;
				case PLAYBACK:
				if (snd_mixer_selem_has_playback_volume(elem)){
					snd_mixer_selem_get_playback_volume_range(elem, &sndMixerPMin, &sndMixerPMax);
					err=snd_mixer_selem_get_playback_volume(elem,SND_MIXER_SCHN_FRONT_LEFT,&newvol);
					newvol-=sndMixerPMin;
					value=(100*newvol)/(sndMixerPMax-sndMixerPMin);
					if (err<0) ms_warning("Could not get playback volume for %s:%s",name,snd_strerror(err));
					//else g_message("Succesfully get playback level for %s.",elemname);
					break;
				}
				break;
				case CAPTURE_SWITCH:
				
				break;
				case PLAYBACK_SWITCH:

				break;
			}
		}
		elem=snd_mixer_elem_next(elem);
	}
	
	return value;
}


static void set_mixer_element(snd_mixer_t *mixer,const char *name, int level,MixerAction action){
	const char *elemname;
	snd_mixer_elem_t *elem;
	long sndMixerPMin=0;
	long sndMixerPMax=0;
	long newvol=0;
	
	elem=snd_mixer_first_elem(mixer);
	
	while (elem!=NULL){
		elemname=snd_mixer_selem_get_name(elem);
		//g_message("Found alsa mixer element %s.",elemname);
		if (strcmp(elemname,name)==0){
			switch(action){
				case CAPTURE:
				if (snd_mixer_selem_has_capture_volume(elem)){
					snd_mixer_selem_get_capture_volume_range(elem, &sndMixerPMin, &sndMixerPMax);
					newvol=(((sndMixerPMax-sndMixerPMin)*level)/100)+sndMixerPMin;
					snd_mixer_selem_set_capture_volume_all(elem,newvol);
					//g_message("Succesfully set capture level for %s.",elemname);
					return;
				}
				break;
				case PLAYBACK:
				if (snd_mixer_selem_has_playback_volume(elem)){
					snd_mixer_selem_get_playback_volume_range(elem, &sndMixerPMin, &sndMixerPMax);
					newvol=(((sndMixerPMax-sndMixerPMin)*level)/100)+sndMixerPMin;
					snd_mixer_selem_set_playback_volume_all(elem,newvol);
					//g_message("Succesfully set playback level for %s.",elemname);
					return;
				}
				break;
				case CAPTURE_SWITCH:
				if (snd_mixer_selem_has_capture_switch(elem)){
					snd_mixer_selem_set_capture_switch_all(elem,level);
					//g_message("Succesfully set capture switch for %s.",elemname);
				}
				break;
				case PLAYBACK_SWITCH:
				if (snd_mixer_selem_has_playback_switch(elem)){
					snd_mixer_selem_set_playback_switch_all(elem,level);
					//g_message("Succesfully set capture switch for %s.",elemname);
				}
				break;

			}
		}
		elem=snd_mixer_elem_next(elem);
	}

	return ;
}


static void alsa_card_set_level(MSSndCard *obj,MSSndCardMixerElem e,int a)
{	
	snd_mixer_t *mixer;
	AlsaData *ad=(AlsaData*)obj->data;
	mixer=alsa_mixer_open(ad->mixdev);
	if (mixer==NULL) return ;
	switch(e){
		case MS_SND_CARD_MASTER:
			set_mixer_element(mixer,"Master",a,PLAYBACK);
		break;
		case MS_SND_CARD_CAPTURE:
			set_mixer_element(mixer,"Capture",a,CAPTURE);
		break;
		case MS_SND_CARD_PLAYBACK:
			set_mixer_element(mixer,"PCM",a,PLAYBACK);
		break;
		default:
			ms_warning("alsa_card_set_level: unsupported command.");
	}
	alsa_mixer_close(mixer);
}

static int alsa_card_get_level(MSSndCard *obj, MSSndCardMixerElem e)
{
	snd_mixer_t *mixer;
	AlsaData *ad=(AlsaData*)obj->data;
	int value = -1;
	mixer=alsa_mixer_open(ad->mixdev);
	if (mixer==NULL) return 0;
	switch(e){
		case MS_SND_CARD_MASTER:
			value=get_mixer_element(mixer,"Master",PLAYBACK);
			break;
		case MS_SND_CARD_CAPTURE:
			value=get_mixer_element(mixer,"Capture",CAPTURE);
			break;
		case MS_SND_CARD_PLAYBACK:
			value=get_mixer_element(mixer,"PCM",PLAYBACK);
			break;
		default:
			ms_warning("alsa_card_set_level: unsupported command.");
	}
	alsa_mixer_close(mixer);
	return value;
}

static void alsa_card_set_source(MSSndCard *obj,MSSndCardCapture source)
{
	snd_mixer_t *mixer;
	AlsaData *ad=(AlsaData*)obj->data;
	mixer=alsa_mixer_open(ad->mixdev);
	if (mixer==NULL) return;
	switch (source){
		case MS_SND_CARD_MIC:
			set_mixer_element(mixer,"Mic",1,CAPTURE_SWITCH);
			set_mixer_element(mixer,"Capture",1,CAPTURE_SWITCH);
			break;
		case MS_SND_CARD_LINE:
			set_mixer_element(mixer,"Line",1,CAPTURE_SWITCH);
			set_mixer_element(mixer,"Capture",1,CAPTURE_SWITCH);
			break;
	}
	alsa_mixer_close(mixer);
}

static MSFilter *alsa_card_create_reader(MSSndCard *card)
{
	AlsaData *ad=(AlsaData*)card->data;
	MSFilter *f=ms_alsa_read_new(ad->pcmdev);
	return f;
}

static MSFilter *alsa_card_create_writer(MSSndCard *card)
{
	AlsaData *ad=(AlsaData*)card->data;
	MSFilter *f=ms_alsa_write_new(ad->pcmdev);
	return f;
}


static void alsa_card_init(MSSndCard *obj){
	AlsaData *ad=ms_new0(AlsaData,1);
	obj->data=ad;
}

static void alsa_card_uninit(MSSndCard *obj){
	AlsaData *ad=(AlsaData*)obj->data;
	if (ad->pcmdev!=NULL) ms_free(ad->pcmdev);
	if (ad->mixdev!=NULL) ms_free(ad->mixdev);
}


static void alsa_card_detect(MSSndCardManager *m){
	int i;
	for (i=-1;i<10;i++){
		MSSndCard *card=alsa_card_new(i);
		if (card!=NULL)
			ms_snd_card_manager_add_card(m,card);
	}
}

MSSndCardDesc alsa_card_desc={
	.driver_type="ALSA",
	.detect=alsa_card_detect,
	.init=alsa_card_init,
	.set_level=alsa_card_set_level,
	.get_level=alsa_card_get_level,
	.set_capture=alsa_card_set_source,
	.create_reader=alsa_card_create_reader,
	.create_writer=alsa_card_create_writer,
	.uninit=alsa_card_uninit,
	.duplicate=alsa_card_duplicate
};

static MSSndCard *alsa_card_duplicate(MSSndCard *obj){
	MSSndCard *card=ms_snd_card_new(&alsa_card_desc);
	AlsaData* dcard=(AlsaData*)card->data;
	AlsaData* dobj=(AlsaData*)obj->data;
	card->name=ms_strdup(obj->name);
	dcard->pcmdev=ms_strdup(dobj->pcmdev);
	dcard->mixdev=ms_strdup(dobj->mixdev);
	return obj;
}

static MSSndCard * alsa_card_new(int id)
{
	MSSndCard * obj;
	char *name=NULL;
	AlsaData *ad;
	int err;
	if (id!=-1){
		err=snd_card_get_name(id,&name);
		if (err<0) {
			return NULL;
		}
	}
	obj=ms_snd_card_new(&alsa_card_desc);
	ad=(AlsaData*)obj->data;
	if (id==-1) {
		/* the default pcm device */
		obj->name=ms_strdup("default device");
		ad->pcmdev=ms_strdup("default");
		ad->mixdev=ms_strdup("default");
	}else{
		/* remove trailing spaces from card name */
		char *pos1, *pos2;
		pos1=ms_strdup(name);
		pos2=pos1+strlen(pos1)-1;
		for (; pos2>pos1 && *pos2==' '; pos2--) *pos2='\0';
		obj->name=pos1;
		ad->pcmdev=ms_strdup_printf("plughw:%i,0",id);
		ad->mixdev=ms_strdup_printf("hw:%i",id);
	}
	free(name);
	/*ms_message("alsa device %s found",obj->name);*/
	return obj;
}

struct _AlsaReadData{
	char *pcmdev;
	snd_pcm_t *handle;
	int rate;
	int nchannels;
};

typedef struct _AlsaReadData AlsaReadData;

void alsa_read_init(MSFilter *obj){
	AlsaReadData *ad=ms_new(AlsaReadData,1);
	ad->pcmdev=NULL;
	ad->handle=NULL;
	ad->rate=8000;
	ad->nchannels=1;
	obj->data=ad;
}

void alsa_read_postprocess(MSFilter *obj){
	AlsaReadData *ad=(AlsaReadData*)obj->data;
	if (ad->handle!=NULL) snd_pcm_close(ad->handle);
	ad->handle=NULL;
}

void alsa_read_uninit(MSFilter *obj){
	AlsaReadData *ad=(AlsaReadData*)obj->data;
	if (ad->pcmdev!=NULL) ms_free(ad->pcmdev);
	if (ad->handle!=NULL) snd_pcm_close(ad->handle);
	ms_free(ad);
}

static bool_t alsa_can_read(snd_pcm_t *dev, int frames){
	return snd_pcm_avail_update(dev)>=frames;
}

void alsa_read_process(MSFilter *obj){
	AlsaReadData *ad=(AlsaReadData*)obj->data;
#ifdef AMD_HACK
	int samples=(160*ad->rate)/8000;
#else
	int samples=(128*ad->rate)/8000;
#endif
	int err;
	mblk_t *om=NULL;
	if (ad->handle==NULL && ad->pcmdev!=NULL){
		ad->handle=alsa_open_r(ad->pcmdev,16,ad->nchannels==2,ad->rate);
	}
	if (ad->handle==NULL) return;
	while (alsa_can_read(ad->handle,samples)){
		int size=samples*2;
		om=allocb(size,0);
		if ((err=alsa_read(ad->handle,om->b_wptr,samples))<=0) {
			ms_warning("Fail to read samples");
			freemsg(om);
			return;
		}
		size=err*2;
		om->b_wptr+=size;
		/*ms_message("alsa_read_process: Outputing %i bytes",size);*/
		ms_queue_put(obj->outputs[0],om);
#ifdef AMD_HACK
		break;
#endif
	}
}

static int alsa_read_set_sample_rate(MSFilter *obj, void *param){
	AlsaReadData *ad=(AlsaReadData*)obj->data;
	ad->rate=*((int*)param);
	return 0;
}

static int alsa_read_set_nchannels(MSFilter *obj, void *param){
	AlsaReadData *ad=(AlsaReadData*)obj->data;
	ad->nchannels=*((int*)param);
	return 0;
}

MSFilterMethod alsa_read_methods[]={
	{MS_FILTER_SET_SAMPLE_RATE, alsa_read_set_sample_rate},
	{MS_FILTER_SET_SAMPLE_RATE, alsa_read_set_nchannels},
	{0,NULL}
};

MSFilterDesc alsa_read_desc={
	.id=MS_ALSA_READ_ID,
	.name="MSAlsaRead",
	.text="Alsa sound source",
	.category=MS_FILTER_OTHER,
	.ninputs=0,
	.noutputs=1,
	.init=alsa_read_init,
	.process=alsa_read_process,
	.postprocess=alsa_read_postprocess,
	.uninit=alsa_read_uninit,
	.methods=alsa_read_methods
};

static MSFilter * ms_alsa_read_new(const char *dev){
	MSFilter *f=ms_filter_new_from_desc(&alsa_read_desc);
	AlsaReadData *ad=(AlsaReadData*)f->data;
	ad->pcmdev=ms_strdup(dev);
	return f;
}

typedef struct _AlsaReadData AlsaWriteData;

void alsa_write_init(MSFilter *obj){
	AlsaWriteData *ad=ms_new(AlsaWriteData,1);
	ad->pcmdev=NULL;
	ad->handle=NULL;
	ad->rate=8000;
	ad->nchannels=1;
	obj->data=ad;
}

void alsa_write_postprocess(MSFilter *obj){
	AlsaReadData *ad=(AlsaReadData*)obj->data;
	if (ad->handle!=NULL) snd_pcm_close(ad->handle);
	ad->handle=NULL;
}

void alsa_write_uninit(MSFilter *obj){
	AlsaWriteData *ad=(AlsaWriteData*)obj->data;
	if (ad->pcmdev!=NULL) ms_free(ad->pcmdev);
	if (ad->handle!=NULL) snd_pcm_close(ad->handle);
	ms_free(ad);
}

int alsa_write_set_sample_rate(MSFilter *obj, void *data){
	int *rate=(int*)data;
	AlsaWriteData *ad=(AlsaWriteData*)obj->data;
	ad->rate=*rate;
	return 0;
}

int alsa_write_set_nchannels(MSFilter *obj, void *data){
	int *n=(int*)data;
	AlsaWriteData *ad=(AlsaWriteData*)obj->data;
	ad->nchannels=*n;
	return 0;
}

void alsa_write_process(MSFilter *obj){
	AlsaWriteData *ad=(AlsaWriteData*)obj->data;
	mblk_t *im=NULL;
	int size;
	int samples;
	int err;
	if (ad->handle==NULL && ad->pcmdev!=NULL){
		ad->handle=alsa_open_w(ad->pcmdev,16,ad->nchannels==2,ad->rate);
	}
	if (ad->handle==NULL) {
		ms_queue_flush(obj->inputs[0]);
		return;
	}
	while ((im=ms_queue_get(obj->inputs[0]))!=NULL){
		while((size=im->b_wptr-im->b_rptr)>0){
			samples=size/(2*ad->nchannels);
			err=alsa_write(ad->handle,im->b_rptr,samples);
			if (err>0) im->b_rptr+=err*(2*ad->nchannels);
			else break;
		}
		freemsg(im);
	}
}

MSFilterMethod alsa_write_methods[]={
	{MS_FILTER_SET_SAMPLE_RATE, alsa_write_set_sample_rate},
	{MS_FILTER_SET_NCHANNELS, alsa_write_set_nchannels},
	{0,NULL}
};

MSFilterDesc alsa_write_desc={
	.id=MS_ALSA_WRITE_ID,
	.name="MSAlsaWrite",
	.text="Alsa sound output",
	.category=MS_FILTER_OTHER,
	.ninputs=1,
	.noutputs=0,
	.init=alsa_write_init,
	.process=alsa_write_process,
	.postprocess=alsa_write_postprocess,
	.uninit=alsa_write_uninit,
	.methods=alsa_write_methods
};


static MSFilter * ms_alsa_write_new(const char *dev){
	MSFilter *f=ms_filter_new_from_desc(&alsa_write_desc);
	AlsaWriteData *ad=(AlsaWriteData*)f->data;
	ad->pcmdev=ms_strdup(dev);
	return f;
}


MS_FILTER_DESC_EXPORT(alsa_write_desc)

MS_FILTER_DESC_EXPORT(alsa_read_desc)

