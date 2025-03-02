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

#include "mediastreamer2/msfilter.h"
#include "mediastreamer2/msvideo.h"
#include "mediastreamer2/msvideoout.h"

#include <ffmpeg/avcodec.h>
#include <ffmpeg/swscale.h>
#include <ffmpeg/avutil.h>

static int video_out_handle_resize(MSFilter *f, void *arg);

bool_t ms_display_poll_event(MSDisplay *d, MSDisplayEvent *ev){
	if (d->desc->pollevent)
		return d->desc->pollevent(d,ev);
	else return FALSE;
}

#ifndef WIN32

#include <SDL/SDL.h>
#include <SDL/SDL_video.h>

static bool_t sdl_initialized=FALSE;

static ms_mutex_t sdl_mutex;

static SDL_Surface *sdl_screen=0;

static SDL_Overlay * sdl_create_window(int w, int h){
	SDL_Overlay *lay;
	sdl_screen = SDL_SetVideoMode(w,h, 0,SDL_SWSURFACE|SDL_RESIZABLE);
	if (sdl_screen == NULL ) {
		ms_warning("Couldn't set video mode: %s\n",
						SDL_GetError());
		return NULL;
	}
	if (sdl_screen->flags & SDL_HWSURFACE) ms_message("SDL surface created in hardware");
	SDL_WM_SetCaption("Linphone Video", NULL);
	ms_message("Using yuv overlay.");
	lay=SDL_CreateYUVOverlay(w,h,SDL_YV12_OVERLAY,sdl_screen);
	if (lay==NULL){
		ms_warning("Couldn't create yuv overlay: %s\n",
						SDL_GetError());
		return NULL;
	}else{
		if (lay->hw_overlay) ms_message("YUV overlay using hardware acceleration.");
	}
	return lay;
}

static bool_t sdl_display_init(MSDisplay *obj, MSPicture *fbuf){
	SDL_Overlay *lay;
	if (!sdl_initialized){
		/* Initialize the SDL library */
		if( SDL_Init(SDL_INIT_VIDEO) < 0 ) {
			ms_error("Couldn't initialize SDL: %s", SDL_GetError());
			return FALSE;
		}
		/* Clean up on exit */
		atexit(SDL_Quit);
		sdl_initialized=TRUE;
		ms_mutex_init(&sdl_mutex,NULL);
	}
	if (obj->data!=NULL)
		SDL_FreeYUVOverlay((SDL_Overlay*)obj->data);
	lay=sdl_create_window(fbuf->w, fbuf->h);
	if (lay){
		fbuf->planes[0]=lay->pixels[0];
		fbuf->planes[1]=lay->pixels[2];
		fbuf->planes[2]=lay->pixels[1];
		fbuf->strides[0]=lay->pitches[0];
		fbuf->strides[1]=lay->pitches[2];
		fbuf->strides[2]=lay->pitches[1];
		obj->data=lay;
		return TRUE;
	}
	return FALSE;
}

static void sdl_display_lock(MSDisplay *obj){
	ms_mutex_lock(&sdl_mutex);
	SDL_LockYUVOverlay((SDL_Overlay*)obj->data);
	ms_mutex_unlock(&sdl_mutex);
}

static void sdl_display_unlock(MSDisplay *obj){
	SDL_Overlay *lay=(SDL_Overlay*)obj->data;
	ms_mutex_lock(&sdl_mutex);
	SDL_UnlockYUVOverlay(lay);
	ms_mutex_unlock(&sdl_mutex);
}

static void sdl_display_update(MSDisplay *obj){
	SDL_Rect rect;
	SDL_Overlay *lay=(SDL_Overlay*)obj->data;
	rect.x=0;
	rect.y=0;
	rect.w=lay->w;
	rect.h=lay->h;
	ms_mutex_lock(&sdl_mutex);
	SDL_DisplayYUVOverlay(lay,&rect);
	ms_mutex_unlock(&sdl_mutex);
}

static bool_t sdl_poll_event(MSDisplay *obj, MSDisplayEvent *ev){
	SDL_Event event;
	static MSDisplayEvent last_ev;
	static struct timeval tv;
	static bool_t got_rs_ev=FALSE;
	struct timeval cur;
	int elapsed;
	bool_t ret=FALSE;
	ms_mutex_lock(&sdl_mutex);
	if (SDL_PollEvent(&event)){
		ms_mutex_unlock(&sdl_mutex);
		switch(event.type){
			case SDL_VIDEORESIZE:
				last_ev.evtype=MS_DISPLAY_RESIZE_EVENT;
				last_ev.w=event.resize.w;
				last_ev.h=event.resize.h;
				got_rs_ev=TRUE;
				gettimeofday(&tv,NULL);
			break;
			default:
			break;
		}
	}else ms_mutex_unlock(&sdl_mutex);
	if (got_rs_ev){
		gettimeofday(&cur,NULL);
		elapsed=((cur.tv_sec-tv.tv_sec)*1000) + ((cur.tv_usec-tv.tv_usec)/1000);
		if (elapsed>1000){
			got_rs_ev=FALSE;
			*ev=last_ev;
			ret=TRUE;
		}
	}
	return ret;
}

static void sdl_display_uninit(MSDisplay *obj){
	SDL_Overlay *lay=(SDL_Overlay*)obj->data;
	if (lay==NULL)
		return;
	if (lay!=NULL)
		SDL_FreeYUVOverlay(lay);
	if (sdl_screen!=NULL){
		SDL_FreeSurface(sdl_screen);
		sdl_screen=NULL;
	}
}

MSDisplayDesc ms_sdl_display_desc={
	.init=sdl_display_init,
	.lock=sdl_display_lock,
	.unlock=sdl_display_unlock,
	.update=sdl_display_update,
	.uninit=sdl_display_uninit,
	.pollevent=sdl_poll_event
};

#else

#include <Vfw.h>


typedef struct _WinDisplay{
	HWND window;
	HDRAWDIB ddh;
	MSPicture fb;
	MSDisplayEvent last_rsz;
	uint8_t *rgb;
	int rgb_len;
	bool_t new_ev;
}WinDisplay;

static LRESULT CALLBACK window_proc(
    HWND hwnd,        // handle to window
    UINT uMsg,        // message identifier
    WPARAM wParam,    // first message parameter
    LPARAM lParam)    // second message parameter
{
	switch(uMsg){
		case WM_DESTROY:
		break;
		case WM_SIZE:
			if (wParam==SIZE_RESTORED){
				int h=(lParam>>16) & 0xffff;
				int w=lParam & 0xffff;
				MSDisplay *obj;
				WinDisplay *wd;
				ms_message("Resized to %i,%i",w,h);
				obj=(MSDisplay*)GetWindowLongPtr(hwnd,GWLP_USERDATA);
				if (obj!=NULL){
					wd=(WinDisplay*)obj->data;
					wd->last_rsz.evtype=MS_DISPLAY_RESIZE_EVENT;
					wd->last_rsz.w=w;
					wd->last_rsz.h=h;
					wd->new_ev=TRUE;
				}else{
					ms_error("Could not retrieve MSDisplay from window !");
				}
			}
		break;
		default:
			return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}
	return 0;
}

static HWND create_window(int w, int h)
{
	WNDCLASS wc;
	HINSTANCE hInstance = GetModuleHandle(NULL);
	HWND hwnd;
	RECT rect;
	wc.style = 0 ;
	wc.lpfnWndProc = window_proc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = NULL;
	wc.hIcon = NULL;
	wc.hCursor = LoadCursor(hInstance, IDC_ARROW);
	wc.hbrBackground = NULL;
	wc.lpszMenuName =  NULL;
	wc.lpszClassName = "Video Window";
	
	if(!RegisterClass(&wc))
	{
		/* already registred! */
	}
	rect.left=100;
	rect.top=100;
	rect.right=rect.left+w;
	rect.bottom=rect.top+h;
	if (!AdjustWindowRect(&rect,WS_OVERLAPPEDWINDOW|WS_VISIBLE /*WS_CAPTION WS_TILED|WS_BORDER*/,FALSE)){
		ms_error("AdjustWindowRect failed.");
	}
	ms_message("AdjustWindowRect: %li,%li %li,%li",rect.left,rect.top,rect.right,rect.bottom);
	hwnd=CreateWindow("Video Window", "Video window", WS_OVERLAPPEDWINDOW|WS_VISIBLE ,
						CW_USEDEFAULT, CW_USEDEFAULT, rect.right-rect.left,rect.bottom-rect.top,
													NULL, NULL, hInstance, NULL);
	if (hwnd==NULL){
		ms_error("Fail to create video window");
	}
	return hwnd;
}

static bool_t win_display_init(MSDisplay *obj, MSPicture *fbuf){
	WinDisplay *wd=(WinDisplay*)obj->data;
	int ysize,usize;

	if (wd!=NULL)
	{
		if (wd->ddh) DrawDibClose(wd->ddh);
		wd->ddh=NULL;
		if (wd->fb.planes[0]) ms_free(wd->fb.planes[0]);
		wd->fb.planes[0]=NULL;
		wd->fb.planes[1]=NULL;
		wd->fb.planes[2]=NULL;
		if (wd->rgb) ms_free(wd->rgb);
		wd->rgb=NULL;
		wd->rgb_len=0;
	}
	else
		wd=(WinDisplay*)ms_new0(WinDisplay,1);
	
	obj->data=wd;
	
	wd->fb.w=fbuf->w;
	wd->fb.h=fbuf->h;
	
	if (wd->window==NULL){
		if (obj->window_id!=0){
			void *p;
			wd->window=(HWND)obj->window_id;
			p=(void*)GetWindowLongPtr(wd->window,GWLP_USERDATA);
			if (p!=NULL){
				ms_error("Gulp: this externally supplied windows seems to "
					"already have a userdata ! resizing will crash !");
			}else SetWindowLongPtr(wd->window,GWLP_USERDATA,(LONG_PTR)obj);
		}else{
			wd->window=create_window(wd->fb.w,wd->fb.h);
			if (wd->window!=NULL) SetWindowLongPtr(wd->window,GWLP_USERDATA,(LONG_PTR)obj);
			else return FALSE;
		}
	}
	
	if (wd->ddh==NULL) wd->ddh=DrawDibOpen();
	if (wd->ddh==NULL){
		ms_error("DrawDibOpen() failed.");
		return FALSE;
	}
	/*allocate yuv and rgb buffers*/
	if (wd->fb.planes[0]) ms_free(wd->fb.planes[0]);
	if (wd->rgb) ms_free(wd->rgb);
	ysize=wd->fb.w*wd->fb.h;
	usize=ysize/4;
	fbuf->planes[0]=wd->fb.planes[0]=(uint8_t*)ms_malloc0(ysize+2*usize);
	fbuf->planes[1]=wd->fb.planes[1]=wd->fb.planes[0]+ysize;
	fbuf->planes[2]=wd->fb.planes[2]=wd->fb.planes[1]+usize;
	fbuf->strides[0]=wd->fb.strides[0]=wd->fb.w;
	fbuf->strides[1]=wd->fb.strides[1]=wd->fb.w/2;
	fbuf->strides[2]=wd->fb.strides[2]=wd->fb.w/2;

	wd->rgb_len=ysize*3;
	wd->rgb=(uint8_t*)ms_malloc0(wd->rgb_len);
	return TRUE;
}

typedef struct rgb{
	uint8_t r,g,b;
} rgb_t;

typedef struct yuv{
	uint8_t y,u,v;
} yuv_t;



static void yuv420p_to_rgb(MSPicture *src, uint8_t *rgb){
	struct SwsContext *sws;
	int rgb_stride=-src->w*3;
	uint8_t *p;

	p=rgb+(src->w*3*(src->h-1));
	sws=sws_getContext(src->w,src->h,PIX_FMT_YUV420P,
			src->w,src->h,PIX_FMT_RGB24,
			0, NULL, NULL, NULL);
	if (sws_scale(sws,src->planes,src->strides, 0,
           			0, &p, &rgb_stride)!=0){
			ms_error("Error in 420->rgb sws_scale().");
	}
	sws_freeContext(sws);

	/*revert colors*/	
	{
		int i,j,stride;
		rgb_t pix;
		stride=src->w*3;
		p=rgb;
		for(i=0;i<src->h;++i){
			for(j=0;j<stride;j+=3){
				pix.r=p[j];
				pix.g=p[j+1];
				pix.b=p[j+2];
				p[j]=pix.b;
				p[j+2]=pix.r;
			}
			p+=stride;
		}
	}

}

static void win_display_update(MSDisplay *obj){
	WinDisplay *wd=(WinDisplay*)obj->data;
	HDC hdc;
	BITMAPINFOHEADER bi;
	RECT rect;
	bool_t ret;
	if (wd->window==NULL) return;
	hdc=GetDC(wd->window);
	if (hdc==NULL) {
		ms_error("Could not get window dc");
		return;
	}
	yuv420p_to_rgb(&wd->fb, wd->rgb);
	memset(&bi,0,sizeof(bi));
	bi.biSize=sizeof(bi);
	GetClientRect(wd->window,&rect);
	/*
	bi.biWidth=wd->fb.w;
	bi.biHeight=wd->fb.h;
	bi.biPlanes=3;
	bi.biBitCount=12;
	bi.biCompression=MAKEFOURCC('I','4','2','0');
	bi.biSizeImage=(wd->fb.w*wd->fb.h*3)/2;
	*/
	bi.biWidth=wd->fb.w;
	bi.biHeight=wd->fb.h;
	bi.biPlanes=1;
	bi.biBitCount=24;
	bi.biCompression=BI_RGB;
	bi.biSizeImage=wd->rgb_len;

	//if (bi.biHeight>rect.bottom)
	//	bi.biHeight=rect.bottom;
	//bi.biSizeImage=(bi.biWidth*bi.biHeight)*3;

	ret=DrawDibDraw(wd->ddh,hdc,0,0,
		//bi.biWidth,bi.biHeight,
		rect.right,rect.bottom,
		&bi,wd->rgb,
		//0,0,rect.right,rect.bottom,0);
		0,0,bi.biWidth,bi.biHeight,0);
	
  	if (!ret) ms_error("DrawDibDraw failed.");
	ReleaseDC(NULL,hdc);
}

static void win_display_uninit(MSDisplay *obj){
	WinDisplay *wd=(WinDisplay*)obj->data;
	if (wd==NULL)
		return;
	if (wd->window && !obj->window_id) DestroyWindow(wd->window);
	if (wd->ddh) DrawDibClose(wd->ddh);
	if (wd->fb.planes[0]) ms_free(wd->fb.planes[0]);
	if (wd->rgb) ms_free(wd->rgb);
	ms_free(wd);
}

bool_t win_display_pollevent(MSDisplay *d, MSDisplayEvent *ev){
	WinDisplay *wd=(WinDisplay*)d->data;
	if (wd->new_ev){
		wd->new_ev=FALSE;
		*ev=wd->last_rsz;
		return TRUE;
	}
	return FALSE;
}

#ifdef _MSC_VER

extern MSDisplayDesc ms_win_display_desc={
	win_display_init,
	NULL,
	NULL,
	win_display_update,
	win_display_uninit,
	win_display_pollevent
};

#else

MSDisplayDesc ms_win_display_desc={
	.init=win_display_init,
	.update=win_display_update,
	.uninit=win_display_uninit,
	.pollevent=win_display_pollevent
};

#endif

#endif

MSDisplay *ms_display_new(MSDisplayDesc *desc){
	MSDisplay *obj=(MSDisplay *)ms_new0(MSDisplay,1);
	obj->desc=desc;
	obj->data=NULL;
	return obj;
}

void ms_display_set_window_id(MSDisplay *d, long id){
	d->window_id=id;
}

void ms_display_destroy(MSDisplay *obj){
	obj->desc->uninit(obj);
	ms_free(obj);
}

typedef struct VideoOut
{
	AVRational ratio;
	MSPicture fbuf;
	MSPicture local_pic;
	MSRect local_rect;
	mblk_t *local_msg;
	int corner;
	struct SwsContext *sws1;
	struct SwsContext *sws2;
	MSDisplay *display;
	bool_t own_display;
	bool_t ready;
} VideoOut;


#define SCALE_FACTOR 6

static void set_corner(VideoOut *s, int corner)
{
  s->corner=corner;
  s->local_pic.w=s->fbuf.w/SCALE_FACTOR;
  s->local_pic.h=s->fbuf.h/SCALE_FACTOR;
  if (corner==1)
    {
      /* top left corner */
	s->local_rect.x=0;
	s->local_rect.y=0;
	s->local_rect.w=s->local_pic.w;
	s->local_rect.h=s->local_pic.h;
    }
  else if (corner==2)
    {
      /* top right corner */
	s->local_rect.x=s->fbuf.w-s->local_pic.w;
	s->local_rect.y=0;
	s->local_rect.w=s->local_pic.w;
	s->local_rect.h=s->local_pic.h;
    }
  else if (corner==3)
    {
      /* bottom left corner */
	s->local_rect.x=0;
	s->local_rect.y=s->fbuf.h-s->local_pic.h;
	s->local_rect.w=s->local_pic.w;
	s->local_rect.h=s->local_pic.h;
    }
  else
    {
      /* default: bottom right corner */
      /* corner can be set to -1: to disable the self view... */
	s->local_rect.x=s->fbuf.w-s->local_pic.w;
	s->local_rect.y=s->fbuf.h-s->local_pic.h;
	s->local_rect.w=s->local_pic.w;
	s->local_rect.h=s->local_pic.h;
    }
}

static void set_vsize(VideoOut *s, MSVideoSize *sz){
	if (s->ratio.num!=0){
#ifndef WIN32
		sz->width=sz->width & (~0x1);
		sz->height=sz->width*s->ratio.den/s->ratio.num;
#else
		int val=sz->width/s->ratio.num;
		sz->width=val*s->ratio.num;
		sz->height=val*s->ratio.den;
#endif
	}
	s->fbuf.w=sz->width;
	s->fbuf.h=sz->height;
	set_corner(s, s->corner);
}

static void video_out_init(MSFilter  *f){
	VideoOut *obj=(VideoOut*)ms_new(VideoOut,1);
	MSVideoSize def_size;
	obj->ratio.num=11;
	obj->ratio.den=9;
	def_size.width=MS_VIDEO_SIZE_CIF_W;
	def_size.height=MS_VIDEO_SIZE_CIF_H;
	obj->local_msg=NULL;
	obj->corner=0;
	obj->sws1=NULL;
	obj->sws2=NULL;
	obj->display=NULL;
	obj->own_display=FALSE;
	obj->ready=FALSE;
	set_vsize(obj,&def_size);
	f->data=obj;
}


static void video_out_uninit(MSFilter *f){
	VideoOut *obj=(VideoOut*)f->data;
	if (obj->display!=NULL && obj->own_display)
		ms_display_destroy(obj->display);
	if (obj->sws1!=NULL){
		sws_freeContext(obj->sws1);
		obj->sws1=NULL;
	}
	if (obj->sws2!=NULL){
		sws_freeContext(obj->sws2);
		obj->sws2=NULL;
	}
	ms_free(obj);
}


static void video_out_preprocess(MSFilter *f){
	VideoOut *obj=(VideoOut*)f->data;
	if (obj->display==NULL){
#ifndef WIN32
		obj->display=ms_display_new(&ms_sdl_display_desc);
#else
		obj->display=ms_display_new(&ms_win_display_desc);
#endif
		obj->own_display=TRUE;
	}
	if (!ms_display_init(obj->display,&obj->fbuf)){
		if (obj->own_display) ms_display_destroy(obj->display);
		obj->display=NULL;
	}
	if (obj->sws1!=NULL){
		sws_freeContext(obj->sws1);
		obj->sws1=NULL;
	}
	if (obj->sws2!=NULL){
		sws_freeContext(obj->sws2);
		obj->sws2=NULL;
	}
	if (obj->local_msg!=NULL) {
		freemsg(obj->local_msg);
		obj->local_msg=NULL;
	}
	obj->ready=TRUE;
}

static void video_out_postprocess(MSFilter *f){
}

static void video_out_process(MSFilter *f){
	VideoOut *obj=(VideoOut*)f->data;
	mblk_t *inm;
	
	video_out_handle_resize(f, NULL);

	ms_filter_lock(f);
	if (obj->display==NULL){
		ms_filter_unlock(f);
		ms_queue_flush(f->inputs[0]);
		ms_queue_flush(f->inputs[1]);
		return;
	}
	/*get most recent message and draw it*/
	if (f->inputs[1]!=NULL && (inm=ms_queue_peek_last(f->inputs[1]))!=0) {

	  if (obj->corner==-1)
	    {
	      if (obj->local_msg!=NULL) {
		freemsg(obj->local_msg);
		obj->local_msg=NULL;
	      }
	    }
	  else
	    {
	      MSPicture src;
	      if (yuv_buf_init_from_mblk(&src,inm)==0){
		if (obj->sws2==NULL){
		  obj->sws2=sws_getContext(src.w,src.h,PIX_FMT_YUV420P,
					   obj->local_pic.w,obj->local_pic.h,PIX_FMT_YUV420P,
					   SWS_FAST_BILINEAR, NULL, NULL, NULL);
		}
		if (obj->local_msg==NULL){
		  obj->local_msg=yuv_buf_alloc(&obj->local_pic,
					       obj->local_pic.w,obj->local_pic.h);
		}
		if (sws_scale(obj->sws2,src.planes,src.strides, 0,
			      0, obj->local_pic.planes, obj->local_pic.strides)!=0){
		  ms_error("Error in sws_scale().");
		}
	      }
	    }
	  ms_queue_flush(f->inputs[1]);
	}
	
	if (f->inputs[0]!=NULL && (inm=ms_queue_peek_last(f->inputs[0]))!=0) {
		MSPicture src;
		if (yuv_buf_init_from_mblk(&src,inm)==0){
			if (obj->sws1==NULL){
				obj->sws1=sws_getContext(src.w,src.h,PIX_FMT_YUV420P,
				obj->fbuf.w,obj->fbuf.h,PIX_FMT_YUV420P,
				SWS_FAST_BILINEAR, NULL, NULL, NULL);
			}
			ms_display_lock(obj->display);
			if (sws_scale(obj->sws1,src.planes,src.strides, 0,
            			0, obj->fbuf.planes, obj->fbuf.strides)!=0){
				ms_error("Error in sws_scale().");
			}
			ms_display_unlock(obj->display);
		}
		ms_queue_flush(f->inputs[0]);
	}
	/*copy resized local view into main buffer, at bottom left corner:*/
	if (obj->local_msg!=NULL){
		MSPicture corner=obj->fbuf;
		MSVideoSize roi;
		roi.width=obj->local_pic.w;
		roi.height=obj->local_pic.h;
		corner.w=obj->local_pic.w;
		corner.h=obj->local_pic.h;
		corner.planes[0]+=obj->local_rect.x+(obj->local_rect.y*corner.strides[0]);
		corner.planes[1]+=(obj->local_rect.x/2)+((obj->local_rect.y/2)*corner.strides[1]);
		corner.planes[2]+=(obj->local_rect.x/2)+((obj->local_rect.y/2)*corner.strides[2]);
		ms_display_lock(obj->display);
		yuv_buf_copy(obj->local_pic.planes,obj->local_pic.strides,
				corner.planes,corner.strides,roi);
		ms_display_unlock(obj->display);
	}
	
	ms_display_update(obj->display);
	ms_filter_unlock(f);
}

static int video_out_set_vsize(MSFilter *f,void *arg){
	VideoOut *s=(VideoOut*)f->data;
	bool_t reconfigure;
	ms_filter_lock(f);
	reconfigure=s->ready;
	set_vsize(s,(MSVideoSize*)arg);
	if (reconfigure) video_out_preprocess(f);
	ms_filter_unlock(f);
	return 0;
}

static int video_out_set_display(MSFilter *f,void *arg){
	VideoOut *s=(VideoOut*)f->data;
	s->display=(MSDisplay*)arg;
	return 0;
}

static int video_out_handle_resize(MSFilter *f, void *arg){
	VideoOut *s=(VideoOut*)f->data;
	MSDisplay *disp=s->display;
	if (disp!=NULL){
		MSDisplayEvent ev;
		if (ms_display_poll_event(disp,&ev)){
			if (ev.evtype==MS_DISPLAY_RESIZE_EVENT){
				MSVideoSize sz;
				sz.width=ev.w;
				sz.height=ev.h;
#ifndef WIN32
				video_out_set_vsize(f,&sz);
#endif
			}
		}
	}
	return 0;
}

static int video_out_set_corner(MSFilter *f,void *arg){
	VideoOut *s=(VideoOut*)f->data;
	set_corner(s, *(int*)arg);
	return 0;
}

static MSFilterMethod methods[]={
	{	MS_FILTER_SET_VIDEO_SIZE	,	video_out_set_vsize },
	{	MS_VIDEO_OUT_SET_DISPLAY	,	video_out_set_display},
	{ 	MS_VIDEO_OUT_HANDLE_RESIZING	,	video_out_handle_resize},
	{	MS_VIDEO_OUT_SET_CORNER 	,	video_out_set_corner},
	{	0	,NULL}
};

#ifdef _MSC_VER

MSFilterDesc ms_video_out_desc={
	MS_VIDEO_OUT_ID,
	"MSVideoOut",
	"A generic video display",
	MS_FILTER_OTHER,
	NULL,
	2,
	0,
	video_out_init,
	video_out_preprocess,
	video_out_process,
	video_out_postprocess,
	video_out_uninit,
	methods
};

#else

MSFilterDesc ms_video_out_desc={
	.id=MS_VIDEO_OUT_ID,
	.name="MSVideoOut",
	.text="A generic video display",
	.category=MS_FILTER_OTHER,
	.ninputs=2,
	.noutputs=0,
	.init=video_out_init,
	.preprocess=video_out_preprocess,
	.process=video_out_process,
	.postprocess=video_out_postprocess,
	.uninit=video_out_uninit,
	.methods=methods
};

#endif

MS_FILTER_DESC_EXPORT(ms_video_out_desc)
