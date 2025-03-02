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

#define UNICODE
#define AYMERIC_TEST
#define _CE_ALLOW_SINGLE_THREADED_OBJECTS_IN_MTA

#include "mediastreamer2/msvideo.h"
#include "mediastreamer2/msticker.h"
#include "mediastreamer2/msv4l.h"

#include "nowebcam.h"
#include <ffmpeg/avcodec.h>

#include <dshow.h>
#include <dmodshow.h>
#include <dmoreg.h>

#include <streams.h>
#include <initguid.h>
#include "dxfilter.h"
#include <qedit.h>
#include <atlbase.h>
#include <atlcom.h>

HRESULT AddGraphToRot(IUnknown *pUnkGraph, DWORD *pdwRegister);
void RemoveGraphFromRot(DWORD pdwRegister);

typedef struct V4wState{

	char dev[512];
	int devidx;

	CComPtr<IGraphBuilder> m_pGraph;
	CComPtr<ICaptureGraphBuilder2> m_pBuilder;
	CComPtr<IMediaControl> m_pControl;
	CDXFilter *m_pDXFilter;
	CComPtr<IBaseFilter> m_pIDXFilter;	
	CComPtr<IBaseFilter> m_pNullRenderer;
	CComPtr<IBaseFilter> m_pDeviceFilter;
	DWORD rotregvalue;

	MSVideoSize vsize;
	int pix_fmt;
	mblk_t *mire[10];
	queue_t rq;
	ms_mutex_t mutex;
	int frame_ind;
	int frame_max;
	float fps;
	float start_time;
	int frame_count;
	bool_t running;
}V4wState;

static V4wState *s_callback=NULL;

static void dummy(void*p){
}

HRESULT ( Callback)(IMediaSample* pSample, REFERENCE_TIME* sTime, REFERENCE_TIME* eTime, BOOL changed)
{
	BYTE *byte_buf=NULL;
	mblk_t *buf;

	V4wState *s = s_callback;
	if (s==NULL)
		return S_OK;

	HRESULT hr = pSample->GetPointer(&byte_buf);
	if (FAILED(hr))
	{
		return S_OK;
	}

	int size = pSample->GetActualDataLength();
	if (size>+1000)
	{
		buf=allocb(size,0);
		memcpy(buf->b_wptr, byte_buf, size);
		if (s->pix_fmt==MS_RGB24)
		{
			/* Conversion from top down bottom up (BGR to RGB and flip) */
 			unsigned long Index,nPixels;
			unsigned char *blue;
			unsigned char tmp;
			short iPixelSize;

			blue=buf->b_wptr;

			nPixels=s->vsize.width*s->vsize.height;
			iPixelSize=24/8;
		 
			for(Index=0;Index!=nPixels;Index++)  // For each pixel
			{
				tmp=*blue;
				*blue=*(blue+2);
				*(blue+2)=tmp;
				blue+=iPixelSize;
			}
 
			unsigned char *pLine1, *pLine2;
			int iLineLen,iIndex;

			iLineLen=s->vsize.width*iPixelSize;
			pLine1=buf->b_wptr;
			pLine2=&(buf->b_wptr)[iLineLen * (s->vsize.height - 1)];

			for( ;pLine1<pLine2;pLine2-=(iLineLen*2))
			{
				for(iIndex=0;iIndex!=iLineLen;pLine1++,pLine2++,iIndex++)
				{
					tmp=*pLine1;
					*pLine1=*pLine2;
					*pLine2=tmp;       
				}
			}
		}
		buf->b_wptr+=size;  
		
		ms_mutex_lock(&s->mutex);
		putq(&s->rq, buf);
		ms_mutex_unlock(&s->mutex);

	}
	return S_OK;
}

int try_format(V4wState *s, int format)
{
    HRESULT hr=S_OK;
    IEnumPins *pEnum=0;
    ULONG ulFound;
    IPin *pPin;

	GUID guid_format;
	DWORD biCompression;
	DWORD biBitCount;

	// Verify input
    if (!s->m_pDeviceFilter)
        return -1;

	if (format == MS_YUV420P)
		guid_format = (GUID)FOURCCMap(MAKEFOURCC('I','4','2','0'));
	else if (format == MS_YUYV)
		guid_format = MEDIASUBTYPE_YUYV;
	else if (format == MS_UYVY)
		guid_format = MEDIASUBTYPE_UYVY;
	else if (format == MS_RGB24)
		guid_format = MEDIASUBTYPE_RGB24;
	else if (format == MS_YUY2)
		guid_format = MEDIASUBTYPE_YUY2;

	if (format == MS_YUV420P)
		biCompression = MAKEFOURCC('I','4','2','0');
	else if (format == MS_YUYV)
		biCompression = MAKEFOURCC('Y','U','Y','V');
	else if (format == MS_UYVY)
		biCompression = MAKEFOURCC('U','Y','V','Y');
	else if (format == MS_RGB24)
		biCompression = BI_RGB;
	else if (format == MS_YUY2)
		biCompression = MAKEFOURCC('Y','U','Y','2');
	
	if (format == MS_YUV420P)
		biBitCount = 12;
	else if (format == MS_YUYV)
		biBitCount = 16;
	else if (format == MS_UYVY)
		biBitCount = 16;
	else if (format == MS_RGB24)
		biBitCount = 24;
	else if (format == MS_YUY2)
		biBitCount = 16;
	
    // Get pin enumerator
	hr = s->m_pDeviceFilter->EnumPins(&pEnum);
    if(FAILED(hr)) 
        return -1;

    pEnum->Reset();

    // Count every pin on the filter
    while(S_OK == pEnum->Next(1, &pPin, &ulFound))
    {
        PIN_DIRECTION pindir = (PIN_DIRECTION) 3;

        hr = pPin->QueryDirection(&pindir);

        if(pindir != PINDIR_INPUT)
		{
			IEnumMediaTypes *ppEnum;
		    ULONG ulFound2;
			hr = pPin->EnumMediaTypes(&ppEnum);
			if(FAILED(hr)) 
				continue;

			AM_MEDIA_TYPE *ppMediaTypes;
			while(S_OK == ppEnum->Next(1, &ppMediaTypes, &ulFound2))
			{
				if (ppMediaTypes->formattype != FORMAT_VideoInfo)
					continue;
				if (ppMediaTypes->majortype != MEDIATYPE_Video)
					continue;
				if (ppMediaTypes->subtype != guid_format)
					continue;
				VIDEOINFO *pvi = (VIDEOINFO *)ppMediaTypes->pbFormat;
				if (pvi->bmiHeader.biCompression!=biCompression)
					continue;
				if (pvi->bmiHeader.biBitCount!=biBitCount)
					continue;

		        pPin->Release();
			    pEnum->Release();
				return 0;
			}
		}

        pPin->Release();
    }

    pEnum->Release();
    return -1;
}

int try_format_size(V4wState *s, int format, int width, int height)
{
    HRESULT hr=S_OK;
    IEnumPins *pEnum=0;
    ULONG ulFound;
    IPin *pPin;

	GUID guid_format;
	DWORD biCompression;
	DWORD biBitCount;

	// Verify input
    if (!s->m_pDeviceFilter)
        return -1;

	if (format == MS_YUV420P)
		guid_format = (GUID)FOURCCMap(MAKEFOURCC('I','4','2','0'));
	else if (format == MS_YUYV)
		guid_format = MEDIASUBTYPE_YUYV;
	else if (format == MS_UYVY)
		guid_format = MEDIASUBTYPE_UYVY;
	else if (format == MS_RGB24)
		guid_format = MEDIASUBTYPE_RGB24;
	else if (format == MS_YUY2)
		guid_format = MEDIASUBTYPE_YUY2;

	if (format == MS_YUV420P)
		biCompression = MAKEFOURCC('I','4','2','0');
	else if (format == MS_YUYV)
		biCompression = MAKEFOURCC('Y','U','Y','V');
	else if (format == MS_UYVY)
		biCompression = MAKEFOURCC('U','Y','V','Y');
	else if (format == MS_RGB24)
		biCompression = BI_RGB;
	else if (format == MS_YUY2)
		biCompression = MAKEFOURCC('Y','U','Y','2');
	
	if (format == MS_YUV420P)
		biBitCount = 12;
	else if (format == MS_YUYV)
		biBitCount = 16;
	else if (format == MS_UYVY)
		biBitCount = 16;
	else if (format == MS_RGB24)
		biBitCount = 24;
	else if (format == MS_YUY2)
		biBitCount = 16;

    // Get pin enumerator
	hr = s->m_pDeviceFilter->EnumPins(&pEnum);
    if(FAILED(hr)) 
        return -1;

    pEnum->Reset();

    // Count every pin on the filter
    while(S_OK == pEnum->Next(1, &pPin, &ulFound))
    {
        PIN_DIRECTION pindir = (PIN_DIRECTION) 3;

        hr = pPin->QueryDirection(&pindir);

        if(pindir != PINDIR_INPUT)
		{
			IEnumMediaTypes *ppEnum;
		    ULONG ulFound2;
			hr = pPin->EnumMediaTypes(&ppEnum);
			if(FAILED(hr)) 
				continue;

			AM_MEDIA_TYPE *ppMediaTypes;
			while(S_OK == ppEnum->Next(1, &ppMediaTypes, &ulFound2))
			{
				if (ppMediaTypes->formattype != FORMAT_VideoInfo)
					continue;
				if (ppMediaTypes->majortype != MEDIATYPE_Video)
					continue;
				if (ppMediaTypes->subtype != guid_format)
					continue;
				VIDEOINFO *pvi = (VIDEOINFO *)ppMediaTypes->pbFormat;
				if (pvi->bmiHeader.biCompression!=biCompression)
					continue;
				if (pvi->bmiHeader.biBitCount!=biBitCount)
					continue;
				if (pvi->bmiHeader.biHeight!=height)
					continue;
				if (pvi->bmiHeader.biWidth!=width)
					continue;

				s->vsize.width = width;
				s->vsize.height = height;

				pPin->Release();
			    pEnum->Release();
				return 0;
			}
		}

        pPin->Release();
    } 

    pEnum->Release();
    return -1;
}

static int v4w_open_videodevice(V4wState *s)
{
	// Initialize COM
	CoInitialize(NULL);

	// get a Graph
	HRESULT hr=s->m_pGraph.CoCreateInstance(CLSID_FilterGraph);
	if(FAILED(hr))
	{
		return -1;
	}

	// get a CaptureGraphBuilder2
	hr=s->m_pBuilder.CoCreateInstance(CLSID_CaptureGraphBuilder2);
	if(FAILED(hr))
	{
		return -2;
	}

	// connect capture graph builder with the graph
	s->m_pBuilder->SetFiltergraph(s->m_pGraph);

	// get mediacontrol so we can start and stop the filter graph
	hr=s->m_pGraph.QueryInterface(&(s->m_pControl));
	if(FAILED(hr))
	{
		return -3;
	}


#ifdef _DEBUG
	HANDLE m_hLogFile=CreateFile(L"DShowGraphLog.txt",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,NULL,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
	if(m_hLogFile!=INVALID_HANDLE_VALUE)
	{
		hr=s->m_pGraph->SetLogFile((DWORD_PTR)m_hLogFile);
		/* ASSERT(SUCCEEDED(hr)); */
	}
	
	//AddGraphToRot(s->m_pGraph, &s->rotregvalue);
#endif

	ICreateDevEnum *pCreateDevEnum = NULL;
	IEnumMoniker *pEnumMoniker = NULL;
	IMoniker *pMoniker = NULL;

	ULONG nFetched = 0;

	hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER, 
		IID_ICreateDevEnum, (PVOID *)&pCreateDevEnum);
	if(FAILED(hr))
	{
		return -4;
	}

	hr = pCreateDevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory,
		&pEnumMoniker, 0);
	if (FAILED(hr) || pEnumMoniker == NULL) {
		//printf("no device\n");
		return -5;
	}

	pEnumMoniker->Reset();

	int pos=0;
    while(S_OK == pEnumMoniker->Next(1, &pMoniker, &nFetched) )
    {
		if (pos>=s->devidx)
			break;
		pos++;
		pMoniker->Release();
		pMoniker=NULL;
	}
	if(pMoniker==NULL)
	{
		return -6;
	}

	hr = pMoniker->BindToObject(0, 0, IID_IBaseFilter, (void**)&s->m_pDeviceFilter );
	if(FAILED(hr))
	{
		return -7;
	}

	s->m_pGraph->AddFilter(s->m_pDeviceFilter, L"Device Filter");

	pMoniker->Release();
	pEnumMoniker->Release();
	pCreateDevEnum->Release();

	if (try_format(s, s->pix_fmt)==0)
		s->pix_fmt = s->pix_fmt;
	else if (try_format(s,MS_YUV420P)==0)
		s->pix_fmt = MS_YUV420P;
	else if (try_format(s,MS_YUY2)==0)
		s->pix_fmt = MS_YUY2;
	else if (try_format(s,MS_YUYV)==0)
		s->pix_fmt = MS_YUYV;
	else if (try_format(s,MS_UYVY)==0)
		s->pix_fmt = MS_UYVY;
	else if (try_format(s,MS_RGB24)==0)
		s->pix_fmt = MS_RGB24;
	else
	{
		ms_error("Unsupported video pixel format.");
		return -8;
	}
	
	if (s->pix_fmt == MS_YUV420P)
		ms_message("Driver supports YUV420P, using that format.");
	else if (s->pix_fmt == MS_YUY2)
		ms_message("Driver supports YUY2 (UYVY), using that format.");
	else if (s->pix_fmt == MS_YUYV)
		ms_message("Driver supports YUV422, using that format.");
	else if (s->pix_fmt == MS_UYVY)
		ms_message("Driver supports UYVY, using that format.");
	else if (s->pix_fmt == MS_RGB24)
		ms_message("Driver supports RGB24, using that format.");

	if (try_format_size(s, s->pix_fmt, s->vsize.width, s->vsize.height)==0)
		ms_message("Selected Size: %ix%i.", s->vsize.width, s->vsize.height);
	else if (try_format_size(s, s->pix_fmt, MS_VIDEO_SIZE_QCIF_W, MS_VIDEO_SIZE_QCIF_H)==0)
		ms_message("Selected Size: %ix%i.", MS_VIDEO_SIZE_QCIF_W, MS_VIDEO_SIZE_QCIF_H);
	else if (try_format_size(s, s->pix_fmt, MS_VIDEO_SIZE_CIF_W, MS_VIDEO_SIZE_CIF_H)==0)
		ms_message("Selected Size: %ix%i.", MS_VIDEO_SIZE_CIF_W, MS_VIDEO_SIZE_CIF_H);
	else if (try_format_size(s, s->pix_fmt, MS_VIDEO_SIZE_4CIF_W, MS_VIDEO_SIZE_4CIF_H)==0)
		ms_message("Selected Size: %ix%i.", MS_VIDEO_SIZE_4CIF_W, MS_VIDEO_SIZE_4CIF_H);
	else if (try_format_size(s, s->pix_fmt, MS_VIDEO_SIZE_VGA_W, MS_VIDEO_SIZE_VGA_H)==0)
		ms_message("Selected Size: %ix%i.", MS_VIDEO_SIZE_VGA_W, MS_VIDEO_SIZE_VGA_H);
	else if (try_format_size(s, s->pix_fmt, MS_VIDEO_SIZE_QVGA_W, MS_VIDEO_SIZE_QVGA_H)==0)
		ms_message("Selected Size: %ix%i.", MS_VIDEO_SIZE_QVGA_W, MS_VIDEO_SIZE_QVGA_H);
	else
	{
		ms_error("No supported size found for format.");
		/* size not supported? */
		return -9;
	}

	// get DXFilter
	s->m_pDXFilter = new CDXFilter(NULL, &hr, FALSE);
	if(s->m_pDXFilter==NULL)
	{
		return -10;
	}
	s->m_pDXFilter->AddRef();

	CMediaType mt;
	mt.SetType(&MEDIATYPE_Video);

	GUID m = MEDIASUBTYPE_RGB24;
	if (s->pix_fmt == MS_YUV420P)
		m = (GUID)FOURCCMap(MAKEFOURCC('I','4','2','0'));
	else if (s->pix_fmt == MS_YUY2)
		m = MEDIASUBTYPE_YUY2;
	else if (s->pix_fmt == MS_YUYV)
		m = MEDIASUBTYPE_YUYV;
	else if (s->pix_fmt == MS_UYVY)
		m = MEDIASUBTYPE_UYVY;
	else if (s->pix_fmt == MS_RGB24)
		m = MEDIASUBTYPE_RGB24;
	mt.SetSubtype(&m);
	
	mt.formattype = FORMAT_VideoInfo;
	mt.SetTemporalCompression(FALSE);

	VIDEOINFO *pvi = (VIDEOINFO *)
	mt.AllocFormatBuffer(sizeof(VIDEOINFO));
	if (NULL == pvi)
		return -11;
	ZeroMemory(pvi, sizeof(VIDEOINFO));

	if (s->pix_fmt == MS_YUV420P)
		pvi->bmiHeader.biCompression = MAKEFOURCC('I','4','2','0');
	else if (s->pix_fmt == MS_YUY2)
		pvi->bmiHeader.biCompression = MAKEFOURCC('Y','U','Y','2');
	else if (s->pix_fmt == MS_YUYV)
		pvi->bmiHeader.biCompression = MAKEFOURCC('Y','U','Y','V');
	else if (s->pix_fmt == MS_UYVY)
		pvi->bmiHeader.biCompression = MAKEFOURCC('U','Y','V','Y');
	else if (s->pix_fmt == MS_RGB24)
		pvi->bmiHeader.biCompression = BI_RGB;
	
	if (s->pix_fmt == MS_YUV420P)
		pvi->bmiHeader.biBitCount = 12;
	else if (s->pix_fmt == MS_YUY2)
		pvi->bmiHeader.biBitCount = 16;
	else if (s->pix_fmt == MS_YUYV)
		pvi->bmiHeader.biBitCount = 16;
	else if (s->pix_fmt == MS_UYVY)
		pvi->bmiHeader.biBitCount = 16;
	else if (s->pix_fmt == MS_RGB24)
		pvi->bmiHeader.biBitCount = 24;

	pvi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	pvi->bmiHeader.biWidth = s->vsize.width;
	pvi->bmiHeader.biHeight = s->vsize.height;
	pvi->bmiHeader.biPlanes = 1;
	pvi->bmiHeader.biSizeImage = GetBitmapSize(&pvi->bmiHeader);
	pvi->bmiHeader.biClrImportant = 0;
	mt.SetSampleSize(pvi->bmiHeader.biSizeImage);
	mt.SetFormat((BYTE*)pvi, sizeof(VIDEOINFO));

	hr = s->m_pDXFilter->SetAcceptedMediaType(&mt);
	if(FAILED(hr))
	{
		return -12;
	}

	hr = s->m_pDXFilter->SetCallback(Callback); 
	if(FAILED(hr))
	{
		return -13;
	}

	hr = s->m_pDXFilter->QueryInterface(IID_IBaseFilter,
	 (LPVOID *)&s->m_pIDXFilter);
	if(FAILED(hr))
	{
		return -14;
	}

	hr = s->m_pGraph->AddFilter(s->m_pIDXFilter, L"DXFilter Filter");
	if(FAILED(hr))
	{
		return -15;
	}


	// get null renderer
	hr=s->m_pNullRenderer.CoCreateInstance(CLSID_NullRenderer);
	if(FAILED(hr))
	{
		return -16;
	}
	if (s->m_pNullRenderer!=NULL)
	{
		s->m_pGraph->AddFilter(s->m_pNullRenderer, L"Null Renderer");
	}

	hr = s->m_pBuilder->RenderStream(&PIN_CATEGORY_PREVIEW,
		&MEDIATYPE_Video, s->m_pDeviceFilter, s->m_pIDXFilter, s->m_pNullRenderer);
	if (FAILED(hr))
	{
		//hr = s->m_pBuilder->RenderStream(&PIN_CATEGORY_CAPTURE,
		//	&MEDIATYPE_Video, s->m_pDeviceFilter, s->m_pIDXFilter, s->m_pNullRenderer);
		if (FAILED(hr))
		{
			return -17;
		}
	}
	
	//m_pDXFilter->SetBufferSamples(TRUE);

	s_callback = s;
	hr = s->m_pControl->Run();
	if(FAILED(hr))
	{
		return -18;
	}

	s->rotregvalue=1;
	return 0;
}

/****************************************************************************

FUNCTION: AddGraphToRot.

DESCRIPTION:
     Adds a DirectShow filter graph to the Running Object Table,
     allowing GraphEdit to "spy" on a remote filter graph.

PARAMETERS:

RETURNS:
  . 0 for success, otherwise an error #.
    . Standard COM/HRESULT error numbers are returned if a COM/HRESULT error
        was encountered.

****************************************************************************/
HRESULT AddGraphToRot(IUnknown *pUnkGraph, DWORD *pdwRegister)
{
  IMoniker * pMoniker;
  IRunningObjectTable *pROT;
  WCHAR wsz[128];
  HRESULT hr;

  if (FAILED(GetRunningObjectTable(0, &pROT)))
    return E_FAIL;

  wsprintfW(wsz, L"FilterGraph %08x pid %08x", (DWORD_PTR)pUnkGraph, GetCurrentProcessId());
  wsprintfW(wsz, L"FilterGraph %08x pid %08x", (DWORD_PTR)pUnkGraph, GetCurrentProcessId());

  hr = CreateItemMoniker(L"!", wsz, &pMoniker);
  if (SUCCEEDED(hr))
  {
    hr = pROT->Register(0, pUnkGraph, pMoniker, pdwRegister);
    pMoniker->Release();
  }

  pROT->Release();
  return hr;
}


/****************************************************************************

FUNCTION: RemoveGraphFromRot.

DESCRIPTION:
     Removes a filter graph from the Running Object Table.

PARAMETERS:

****************************************************************************/
void RemoveGraphFromRot(DWORD pdwRegister)
{
  IRunningObjectTable *pROT;

  if (SUCCEEDED(GetRunningObjectTable(0, &pROT)))
  {
    pROT->Revoke(pdwRegister);
    pROT->Release();
  }
}

static void v4w_init(MSFilter *f){
	V4wState *s=(V4wState *)ms_new0(V4wState,1);
	int idx;
	s->devidx=0;
	s->vsize.width=MS_VIDEO_SIZE_CIF_W;
	s->vsize.height=MS_VIDEO_SIZE_CIF_H;
	//s->pix_fmt=MS_RGB24;
	s->pix_fmt=MS_YUV420P;

	s->rotregvalue = 0;
	s->m_pGraph=NULL;
	s->m_pBuilder=NULL;
	s->m_pControl=NULL;
	s->m_pDXFilter=NULL;
	s->m_pIDXFilter=NULL;
	s->m_pDeviceFilter=NULL;

	qinit(&s->rq);
	for (idx=0;idx<10;idx++)
	{
		s->mire[idx]=NULL;
	}
	ms_mutex_init(&s->mutex,NULL);
	s->start_time=0;
	s->frame_count=-1;
	s->fps=15;

	f->data=s;
}


static int _v4w_start(V4wState *s, void *arg)
{
	int i;
	s->frame_count=-1;

	i = v4w_open_videodevice(s);

	if (s->rotregvalue==0){
		//RemoveGraphFromRot(s->rotregvalue);		
		if (s->m_pNullRenderer!=NULL)
			s->m_pGraph->RemoveFilter(s->m_pNullRenderer);
		if (s->m_pIDXFilter!=NULL)
			s->m_pGraph->RemoveFilter(s->m_pIDXFilter);
		if (s->m_pDeviceFilter!=NULL)
			s->m_pGraph->RemoveFilter(s->m_pDeviceFilter);
		s->m_pBuilder=NULL;
		s->m_pControl=NULL;
		s->m_pIDXFilter=NULL;
		if (s->m_pDXFilter!=NULL)
			s->m_pDXFilter->Release();
		s->m_pDXFilter=NULL;
		s->m_pGraph=NULL;
		s->m_pNullRenderer=NULL;
		s->m_pDeviceFilter=NULL;
		CoUninitialize();
		s_callback = NULL;
		flushq(&s->rq,0);
		ms_message("v4w: graph not started (err=%i)", i);
		s->rotregvalue=0;
		s->pix_fmt = MS_YUV420P;
	}
	return i;
}

static int _v4w_stop(V4wState *s, void *arg){
	s->frame_count=-1;
	if (s->rotregvalue>0){
		HRESULT hr = s->m_pControl->Stop();
		if(FAILED(hr))
		{
			ms_message("v4w: could not stop graph");
		}
		if (s->m_pNullRenderer!=NULL)
			s->m_pGraph->RemoveFilter(s->m_pNullRenderer);
		if (s->m_pIDXFilter!=NULL)
			s->m_pGraph->RemoveFilter(s->m_pIDXFilter);
		if (s->m_pDeviceFilter!=NULL)
			s->m_pGraph->RemoveFilter(s->m_pDeviceFilter);
		//RemoveGraphFromRot(s->rotregvalue);
		s->m_pBuilder=NULL;
		s->m_pControl=NULL;
		s->m_pIDXFilter=NULL;
		if (s->m_pDXFilter!=NULL)
			s->m_pDXFilter->Release();
		s->m_pDXFilter=NULL;
		s->m_pGraph=NULL;
		s->m_pNullRenderer=NULL;
		s->m_pDeviceFilter=NULL;
		CoUninitialize();
		s_callback = NULL;
		flushq(&s->rq,0);
		ms_message("v4w: graph destroyed");
		s->rotregvalue=0;
	}
	return 0;
}


static int v4w_start(MSFilter *f, void *arg){
	V4wState *s=(V4wState*)f->data;
	_v4w_start(s, NULL);
	return 0;
}

static int v4w_stop(MSFilter *f, void *arg){
	V4wState *s=(V4wState*)f->data;
	_v4w_stop(s, NULL);
	return 0;
}


static void v4w_uninit(MSFilter *f){
	V4wState *s=(V4wState*)f->data;
	int idx;
	flushq(&s->rq,0);
	ms_mutex_destroy(&s->mutex);
	for (idx=0;idx<10;idx++)
	{
		if (s->mire[idx]==NULL)
			break;
		freemsg(s->mire[idx]);
	}
	if (s->rotregvalue>0){
		HRESULT hr = s->m_pControl->Stop();
		if(FAILED(hr))
		{
			ms_message("v4w: could not stop graph");
		}
		if (s->m_pNullRenderer!=NULL)
			s->m_pGraph->RemoveFilter(s->m_pNullRenderer);
		if (s->m_pIDXFilter!=NULL)
			s->m_pGraph->RemoveFilter(s->m_pIDXFilter);
		if (s->m_pDeviceFilter!=NULL)
			s->m_pGraph->RemoveFilter(s->m_pDeviceFilter);
		//RemoveGraphFromRot(s->rotregvalue);
		s->m_pBuilder=NULL;
		s->m_pControl=NULL;
		s->m_pIDXFilter=NULL;
		if (s->m_pDXFilter!=NULL)
			s->m_pDXFilter->Release();
		s->m_pDXFilter=NULL;
		s->m_pGraph=NULL;
		s->m_pNullRenderer=NULL;
		s->m_pDeviceFilter=NULL;
		CoUninitialize();
		s_callback = NULL;
		flushq(&s->rq,0);
		ms_message("v4w: graph destroyed");
		s->rotregvalue=0;
	}
	ms_free(s);
}

static mblk_t * v4w_make_nowebcam(V4wState *s){
#if defined(_WIN32_WCE)
	return NULL;
#else
	int idx;
	int count;
	if (s->mire[0]==NULL && s->frame_ind==0){
		/* load several images to fake a movie */
		for (idx=0;idx<10;idx++)
		{
			s->mire[idx]=ms_load_nowebcam(&s->vsize, idx);
			if (s->mire[idx]==NULL)
				break;
		}
		if (idx==0)
			s->mire[0]=ms_load_nowebcam(&s->vsize, -1);
	}
	for (count=0;count<10;count++)
	{
		if (s->mire[count]==NULL)
			break;
	}

	s->frame_ind++;
	if (count==0)
		return NULL;

	idx = s->frame_ind%count;
	if (s->mire[idx]!=NULL)
		return s->mire[idx];
	return s->mire[0];
#endif
}

static void v4w_preprocess(MSFilter * obj){
	V4wState *s=(V4wState*)obj->data;
	s->running=TRUE;
	if (s->rotregvalue==0)
		s->fps=1;
}

static void v4w_postprocess(MSFilter * obj){
	V4wState *s=(V4wState*)obj->data;
	s->running=FALSE;
}

static void v4w_process(MSFilter * obj){
	V4wState *s=(V4wState*)obj->data;
	mblk_t *m;
	uint32_t timestamp;
	int cur_frame;

	if (s->frame_count==-1){
		s->start_time=obj->ticker->time;
		s->frame_count=0;
	}

	cur_frame=((obj->ticker->time-s->start_time)*s->fps/1000.0);
	if (cur_frame>s->frame_count){
		mblk_t *om=NULL;
		ms_mutex_lock(&s->mutex);
		/*keep the most recent frame if several frames have been captured */
		if (s->rotregvalue!=0){
			while((m=getq(&s->rq))!=NULL){
				if (om!=NULL) freemsg(om);
				om=m;
			}
		}else {
			mblk_t *nowebcam = v4w_make_nowebcam(s);
			if (nowebcam!=NULL)
				om=dupmsg(nowebcam);
		}
		ms_mutex_unlock(&s->mutex);
		if (om!=NULL){
			timestamp=obj->ticker->time*90;/* rtp uses a 90000 Hz clockrate for video*/
			mblk_set_timestamp_info(om,timestamp);
			ms_queue_put(obj->outputs[0],om);
			/*ms_message("picture sent");*/
		}
		s->frame_count++;
	}
}



static int v4w_set_fps(MSFilter *f, void *arg){
	V4wState *s=(V4wState*)f->data;
	s->fps=*((float*)arg);
	return 0;
}

static int v4w_get_pix_fmt(MSFilter *f,void *arg){
	V4wState *s=(V4wState*)f->data;
	*((MSPixFmt*)arg) = (MSPixFmt)s->pix_fmt;
	return 0;
}

static int v4w_set_vsize(MSFilter *f, void *arg){
	V4wState *s=(V4wState*)f->data;
	s->vsize=*((MSVideoSize*)arg);
	return 0;
}

static int v4w_get_vsize(MSFilter *f, void *arg){
	V4wState *s=(V4wState*)f->data;
	MSVideoSize *vs=(MSVideoSize*)arg;
	vs->width=s->vsize.width;
	vs->height=s->vsize.height;
	return 0;
}

static int v4w_set_device(MSFilter *f, void *arg){
	V4wState *s=(V4wState*)f->data;
	s->devidx=*((int*)arg);
	return 0;
}

static MSFilterMethod methods[]={
	{	MS_FILTER_SET_FPS	,	v4w_set_fps	},
	{	MS_FILTER_GET_PIX_FMT	,	v4w_get_pix_fmt	},
	{	MS_FILTER_SET_VIDEO_SIZE, v4w_set_vsize	},
	{	MS_FILTER_GET_VIDEO_SIZE, v4w_get_vsize	},
	{	MS_V4L_START			,	v4w_start		},
	{	MS_V4L_STOP			,	v4w_stop		},
	{	MS_V4L_SET_DEVICE	,	v4w_set_device		},
	{	0								,	NULL			}
};

#ifdef _MSC_VER

MSFilterDesc ms_v4w_desc={
	MS_V4L_ID,
	"MSV4w",
	"A video4windows compatible source filter to stream pictures.",
	MS_FILTER_OTHER,
	NULL,
	0,
	1,
	v4w_init,
	v4w_preprocess,
	v4w_process,
	v4w_postprocess,
	v4w_uninit,
	methods
};

#else

MSFilterDesc ms_v4w_desc={
	.id=MS_V4L_ID,
	.name="MSV4w",
	.text="A video4windows compatible source filter to stream pictures.",
	.ninputs=0,
	.noutputs=1,
	.category=MS_FILTER_OTHER,
	.init=v4w_init,
	.preprocess=v4w_preprocess,
	.process=v4w_process,
	.postprocess=v4w_postprocess,
	.uninit=v4w_uninit,
	.methods=methods
};

#endif

MS_FILTER_DESC_EXPORT(ms_v4w_desc)
