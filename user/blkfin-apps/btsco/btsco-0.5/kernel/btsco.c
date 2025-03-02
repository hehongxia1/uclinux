/*
 *  Bluetooth SCO soundcard
 *  Copyright (c) 2003, 2004 by Jonathan Paisley <jp@dcs.gla.ac.uk>
 *
 *  Based on dummy.c which is
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */


/* note: defining these two independently is not tested, 
 * thus not recommended
 */

/* enable dynamic compression */
#define DYNAMIC_COMPRESSION
/* enable automatic endianness fixup */
#define AUTO_FIXUP_BYTESHIFT


#ifdef DYNAMIC_COMPRESSION
/* Autoadjust mic at most this often in 1/8000s */
#define GRABSAMPLES 400
/* Maximum push for the mike 16= 1:1 - default 20:1 = 320 */
#define COMPRESSION_MAX_16 320
/* Minimum push for the mike  1= 1:16 */
#define COMPRESSION_MIN_16 1
#endif

#define chip_t snd_card_bt_sco_t

#include <sound/driver.h>
#include <linux/version.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/socket.h>
#include <linux/file.h>
#include <linux/completion.h>
#include <linux/smp_lock.h>
#include <net/sock.h>
#include <net/bluetooth/bluetooth.h>

#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/rawmidi.h>
#include <sound/hwdep.h>
#define SNDRV_GET_ID
#include <sound/initval.h>

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,15)
#include <linux/mutex.h>
#else
#define mutex semaphore
#define mutex_init init_MUTEX
#define mutex_lock down
#define mutex_unlock up
#endif

//#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,12)
#include <linux/freezer.h>
//#endif

#ifndef SNDRV_HWDEP_IFACE_BLUETOOTH
#define SNDRV_HWDEP_IFACE_BLUETOOTH (SNDRV_HWDEP_IFACE_EMUX_WAVETABLE + 1)
#endif

#ifndef SNDRV_HWDEP_IFACE_BT_SCO
#define SNDRV_HWDEP_IFACE_BT_SCO (SNDRV_HWDEP_IFACE_BLUETOOTH + 1)
#endif

#define SNDRV_BT_SCO_IOCTL_SET_SCO_SOCKET _IOW ('H', 0x10, int)
#define SNDRV_BT_SCO_IOCTL_REQ_INFO _IO ('H', 0x11)

MODULE_AUTHOR("Jonathan Paisley <jp@dcs.gla.ac.uk>");
MODULE_DESCRIPTION("Bluetooth SCO Headset Soundcard");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{ALSA,Bluetooth SCO Soundcard}}");

static char *mod_revision = "$Revision: 1.13 $";

static int index[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = -2}; /* Exclude the first card */

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for Bluetooth SCO Headset Soundcard.");

#undef dprintk
#if 1
#define dprintk(fmt...) printk(KERN_INFO "snd-bt-sco: " fmt)
#else
#define dprintk(fmt...) do {} while(0)
#endif

#define MAX_BUFFER_SIZE		(32*1024)

#define MIXER_ADDR_MASTER	0
#define MIXER_ADDR_MIC		1
#define MIXER_ADDR_LAST		1

#define MIXER_MASK_MASTER	1
#define MIXER_MASK_MIC		2

#define MIXER_MIN_VOLUME	1
#define MIXER_MAX_VOLUME	15

struct snd_card_bt_sco_pcm;

typedef struct snd_card_bt_sco_info {
	int mixer_volume[MIXER_ADDR_LAST + 1];
	int playback_count, capture_count;
} snd_card_bt_sco_info_t;

typedef struct snd_card_bt_sco {
	struct snd_card *card;
	spinlock_t mixer_lock;
	int mixer_volume[MIXER_ADDR_LAST + 1];
#ifdef DYNAMIC_COMPRESSION
	struct snd_kcontrol *mixer_controls[MIXER_ADDR_LAST + 2 + 1];	/* also loopback and agc */
#else
	struct snd_kcontrol *mixer_controls[MIXER_ADDR_LAST + 2 ];	/* also loopback */
#endif
	volatile int loopback;
#ifdef DYNAMIC_COMPRESSION
	volatile int agc;
#endif
	atomic_t playback_count, capture_count;
	volatile int count_changed;
	spinlock_t count_changed_lock;

	spinlock_t mixer_changed_lock;
	volatile int mixer_changed;
	wait_queue_head_t hwdep_wait;

	int thread_pid;
	struct completion thread_done;

	volatile int thread_exit;
	struct mutex thread_sem;

	volatile struct socket *sco_sock;
	struct mutex sock_sem;
	wait_queue_head_t wait;

	struct mutex playback_sem;
	struct snd_card_bt_sco_pcm *playback;
	struct mutex capture_sem;
	struct snd_card_bt_sco_pcm *capture;
} snd_card_bt_sco_t;

typedef struct snd_card_bt_sco_pcm {
	snd_card_bt_sco_t *bt_sco;
	spinlock_t lock;
	unsigned int pcm_size;
	unsigned int pcm_count;
	unsigned int pcm_bps;	/* bytes per second */
	unsigned int pcm_irq_pos;	/* IRQ position */
	unsigned int pcm_buf_pos;	/* position in buffer */
	struct snd_pcm_substream *substream;
} snd_card_bt_sco_pcm_t;

static struct snd_card *snd_bt_sco_cards[SNDRV_CARDS] = SNDRV_DEFAULT_PTR;

static int snd_card_bt_sco_playback_trigger(struct snd_pcm_substream *
					    substream, int cmd)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	snd_card_bt_sco_pcm_t *bspcm = runtime->private_data;
	snd_card_bt_sco_t *bt_sco = snd_pcm_substream_chip(substream);

	dprintk("playback_trigger %d\n", cmd);

	if (cmd == SNDRV_PCM_TRIGGER_START) {
		bt_sco->playback = bspcm;
		dprintk("setting playback to bspcm\n");
	} else if (cmd == SNDRV_PCM_TRIGGER_STOP) {
		bt_sco->playback = NULL;
		dprintk("setting playback to NULL\n");
	} else {
		return -EINVAL;
	}
	return 0;
}

static int snd_card_bt_sco_capture_trigger(struct snd_pcm_substream *
					   substream, int cmd)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	snd_card_bt_sco_pcm_t *bspcm = runtime->private_data;
	snd_card_bt_sco_t *bt_sco = snd_pcm_substream_chip(substream);

	dprintk("capture_trigger %d\n", cmd);

	if (cmd == SNDRV_PCM_TRIGGER_START) {
		bt_sco->capture = bspcm;
		dprintk("setting capture to bspcm\n");
	} else if (cmd == SNDRV_PCM_TRIGGER_STOP) {
		bt_sco->capture = NULL;
		dprintk("setting capture to NULL\n");
	} else {
		return -EINVAL;
	}
	return 0;
}

static int snd_card_bt_sco_pcm_prepare(struct snd_pcm_substream * substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	snd_card_bt_sco_pcm_t *bspcm = runtime->private_data;
	unsigned int bps;

	bps = runtime->rate * runtime->channels;
	bps *= snd_pcm_format_width(runtime->format);
	bps /= 8;
	if (bps <= 0)
		return -EINVAL;
	bspcm->pcm_bps = bps;
	bspcm->pcm_size = snd_pcm_lib_buffer_bytes(substream);
	bspcm->pcm_count = snd_pcm_lib_period_bytes(substream);
	bspcm->pcm_irq_pos = 0;
	bspcm->pcm_buf_pos = 0;
	dprintk("prepare ok bps: %d size: %d count: %d\n",
		bspcm->pcm_bps, bspcm->pcm_size, bspcm->pcm_count);
	return 0;
}

static int snd_card_bt_sco_playback_prepare(struct snd_pcm_substream * substream)
{
	return snd_card_bt_sco_pcm_prepare(substream);
}

static int snd_card_bt_sco_capture_prepare(struct snd_pcm_substream * substream)
{
	dprintk("capture_prepare\n");
	return snd_card_bt_sco_pcm_prepare(substream);
}

static void snd_card_bt_sco_pcm_receive(snd_card_bt_sco_pcm_t * bspcm,
					unsigned char *data, unsigned int len)
{
	unsigned long flags;
	unsigned int oldptr;

	spin_lock_irqsave(&bspcm->lock, flags);
	oldptr = bspcm->pcm_buf_pos;
	bspcm->pcm_irq_pos += len;
	bspcm->pcm_buf_pos += len;
	bspcm->pcm_buf_pos %= bspcm->pcm_size;
	spin_unlock_irqrestore(&bspcm->lock, flags);
	/* copy a data chunk */
	if (oldptr + len > bspcm->pcm_size) {
		unsigned int cnt = bspcm->pcm_size - oldptr;
		memcpy(bspcm->substream->runtime->dma_area + oldptr, data, cnt);
		memcpy(bspcm->substream->runtime->dma_area, data + cnt,
		       len - cnt);
	} else {
		memcpy(bspcm->substream->runtime->dma_area + oldptr, data, len);
	}
	/* update the pointer, call callback if necessary */
	spin_lock_irqsave(&bspcm->lock, flags);
	if (bspcm->pcm_irq_pos >= bspcm->pcm_count) {
		bspcm->pcm_irq_pos %= bspcm->pcm_count;
		spin_unlock_irqrestore(&bspcm->lock, flags);
		snd_pcm_period_elapsed(bspcm->substream);
	} else
		spin_unlock_irqrestore(&bspcm->lock, flags);

}

static void snd_card_bt_sco_pcm_send(snd_card_bt_sco_pcm_t * bspcm,
				     unsigned char *data, unsigned int len)
{
	unsigned long flags;
	unsigned int oldptr;

	spin_lock_irqsave(&bspcm->lock, flags);
	oldptr = bspcm->pcm_buf_pos;
	bspcm->pcm_irq_pos += len;
	bspcm->pcm_buf_pos += len;
	bspcm->pcm_buf_pos %= bspcm->pcm_size;
	spin_unlock_irqrestore(&bspcm->lock, flags);
	/* copy a data chunk */
	if (oldptr + len > bspcm->pcm_size) {
		unsigned int cnt = bspcm->pcm_size - oldptr;
		memcpy(data, bspcm->substream->runtime->dma_area + oldptr, cnt);
		memcpy(data + cnt, bspcm->substream->runtime->dma_area,
		       len - cnt);
	} else {
		memcpy(data, bspcm->substream->runtime->dma_area + oldptr, len);
	}
	/* update the pointer, call callback if necessary */
	spin_lock_irqsave(&bspcm->lock, flags);
	if (bspcm->pcm_irq_pos >= bspcm->pcm_count) {
		bspcm->pcm_irq_pos %= bspcm->pcm_count;
		spin_unlock_irqrestore(&bspcm->lock, flags);
		snd_pcm_period_elapsed(bspcm->substream);
	} else
		spin_unlock_irqrestore(&bspcm->lock, flags);
}

static snd_pcm_uframes_t
snd_card_bt_sco_playback_pointer(struct snd_pcm_substream * substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	snd_card_bt_sco_pcm_t *bspcm = runtime->private_data;

	return bytes_to_frames(runtime, bspcm->pcm_buf_pos);
}

static snd_pcm_uframes_t
snd_card_bt_sco_capture_pointer(struct snd_pcm_substream * substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	snd_card_bt_sco_pcm_t *bspcm = runtime->private_data;

	return bytes_to_frames(runtime, bspcm->pcm_buf_pos);
}

static struct snd_pcm_hardware snd_card_bt_sco_playback = {
	.info = (SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
		 SNDRV_PCM_INFO_MMAP_VALID),
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.rates = SNDRV_PCM_RATE_8000,
	.rate_min = 8000,
	.rate_max = 8000,
	.channels_min = 1,
	.channels_max = 1,
	.buffer_bytes_max = MAX_BUFFER_SIZE,
	.period_bytes_min = 24,
	.period_bytes_max = MAX_BUFFER_SIZE,
	.periods_min = 1,
	.periods_max = 4 * 8000 / 24,
	.fifo_size = 0,
};

static struct snd_pcm_hardware snd_card_bt_sco_capture = {
	.info = (SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
		 SNDRV_PCM_INFO_MMAP_VALID),
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.rates = SNDRV_PCM_RATE_8000,
	.rate_min = 8000,
	.rate_max = 8000,
	.channels_min = 1,
	.channels_max = 1,
	.buffer_bytes_max = MAX_BUFFER_SIZE,
	.period_bytes_min = 24,
	.period_bytes_max = MAX_BUFFER_SIZE,
	.periods_min = 1,
	.periods_max = 4 * 8000 / 24,
	.fifo_size = 0,
};

static void snd_card_bt_sco_runtime_free(struct snd_pcm_runtime * runtime)
{
	snd_card_bt_sco_pcm_t *bspcm = runtime->private_data;
	kfree(bspcm);
}

static int snd_card_bt_sco_playback_open(struct snd_pcm_substream * substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	snd_card_bt_sco_pcm_t *bspcm;
	snd_card_bt_sco_t *bt_sco = snd_pcm_substream_chip(substream);

	dprintk("playback_open\n");

	bspcm = kmalloc(sizeof(*bspcm), GFP_KERNEL);
	if (bspcm == NULL)
		return -ENOMEM;
	memset(bspcm, 0, sizeof(*bspcm));
	if ((runtime->dma_area =
	     snd_malloc_pages(MAX_BUFFER_SIZE, GFP_KERNEL)) == NULL) {
		kfree(bspcm);
		return -ENOMEM;
	}
	runtime->dma_bytes = MAX_BUFFER_SIZE;
	spin_lock_init(&bspcm->lock);
	bspcm->substream = substream;
	runtime->private_data = bspcm;
	runtime->private_free = snd_card_bt_sco_runtime_free;
	runtime->hw = snd_card_bt_sco_playback;

	atomic_inc(&bt_sco->playback_count);
	spin_lock_irq(&bt_sco->count_changed_lock);
	bt_sco->count_changed = 1;
	spin_unlock_irq(&bt_sco->count_changed_lock);
	wake_up(&bt_sco->hwdep_wait);

	return 0;
}

static int snd_card_bt_sco_capture_open(struct snd_pcm_substream * substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	snd_card_bt_sco_pcm_t *bspcm;
	snd_card_bt_sco_t *bt_sco = snd_pcm_substream_chip(substream);

	dprintk("capture_open\n");

	bspcm = kmalloc(sizeof(*bspcm), GFP_KERNEL);
	if (bspcm == NULL)
		return -ENOMEM;
	memset(bspcm, 0, sizeof(*bspcm));
	if ((runtime->dma_area =
	     snd_malloc_pages(MAX_BUFFER_SIZE, GFP_KERNEL)) == NULL) {
		kfree(bspcm);
		return -ENOMEM;
	}
	runtime->dma_bytes = MAX_BUFFER_SIZE;
	memset(runtime->dma_area, 0, runtime->dma_bytes);
	spin_lock_init(&bspcm->lock);
	bspcm->substream = substream;
	runtime->private_data = bspcm;
	runtime->private_free = snd_card_bt_sco_runtime_free;
	runtime->hw = snd_card_bt_sco_capture;

	atomic_inc(&bt_sco->capture_count);
	spin_lock_irq(&bt_sco->count_changed_lock);
	bt_sco->count_changed = 1;
	spin_unlock_irq(&bt_sco->count_changed_lock);
	wake_up(&bt_sco->hwdep_wait);

	return 0;
}

static int snd_card_bt_sco_playback_close(struct snd_pcm_substream * substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	snd_card_bt_sco_t *bt_sco = snd_pcm_substream_chip(substream);

	snd_assert(bt_sco->playback == NULL,;
	    );

	/* Ensure any references to this in our thread have finished */
	mutex_lock(&bt_sco->playback_sem);
	mutex_unlock(&bt_sco->playback_sem);

	atomic_dec(&bt_sco->playback_count);
	spin_lock_irq(&bt_sco->count_changed_lock);
	bt_sco->count_changed = 1;
	spin_unlock_irq(&bt_sco->count_changed_lock);
	wake_up(&bt_sco->hwdep_wait);

	snd_free_pages(runtime->dma_area, runtime->dma_bytes);
	return 0;
}

static int snd_card_bt_sco_capture_close(struct snd_pcm_substream * substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_card_bt_sco *bt_sco =
	    (struct snd_card_bt_sco *)substream->private_data;

	snd_assert(bt_sco->capture == NULL,;
	    );

	/* Ensure any references to this in our thread have finished */
	mutex_lock(&bt_sco->capture_sem);
	mutex_unlock(&bt_sco->capture_sem);

	atomic_dec(&bt_sco->capture_count);
	spin_lock_irq(&bt_sco->count_changed_lock);
	bt_sco->count_changed = 1;
	spin_unlock_irq(&bt_sco->count_changed_lock);
	wake_up(&bt_sco->hwdep_wait);

	snd_free_pages(runtime->dma_area, runtime->dma_bytes);
	return 0;
}

static struct snd_pcm_ops snd_card_bt_sco_playback_ops = {
	.open = snd_card_bt_sco_playback_open,
	.close = snd_card_bt_sco_playback_close,
	.ioctl = snd_pcm_lib_ioctl,
	.prepare = snd_card_bt_sco_playback_prepare,
	.trigger = snd_card_bt_sco_playback_trigger,
	.pointer = snd_card_bt_sco_playback_pointer,
};

static struct snd_pcm_ops snd_card_bt_sco_capture_ops = {
	.open = snd_card_bt_sco_capture_open,
	.close = snd_card_bt_sco_capture_close,
	.ioctl = snd_pcm_lib_ioctl,
	.prepare = snd_card_bt_sco_capture_prepare,
	.trigger = snd_card_bt_sco_capture_trigger,
	.pointer = snd_card_bt_sco_capture_pointer,
};

static int __init snd_card_bt_sco_pcm(snd_card_bt_sco_t * bt_sco)
{
	struct snd_pcm *pcm;
	int err;

	if ((err =
	     snd_pcm_new(bt_sco->card, "Bluetooth SCO PCM", 0, 1, 1, &pcm)) < 0)
		return err;
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK,
			&snd_card_bt_sco_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE,
			&snd_card_bt_sco_capture_ops);
	pcm->private_data = bt_sco;
	pcm->info_flags = 0;
	strcpy(pcm->name, "BT SCO PCM");
	return 0;
}

#define BT_SCO_VOLUME(xname, xindex, addr) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
                        .info = snd_bt_sco_volume_info, \
                                .get = snd_bt_sco_volume_get, .put = snd_bt_sco_volume_put, \
                                                                        .private_value = addr }

static int snd_bt_sco_volume_info(struct snd_kcontrol * kcontrol,
				  struct snd_ctl_elem_info * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = MIXER_MIN_VOLUME;
	uinfo->value.integer.max = MIXER_MAX_VOLUME;
	return 0;
}

static int snd_bt_sco_volume_get(struct snd_kcontrol * kcontrol,
				 struct snd_ctl_elem_value * ucontrol)
{
	snd_card_bt_sco_t *bt_sco = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int addr = kcontrol->private_value;

	spin_lock_irqsave(&bt_sco->mixer_lock, flags);
	ucontrol->value.integer.value[0] = bt_sco->mixer_volume[addr];
	spin_unlock_irqrestore(&bt_sco->mixer_lock, flags);
	return 0;
}

static int snd_bt_sco_volume_put(struct snd_kcontrol * kcontrol,
				 struct snd_ctl_elem_value * ucontrol)
{
	snd_card_bt_sco_t *bt_sco = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int changed, addr = kcontrol->private_value;
	int vol;

	vol = ucontrol->value.integer.value[0];
	if (vol < MIXER_MIN_VOLUME)
		vol = MIXER_MIN_VOLUME;
	if (vol > MIXER_MAX_VOLUME)
		vol = MIXER_MAX_VOLUME;
	spin_lock_irqsave(&bt_sco->mixer_lock, flags);
	changed = bt_sco->mixer_volume[addr] != vol;
	bt_sco->mixer_volume[addr] = vol;
	spin_unlock_irqrestore(&bt_sco->mixer_lock, flags);
	if (changed) {
		spin_lock_irqsave(&bt_sco->mixer_changed_lock, flags);
		bt_sco->mixer_changed = 1;
		spin_unlock_irqrestore(&bt_sco->mixer_changed_lock, flags);
		wake_up(&bt_sco->hwdep_wait);
	}
	return changed;
}

static int snd_bt_sco_boolean_info(struct snd_kcontrol * kcontrol,
				   struct snd_ctl_elem_info * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int snd_bt_sco_loopback_get(struct snd_kcontrol * kcontrol,
				   struct snd_ctl_elem_value * ucontrol)
{
	snd_card_bt_sco_t *bt_sco = snd_kcontrol_chip(kcontrol);
	unsigned long flags;

	spin_lock_irqsave(&bt_sco->mixer_lock, flags);
	ucontrol->value.integer.value[0] = bt_sco->loopback;
	spin_unlock_irqrestore(&bt_sco->mixer_lock, flags);
	return 0;
}

static int snd_bt_sco_loopback_put(struct snd_kcontrol * kcontrol,
				   struct snd_ctl_elem_value * ucontrol)
{
	snd_card_bt_sco_t *bt_sco = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int changed;
	int loopback;

	loopback = !!ucontrol->value.integer.value[0];

	spin_lock_irqsave(&bt_sco->mixer_lock, flags);
	changed = bt_sco->loopback != loopback;
	bt_sco->loopback = loopback;
	spin_unlock_irqrestore(&bt_sco->mixer_lock, flags);
	return changed;
}

#ifdef DYNAMIC_COMPRESSION
static int snd_bt_sco_agc_get(struct snd_kcontrol * kcontrol,
				   struct snd_ctl_elem_value * ucontrol)
{
	snd_card_bt_sco_t *bt_sco = snd_kcontrol_chip(kcontrol);
	unsigned long flags;

	spin_lock_irqsave(&bt_sco->mixer_lock, flags);
	ucontrol->value.integer.value[0] = bt_sco->agc;
	spin_unlock_irqrestore(&bt_sco->mixer_lock, flags);
	return 0;
}

static int snd_bt_sco_agc_put(struct snd_kcontrol * kcontrol,
				   struct snd_ctl_elem_value * ucontrol)
{
	snd_card_bt_sco_t *bt_sco = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int changed;
	int agc;

	agc = !!ucontrol->value.integer.value[0];

	spin_lock_irqsave(&bt_sco->mixer_lock, flags);
	changed = bt_sco->agc != agc;
	bt_sco->agc = agc;
	spin_unlock_irqrestore(&bt_sco->mixer_lock, flags);
	return changed;
}
#endif

#define BT_SCO_CONTROLS (sizeof(snd_bt_sco_controls)/sizeof(struct snd_kcontrol_new))

static struct snd_kcontrol_new snd_bt_sco_controls[] = {
	BT_SCO_VOLUME("Master Volume", 0, MIXER_ADDR_MASTER),
	BT_SCO_VOLUME("Mic Volume", 0, MIXER_ADDR_MIC),
	{.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	 .name = "Loopback Switch",
	 .index = 0,
	 .info = snd_bt_sco_boolean_info,
	 .get = snd_bt_sco_loopback_get,
	 .put = snd_bt_sco_loopback_put,
	 }
#ifdef DYNAMIC_COMPRESSION
	,
	{.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	 .name = "AGC Switch",
	 .index = 0,
	 .info = snd_bt_sco_boolean_info,
	 .get = snd_bt_sco_agc_get,
	 .put = snd_bt_sco_agc_put,
	 }
#endif
};

int __init snd_card_bt_sco_new_mixer(snd_card_bt_sco_t * bt_sco)
{
	struct snd_card *card = bt_sco->card;

	unsigned int idx;
	int err;

	snd_assert(bt_sco != NULL, return -EINVAL);
	spin_lock_init(&bt_sco->mixer_lock);
	strcpy(card->mixername, "BT Headset Mixer");

	for (idx = 0; idx < BT_SCO_CONTROLS; idx++) {
		bt_sco->mixer_controls[idx] =
		    snd_ctl_new1(&snd_bt_sco_controls[idx], bt_sco);

		if ((err = snd_ctl_add(card, bt_sco->mixer_controls[idx])) < 0)
			return err;
	}
	return 0;
}

static int snd_card_bt_open(struct snd_hwdep * hw, struct file *file)
{
	return 0;
}

static int snd_card_bt_release(struct snd_hwdep * hw, struct file *file)
{
	return 0;
}

static int snd_card_bt_ioctl(struct snd_hwdep * hw, struct file *file,
			     unsigned int cmd, unsigned long arg)
{
	snd_card_bt_sco_t *bt_sco = hw->card->private_data;
	struct socket *sock;
	int err = -ENOTTY;
	int fd = arg;

	switch (cmd) {
	case SNDRV_BT_SCO_IOCTL_SET_SCO_SOCKET:
		err = 0;
		/*  Interrupt any socket operations, so that we may
		 *  change the socket */
		mutex_lock(&bt_sco->sock_sem);
		kill_proc(bt_sco->thread_pid, SIGINT, 1);
		if (bt_sco->sco_sock) {
			dprintk("Disposing of previous socket count %d\n",
				file_count(bt_sco->sco_sock->file));
			/* Extra brackets needed here since sockfd_put is a poorly implemented macro */
			sockfd_put(((struct socket *)bt_sco->sco_sock));

			bt_sco->sco_sock = NULL;
		}

		if (fd >= 0) {
			err = -EINVAL;
			sock = sockfd_lookup(fd, &err);
			if (sock) {
				if (sock->sk->sk_family == PF_BLUETOOTH &&
				    sock->sk->sk_protocol == BTPROTO_SCO) {
					bt_sco->sco_sock = sock;
					wake_up(&bt_sco->wait);
					err = 0;
				} else {
					dprintk
					    ("Not a bluetooth SCO socket %d:%d\n",
					     sock->sk->sk_family,
					     sock->sk->sk_protocol);
					sockfd_put(sock);
				}
			}
		}
		mutex_unlock(&bt_sco->sock_sem);
		break;
	case SNDRV_BT_SCO_IOCTL_REQ_INFO:
		spin_lock_irq(&bt_sco->count_changed_lock);
		bt_sco->count_changed = 1;
		spin_unlock_irq(&bt_sco->count_changed_lock);
		wake_up(&bt_sco->hwdep_wait);
		break;
	}
	return err;
}

static long snd_card_bt_write(struct snd_hwdep * hw, const char *buf, long count,
			      loff_t * offset)
{
	snd_card_bt_sco_t *bt_sco = hw->card->private_data;
	int mixer_volume[MIXER_ADDR_LAST + 1];
	int retval;
	int i;

	if (count != sizeof(mixer_volume))
		return -EINVAL;

	if (copy_from_user(mixer_volume, buf, sizeof(mixer_volume)))
		return -EFAULT;

	retval = sizeof(mixer_volume);

	spin_lock_irq(&bt_sco->mixer_lock);
	for (i = 0; i <= MIXER_ADDR_LAST; i++) {
		int vol = mixer_volume[i];
		if (vol > MIXER_MAX_VOLUME)
			vol = MIXER_MAX_VOLUME;
		if (vol < MIXER_MIN_VOLUME)
			vol = MIXER_MIN_VOLUME;
		if (bt_sco->mixer_volume[i] != vol) {
			bt_sco->mixer_volume[i] = vol;
			snd_ctl_notify(bt_sco->card,
				       SNDRV_CTL_EVENT_MASK_VALUE,
				       &bt_sco->mixer_controls[i]->id);
		}
	}
	spin_unlock_irq(&bt_sco->mixer_lock);

	return retval;
}

static long snd_card_bt_read(struct snd_hwdep * hw, char *buf, long count,
			     loff_t * offset)
{
	snd_card_bt_sco_t *bt_sco = hw->card->private_data;
	DECLARE_WAITQUEUE(wait, current);
	ssize_t retval;
	int changed;
	snd_card_bt_sco_info_t infobuf;

	if (count < sizeof(bt_sco->mixer_volume))
		return -EINVAL;

	add_wait_queue(&bt_sco->hwdep_wait, &wait);
	current->state = TASK_INTERRUPTIBLE;
	do {
		changed = 0;
		spin_lock_irq(&bt_sco->mixer_changed_lock);
		if(bt_sco->mixer_changed)
			changed = 1;
		bt_sco->mixer_changed = 0;
		spin_unlock_irq(&bt_sco->mixer_changed_lock);

		spin_lock_irq(&bt_sco->count_changed_lock);
		if(bt_sco->count_changed)
			changed = 1;
		bt_sco->count_changed = 0;
		spin_unlock_irq(&bt_sco->count_changed_lock);

		if (changed != 0)
			break;

		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			goto out;
		}
		schedule();
	} while (1);
	
	memcpy(infobuf.mixer_volume, bt_sco->mixer_volume, sizeof(infobuf.mixer_volume));
	infobuf.playback_count = atomic_read(&bt_sco->playback_count);
	infobuf.capture_count = atomic_read(&bt_sco->capture_count);
	
	if (copy_to_user
	    (buf, &infobuf, sizeof(infobuf)))
		retval = -EFAULT;
	else
		retval = sizeof(infobuf);

      out:
	current->state = TASK_RUNNING;
	remove_wait_queue(&bt_sco->hwdep_wait, &wait);
	return retval;
}

static unsigned int snd_card_bt_poll(struct snd_hwdep * hw,
				     struct file *file, poll_table * wait)
{
	snd_card_bt_sco_t *bt_sco = hw->card->private_data;
	int ret;

	poll_wait(file, &bt_sco->hwdep_wait, wait);

	ret = 0;
	spin_lock_irq(&bt_sco->mixer_changed_lock);
	if(bt_sco->mixer_changed)
		ret |= POLLIN | POLLRDNORM;
	spin_unlock_irq(&bt_sco->mixer_changed_lock);

	spin_lock_irq(&bt_sco->count_changed_lock);
	if(bt_sco->count_changed)
		ret |= POLLIN | POLLRDNORM;
	spin_unlock_irq(&bt_sco->count_changed_lock);

	return ret;
}

static int snd_card_bt_sco_thread(void *data)
{
	struct snd_card *card = (struct snd_card *) data;
	snd_card_bt_sco_t *bt_sco = card->private_data;
	struct socket *sock;
	int len;
#define BUF_SIZE 256
	unsigned char buf[BUF_SIZE];
	struct msghdr msg;
	struct iovec iov;
	sigset_t unblocked;
#if defined(DYNAMIC_COMPRESSION) || defined(AUTO_FIXUP_BYTESHIFT)
	int i;
#endif
#ifdef DYNAMIC_COMPRESSION
	static int factor=16;
	static int maxvalsmoothed=0;
	static int maxvalgrablen=GRABSAMPLES; /* adjust volume at most 4 times/second */
#endif
#ifdef AUTO_FIXUP_BYTESHIFT
	static int shift=0;
	static unsigned char lastbyte;
#endif

	lock_kernel();

	daemonize("snd-bt-scod");
	sigemptyset(&unblocked);
	sigaddset(&unblocked, SIGINT);
	sigaddset(&unblocked, SIGTERM);
	sigprocmask(SIG_UNBLOCK, &unblocked, NULL);

	/* Pretend so that copy_to_user and friends work */
	set_fs(KERNEL_DS);

	dprintk("snd-bt-scod thread starting\n");
	mutex_unlock(&bt_sco->thread_sem);

	do {

//#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,12))
		try_to_freeze();
//#else
//		if (current->flags & PF_FREEZE)
//			refrigerator(PF_FREEZE);
//#endif

		if (signal_pending(current))
			flush_signals(current);

		/*      This may be woken up by a wake_up() when
		 *      a new socket is installed, or by a signal.
		 *      Signals are sent to terminate the thread,
		 *      in which case thread_exit is set, and to force
		 *      recvmesg() to wake up (from the ioctl handler)
		 */
		wait_event_interruptible(bt_sco->wait, bt_sco->sco_sock != 0);
		if (bt_sco->thread_exit)
			break;

		mutex_lock(&bt_sco->sock_sem);
		sock = (struct socket *)bt_sco->sco_sock;
		if (sock)
			get_file(sock->file);
		mutex_unlock(&bt_sco->sock_sem);

		if (!sock)
			continue;

		/* We have a socket, let's read from it and write to it... */

		memset(&msg, 0, sizeof(msg));
		msg.msg_iov = &iov;
		iov.iov_base = buf;
		iov.iov_len = BUF_SIZE;

		/* This will block until we receive data or a signal */
		len = sock_recvmsg(sock, &msg, BUF_SIZE, 0);
		if (len > 0) {

#if defined (AUTO_FIXUP_BYTESHIFT) || defined (DYNAMIC_COMPRESSION)

#ifdef AUTO_FIXUP_BYTESHIFT
			int lostatcnt=0;
#endif
			if (len&1) dprintk("odd len %d\n",len);
#ifdef AUTO_FIXUP_BYTESHIFT
			if (shift) {
				unsigned char newlastbyte;
				newlastbyte=buf[len-1];
				memmove(buf+1,buf,len-1);
				buf[0]=lastbyte;
				lastbyte=newlastbyte;
			}
#endif
			for(i=0;i<len-1;i+=2) {
				short int j;
				int k;
				j=(buf[i+1]<<8)|buf[i];

#ifdef AUTO_FIXUP_BYTESHIFT    
				/* occasionally the Headset will loose a byte
				 * on startup. Thus kind of swapping lo/hi.
				 * counting, if _all_ lo bytes (which are 
				 * actually high bytes) are 0 or -1
				 * will detect this with a very high probability
				 */
				if ((j&0xff)==0||(j&0xff)==0xff) {
					lostatcnt++;
				}
#endif
#ifdef DYNAMIC_COMPRESSION
				/* scale the mic input - we do some kind
				 * of dynamics compression
				 */
				k=((int)j*factor)/16;

				/* clip overshoot. Better than just letting
				 * it wrap around. Immediately adjust factor.
				 */
				if (k>0x7fff) {
					k=0x7fff;
					if (bt_sco->agc&&factor>COMPRESSION_MIN_16) factor--;
				} else if (k<-0x8000) {
					k=0x8000;
					if (bt_sco->agc&&factor>COMPRESSION_MIN_16) factor--;
				}
				buf[i+1]=(k>>8)&0xff;
				buf[i  ]=k&0xff;

				/* find the highest absolute value in a 
				 * GRABSAMPLES long interval.
				 */
				if (k<0) k=-j;
				if (k>maxvalsmoothed) maxvalsmoothed=k;
				/* if the interval is over, recalculate
				 * the compression factor. Move it slowly.
				 */
				if (maxvalgrablen--<=0) {
					maxvalgrablen=GRABSAMPLES;
					/* If the noise goes up over 1000, we stop
					 * pushing the software gain 
					 */
					if (maxvalsmoothed<1000&&factor<COMPRESSION_MAX_16) {
						factor++;
						// dprintk("Up to %d\n",factor);
					}
					if (!bt_sco->agc) factor=16;
					maxvalsmoothed=0;
				}
#endif
			}
#ifdef AUTO_FIXUP_BYTESHIFT
			if (lostatcnt==len/2&&len>32) {
				shift=!shift;
				//dprintk("Shift problem detected! Fixing to %d.\n",shift);
			}
#endif
#endif /* any of them */
			mutex_lock(&bt_sco->capture_sem);
			if (bt_sco->capture) {
				snd_card_bt_sco_pcm_receive
				    (bt_sco->capture, buf, len);
			}
			mutex_unlock(&bt_sco->capture_sem);

			mutex_lock(&bt_sco->playback_sem);

			if (bt_sco->playback || !bt_sco->loopback) {
				memset(buf, 0, len);
#if 0
				/* fill with tone instead of silence */
				int i;

				for (i = 0; i < len / 2; i++) {
					buf[i] = 0;
				}
				for (i = len / 2; i < len; i++) {
					buf[i] = 127;
				}
#endif
			}
			if (bt_sco->playback) {
				int i, notzero = -1;

				snd_card_bt_sco_pcm_send
				    (bt_sco->playback, buf, len);

				/* Strangely, when the device is open but no audio is
				   being written by the app, there's an occasional glitch
				   in the silence data. This hack eliminates it. */

				for (i = 0; i < len; i++) {
					if (buf[i] != 0) {
						if (notzero >= 0)
							break;
						notzero = i;
					}
				}
				if (notzero >= 0 && i >= len) {
					buf[notzero] = 0;
				}
			}
			mutex_unlock(&bt_sco->playback_sem);

#if 0
			/* This chunk of code lets us record (using arecord)
			   what data alsa is sending out.

			   e.g., when idle, we'd expect something like:

			   8080 8080 8080 8080 8483 8281 8182 8384
			   8080 8080 8080 8080 8080 8080 8080 8080
			   8080 8080 8080 8080 8483 8281 8182 8384
			   8080 8080 8080 8080 8080 8080 8080 8080

			   (this is from 'xxd' of a wav file, that data in
			   which is unsigned, whereas we are dealing with signed).
			 */

			mutex_lock(&bt_sco->capture_sem);
			if (bt_sco->capture) {
				snd_card_bt_sco_pcm_receive
				    (bt_sco->capture, "\001\002\003\004", 4);
				snd_card_bt_sco_pcm_receive
				    (bt_sco->capture, buf, len);
				snd_card_bt_sco_pcm_receive
				    (bt_sco->capture, "\004\003\002\001", 4);
			}
			mutex_unlock(&bt_sco->capture_sem);
#endif
			msg.msg_flags = 0;
			msg.msg_iov = &iov;
			iov.iov_base = buf;
			iov.iov_len = BUF_SIZE;
			sock_sendmsg(sock, &msg, len);
		}

		/* Expect this to be 3 because we (this thead) have a copy,
		   the driver process keeps one, and the app has the socket open.
		 */
		if (file_count(sock->file) != 3) {
			dprintk("file_count is %d (expected 3)\n",
				file_count(sock->file));
		}
		fput(sock->file);

		schedule();
	} while (!bt_sco->thread_exit);

	dprintk("thread exiting\n");

	unlock_kernel();
	complete_and_exit(&bt_sco->thread_done, 0);
}

static void snd_card_bt_private_free(struct snd_card * card)
{
	snd_card_bt_sco_t *bt_sco = card->private_data;

	dprintk("private_free, killing thread\n");
	bt_sco->thread_exit = 1;
	kill_proc(bt_sco->thread_pid, SIGTERM, 1);
	wait_for_completion(&bt_sco->thread_done);
	dprintk("private_free, thread exited\n");

	if (bt_sco->sco_sock) {
		dprintk("shutdown: freeing socket count %d\n",
			file_count(bt_sco->sco_sock->file));

		sockfd_put(((struct socket *)bt_sco->sco_sock));
	}

	kfree(bt_sco);
}

static int __init snd_card_bt_sco_probe(int dev)
{
	struct snd_card *card;
	snd_card_bt_sco_t *bt_sco;
	int err;
	struct snd_hwdep *hw;

	card =
	    snd_card_new(index[dev], SNDRV_DEFAULT_STR1,
			 THIS_MODULE, 0);
	if (card == NULL)
		return -ENOMEM;

	bt_sco = kmalloc(sizeof(*bt_sco), GFP_KERNEL);
	if(bt_sco == NULL)
		return -ENOMEM;
	memset(bt_sco, 0, sizeof(*bt_sco));
	card->private_data = bt_sco;
	card->private_free = snd_card_bt_private_free;

	bt_sco->card = card;

	init_completion(&bt_sco->thread_done);
	mutex_init(&bt_sco->thread_sem);
	mutex_lock(&bt_sco->thread_sem);
	mutex_init(&bt_sco->sock_sem);
	mutex_init(&bt_sco->capture_sem);
	mutex_init(&bt_sco->playback_sem);
	init_waitqueue_head(&bt_sco->wait);
	init_waitqueue_head(&bt_sco->hwdep_wait);
	spin_lock_init(&bt_sco->mixer_changed_lock);
	spin_lock_init(&bt_sco->count_changed_lock);

	/* These clone flags copied from some other driver.
	   Not sure that they're really correct... */
	bt_sco->thread_pid =
	    kernel_thread(snd_card_bt_sco_thread, card, CLONE_KERNEL);
	if (bt_sco->thread_pid < 0) {
		err = bt_sco->thread_pid;
		goto __nodev;
	}

	mutex_lock(&bt_sco->thread_sem);

	if ((err = snd_card_bt_sco_pcm(bt_sco)) < 0)
		goto __nodev;
	if ((err = snd_card_bt_sco_new_mixer(bt_sco)) < 0)
		goto __nodev;
	strcpy(card->driver, "Bluetooth SCO");
	strcpy(card->shortname, "BT Headset");
	sprintf(card->longname, "BT Headset %i", dev + 1);

	err = snd_hwdep_new(card, "BTSCO", 0, &hw);
	if (err < 0)
		goto __nodev;

	sprintf(hw->name, "BTSCO");
	hw->iface = SNDRV_HWDEP_IFACE_BT_SCO;
	hw->ops.open = snd_card_bt_open;
	hw->ops.ioctl = snd_card_bt_ioctl;
	hw->ops.release = snd_card_bt_release;
	hw->ops.read = snd_card_bt_read;
	hw->ops.write = snd_card_bt_write;
	hw->ops.poll = snd_card_bt_poll;

	if ((err = snd_card_register(card)) == 0) {
		snd_bt_sco_cards[dev] = card;
		return 0;
	}
      __nodev:
	snd_card_free(card);
	return err;
}

static int __init alsa_card_bt_sco_init(void)
{
	printk(KERN_INFO "snd-bt-sco revision %s\n", mod_revision + 11);

	if (snd_card_bt_sco_probe(0) < 0) {
#ifdef MODULE
		printk(KERN_ERR
		       "Bluetooth SCO soundcard not found or device busy\n");
#endif
		return -ENODEV;
	}
	return 0;
}

static void __exit alsa_card_bt_sco_exit(void)
{
	int idx;

	for (idx = 0; idx < SNDRV_CARDS; idx++)
		snd_card_free(snd_bt_sco_cards[idx]);
}

module_init(alsa_card_bt_sco_init)
    module_exit(alsa_card_bt_sco_exit)
#ifndef MODULE
static int __init alsa_card_bt_sco_setup(char *str)
{
	static unsigned __initdata nr_dev = 0;

	if (nr_dev >= SNDRV_CARDS)
		return 0;
	nr_dev++;
	return 1;
}

__setup("snd-bt-sco=", alsa_card_bt_sco_setup);

#endif				/* ifndef MODULE */
