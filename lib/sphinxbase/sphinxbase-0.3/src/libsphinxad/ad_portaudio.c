/* ====================================================================
 * Copyright (c) 1999-2006 Carnegie Mellon University.  All rights
 * reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer. 
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * This work was supported in part by funding from the Defense Advanced 
 * Research Projects Agency and the National Science Foundation of the 
 * United States of America, and the CMU Sphinx Speech Consortium.
 *
 * THIS SOFTWARE IS PROVIDED BY CARNEGIE MELLON UNIVERSITY ``AS IS'' AND 
 * ANY EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, 
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL CARNEGIE MELLON UNIVERSITY
 * NOR ITS EMPLOYEES BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, 
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY 
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 *
 */

/*
 * ad.c -- Wraps a "sphinx-II standard" audio interface around the basic audio
 * 		utilities.
 */

#include <stdio.h>
#include <string.h>
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "sphinx_types.h"
#include "ad.h"
#include "pablio.h"

#define QUIT(x)		{fprintf x; exit(-1);}


ad_rec_t *
ad_open_dev(const char *dev, int32 samples_per_sec)
{
    PABLIO_Stream *astream;
    ad_rec_t *adrec;
    PaError err;

    if ((err = OpenAudioStream(&astream, samples_per_sec, paInt16,
                               PABLIO_READ | PABLIO_MONO))) {
        fprintf(stderr, "OpenAudioStream failed: %d\n", err);
        return NULL;
    }

    if ((adrec = calloc(1, sizeof(*adrec))) == NULL)
        return NULL;
    adrec->astream = astream;
    adrec->sps = samples_per_sec;
    adrec->bps = sizeof(int16);

    return adrec;
}

ad_rec_t *
ad_open_sps(int32 sps)
{
    return ad_open_dev(DEFAULT_DEVICE, sps);
}


ad_rec_t *
ad_open(void)
{
    return ad_open_sps(DEFAULT_SAMPLES_PER_SEC);
}


int32
ad_start_rec(ad_rec_t * r)
{
    r->recording = 1;
    return 0;
}


int32
ad_stop_rec(ad_rec_t * r)
{
    r->recording = 0;
    return 0;
}


int32
ad_read(ad_rec_t * r, int16 * buf, int32 max)
{
    long to_read;

    if (r == NULL || !r->recording)
        return -1;

    to_read = GetAudioStreamReadable(r->astream);
    if (to_read > max)
	to_read = max;
    ReadAudioStream(r->astream, buf, to_read);
    return to_read;
}


int32
ad_close(ad_rec_t * r)
{
    CloseAudioStream(r->astream);
    free(r);
    return 0;
}
