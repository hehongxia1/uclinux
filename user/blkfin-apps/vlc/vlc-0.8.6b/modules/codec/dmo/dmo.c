/*****************************************************************************
 * dmo.c : DirectMedia Object decoder module for vlc
 *****************************************************************************
 * Copyright (C) 2002, 2003 the VideoLAN team
 * $Id: dmo.c 16460 2006-08-31 22:01:13Z hartman $
 *
 * Author: Gildas Bazin <gbazin@videolan.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

/*****************************************************************************
 * Preamble
 *****************************************************************************/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <vlc/vlc.h>
#include <vlc/decoder.h>
#include <vlc/vout.h>

#ifndef WIN32
#    define LOADER
#else
#   include <objbase.h>
#endif

#ifdef LOADER
/* Need the w32dll loader from mplayer */
#   include <wine/winerror.h>
#   include <ldt_keeper.h>
#   include <wine/windef.h>
#endif

#include "codecs.h"
#include "dmo.h"

//#define DMO_DEBUG 1

#ifdef LOADER
/* Not Needed */
long CoInitialize( void *pvReserved ) { return -1; }
void CoUninitialize( void ) { }

/* A few prototypes */
HMODULE WINAPI LoadLibraryA(LPCSTR);
#define LoadLibrary LoadLibraryA
FARPROC WINAPI GetProcAddress(HMODULE,LPCSTR);
int     WINAPI FreeLibrary(HMODULE);
#endif /* LOADER */

typedef long (STDCALL *GETCLASS) ( const GUID*, const GUID*, void** );

static int pi_channels_maps[7] =
{
    0,
    AOUT_CHAN_CENTER,
    AOUT_CHAN_LEFT | AOUT_CHAN_RIGHT,
    AOUT_CHAN_CENTER | AOUT_CHAN_LEFT | AOUT_CHAN_RIGHT,
    AOUT_CHAN_LEFT | AOUT_CHAN_RIGHT | AOUT_CHAN_REARLEFT
     | AOUT_CHAN_REARRIGHT,
    AOUT_CHAN_LEFT | AOUT_CHAN_RIGHT | AOUT_CHAN_CENTER
     | AOUT_CHAN_REARLEFT | AOUT_CHAN_REARRIGHT,
    AOUT_CHAN_LEFT | AOUT_CHAN_RIGHT | AOUT_CHAN_CENTER
     | AOUT_CHAN_REARLEFT | AOUT_CHAN_REARRIGHT | AOUT_CHAN_LFE
};

/*****************************************************************************
 * Module descriptor
 *****************************************************************************/
static int  DecoderOpen  ( vlc_object_t * );
static int  DecOpen      ( vlc_object_t * );
static void DecoderClose ( vlc_object_t * );
static void *DecodeBlock ( decoder_t *, block_t ** );

static int  EncoderOpen  ( vlc_object_t * );
static int  EncOpen      ( vlc_object_t * );
static void EncoderClose ( vlc_object_t * );
static block_t *EncodeBlock( encoder_t *, void * );

static int LoadDMO( vlc_object_t *, HINSTANCE *, IMediaObject **,
                    es_format_t *, vlc_bool_t );
static void CopyPicture( decoder_t *, picture_t *, uint8_t * );

vlc_module_begin();
    set_description( _("DirectMedia Object decoder") );
    add_shortcut( "dmo" );
    set_capability( "decoder", 1 );
    set_callbacks( DecoderOpen, DecoderClose );
    set_category( CAT_INPUT );
    set_subcategory( SUBCAT_INPUT_SCODEC );

#   define ENC_CFG_PREFIX "sout-dmo-"
    add_submodule();
    set_description( _("DirectMedia Object encoder") );
    set_capability( "encoder", 10 );
    set_callbacks( EncoderOpen, EncoderClose );

vlc_module_end();

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/

/****************************************************************************
 * Decoder descriptor declaration
 ****************************************************************************/
struct decoder_sys_t
{
    HINSTANCE hmsdmo_dll;
    IMediaObject *p_dmo;

    int i_min_output;
    uint8_t *p_buffer;

    date_t end_date;

#ifdef LOADER
    ldt_fs_t    *ldt_fs;
#endif
};

static const GUID guid_wvc1 = { 0xc9bfbccf, 0xe60e, 0x4588, { 0xa3, 0xdf, 0x5a, 0x03, 0xb1, 0xfd, 0x95, 0x85 } };

static const GUID guid_wmv9 = { 0x724bb6a4, 0xe526, 0x450f, { 0xaf, 0xfa, 0xab, 0x9b, 0x45, 0x12, 0x91, 0x11 } };
static const GUID guid_wma9 = { 0x27ca0808, 0x01f5, 0x4e7a, { 0x8b, 0x05, 0x87, 0xf8, 0x07, 0xa2, 0x33, 0xd1 } };

static const GUID guid_wmv = { 0x82d353df, 0x90bd, 0x4382, { 0x8b, 0xc2, 0x3f, 0x61, 0x92, 0xb7, 0x6e, 0x34 } };
static const GUID guid_wma = { 0x874131cb, 0x4ecc, 0x443b, { 0x89, 0x48, 0x74, 0x6b, 0x89, 0x59, 0x5d, 0x20 } };

static const GUID guid_wmv_enc = { 0x3181343b, 0x94a2, 0x4feb, { 0xad, 0xef, 0x30, 0xa1, 0xdd, 0xe6, 0x17, 0xb4 } };
static const GUID guid_wma_enc = { 0x70f598e9, 0xf4ab, 0x495a, { 0x99, 0xe2, 0xa7, 0xc4, 0xd3, 0xd8, 0x9a, 0xbf } };

typedef struct
{
    vlc_fourcc_t i_fourcc;
    const char   *psz_dll;
    const GUID   *p_guid;

} codec_dll;

static const codec_dll decoders_table[] =
{
    /* WVC1 */
    { VLC_FOURCC('W','V','C','1'), "wvc1dmod.dll", &guid_wvc1 },
    { VLC_FOURCC('w','v','c','1'), "wvc1dmod.dll", &guid_wvc1 },
    /* WMV3 */
    { VLC_FOURCC('W','M','V','3'), "wmv9dmod.dll", &guid_wmv9 },
    { VLC_FOURCC('w','m','v','3'), "wmv9dmod.dll", &guid_wmv9 },
    /* WMV2 */
    { VLC_FOURCC('W','M','V','2'), "wmvdmod.dll", &guid_wmv },
    { VLC_FOURCC('w','m','v','2'), "wmvdmod.dll", &guid_wmv },
    /* WMV1 */
    { VLC_FOURCC('W','M','V','1'), "wmvdmod.dll", &guid_wmv },
    { VLC_FOURCC('w','m','v','1'), "wmvdmod.dll", &guid_wmv },

    /* WMA 3 */
    { VLC_FOURCC('W','M','A','3'), "wma9dmod.dll", &guid_wma9 },
    { VLC_FOURCC('w','m','a','3'), "wma9dmod.dll", &guid_wma9 },
    { VLC_FOURCC('w','m','a','p'), "wma9dmod.dll", &guid_wma9 },
    /* WMA 2 */
    { VLC_FOURCC('W','M','A','2'), "wma9dmod.dll", &guid_wma9 },
    { VLC_FOURCC('w','m','a','2'), "wma9dmod.dll", &guid_wma9 },

    /* WMA Speech */
    { VLC_FOURCC('w','m','a','s'), "wmspdmod.dll", &guid_wma },

    /* */
    { 0, NULL, NULL }
};

static const codec_dll encoders_table[] =
{
    /* WMV2 */
    { VLC_FOURCC('W','M','V','2'), "wmvdmoe.dll", &guid_wmv_enc },
    { VLC_FOURCC('w','m','v','2'), "wmvdmoe.dll", &guid_wmv_enc },
    /* WMV1 */
    { VLC_FOURCC('W','M','V','1'), "wmvdmoe.dll", &guid_wmv_enc },
    { VLC_FOURCC('w','m','v','1'), "wmvdmoe.dll", &guid_wmv_enc },

    /* WMA 3 */
    { VLC_FOURCC('W','M','A','3'), "wmadmoe.dll", &guid_wma_enc },
    { VLC_FOURCC('w','m','a','3'), "wmadmoe.dll", &guid_wma_enc },
    /* WMA 2 */
    { VLC_FOURCC('W','M','A','2'), "wmadmoe.dll", &guid_wma_enc },
    { VLC_FOURCC('w','m','a','2'), "wmadmoe.dll", &guid_wma_enc },

    /* */
    { 0, NULL, NULL }
};

static void WINAPI DMOFreeMediaType( DMO_MEDIA_TYPE *mt )
{
    if( mt->cbFormat != 0 ) CoTaskMemFree( (PVOID)mt->pbFormat );
    if( mt->pUnk != NULL ) mt->pUnk->vt->Release( (IUnknown *)mt->pUnk );
    mt->cbFormat = 0;
    mt->pbFormat = NULL;
    mt->pUnk = NULL;
}

/*****************************************************************************
 * DecoderOpen: open dmo codec
 *****************************************************************************/
static int DecoderOpen( vlc_object_t *p_this )
{
    decoder_t *p_dec = (decoder_t*)p_this;

#ifndef LOADER
    int i_ret = DecOpen( p_this );
    if( i_ret != VLC_SUCCESS ) return i_ret;


#else
    /* We can't open it now, because of ldt_keeper or something
     * Open/Decode/Close has to be done in the same thread */
    int i;

    p_dec->p_sys = NULL;

    /* Probe if we support it */
    for( i = 0; decoders_table[i].i_fourcc != 0; i++ )
    {
        if( decoders_table[i].i_fourcc == p_dec->fmt_in.i_codec )
        {
            msg_Dbg( p_dec, "DMO codec for %4.4s may work with dll=%s",
                     (char*)&p_dec->fmt_in.i_codec,
                     decoders_table[i].psz_dll );
            break;
        }
    }

    p_dec->p_sys = NULL;
    if( !decoders_table[i].i_fourcc ) return VLC_EGENERIC;
#endif /* LOADER */

    /* Set callbacks */
    p_dec->pf_decode_video = (picture_t *(*)(decoder_t *, block_t **))
        DecodeBlock;
    p_dec->pf_decode_audio = (aout_buffer_t *(*)(decoder_t *, block_t **))
        DecodeBlock;

    return VLC_SUCCESS;
}

/*****************************************************************************
 * DecOpen: open dmo codec
 *****************************************************************************/
static int DecOpen( vlc_object_t *p_this )
{
    decoder_t *p_dec = (decoder_t*)p_this;
    decoder_sys_t *p_sys = NULL;

    DMO_MEDIA_TYPE dmo_input_type, dmo_output_type;
    IMediaObject *p_dmo = NULL;
    HINSTANCE hmsdmo_dll = NULL;

    VIDEOINFOHEADER *p_vih = NULL;
    WAVEFORMATEX *p_wf = NULL;

#ifdef LOADER
    ldt_fs_t *ldt_fs = Setup_LDT_Keeper();
#else
    /* Initialize OLE/COM */
    CoInitialize( 0 );
#endif /* LOADER */

    if( LoadDMO( p_this, &hmsdmo_dll, &p_dmo, &p_dec->fmt_in, VLC_FALSE )
        != VLC_SUCCESS )
    {
        hmsdmo_dll = 0;
        p_dmo = 0;
        goto error;
    }

    /* Setup input format */
    memset( &dmo_input_type, 0, sizeof(dmo_input_type) );
    dmo_input_type.pUnk = 0;

    if( p_dec->fmt_in.i_cat == AUDIO_ES )
    {
        uint16_t i_tag;
        int i_size = sizeof(WAVEFORMATEX) + p_dec->fmt_in.i_extra;
        p_wf = malloc( i_size );

        memset( p_wf, 0, sizeof(WAVEFORMATEX) );
        if( p_dec->fmt_in.i_extra )
            memcpy( &p_wf[1], p_dec->fmt_in.p_extra, p_dec->fmt_in.i_extra );

        dmo_input_type.majortype  = MEDIATYPE_Audio;
        dmo_input_type.subtype    = dmo_input_type.majortype;
        dmo_input_type.subtype.Data1 = p_dec->fmt_in.i_codec;
        fourcc_to_wf_tag( p_dec->fmt_in.i_codec, &i_tag );
        if( i_tag ) dmo_input_type.subtype.Data1 = i_tag;

        p_wf->wFormatTag = dmo_input_type.subtype.Data1;
        p_wf->nSamplesPerSec = p_dec->fmt_in.audio.i_rate;
        p_wf->nChannels = p_dec->fmt_in.audio.i_channels;
        p_wf->wBitsPerSample = p_dec->fmt_in.audio.i_bitspersample;
        p_wf->nBlockAlign = p_dec->fmt_in.audio.i_blockalign;
        p_wf->nAvgBytesPerSec = p_dec->fmt_in.i_bitrate / 8;
        p_wf->cbSize = p_dec->fmt_in.i_extra;

        dmo_input_type.formattype = FORMAT_WaveFormatEx;
        dmo_input_type.cbFormat   = i_size;
        dmo_input_type.pbFormat   = (char *)p_wf;
        dmo_input_type.bFixedSizeSamples = 1;
        dmo_input_type.bTemporalCompression = 0;
        dmo_input_type.lSampleSize = p_wf->nBlockAlign;
    }
    else
    {
        BITMAPINFOHEADER *p_bih;

        int i_size = sizeof(VIDEOINFOHEADER) + p_dec->fmt_in.i_extra;
        p_vih = malloc( i_size );

        memset( p_vih, 0, sizeof(VIDEOINFOHEADER) );
        if( p_dec->fmt_in.i_extra )
            memcpy( &p_vih[1], p_dec->fmt_in.p_extra, p_dec->fmt_in.i_extra );

        p_bih = &p_vih->bmiHeader;
        p_bih->biCompression = p_dec->fmt_in.i_codec;
        p_bih->biWidth = p_dec->fmt_in.video.i_width;
        p_bih->biHeight = p_dec->fmt_in.video.i_height;
        p_bih->biBitCount = p_dec->fmt_in.video.i_bits_per_pixel;
        p_bih->biPlanes = 1;
        p_bih->biSize = i_size - sizeof(VIDEOINFOHEADER) +
            sizeof(BITMAPINFOHEADER);

        p_vih->rcSource.left = p_vih->rcSource.top = 0;
        p_vih->rcSource.right = p_dec->fmt_in.video.i_width;
        p_vih->rcSource.bottom = p_dec->fmt_in.video.i_height;
        p_vih->rcTarget = p_vih->rcSource;

        dmo_input_type.majortype  = MEDIATYPE_Video;
        dmo_input_type.subtype    = dmo_input_type.majortype;
        dmo_input_type.subtype.Data1 = p_dec->fmt_in.i_codec;
        dmo_input_type.formattype = FORMAT_VideoInfo;
        dmo_input_type.bFixedSizeSamples = 0;
        dmo_input_type.bTemporalCompression = 1;
        dmo_input_type.cbFormat = i_size;
        dmo_input_type.pbFormat = (char *)p_vih;
    }

    if( p_dmo->vt->SetInputType( p_dmo, 0, &dmo_input_type, 0 ) )
    {
        msg_Err( p_dec, "can't set DMO input type" );
        goto error;
    }
    msg_Dbg( p_dec, "DMO input type set" );

    /* Setup output format */
    memset( &dmo_output_type, 0, sizeof(dmo_output_type) );
    dmo_output_type.pUnk = 0;

    if( p_dec->fmt_in.i_cat == AUDIO_ES )
    {
        /* Setup the format */
        p_dec->fmt_out.i_codec = AOUT_FMT_S16_NE;
        p_dec->fmt_out.audio.i_rate     = p_dec->fmt_in.audio.i_rate;
        p_dec->fmt_out.audio.i_channels = p_dec->fmt_in.audio.i_channels;
        p_dec->fmt_out.audio.i_bitspersample = 16;//p_dec->fmt_in.audio.i_bitspersample; We request 16
        p_dec->fmt_out.audio.i_physical_channels =
            p_dec->fmt_out.audio.i_original_channels =
                pi_channels_maps[p_dec->fmt_out.audio.i_channels];

        p_wf->wFormatTag = WAVE_FORMAT_PCM;
        p_wf->nSamplesPerSec = p_dec->fmt_out.audio.i_rate;
        p_wf->nChannels = p_dec->fmt_out.audio.i_channels;
        p_wf->wBitsPerSample = p_dec->fmt_out.audio.i_bitspersample;
        p_wf->nBlockAlign =
            p_wf->wBitsPerSample / 8 * p_wf->nChannels;
        p_wf->nAvgBytesPerSec =
            p_wf->nSamplesPerSec * p_wf->nBlockAlign;
        p_wf->cbSize = 0;

        dmo_output_type.majortype  = MEDIATYPE_Audio;
        dmo_output_type.formattype = FORMAT_WaveFormatEx;
        dmo_output_type.subtype    = MEDIASUBTYPE_PCM;
        dmo_output_type.cbFormat   = sizeof(WAVEFORMATEX);
        dmo_output_type.pbFormat   = (char *)p_wf;
        dmo_output_type.bFixedSizeSamples = 1;
        dmo_output_type.bTemporalCompression = 0;
        dmo_output_type.lSampleSize = p_wf->nBlockAlign;
    }
    else
    {
        BITMAPINFOHEADER *p_bih;
        DMO_MEDIA_TYPE mt;
        unsigned i_chroma = VLC_FOURCC('Y','U','Y','2');
        int i_planes = 1, i_bpp = 16;
        int i = 0;

        /* Find out which chroma to use */
        while( !p_dmo->vt->GetOutputType( p_dmo, 0, i++, &mt ) )
        {
            if( mt.subtype.Data1 == VLC_FOURCC('Y','V','1','2') )
            {
                i_chroma = mt.subtype.Data1;
                i_planes = 3; i_bpp = 12;
            }

            DMOFreeMediaType( &mt );
        }

        p_dec->fmt_out.i_codec = i_chroma == VLC_FOURCC('Y','V','1','2') ?
            VLC_FOURCC('I','4','2','0') : i_chroma;
        p_dec->fmt_out.video.i_width = p_dec->fmt_in.video.i_width;
        p_dec->fmt_out.video.i_height = p_dec->fmt_in.video.i_height;
        p_dec->fmt_out.video.i_bits_per_pixel = i_bpp;

        /* If an aspect-ratio was specified in the input format then force it */
        if( p_dec->fmt_in.video.i_aspect )
        {
            p_dec->fmt_out.video.i_aspect = p_dec->fmt_in.video.i_aspect;
        }
        else
        {
            p_dec->fmt_out.video.i_aspect = VOUT_ASPECT_FACTOR *
                p_dec->fmt_out.video.i_width / p_dec->fmt_out.video.i_height;
        }

        p_bih = &p_vih->bmiHeader;
        p_bih->biCompression = i_chroma;
        p_bih->biHeight *= -1;
        p_bih->biBitCount = p_dec->fmt_out.video.i_bits_per_pixel;
        p_bih->biSizeImage = p_dec->fmt_in.video.i_width *
            p_dec->fmt_in.video.i_height *
            (p_dec->fmt_in.video.i_bits_per_pixel + 7) / 8;

        p_bih->biPlanes = i_planes;
        p_bih->biSize = sizeof(BITMAPINFOHEADER);

        dmo_output_type.majortype = MEDIATYPE_Video;
        dmo_output_type.formattype = FORMAT_VideoInfo;
        dmo_output_type.subtype = dmo_output_type.majortype;
        dmo_output_type.subtype.Data1 = p_bih->biCompression;
        dmo_output_type.bFixedSizeSamples = VLC_TRUE;
        dmo_output_type.bTemporalCompression = 0;
        dmo_output_type.lSampleSize = p_bih->biSizeImage;
        dmo_output_type.cbFormat = sizeof(VIDEOINFOHEADER);
        dmo_output_type.pbFormat = (char *)p_vih;
    }

#ifdef DMO_DEBUG
    /* Enumerate output types */
    if( p_dec->fmt_in.i_cat == VIDEO_ES )
    {
        int i = 0;
        DMO_MEDIA_TYPE mt;

        while( !p_dmo->vt->GetOutputType( p_dmo, 0, i++, &mt ) )
        {
            msg_Dbg( p_dec, "available output chroma: %4.4s",
                     (char *)&mt.subtype.Data1 );
            DMOFreeMediaType( &mt );
        }
    }
#endif

    if( p_dmo->vt->SetOutputType( p_dmo, 0, &dmo_output_type, 0 ) )
    {
        msg_Err( p_dec, "can't set DMO output type" );
        goto error;
    }
    msg_Dbg( p_dec, "DMO output type set" );

    /* Allocate the memory needed to store the decoder's structure */
    if( ( p_dec->p_sys = p_sys =
          (decoder_sys_t *)malloc(sizeof(decoder_sys_t)) ) == NULL )
    {
        msg_Err( p_dec, "out of memory" );
        goto error;
    }

    p_sys->hmsdmo_dll = hmsdmo_dll;
    p_sys->p_dmo = p_dmo;
#ifdef LOADER
    p_sys->ldt_fs = ldt_fs;
#endif

    /* Find out some properties of the output */
    {
        uint32_t i_size, i_align;

        p_sys->i_min_output = 0;
        if( p_dmo->vt->GetOutputSizeInfo( p_dmo, 0, &i_size, &i_align ) )
        {
            msg_Err( p_dec, "GetOutputSizeInfo() failed" );
            goto error;
        }
        else
        {
            msg_Dbg( p_dec, "GetOutputSizeInfo(): bytes %i, align %i",
                     i_size, i_align );
            p_sys->i_min_output = i_size;
            p_sys->p_buffer = malloc( i_size );
            if( !p_sys->p_buffer ) goto error;
        }
    }

    /* Set output properties */
    p_dec->fmt_out.i_cat = p_dec->fmt_in.i_cat;
    if( p_dec->fmt_out.i_cat == AUDIO_ES )
        date_Init( &p_sys->end_date, p_dec->fmt_in.audio.i_rate, 1 );
    else
        date_Init( &p_sys->end_date, 25 /* FIXME */, 1 );

    if( p_vih ) free( p_vih );
    if( p_wf ) free( p_wf );

    return VLC_SUCCESS;

 error:

    if( p_dmo ) p_dmo->vt->Release( (IUnknown *)p_dmo );
    if( hmsdmo_dll ) FreeLibrary( hmsdmo_dll );

#ifdef LOADER
    Restore_LDT_Keeper( ldt_fs );
#else
    /* Uninitialize OLE/COM */
    CoUninitialize();
#endif /* LOADER */

    if( p_vih ) free( p_vih );
    if( p_wf )  free( p_wf );
    if( p_sys ) free( p_sys );

    return VLC_EGENERIC;
}

/*****************************************************************************
 * LoadDMO: Load the DMO object
 *****************************************************************************/
static int LoadDMO( vlc_object_t *p_this, HINSTANCE *p_hmsdmo_dll,
                    IMediaObject **pp_dmo, es_format_t *p_fmt,
                    vlc_bool_t b_out )
{
    DMO_PARTIAL_MEDIATYPE dmo_partial_type;
    int i_err;

#ifndef LOADER
    long (STDCALL *OurDMOEnum)( const GUID *, uint32_t, uint32_t,
                               const DMO_PARTIAL_MEDIATYPE *,
                               uint32_t, const DMO_PARTIAL_MEDIATYPE *,
                               IEnumDMO ** );

    IEnumDMO *p_enum_dmo = NULL;
    WCHAR *psz_dmo_name;
    GUID clsid_dmo;
    uint32_t i_dummy;
#endif

    GETCLASS GetClass;
    IClassFactory *cFactory = NULL;
    IUnknown *cObject = NULL;
    const codec_dll *codecs_table = b_out ? encoders_table : decoders_table;
    int i_codec;

    /* Look for a DMO which can handle the requested codec */
    if( p_fmt->i_cat == AUDIO_ES )
    {
        uint16_t i_tag;
        dmo_partial_type.type = MEDIATYPE_Audio;
        dmo_partial_type.subtype = dmo_partial_type.type;
        dmo_partial_type.subtype.Data1 = p_fmt->i_codec;
        fourcc_to_wf_tag( p_fmt->i_codec, &i_tag );
        if( i_tag ) dmo_partial_type.subtype.Data1 = i_tag;
    }
    else
    {
        dmo_partial_type.type = MEDIATYPE_Video;
        dmo_partial_type.subtype = dmo_partial_type.type;
        dmo_partial_type.subtype.Data1 = p_fmt->i_codec;
    }

#ifndef LOADER
    /* Load msdmo DLL */
    *p_hmsdmo_dll = LoadLibrary( "msdmo.dll" );
    if( *p_hmsdmo_dll == NULL )
    {
        msg_Dbg( p_this, "failed loading msdmo.dll" );
        return VLC_EGENERIC;
    }
    OurDMOEnum = (void *)GetProcAddress( *p_hmsdmo_dll, "DMOEnum" );
    if( OurDMOEnum == NULL )
    {
        msg_Dbg( p_this, "GetProcAddress failed to find DMOEnum()" );
        FreeLibrary( *p_hmsdmo_dll );
        return VLC_EGENERIC;
    }

    if( !b_out )
    {
        i_err = OurDMOEnum( &GUID_NULL, 1 /*DMO_ENUMF_INCLUDE_KEYED*/,
                            1, &dmo_partial_type, 0, NULL, &p_enum_dmo );
    }
    else
    {
        i_err = OurDMOEnum( &GUID_NULL, 1 /*DMO_ENUMF_INCLUDE_KEYED*/,
                            0, NULL, 1, &dmo_partial_type, &p_enum_dmo );
    }
    if( i_err )
    {
        FreeLibrary( *p_hmsdmo_dll );
        /* return VLC_EGENERIC; */
        /* Try loading the dll directly */
        goto loader;
    }

    /* Pickup the first available codec */
    *pp_dmo = 0;
    while( ( S_OK == p_enum_dmo->vt->Next( p_enum_dmo, 1, &clsid_dmo,
                     &psz_dmo_name, &i_dummy /* NULL doesn't work */ ) ) )
    {
        char psz_temp[MAX_PATH];
        wcstombs( psz_temp, psz_dmo_name, MAX_PATH );
        msg_Dbg( p_this, "found DMO: %s", psz_temp );
        CoTaskMemFree( psz_dmo_name );

        /* Create DMO */
        if( CoCreateInstance( &clsid_dmo, NULL, CLSCTX_INPROC,
                              &IID_IMediaObject, (void **)pp_dmo ) )
        {
            msg_Warn( p_this, "can't create DMO: %s", psz_temp );
            *pp_dmo = 0;
        }
        else break;
    }

    p_enum_dmo->vt->Release( (IUnknown *)p_enum_dmo );

    if( !*pp_dmo )
    {
        FreeLibrary( *p_hmsdmo_dll );
        /* return VLC_EGENERIC; */
        /* Try loading the dll directly */
        goto loader;
    }

    return VLC_SUCCESS;

loader:
#endif   /* LOADER */

    for( i_codec = 0; codecs_table[i_codec].i_fourcc != 0; i_codec++ )
    {
        if( codecs_table[i_codec].i_fourcc == p_fmt->i_codec )
            break;
    }
    if( codecs_table[i_codec].i_fourcc == 0 )
        return VLC_EGENERIC;    /* Can't happen */

    *p_hmsdmo_dll = LoadLibrary( codecs_table[i_codec].psz_dll );
    if( *p_hmsdmo_dll == NULL )
    {
        msg_Dbg( p_this, "failed loading '%s'",
                 codecs_table[i_codec].psz_dll );
        return VLC_EGENERIC;
    }

    GetClass = (GETCLASS)GetProcAddress( *p_hmsdmo_dll, "DllGetClassObject" );
    if (!GetClass)
    {
        msg_Dbg( p_this, "GetProcAddress failed to find DllGetClassObject()" );
        FreeLibrary( *p_hmsdmo_dll );
        return VLC_EGENERIC;
    }

    i_err = GetClass( codecs_table[i_codec].p_guid, &IID_IClassFactory,
                      (void**)&cFactory );
    if( i_err || cFactory == NULL )
    {
        msg_Dbg( p_this, "no such class object" );
        FreeLibrary( *p_hmsdmo_dll );
        return VLC_EGENERIC;
    }

    i_err = cFactory->vt->CreateInstance( cFactory, 0, &IID_IUnknown,
                                          (void**)&cObject );
    cFactory->vt->Release( (IUnknown*)cFactory );
    if( i_err || !cObject )
    {
        msg_Dbg( p_this, "class factory failure" );
        FreeLibrary( *p_hmsdmo_dll );
        return VLC_EGENERIC;
    }
    i_err = cObject->vt->QueryInterface( cObject, &IID_IMediaObject,
                                        (void**)pp_dmo );
    cObject->vt->Release( (IUnknown*)cObject );
    if( i_err || !*pp_dmo )
    {
        msg_Dbg( p_this, "QueryInterface failure" );
        FreeLibrary( *p_hmsdmo_dll );
        return VLC_EGENERIC;
    }

    return VLC_SUCCESS;
}

/*****************************************************************************
 * DecoderClose: close codec
 *****************************************************************************/
void DecoderClose( vlc_object_t *p_this )
{
    decoder_t *p_dec = (decoder_t*)p_this;
    decoder_sys_t *p_sys = p_dec->p_sys;

    if( !p_sys ) return;

    if( p_sys->p_dmo ) p_sys->p_dmo->vt->Release( (IUnknown *)p_sys->p_dmo );
    FreeLibrary( p_sys->hmsdmo_dll );

#ifdef LOADER
#if 0
    Restore_LDT_Keeper( p_sys->ldt_fs );
#endif
#else
    /* Uninitialize OLE/COM */
    CoUninitialize();
#endif

    if( p_sys->p_buffer ) free( p_sys->p_buffer );
    free( p_sys );
}

/****************************************************************************
 * DecodeBlock: the whole thing
 ****************************************************************************
 * This function must be fed with ogg packets.
 ****************************************************************************/
static void *DecodeBlock( decoder_t *p_dec, block_t **pp_block )
{
    decoder_sys_t *p_sys = p_dec->p_sys;
    block_t *p_block;
    int i_result;

    DMO_OUTPUT_DATA_BUFFER db;
    CMediaBuffer *p_out;
    block_t block_out;
    uint32_t i_status;

    if( p_sys == NULL )
    {
        if( DecOpen( VLC_OBJECT(p_dec) ) )
        {
            msg_Err( p_dec, "DecOpen failed" );
            return NULL;
        }
        p_sys = p_dec->p_sys;
    }

    if( !pp_block ) return NULL;

    p_block = *pp_block;

    /* Won't work with streams with B-frames, but do we have any ? */
    if( p_block && p_block->i_pts <= 0 ) p_block->i_pts = p_block->i_dts;

    /* Date management */
    if( p_block && p_block->i_pts > 0 &&
        p_block->i_pts != date_Get( &p_sys->end_date ) )
    {
        date_Set( &p_sys->end_date, p_block->i_pts );
    }

#if 0 /* Breaks the video decoding */
    if( !date_Get( &p_sys->end_date ) )
    {
        /* We've just started the stream, wait for the first PTS. */
        if( p_block ) block_Release( p_block );
        return NULL;
    }
#endif

    /* Feed input to the DMO */
    if( p_block && p_block->i_buffer )
    {
        CMediaBuffer *p_in;

        p_in = CMediaBufferCreate( p_block, p_block->i_buffer, VLC_TRUE );

        i_result = p_sys->p_dmo->vt->ProcessInput( p_sys->p_dmo, 0,
                       (IMediaBuffer *)p_in, DMO_INPUT_DATA_BUFFERF_SYNCPOINT,
                       0, 0 );

        p_in->vt->Release( (IUnknown *)p_in );

        if( i_result == S_FALSE )
        {
            /* No output generated */
#ifdef DMO_DEBUG
            msg_Dbg( p_dec, "ProcessInput(): no output generated" );
#endif
            return NULL;
        }
        else if( i_result == (int)DMO_E_NOTACCEPTING )
        {
            /* Need to call ProcessOutput */
            msg_Dbg( p_dec, "ProcessInput(): not accepting" );
        }
        else if( i_result != S_OK )
        {
            msg_Dbg( p_dec, "ProcessInput(): failed" );
            return NULL;
        }
        else
        {
            //msg_Dbg( p_dec, "ProcessInput(): successful" );
            *pp_block = 0;
        }
    }
    else if( p_block && !p_block->i_buffer )
    {
        block_Release( p_block );
        *pp_block = 0;
    }

    /* Get output from the DMO */
    block_out.p_buffer = p_sys->p_buffer;
    block_out.i_buffer = 0;

    p_out = CMediaBufferCreate( &block_out, p_sys->i_min_output, VLC_FALSE );
    memset( &db, 0, sizeof(db) );
    db.pBuffer = (IMediaBuffer *)p_out;

    i_result = p_sys->p_dmo->vt->ProcessOutput( p_sys->p_dmo,
                   DMO_PROCESS_OUTPUT_DISCARD_WHEN_NO_BUFFER,
                   1, &db, &i_status );

    if( i_result != S_OK )
    {
        if( i_result != S_FALSE )
            msg_Dbg( p_dec, "ProcessOutput(): failed" );
#if DMO_DEBUG
        else
            msg_Dbg( p_dec, "ProcessOutput(): no output" );
#endif

        p_out->vt->Release( (IUnknown *)p_out );
        return NULL;
    }

#if DMO_DEBUG
    msg_Dbg( p_dec, "ProcessOutput(): success" );
#endif

    if( !block_out.i_buffer )
    {
#if DMO_DEBUG
        msg_Dbg( p_dec, "ProcessOutput(): no output (i_buffer_out == 0)" );
#endif
        p_out->vt->Release( (IUnknown *)p_out );
        return NULL;
    }

    if( p_dec->fmt_out.i_cat == VIDEO_ES )
    {
        /* Get a new picture */
        picture_t *p_pic = p_dec->pf_vout_buffer_new( p_dec );
        if( !p_pic ) return NULL;

        CopyPicture( p_dec, p_pic, block_out.p_buffer );

        /* Date management */
        p_pic->date = date_Get( &p_sys->end_date );
        date_Increment( &p_sys->end_date, 1 );

        p_out->vt->Release( (IUnknown *)p_out );

        return p_pic;
    }
    else
    {
        aout_buffer_t *p_aout_buffer;
        int i_samples = block_out.i_buffer /
            ( p_dec->fmt_out.audio.i_bitspersample *
              p_dec->fmt_out.audio.i_channels / 8 );

        p_aout_buffer = p_dec->pf_aout_buffer_new( p_dec, i_samples );
        memcpy( p_aout_buffer->p_buffer,
                block_out.p_buffer, block_out.i_buffer );

        /* Date management */
        p_aout_buffer->start_date = date_Get( &p_sys->end_date );
        p_aout_buffer->end_date =
            date_Increment( &p_sys->end_date, i_samples );

        p_out->vt->Release( (IUnknown *)p_out );

        return p_aout_buffer;
    }

    return NULL;
}

static void CopyPicture( decoder_t *p_dec, picture_t *p_pic, uint8_t *p_in )
{
    int i_plane, i_line, i_width, i_dst_stride;
    uint8_t *p_dst, *p_src = p_in;

    p_dst = p_pic->p[1].p_pixels;
    p_pic->p[1].p_pixels = p_pic->p[2].p_pixels;
    p_pic->p[2].p_pixels = p_dst;

    for( i_plane = 0; i_plane < p_pic->i_planes; i_plane++ )
    {
        p_dst = p_pic->p[i_plane].p_pixels;
        i_width = p_pic->p[i_plane].i_visible_pitch;
        i_dst_stride  = p_pic->p[i_plane].i_pitch;

        for( i_line = 0; i_line < p_pic->p[i_plane].i_visible_lines; i_line++ )
        {
            p_dec->p_vlc->pf_memcpy( p_dst, p_src, i_width );
            p_src += i_width;
            p_dst += i_dst_stride;
        }
    }

    p_dst = p_pic->p[1].p_pixels;
    p_pic->p[1].p_pixels = p_pic->p[2].p_pixels;
    p_pic->p[2].p_pixels = p_dst;
}

/****************************************************************************
 * Encoder descriptor declaration
 ****************************************************************************/
struct encoder_sys_t
{
    HINSTANCE hmsdmo_dll;
    IMediaObject *p_dmo;

    int i_min_output;

    date_t end_date;

#ifdef LOADER
    ldt_fs_t    *ldt_fs;
#endif
};

/*****************************************************************************
 * EncoderOpen: open dmo codec
 *****************************************************************************/
static int EncoderOpen( vlc_object_t *p_this )
{
    encoder_t *p_enc = (encoder_t*)p_this;

#ifndef LOADER
    int i_ret = EncOpen( p_this );
    if( i_ret != VLC_SUCCESS ) return i_ret;

#else
    /* We can't open it now, because of ldt_keeper or something
     * Open/Encode/Close has to be done in the same thread */
    int i;

    /* Probe if we support it */
    for( i = 0; encoders_table[i].i_fourcc != 0; i++ )
    {
        if( encoders_table[i].i_fourcc == p_enc->fmt_out.i_codec )
        {
            msg_Dbg( p_enc, "DMO codec for %4.4s may work with dll=%s",
                     (char*)&p_enc->fmt_out.i_codec,
                     encoders_table[i].psz_dll );
            break;
        }
    }

    p_enc->p_sys = NULL;
    if( !encoders_table[i].i_fourcc ) return VLC_EGENERIC;
#endif /* LOADER */

    /* Set callbacks */
    p_enc->pf_encode_video = (block_t *(*)(encoder_t *, picture_t *))
        EncodeBlock;
    p_enc->pf_encode_audio = (block_t *(*)(encoder_t *, aout_buffer_t *))
        EncodeBlock;

    return VLC_SUCCESS;
}

/*****************************************************************************
 * EncoderSetVideoType: configures the input and output types of the dmo
 *****************************************************************************/
static int EncoderSetVideoType( encoder_t *p_enc, IMediaObject *p_dmo )
{
    int i, i_selected, i_err;
    DMO_MEDIA_TYPE dmo_type;
    VIDEOINFOHEADER vih, *p_vih;
    BITMAPINFOHEADER *p_bih;

    /* FIXME */
    p_enc->fmt_in.video.i_bits_per_pixel = 
        p_enc->fmt_out.video.i_bits_per_pixel = 12;

    /* Enumerate input format (for debug output) */
    i = 0;
    while( !p_dmo->vt->GetInputType( p_dmo, 0, i++, &dmo_type ) )
    {
        p_vih = (VIDEOINFOHEADER *)dmo_type.pbFormat;

        msg_Dbg( p_enc, "available input chroma: %4.4s",
                 (char *)&dmo_type.subtype.Data1 );
        if( !memcmp( &dmo_type.subtype, &MEDIASUBTYPE_RGB565, 16 ) )
            msg_Dbg( p_enc, "-> MEDIASUBTYPE_RGB565" );
        if( !memcmp( &dmo_type.subtype, &MEDIASUBTYPE_RGB24, 16 ) )
            msg_Dbg( p_enc, "-> MEDIASUBTYPE_RGB24" );

        DMOFreeMediaType( &dmo_type );
    }

    /* Setup input format */
    memset( &dmo_type, 0, sizeof(dmo_type) );
    dmo_type.pUnk = 0;
    memset( &vih, 0, sizeof(VIDEOINFOHEADER) );

    p_bih = &vih.bmiHeader;
    p_bih->biCompression = VLC_FOURCC('I','4','2','0');
    p_bih->biWidth = p_enc->fmt_in.video.i_width;
    p_bih->biHeight = p_enc->fmt_in.video.i_height;
    p_bih->biBitCount = p_enc->fmt_in.video.i_bits_per_pixel;
    p_bih->biSizeImage = p_enc->fmt_in.video.i_width *
        p_enc->fmt_in.video.i_height * p_enc->fmt_in.video.i_bits_per_pixel /8;
    p_bih->biPlanes = 3;
    p_bih->biSize = sizeof(BITMAPINFOHEADER);

    vih.rcSource.left = vih.rcSource.top = 0;
    vih.rcSource.right = p_enc->fmt_in.video.i_width;
    vih.rcSource.bottom = p_enc->fmt_in.video.i_height;
    vih.rcTarget = vih.rcSource;

    vih.AvgTimePerFrame = I64C(10000000) / 25; //FIXME

    dmo_type.majortype = MEDIATYPE_Video;
    //dmo_type.subtype = MEDIASUBTYPE_RGB24;
    dmo_type.subtype = MEDIASUBTYPE_I420;
    //dmo_type.subtype.Data1 = p_bih->biCompression;
    dmo_type.formattype = FORMAT_VideoInfo;
    dmo_type.bFixedSizeSamples = TRUE;
    dmo_type.bTemporalCompression = FALSE;
    dmo_type.lSampleSize = p_bih->biSizeImage;
    dmo_type.cbFormat = sizeof(VIDEOINFOHEADER);
    dmo_type.pbFormat = (char *)&vih;

    if( ( i_err = p_dmo->vt->SetInputType( p_dmo, 0, &dmo_type, 0 ) ) )
    {
        msg_Err( p_enc, "can't set DMO input type: %x", i_err );
        return VLC_EGENERIC;
    }

    msg_Dbg( p_enc, "successfully set input type" );

    /* Setup output format */
    memset( &dmo_type, 0, sizeof(dmo_type) );
    dmo_type.pUnk = 0;

    /* Enumerate output types */
    i = 0, i_selected = -1;
    while( !p_dmo->vt->GetOutputType( p_dmo, 0, i++, &dmo_type ) )
    {
        p_vih = (VIDEOINFOHEADER *)dmo_type.pbFormat;

        msg_Dbg( p_enc, "available output codec: %4.4s",
                 (char *)&dmo_type.subtype.Data1 );

        if( p_vih->bmiHeader.biCompression == p_enc->fmt_out.i_codec )
            i_selected = i - 1;

        DMOFreeMediaType( &dmo_type );
    }

    if( i_selected < 0 )
    {
        msg_Err( p_enc, "couldn't find codec: %4.4s",
                 (char *)&p_enc->fmt_out.i_codec );
        return VLC_EGENERIC;
    }

    p_dmo->vt->GetOutputType( p_dmo, 0, i_selected, &dmo_type );
    ((VIDEOINFOHEADER *)dmo_type.pbFormat)->dwBitRate =
        p_enc->fmt_out.i_bitrate;

    /* Get the private data for the codec */
    while( 1 )
    {
        IWMCodecPrivateData *p_privdata;
        VIDEOINFOHEADER *p_vih;
        uint8_t *p_data = 0;
        uint32_t i_data = 0, i_vih;

        i_err = p_dmo->vt->QueryInterface( (IUnknown *)p_dmo,
                                           &IID_IWMCodecPrivateData,
                                           (void **)&p_privdata );
        if( i_err ) break;

        i_err = p_privdata->vt->SetPartialOutputType( p_privdata, &dmo_type );
        if( i_err )
        {
            msg_Err( p_enc, "SetPartialOutputType() failed" );
            p_privdata->vt->Release( (IUnknown *)p_privdata );
            break;
        }

        i_err = p_privdata->vt->GetPrivateData( p_privdata, NULL, &i_data );
        if( i_err )
        {
            msg_Err( p_enc, "GetPrivateData() failed" );
            p_privdata->vt->Release( (IUnknown *)p_privdata );
            break;
        }

        p_data = malloc( i_data );
        i_err = p_privdata->vt->GetPrivateData( p_privdata, p_data, &i_data );

        /* Update the media type with the private data */
        i_vih = dmo_type.cbFormat + i_data;
        p_vih = CoTaskMemAlloc( i_vih );
        memcpy( p_vih, dmo_type.pbFormat, dmo_type.cbFormat );
        memcpy( ((uint8_t *)p_vih) + dmo_type.cbFormat, p_data, i_data );
        DMOFreeMediaType( &dmo_type );
        dmo_type.pbFormat = (char*)p_vih;
        dmo_type.cbFormat = i_vih;

        msg_Dbg( p_enc, "found extra data: %i", i_data );
        p_enc->fmt_out.i_extra = i_data;
        p_enc->fmt_out.p_extra = p_data;
        break;
    }

    i_err = p_dmo->vt->SetOutputType( p_dmo, 0, &dmo_type, 0 );

    p_vih = (VIDEOINFOHEADER *)dmo_type.pbFormat;
    p_enc->fmt_in.i_codec = VLC_FOURCC('I','4','2','0');

    DMOFreeMediaType( &dmo_type );
    if( i_err )
    {
        msg_Err( p_enc, "can't set DMO output type: %i", i_err );
        return VLC_EGENERIC;
    }

    msg_Dbg( p_enc, "successfully set output type" );
    return VLC_SUCCESS;
}

/*****************************************************************************
 * EncoderSetAudioType: configures the input and output types of the dmo
 *****************************************************************************/
static int EncoderSetAudioType( encoder_t *p_enc, IMediaObject *p_dmo )
{
    int i, i_selected, i_err;
    unsigned int i_last_byterate;
    uint16_t i_tag;
    DMO_MEDIA_TYPE dmo_type;
    WAVEFORMATEX *p_wf;

    /* Setup the format structure */
    fourcc_to_wf_tag( p_enc->fmt_out.i_codec, &i_tag );
    if( i_tag == 0 ) return VLC_EGENERIC;

    p_enc->fmt_in.i_codec = AOUT_FMT_S16_NE;
    p_enc->fmt_in.audio.i_bitspersample = 16;

    /* We first need to choose an output type from the predefined
     * list of choices (we cycle through the list to select the best match) */
    i = 0; i_selected = -1; i_last_byterate = 0;
    while( !p_dmo->vt->GetOutputType( p_dmo, 0, i++, &dmo_type ) )
    {
        p_wf = (WAVEFORMATEX *)dmo_type.pbFormat;
        msg_Dbg( p_enc, "available format :%i, sample rate: %i, channels: %i, "
                 "bits per sample: %i, bitrate: %i, blockalign: %i",
                 (int) p_wf->wFormatTag, (int)p_wf->nSamplesPerSec,
                 (int)p_wf->nChannels, (int)p_wf->wBitsPerSample,
                 (int)p_wf->nAvgBytesPerSec * 8, (int)p_wf->nBlockAlign );

        if( p_wf->wFormatTag == i_tag &&
            p_wf->nSamplesPerSec == p_enc->fmt_in.audio.i_rate &&
            p_wf->nChannels == p_enc->fmt_in.audio.i_channels &&
            p_wf->wBitsPerSample == p_enc->fmt_in.audio.i_bitspersample )
        {
            if( (int)p_wf->nAvgBytesPerSec <
                p_enc->fmt_out.i_bitrate * 110 / 800 /* + 10% */ &&
                p_wf->nAvgBytesPerSec > i_last_byterate )
            {
                i_selected = i - 1;
                i_last_byterate = p_wf->nAvgBytesPerSec;
                msg_Dbg( p_enc, "selected entry %i (bitrate: %i)",
                         i_selected, p_wf->nAvgBytesPerSec * 8 );
            }
        }

        DMOFreeMediaType( &dmo_type );
    }

    if( i_selected < 0 )
    {
        msg_Err( p_enc, "couldn't find a matching ouput" );
        return VLC_EGENERIC;
    }

    p_dmo->vt->GetOutputType( p_dmo, 0, i_selected, &dmo_type );
    p_wf = (WAVEFORMATEX *)dmo_type.pbFormat;

    msg_Dbg( p_enc, "selected format: %i, sample rate:%i, "
             "channels: %i, bits per sample: %i, bitrate: %i, blockalign: %i",
             (int)p_wf->wFormatTag, (int)p_wf->nSamplesPerSec,
             (int)p_wf->nChannels, (int)p_wf->wBitsPerSample,
             (int)p_wf->nAvgBytesPerSec * 8, (int)p_wf->nBlockAlign );

    p_enc->fmt_out.audio.i_rate = p_wf->nSamplesPerSec;
    p_enc->fmt_out.audio.i_channels = p_wf->nChannels;
    p_enc->fmt_out.audio.i_bitspersample = p_wf->wBitsPerSample;
    p_enc->fmt_out.audio.i_blockalign = p_wf->nBlockAlign;
    p_enc->fmt_out.i_bitrate = p_wf->nAvgBytesPerSec * 8;

    if( p_wf->cbSize )
    {
        msg_Dbg( p_enc, "found cbSize: %i", p_wf->cbSize );
        p_enc->fmt_out.i_extra = p_wf->cbSize;
        p_enc->fmt_out.p_extra = malloc( p_enc->fmt_out.i_extra );
        memcpy( p_enc->fmt_out.p_extra, &p_wf[1], p_enc->fmt_out.i_extra );
    }

    i_err = p_dmo->vt->SetOutputType( p_dmo, 0, &dmo_type, 0 );
    DMOFreeMediaType( &dmo_type );

    if( i_err )
    {
        msg_Err( p_enc, "can't set DMO output type: %i", i_err );
        return VLC_EGENERIC;
    }

    msg_Dbg( p_enc, "successfully set output type" );

    /* Setup the input type */
    i = 0; i_selected = -1;
    while( !p_dmo->vt->GetInputType( p_dmo, 0, i++, &dmo_type ) )
    {
        p_wf = (WAVEFORMATEX *)dmo_type.pbFormat;
        msg_Dbg( p_enc, "available format :%i, sample rate: %i, channels: %i, "
                 "bits per sample: %i, bitrate: %i, blockalign: %i",
                 (int) p_wf->wFormatTag, (int)p_wf->nSamplesPerSec,
                 (int)p_wf->nChannels, (int)p_wf->wBitsPerSample,
                 (int)p_wf->nAvgBytesPerSec * 8, (int)p_wf->nBlockAlign );

        if( p_wf->wFormatTag == WAVE_FORMAT_PCM &&
            p_wf->nSamplesPerSec == p_enc->fmt_in.audio.i_rate &&
            p_wf->nChannels == p_enc->fmt_in.audio.i_channels &&
            p_wf->wBitsPerSample == p_enc->fmt_in.audio.i_bitspersample )
        {
            i_selected = i - 1;
        }

        DMOFreeMediaType( &dmo_type );
    }

    if( i_selected < 0 )
    {
        msg_Err( p_enc, "couldn't find a matching input" );
        return VLC_EGENERIC;
    }

    p_dmo->vt->GetInputType( p_dmo, 0, i_selected, &dmo_type );
    i_err = p_dmo->vt->SetInputType( p_dmo, 0, &dmo_type, 0 );
    DMOFreeMediaType( &dmo_type );
    if( i_err )
    {
        msg_Err( p_enc, "can't set DMO input type: %x", i_err );
        return VLC_EGENERIC;
    }
    msg_Dbg( p_enc, "successfully set input type" );

    return VLC_SUCCESS;
}

/*****************************************************************************
 * EncOpen: open dmo codec
 *****************************************************************************/
static int EncOpen( vlc_object_t *p_this )
{
    encoder_t *p_enc = (encoder_t*)p_this;
    encoder_sys_t *p_sys = NULL;
    IMediaObject *p_dmo = NULL;
    HINSTANCE hmsdmo_dll = NULL;

#ifdef LOADER
    ldt_fs_t *ldt_fs = Setup_LDT_Keeper();
#else
    /* Initialize OLE/COM */
    CoInitialize( 0 );
#endif /* LOADER */

    if( LoadDMO( p_this, &hmsdmo_dll, &p_dmo, &p_enc->fmt_out, VLC_TRUE )
        != VLC_SUCCESS )
    {
        hmsdmo_dll = 0;
        p_dmo = 0;
        goto error;
    }

    if( p_enc->fmt_in.i_cat == VIDEO_ES )
    {
        if( EncoderSetVideoType( p_enc, p_dmo ) != VLC_SUCCESS ) goto error;
    }
    else
    {
        if( EncoderSetAudioType( p_enc, p_dmo ) != VLC_SUCCESS ) goto error;
    }

    /* Allocate the memory needed to store the decoder's structure */
    if( ( p_enc->p_sys = p_sys =
          (encoder_sys_t *)malloc(sizeof(encoder_sys_t)) ) == NULL )
    {
        msg_Err( p_enc, "out of memory" );
        goto error;
    }

    p_sys->hmsdmo_dll = hmsdmo_dll;
    p_sys->p_dmo = p_dmo;
#ifdef LOADER
    p_sys->ldt_fs = ldt_fs;
#endif

    /* Find out some properties of the inputput */
    {
        uint32_t i_size, i_align, dum;

        if( p_dmo->vt->GetInputSizeInfo( p_dmo, 0, &i_size, &i_align, &dum ) )
            msg_Err( p_enc, "GetInputSizeInfo() failed" );
        else
            msg_Dbg( p_enc, "GetInputSizeInfo(): bytes %i, align %i, %i",
                     i_size, i_align, dum );
    }

    /* Find out some properties of the output */
    {
        uint32_t i_size, i_align;

        p_sys->i_min_output = 0;
        if( p_dmo->vt->GetOutputSizeInfo( p_dmo, 0, &i_size, &i_align ) )
        {
            msg_Err( p_enc, "GetOutputSizeInfo() failed" );
            goto error;
        }
        else
        {
            msg_Dbg( p_enc, "GetOutputSizeInfo(): bytes %i, align %i",
                     i_size, i_align );
            p_sys->i_min_output = i_size;
        }
    }

    /* Set output properties */
    p_enc->fmt_out.i_cat = p_enc->fmt_out.i_cat;
    if( p_enc->fmt_out.i_cat == AUDIO_ES )
        date_Init( &p_sys->end_date, p_enc->fmt_out.audio.i_rate, 1 );
    else
        date_Init( &p_sys->end_date, 25 /* FIXME */, 1 );

    return VLC_SUCCESS;

 error:

    if( p_dmo ) p_dmo->vt->Release( (IUnknown *)p_dmo );
    if( hmsdmo_dll ) FreeLibrary( hmsdmo_dll );

#ifdef LOADER
    Restore_LDT_Keeper( ldt_fs );
#else
    /* Uninitialize OLE/COM */
    CoUninitialize();
#endif /* LOADER */

    if( p_sys ) free( p_sys );

    return VLC_EGENERIC;
}

/****************************************************************************
 * Encode: the whole thing
 ****************************************************************************/
static block_t *EncodeBlock( encoder_t *p_enc, void *p_data )
{
    encoder_sys_t *p_sys = p_enc->p_sys;
    CMediaBuffer *p_in;
    block_t *p_chain = NULL;
    block_t *p_block_in;
    uint32_t i_status;
    int i_result;
    mtime_t i_pts;

    if( p_sys == NULL )
    {
        if( EncOpen( VLC_OBJECT(p_enc) ) )
        {
            msg_Err( p_enc, "EncOpen failed" );
            return NULL;
        }
        p_sys = p_enc->p_sys;
    }

    if( !p_data ) return NULL;

    if( p_enc->fmt_out.i_cat == VIDEO_ES )
    {
        /* Get picture data */
        int i_plane, i_line, i_width, i_src_stride;
        picture_t *p_pic = (picture_t *)p_data;
        uint8_t *p_dst;

        int i_buffer = p_enc->fmt_in.video.i_width *
            p_enc->fmt_in.video.i_height *
            p_enc->fmt_in.video.i_bits_per_pixel / 8;

        p_block_in = block_New( p_enc, i_buffer );

        /* Copy picture stride by stride */
        p_dst = p_block_in->p_buffer;
        for( i_plane = 0; i_plane < p_pic->i_planes; i_plane++ )
        {
            uint8_t *p_src = p_pic->p[i_plane].p_pixels;
            i_width = p_pic->p[i_plane].i_visible_pitch;
            i_src_stride = p_pic->p[i_plane].i_pitch;

            for( i_line = 0; i_line < p_pic->p[i_plane].i_visible_lines;
                 i_line++ )
            {
                p_enc->p_vlc->pf_memcpy( p_dst, p_src, i_width );
                p_dst += i_width;
                p_src += i_src_stride;
            }
        }

        i_pts = p_pic->date;
    }
    else
    {
        aout_buffer_t *p_aout_buffer = (aout_buffer_t *)p_data;
        p_block_in = block_New( p_enc, p_aout_buffer->i_nb_bytes );
        memcpy( p_block_in->p_buffer, p_aout_buffer->p_buffer,
                p_block_in->i_buffer );

        i_pts = p_aout_buffer->start_date;
    }

    /* Feed input to the DMO */
    p_in = CMediaBufferCreate( p_block_in, p_block_in->i_buffer, VLC_TRUE );
    i_result = p_sys->p_dmo->vt->ProcessInput( p_sys->p_dmo, 0,
       (IMediaBuffer *)p_in, DMO_INPUT_DATA_BUFFERF_TIME, i_pts * 10, 0 );

    p_in->vt->Release( (IUnknown *)p_in );
    if( i_result == S_FALSE )
    {
        /* No output generated */
#ifdef DMO_DEBUG
        msg_Dbg( p_enc, "ProcessInput(): no output generated "I64Fd, i_pts );
#endif
        return NULL;
    }
    else if( i_result == (int)DMO_E_NOTACCEPTING )
    {
        /* Need to call ProcessOutput */
        msg_Dbg( p_enc, "ProcessInput(): not accepting" );
    }
    else if( i_result != S_OK )
    {
        msg_Dbg( p_enc, "ProcessInput(): failed: %x", i_result );
        return NULL;
    }

#if DMO_DEBUG
    msg_Dbg( p_enc, "ProcessInput(): success" );
#endif

    /* Get output from the DMO */
    while( 1 )
    {
        DMO_OUTPUT_DATA_BUFFER db;
        block_t *p_block_out;
        CMediaBuffer *p_out;

        p_block_out = block_New( p_enc, p_sys->i_min_output );
        p_block_out->i_buffer = 0;
        p_out = CMediaBufferCreate(p_block_out, p_sys->i_min_output, VLC_FALSE);
        memset( &db, 0, sizeof(db) );
        db.pBuffer = (IMediaBuffer *)p_out;

        i_result = p_sys->p_dmo->vt->ProcessOutput( p_sys->p_dmo,
                                                    0, 1, &db, &i_status );

        if( i_result != S_OK )
        {
            if( i_result != S_FALSE )
                msg_Dbg( p_enc, "ProcessOutput(): failed: %x", i_result );
#if DMO_DEBUG
            else
                msg_Dbg( p_enc, "ProcessOutput(): no output" );
#endif

            p_out->vt->Release( (IUnknown *)p_out );
            block_Release( p_block_out );
            return p_chain;
        }

        if( !p_block_out->i_buffer )
        {
#if DMO_DEBUG
            msg_Dbg( p_enc, "ProcessOutput(): no output (i_buffer_out == 0)" );
#endif
            p_out->vt->Release( (IUnknown *)p_out );
            block_Release( p_block_out );
            return p_chain;
        }

        if( db.dwStatus & DMO_OUTPUT_DATA_BUFFERF_TIME )
        {
#if DMO_DEBUG
            msg_Dbg( p_enc, "ProcessOutput(): pts: "I64Fd", "I64Fd,
                     i_pts, db.rtTimestamp / 10 );
#endif
            i_pts = db.rtTimestamp / 10;
        }

        if( db.dwStatus & DMO_OUTPUT_DATA_BUFFERF_TIMELENGTH )
        {
            p_block_out->i_length = db.rtTimelength / 10;
#if DMO_DEBUG
            msg_Dbg( p_enc, "ProcessOutput(): length: "I64Fd,
                     p_block_out->i_length );
#endif
        }

        if( p_enc->fmt_out.i_cat == VIDEO_ES )
        {
            if( db.dwStatus & DMO_OUTPUT_DATA_BUFFERF_SYNCPOINT )
                p_block_out->i_flags |= BLOCK_FLAG_TYPE_I;
            else
                p_block_out->i_flags |= BLOCK_FLAG_TYPE_P;
        }

        p_block_out->i_dts = p_block_out->i_pts = i_pts;
        block_ChainAppend( &p_chain, p_block_out );
    }
}

/*****************************************************************************
 * EncoderClose: close codec
 *****************************************************************************/
void EncoderClose( vlc_object_t *p_this )
{
    encoder_t *p_enc = (encoder_t*)p_this;
    encoder_sys_t *p_sys = p_enc->p_sys;

    if( !p_sys ) return;

    if( p_sys->p_dmo ) p_sys->p_dmo->vt->Release( (IUnknown *)p_sys->p_dmo );
    FreeLibrary( p_sys->hmsdmo_dll );

#ifdef LOADER
#if 0
    Restore_LDT_Keeper( p_sys->ldt_fs );
#endif
#else
    /* Uninitialize OLE/COM */
    CoUninitialize();
#endif

    free( p_sys );
}
