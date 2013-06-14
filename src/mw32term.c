/* MW32 Communication module for Windows.
   Copyright (C) 1989, 93, 94, 95, 96, 1997, 1998, 1999, 2000, 2001
   Free Software Foundation, Inc.

This file is part of GNU Emacs.

GNU Emacs is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GNU Emacs is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU Emacs; see the file COPYING.  If not, write to
the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

/* New display code by Gerd Moellmann <gerd@gnu.org>.  */
/* Xt features made by Fred Pierresteguy.  */
/* MW32 implementation by MIYASHITA Hisashi <himi@meadowy.org> */
/*
   TODO list:
   ----------
   mw32color
   mouse cursor.
   stipple op.
   mw32_scroll_bar_report_motion
*/

#include <config.h>

/* On 4.3 these lose if they come after xterm.h.  */
/* Putting these at the beginning seems to be standard for other .c files.  */
#include <signal.h>

#include <stdio.h>

#include "lisp.h"
#include "blockinput.h"
#include "mw32sync.h"
#include <shellapi.h>
#if _MSC_VER < 1500
#include <winable.h>
#endif
#include "mw32term.h"
#include "mw32mci.h"

#include "systty.h"
#include "systime.h"

#include <ctype.h>
#include <errno.h>

#include "charset.h"
#include "coding.h"
#include "ccl.h"
#include "frame.h"
#include "dispextern.h"
#include "fontset.h"
#include "termhooks.h"
#include "termopts.h"
#include "termchar.h"
#include "gnu.h"
#include "disptab.h"
#include "buffer.h"
#include "window.h"
#include "keyboard.h"
#include "intervals.h"
#include "process.h"
#include "atimer.h"
#include "keymap.h"
#if defined (__MINGW32__)
#include <limits.h>
#endif /* __MINGW32__ */
#include <mmsystem.h>


static HBITMAP fringe_bmp[MAX_FRINGE_BITMAPS];


/* Temporary variable for XTread_socket.  */

static int last_mousemove_x = 0;
static int last_mousemove_y = 0;

/* Non-zero means that a HELP_EVENT has been generated since Emacs
   start.  */

static int any_help_event_p;

/* Non-zero means draw block and hollow cursor as wide as the glyph
   under it.  For example, if a block cursor is over a tab, it will be
   drawn as wide as that tab on the display.  */

int mw32_stretch_cursor_p;

/* Type of visible bell. */

Lisp_Object Vmw32_visible_bell_type;

/* This is a chain of structures for all the X displays currently in
   use.  */

struct mw32_display_info *mw32_display_list;

/* This is a list of cons cells, each of the form (NAME
   . FONT-LIST-CACHE), one for each element of x_display_list and in
   the same order.  NAME is the name of the frame.  FONT-LIST-CACHE
   records previous values returned by x-list-fonts.  */

Lisp_Object x_display_name_list;

/* Frame being updated by update_frame.  This is declared in term.c.
   This is set by update_begin and looked at by all the XT functions.
   It is zero while not inside an update.  In that case, the XT
   functions assume that `selected_frame' is the frame to apply to.  */

extern struct frame *updating_frame;

/* This is a frame waiting to be auto-raised, within XTread_socket.  */

struct frame *pending_autoraise_frame;

/* Nominal cursor position -- where to draw output.
   HPOS and VPOS are window relative glyph matrix coordinates.
   X and Y are window relative pixel coordinates.  */

struct cursor_pos output_cursor;

/* Mouse movement.

   Formerly, we used PointerMotionHintMask (in standard_event_mask)
   so that we would have to call XQueryPointer after each MotionNotify
   event to ask for another such event.  However, this made mouse tracking
   slow, and there was a bug that made it eventually stop.

   Simply asking for MotionNotify all the time seems to work better.

   In order to avoid asking for motion events and then throwing most
   of them away or busy-polling the server for mouse positions, we ask
   the server for pointer motion hints.  This means that we get only
   one event per group of mouse movements.  "Groups" are delimited by
   other kinds of events (focus changes and button clicks, for
   example), or by XQueryPointer calls; when one of these happens, we
   get another MotionNotify event the next time the mouse moves.  This
   is at least as efficient as getting motion events when mouse
   tracking is on, and I suspect only negligibly worse when tracking
   is off.  */

/* Where the mouse was last time we reported a mouse event.  */

FRAME_PTR last_mouse_frame;
static RECT last_mouse_glyph;
static Lisp_Object last_mouse_press_frame;



/* wheel message. */
#ifdef W32_INTELLIMOUSE
static UINT mw32_wheel_message = WM_MOUSEWHEEL;
#endif

/* to prevent thread from operating messages.  */
CRITICAL_SECTION critsec_message;

/* to prevent thread from accessing events.  */
CRITICAL_SECTION critsec_access_event;

/* make thread waiting for gobbling input.  */
HANDLE next_message_block_event;

/* flag if mw32term is blocked in message loop. */
int message_loop_blocked_p;

/* Message Handling thread & its id.  */
HANDLE msg_thread;
DWORD msg_thread_id;

/* Main thread & its id. */
HANDLE main_thread;
DWORD main_thread_id;

/* To handle C-g quickly, this event handle is
   pulsed when C-g is detected. */
HANDLE interrupt_handle;

/* thread id that owns block_input critical section.  */
DWORD block_input_ownthread;

/* If the system has already created frame, this variable is set to
   its window handle.  This variable is used for sending message to
   window procedure. */
HWND mw32_frame_window;


/* The scroll bar in which the last X motion event occurred.

   If the last X motion event occurred in a scroll bar, we set this so
   XTmouse_position can know whether to report a scroll bar motion or
   an ordinary motion.

   If the last X motion event didn't occur in a scroll bar, we set
   this to Qnil, to tell XTmouse_position to return an ordinary motion
   event.  */

static Lisp_Object last_mouse_scroll_bar;

/* This is a hack.  We would really prefer that MW32_mouse_position would
   return the time associated with the position it returns, but there
   doesn't seem to be any way to wrest the time-stamp from the server
   along with the position query.  So, we just keep track of the time
   of the last movement we received, and return that in hopes that
   it's somewhat accurate.  */

static Time last_mouse_movement_time;

/* For synchronous note_mouse_movement.  */
/* last_mouse_motion_frame maintains the frame where a mouse moves last time.
   Its value is set to NULL when a mouse is not moved.  */
static Lisp_Object last_mouse_motion_frame;

/* last_mouse_motion_message describes the mouse movement last time.  */
static MSG last_mouse_motion_message;

/* Layered Window */
SETLAYEREDWINDOWATTRPROC SetLayeredWindowAttributesProc = NULL;

typedef UINT (WINAPI *SENDINPUTPROC)(UINT, INPUT*, int);
SENDINPUTPROC SendInputProc = NULL;

/* Incremented by XTread_socket whenever it really tries to read
   events.  */

#ifdef __STDC__
static int volatile input_signal_count;
#else
static int input_signal_count;
#endif

/* Initial values of argv and argc.  */

extern Lisp_Object Vsystem_name;

/* Tells if a window manager is present or not.  */

extern Lisp_Object Qmouse_face;

/* for mw32fns module interfaces. */
static void mw32_lower_frame P_ ((struct frame *f));
static void mw32_raise_frame P_ ((struct frame *));
void mw32_initialize P_ ((void));

/* MW32 internal services */
extern int clear_mouse_face P_ ((struct mw32_display_info *));
extern void frame_to_window_pixel_xy P_ ((struct window *, int *, int *));
extern void set_output_cursor P_ ((struct cursor_pos *));
extern struct glyph *x_y_to_hpos_vpos P_ ((struct window *, int, int,
					   int *, int *, int *, int *, int *));
extern void note_tool_bar_highlight P_ ((struct frame *f, int, int));
extern void notice_overwritten_cursor P_ ((struct window *,
					   enum glyph_row_area area,
					   int x0, int y0, int x1, int y1));
static void mw32_frame_rehighlight_1 P_ ((struct mw32_display_info *dpyinfo));
static int mw32_get_keymodifier_state P_(());
#define MW32GETMODIFIER(dpyinfo) mw32_get_keymodifier_state()
#define MW32GETMOUSEMODIFIER(dpyinfo, mouse) \
 (MW32GETMODIFIER (dpyinfo) | (mouse ? up_modifier : down_modifier))

#define MW32_MOUSE_HIDE_TIMER_ID 1

static void mw32_clip_to_row P_ ((HDC, struct window *,
				  struct glyph_row *, int));

/* Window that is tracking the mouse.  */
static HWND track_mouse_window = NULL;

typedef BOOL (WINAPI *TrackMouseEvent_Proc) (LPTRACKMOUSEEVENT);

TrackMouseEvent_Proc track_mouse_event_fn = NULL;

/* If non-nil inhibit frame relocation in mw32_calc_absolute_position() */
int mw32_restrict_frame_position = TRUE;

extern Lisp_Object mw32_create_image_blob_from_icon P_((HICON hicon));

EXFUN (Finternal_get_lisp_face_attribute, 3);
EXFUN (Funix_to_dos_filename, 1);

/* Flush message queue of frame F, or of all frames if F is null.  */

static void
mw32i_flush (struct frame *f)
{
#if 0
  if (f == NULL)
    {
      Lisp_Object rest, frame;
      FOR_EACH_FRAME (rest, frame)
	mw32i_flush (XFRAME (frame));
    }
  else if (FRAME_MW32_P (f))
    {
      /* wait until the message queue is flushed. */
    }
#else
      GdiFlush ();
#endif
}


/***********************************************************************
			      Debugging
 ***********************************************************************/

#if 0

/* This is a function useful for recording debugging information about
   the sequence of occurrences in this file.  */

struct record
{
  char *locus;
  int type;
};

struct record event_record[100];

int event_record_index;

record_event (locus, type)
     char *locus;
     int type;
{
  if (event_record_index == sizeof (event_record) / sizeof (struct record))
    event_record_index = 0;

  event_record[event_record_index].locus = locus;
  event_record[event_record_index].type = type;
  event_record_index++;
}

#endif /* 0 */


/***********************************************************************
			  FRAME HDC handling
 ***********************************************************************/
/* main_thread_hdc_nestlevel never exceeds this limit under normal usage. */
#define GET_FRAME_HDC_LEVEL_LIMIT 10

/* main_thread_hdc stack never exceeds this limit under normal usage. */
#define MAIN_THREAD_FRAME_HDC_STACK 10

/* Window's DC that is used by main thread. */
HDC main_thread_hdc = INVALID_HANDLE_VALUE;

/* Nesting level of main_thread_hdc. */
int main_thread_hdc_nestlevel = 0;

/* hwnd for main_thread_hdc to use on release. */
HWND main_thread_hdc_hwnd = INVALID_HANDLE_VALUE;

/* frame when main_thread_hdc is operated. */
FRAME_PTR main_thread_hdc_frame = NULL;

/* Stack level of main_thread_hdc. */
int old_main_thread_hdc_stack;

/* Stack of main_thread_hdc. */
HDC old_main_thread_hdc[MAIN_THREAD_FRAME_HDC_STACK];

/* Stack of main_thread_hdc_nestlevel. */
int old_main_thread_hdc_nestlevel[MAIN_THREAD_FRAME_HDC_STACK];

/* Stack of main_thread_hdc_hwnd. */
HWND old_main_thread_hdc_hwnd[MAIN_THREAD_FRAME_HDC_STACK];

/* Stack of main_thread_hdc_frame. */
FRAME_PTR old_main_thread_hdc_frame[MAIN_THREAD_FRAME_HDC_STACK];

void
mw32_init_frame_hdc ()
{
  int i;

  for (i = 0; i < MAIN_THREAD_FRAME_HDC_STACK; i++)
    {
      old_main_thread_hdc[i] = INVALID_HANDLE_VALUE;;
      old_main_thread_hdc_nestlevel[i] = 0;
      old_main_thread_hdc_hwnd[i] = INVALID_HANDLE_VALUE;
      old_main_thread_hdc_frame[i] = NULL;
    }
  old_main_thread_hdc_stack = 0;
}

/* Check whether the same frame is a nest. */
#define MW32_HDC_NEST_CHECK 0

HDC
mw32_get_frame_hdc (f)
     FRAME_PTR f;
{
  HDC ret_hdc = INVALID_HANDLE_VALUE;
#if MW32_HDC_NEST_CHECK
  int i;
#endif

  if (FRAME_WINDOW_P (f))
    {
      if (MW32_MAIN_THREAD_P())
	{
	  if (main_thread_hdc != INVALID_HANDLE_VALUE)
	    {
	      if (main_thread_hdc_hwnd != FRAME_MW32_WINDOW (f))
		{
		  if (old_main_thread_hdc_stack < MAIN_THREAD_FRAME_HDC_STACK)
		    {
		      old_main_thread_hdc_hwnd[old_main_thread_hdc_stack]
			= main_thread_hdc_hwnd;
		      main_thread_hdc_hwnd = INVALID_HANDLE_VALUE;
		      old_main_thread_hdc_nestlevel[old_main_thread_hdc_stack]
			= main_thread_hdc_nestlevel;
		      main_thread_hdc_nestlevel = 0;

		      MW32_BLOCK_FRAME_HDC (main_thread_hdc_frame);
		      old_main_thread_hdc[old_main_thread_hdc_stack]
			= main_thread_hdc;
		      main_thread_hdc = INVALID_HANDLE_VALUE;
		      MW32_UNBLOCK_FRAME_HDC (main_thread_hdc_frame);

		      old_main_thread_hdc_frame[old_main_thread_hdc_stack]
			= main_thread_hdc_frame;
		      main_thread_hdc_frame = NULL;
		      old_main_thread_hdc_stack++;

		      update_begin_hook (f);
		      main_thread_hdc_frame = f;
		      ret_hdc = main_thread_hdc;

#if MW32_HDC_NEST_CHECK
		      /* Check whether the same frame is a nest. */
		      for (i = 0; i < old_main_thread_hdc_stack; i++)
			{
			  if (f == old_main_thread_hdc_frame[i])
			    {
			      abort ();
			    }
			}
#endif
		    }
		  else
		    {
		      abort ();
		    }
		}
	      else
		{
		  if (main_thread_hdc_nestlevel++ > GET_FRAME_HDC_LEVEL_LIMIT)
		    {
		      abort ();
		    }
		  else
		    {
		      ret_hdc = main_thread_hdc;
		    }
		}
	    }
	  else
	    {
	      update_begin_hook (f);
	      main_thread_hdc_frame = f;
	      ret_hdc = main_thread_hdc;
	    }
	}
      else
	{
	  if ((f)->output_data.mw32->hdc != INVALID_HANDLE_VALUE)
	    {
	      if ((f)->output_data.mw32->hdc_nestlevel++
		  > GET_FRAME_HDC_LEVEL_LIMIT)
		{
		  abort ();
		}
	      else
		{
		  ret_hdc = (f)->output_data.mw32->hdc;
		}
	    }
	  else
	    {
	      update_begin_hook (f);
	      ret_hdc = (f)->output_data.mw32->hdc;
	    }
	}
    }
  return ret_hdc;
}

void
mw32_release_frame_hdc (f)
     FRAME_PTR f;
{
  if (FRAME_WINDOW_P (f))
    {
      int *pcount;
      HDC *phdc;
      if (MW32_MAIN_THREAD_P ())
	{
	  phdc = &main_thread_hdc;
	  pcount = &main_thread_hdc_nestlevel;
	}
      else
	{
	  phdc = &((f)->output_data.mw32->hdc);
	  pcount = &((f)->output_data.mw32->hdc_nestlevel);
	}

      if (*phdc == INVALID_HANDLE_VALUE)
	abort ();

      if (*pcount > 1)
	(*pcount)--;
      else
	{
	  update_end_hook (f);
	  if (MW32_MAIN_THREAD_P ())
	    {
	      if (old_main_thread_hdc_stack)
		{
		  old_main_thread_hdc_stack--;
		  main_thread_hdc_frame
		    = old_main_thread_hdc_frame[old_main_thread_hdc_stack];
		  old_main_thread_hdc_frame[old_main_thread_hdc_stack]
		    = NULL;

		  MW32_BLOCK_FRAME_HDC (main_thread_hdc_frame);
		  main_thread_hdc
		    = old_main_thread_hdc[old_main_thread_hdc_stack];
		  old_main_thread_hdc[old_main_thread_hdc_stack]
		    = INVALID_HANDLE_VALUE;
		  MW32_UNBLOCK_FRAME_HDC (main_thread_hdc_frame);

		  main_thread_hdc_nestlevel
		    = old_main_thread_hdc_nestlevel[old_main_thread_hdc_stack];
		  old_main_thread_hdc_nestlevel[old_main_thread_hdc_stack]
		    = 0;
		  main_thread_hdc_hwnd
		    = old_main_thread_hdc_hwnd[old_main_thread_hdc_stack];
		  old_main_thread_hdc_hwnd[old_main_thread_hdc_stack]
		    = INVALID_HANDLE_VALUE;
		}
	      else
		{
		  main_thread_hdc_frame = NULL;
		}
	    }
	}
    }
}

void
mw32_cleanup_frame_hdc (f)
     FRAME_PTR f;
{
  if (FRAME_WINDOW_P (f))
    {
      if (MW32_MAIN_THREAD_P ())
	{
	  if (main_thread_hdc != INVALID_HANDLE_VALUE)
	    {
	      main_thread_hdc_nestlevel = 1;
	      update_end_hook (f);
	      if (old_main_thread_hdc_stack)
		{
		  old_main_thread_hdc_stack--;
		  main_thread_hdc_frame
		    = old_main_thread_hdc_frame[old_main_thread_hdc_stack];
		  old_main_thread_hdc_frame[old_main_thread_hdc_stack] = NULL;

		  MW32_BLOCK_FRAME_HDC (main_thread_hdc_frame);
		  main_thread_hdc
		    = old_main_thread_hdc[old_main_thread_hdc_stack];
		  old_main_thread_hdc[old_main_thread_hdc_stack]
		    = INVALID_HANDLE_VALUE;
		  MW32_UNBLOCK_FRAME_HDC (main_thread_hdc_frame);

		  main_thread_hdc_nestlevel
		    = old_main_thread_hdc_nestlevel[old_main_thread_hdc_stack];
		  old_main_thread_hdc_nestlevel[old_main_thread_hdc_stack] = 0;
		  main_thread_hdc_hwnd
		    = old_main_thread_hdc_hwnd[old_main_thread_hdc_stack];
		  old_main_thread_hdc_hwnd[old_main_thread_hdc_stack]
		    = INVALID_HANDLE_VALUE;
		}
	      else
		{
		  main_thread_hdc_frame = NULL;
		}
	    }
	}
      else
	{
	  if ((f)->output_data.mw32->hdc != INVALID_HANDLE_VALUE)
	    {
	      (f)->output_data.mw32->hdc_nestlevel = 1;
	      update_end_hook (f);
	    }
	}
    }
}



#ifdef MEADOW
/* Under HyperThreading environment, SaveDC sometimes failes with
   'Invalid Handle'. */
#define SAVEDC_RETRY 5

int
safe_SaveDC (HDC hdc)
{
  int ret;
  int retry = SAVEDC_RETRY;
  int cause = 0;

  do
    {
      ret = SaveDC (hdc);
    }
  while (ret == 0 && retry-- > 0);

  if (ret == 0)
    {
      cause = GetLastError();
      abort (); /* Yield */
    }
  return ret;
}

#define SaveDC(hdc) (safe_SaveDC (hdc))
#endif


/* Return the struct mw32_display_info corresponding to DPY.  */

struct mw32_display_info *
mw32_display_info_for_display (DWORD id)
{
  return mw32_display_list;
}

/* Allocate & Return Frame's device contest.  */

void
mw32_setup_default_hdc (HDC hdc)
{
  SetTextAlign (hdc, TA_BASELINE);
  SetMapMode (hdc, MM_TEXT);
  SetBkMode (hdc, TRANSPARENT);
}


/* Destroy Frame's dc.  */

void
mw32_destroy_frame_hdc (struct frame *f)
{
  if (f->output_data.mw32->hdc != INVALID_HANDLE_VALUE)
    {
      ReleaseDC (FRAME_MW32_WINDOW (f), f->output_data.mw32->hdc);
      f->output_data.mw32->hdc = INVALID_HANDLE_VALUE;
    }
}

void
mw32_fill_area_pix (struct frame *f, PIX_TYPE pix,
		    int x0, int y0, int x1, int y1)
{
  RECT r;
  HBRUSH hb;
  HANDLE oldobj;
  HDC hdc = GET_FRAME_HDC (f);

  r.left = x0;
  r.top = y0;
  r.right = x1;
  r.bottom = y1;
  hb = CreateSolidBrush (pix);
  oldobj = SelectObject (hdc, hb);
  FillRect (hdc, &r, hb);
  SelectObject (hdc, oldobj);
  DeleteObject (hb);

  RELEASE_FRAME_HDC (f);
}

void
mw32_fill_area (struct frame *f, int x0, int y0, int x1, int y1)
{
  mw32_fill_area_pix (f, FRAME_FOREGROUND_PIXEL (f), x0, y0, x1, y1);
}

void
mw32_clear_native_frame_area (struct frame *f, int x0, int y0, int x1, int y1)
{
  mw32_fill_area_pix (f, FRAME_BACKGROUND_PIXEL (f), x0, y0, x1, y1);
}

void
mw32_clear_frame_area (struct frame *f, int x, int y, int width, int height)
{
  mw32_fill_area_pix (f, FRAME_BACKGROUND_PIXEL (f),
		      x, y, x + width, y + height);
}

void
mw32_set_clip_area (struct frame *f, int x0, int y0, int x1, int y1)
{
  HDC hdc;
  HRGN region;
  hdc = GET_FRAME_HDC (f);
  region = CreateRectRgn (x0, y0, x1, y1);
  SelectClipRgn (hdc, region);
  DeleteObject (region);
  RELEASE_FRAME_HDC (f);
}

void
mw32_unset_clip_area (struct frame *f)
{
  HDC hdc;
  hdc = GET_FRAME_HDC (f);
  SelectClipRgn (hdc, NULL);
  RELEASE_FRAME_HDC (f);
}

void
mw32_set_caret (struct frame *f, int state)
{
  int blocked = FALSE;

  if (MW32_FRAME_CARET_STATE (f) == NO_CARET
      && (state == BLOCK_CARET || state == UNBLOCK_CARET)) return;

  if (MW32_FRAME_CARET_BLOCKED (f))
    {
      if (state == BLOCK_CARET)
	return;

      if (state == SHOWN_CARET || state == HIDDEN_CARET)
	{
	  MW32_FRAME_CARET_STATE (f) = state;
	  return;
	}
    }

  /* If this function is called during processing delete_frame, Lisp
     frame object is already deleted from frame_list. This condition
     is marked with f->output_data.mw32->window_desc that has a value
     of INVALID_HANDLE_VALUE. */
  if (f->output_data.mw32->window_desc == INVALID_HANDLE_VALUE)
    return;

  if (state == BLOCK_CARET || state == HIDDEN_CARET)
    ResetEvent (f->output_data.mw32->setcaret_event);

  SEND_INFORM_MESSAGE (FRAME_MW32_WINDOW (f),
		       WM_EMACS_SETCARET,
		       state, 0);

  /* Timing is significant when hiding caret */
  if (state == BLOCK_CARET || state == HIDDEN_CARET)
    WaitForSingleObject (f->output_data.mw32->setcaret_event, 10);
}

/* Draw a hollow rectangle at the specified position.  */
void
mw32_draw_rectangle (HDC hdc, int x, int y, int width, int height)
{
  HBRUSH hb, oldhb;
  HPEN hp, oldhp;

  hb = CreateSolidBrush (GetBkColor (hdc));
  hp = CreatePen (PS_SOLID, 0, GetTextColor (hdc));
  oldhb = SelectObject (hdc, hb);
  oldhp = SelectObject (hdc, hp);

  Rectangle (hdc, x, y, x + width, y + height);

  SelectObject (hdc, oldhb);
  SelectObject (hdc, oldhp);
  DeleteObject (hb);
  DeleteObject (hp);
}


/***********************************************************************
		    Starting and ending an update
 ***********************************************************************/

/* Start an update of frame F.  This function is installed as a hook
   for update_begin, i.e. it is called when update_begin is called.
   This function is called prior to calls to x_update_window_begin for
   each window being updated.  Currently, there is nothing to do here
   because all interesting stuff is done on a window basis.  */

static void
MW32_update_begin (struct frame *f)
{
  if (FRAME_MW32_P (f))
    {
      MW32_BLOCK_CARET (f);
      if (MW32_MAIN_THREAD_P ())
	{
	  if (main_thread_hdc != INVALID_HANDLE_VALUE)
	    {
	      if (main_thread_hdc_nestlevel > GET_FRAME_HDC_LEVEL_LIMIT)
		abort();
	      main_thread_hdc_nestlevel++;
	      return;
	    }

	  main_thread_hdc_hwnd = FRAME_MW32_WINDOW (f);
	  MW32_BLOCK_FRAME_HDC (f);
	  main_thread_hdc = GetDC (main_thread_hdc_hwnd);
	  MW32_UNBLOCK_FRAME_HDC (f);

	  main_thread_hdc_nestlevel = 1;
	  mw32_setup_default_hdc (main_thread_hdc);
	}
      else
	{
	  if (f->output_data.mw32->hdc != INVALID_HANDLE_VALUE)
	    {
	      if (f->output_data.mw32->hdc_nestlevel
		  > GET_FRAME_HDC_LEVEL_LIMIT)
		abort ();
	      f->output_data.mw32->hdc_nestlevel++;
	      return;
	    }

	  f->output_data.mw32->hdc = GetDC (FRAME_MW32_WINDOW (f));
	  f->output_data.mw32->hdc_nestlevel = 1;
	  mw32_setup_default_hdc (f->output_data.mw32->hdc);
	}
    }
}

/* Change alpha of frame. */
void
mw32_update_frame_alpha (struct frame *f)
{
  int newalpha, oldalpha;

  if (! SetLayeredWindowAttributesProc) return;

  if (MW32_MAIN_THREAD_P ())
    {
      POST_INFORM_MESSAGE (FRAME_MW32_WINDOW (f),
			   WM_EMACS_UPDATE_ALPHA, (WPARAM)f, (LPARAM)0);
      return;
    }

  oldalpha = f->output_data.mw32->current_alpha;

  if (f->output_data.mw32->frame_moving_or_sizing == 1)
    newalpha = f->output_data.mw32->alpha[ALPHA_MOVING];
  else if (f->output_data.mw32->frame_moving_or_sizing == 2)
    newalpha = f->output_data.mw32->alpha[ALPHA_SIZING];
  else if (FRAME_MW32_DISPLAY_INFO (f)->mw32_highlight_frame == f)
    newalpha = f->output_data.mw32->alpha[ALPHA_ACTIVE];
  else
    newalpha = f->output_data.mw32->alpha[ALPHA_INACTIVE];

  if (newalpha < 0 && oldalpha >= 0)
    SetWindowLong (FRAME_MW32_WINDOW (f), GWL_EXSTYLE,
		   GetWindowLong (FRAME_MW32_WINDOW (f), GWL_EXSTYLE)
		   & ~WS_EX_LAYERED);
  else if (newalpha >= 0)
    {
      if (oldalpha < 0)
	SetWindowLong (FRAME_MW32_WINDOW (f), GWL_EXSTYLE,
		       GetWindowLong (FRAME_MW32_WINDOW (f), GWL_EXSTYLE)
		       | WS_EX_LAYERED);

      if (oldalpha != newalpha)
	SetLayeredWindowAttributesProc (FRAME_MW32_WINDOW (f),
					RGB (255, 255, 255),
					(BYTE) ((float) newalpha
						/ (float) 100 * (float) 255),
					LWA_ALPHA);
    }

  f->output_data.mw32->current_alpha = newalpha;
}

/* End update of frame F.  This function is installed as a hook in
   update_end.  */

static void
MW32_update_end (struct frame *f)
{
  /* Mouse highlight may be displayed again.  */
  if (FRAME_MW32_P (f))
    {
      Lisp_Object window;
      HDC *phdc;
      HWND hwnd;
      int *pcount;

      FRAME_MW32_DISPLAY_INFO (f)->mouse_face_defer = 0;

      if (MW32_MAIN_THREAD_P ())
	{
	  phdc = &main_thread_hdc;
	  pcount = &main_thread_hdc_nestlevel;
	  hwnd = main_thread_hdc_hwnd;
	}
      else
	{
	  phdc = &(f->output_data.mw32->hdc);
	  pcount = &(f->output_data.mw32->hdc_nestlevel);
	  hwnd = FRAME_MW32_WINDOW (f);
	}

      (*pcount)--;
      if (*pcount > 0)
	return;

      MW32_UNBLOCK_CARET (f);
      ReleaseDC (hwnd, *phdc);
      *phdc = INVALID_HANDLE_VALUE;

      /* Synchronize selected_window's caret state */
      window = FRAME_SELECTED_WINDOW (f);
      if (WINDOWP (window))
	{
	  struct window *w = XWINDOW (window);
	  if (CARET_CURSOR_P (w->phys_cursor_type)
	      && w->phys_cursor_on_p
	      && ! MW32_FRAME_CARET_SHOWN (f))
	    mw32_set_caret (f, SHOWN_CARET);
	}
    }
}


/* Start update of window W.  Set the global variable updated_window
   to the window being updated and set output_cursor to the cursor
   position of W.  */

static void
mw32i_update_window_begin (struct window *w)
{
  struct frame *f = XFRAME (WINDOW_FRAME (w));
  struct mw32_display_info *display_info = FRAME_MW32_DISPLAY_INFO (f);

  updated_window = w;
  set_output_cursor (&w->cursor);

  BLOCK_INPUT;

  GET_FRAME_HDC (f);

  if (f == display_info->mouse_face_mouse_frame)
    {
      /* Don't do highlighting for mouse motion during the update.  */
      display_info->mouse_face_defer = 1;

      /* If F needs to be redrawn, simply forget about any prior mouse
	 highlighting.  */
      if (FRAME_GARBAGED_P (f))
	display_info->mouse_face_window = Qnil;

#if 0 /* Rows in a current matrix containing glyphs in mouse-face have
	 their mouse_face_p flag set, which means that they are always
	 unequal to rows in a desired matrix which never have that
	 flag set.  So, rows containing mouse-face glyphs are never
	 scrolled, and we don't have to switch the mouse highlight off
	 here to prevent it from being scrolled.  */

      /* Can we tell that this update does not affect the window
	 where the mouse highlight is?  If so, no need to turn off.
	 Likewise, don't do anything if the frame is garbaged;
	 in that case, the frame's current matrix that we would use
	 is all wrong, and we will redisplay that line anyway.  */
      if (!NILP (display_info->mouse_face_window)
	  && w == XWINDOW (display_info->mouse_face_window))
	{
	  int i;

	  for (i = 0; i < w->desired_matrix->nrows; ++i)
	    if (MATRIX_ROW_ENABLED_P (w->desired_matrix, i))
	      break;

	  if (i < w->desired_matrix->nrows)
	    clear_mouse_face (display_info);

	  /* If we should require GdiFlush(),
	     insert here.  */
	}
#endif /* 0 */
    }

  UNBLOCK_INPUT;
}

/* Draw a vertical window border from (x,y0) to (x,y1)  */

static void
mw32_draw_vertical_window_border (struct window *w, int x, int y0, int y1)
{
  struct frame *f = XFRAME (WINDOW_FRAME (w));

  mw32_fill_area (f, x, y0, x + 1, y1);
}

/* End update of window W (which is equal to updated_window).

   Draw vertical borders between horizontally adjacent windows, and
   display W's cursor if CURSOR_ON_P is non-zero.

   MOUSE_FACE_OVERWRITTEN_P non-zero means that some row containing
   glyphs in mouse-face were overwritten.  In that case we have to
   make sure that the mouse-highlight is properly redrawn.

   W may be a menu bar pseudo-window in case we don't have X toolkit
   support.  Such windows don't have a cursor, so don't display it
   here.  */

static void
mw32i_update_window_end (struct window *w,
			 int cursor_on_p,
			 int mouse_face_overwritten_p)
{
  struct frame *f = XFRAME (WINDOW_FRAME (w));
  struct mw32_display_info *dpyinfo = FRAME_MW32_DISPLAY_INFO (f);

  if (!w->pseudo_window_p)
    {
      if (cursor_on_p)
	display_and_set_cursor (w, 1, output_cursor.hpos,
				output_cursor.vpos,
				output_cursor.x, output_cursor.y);

      x_draw_vertical_border (w);
      /* If we should require GdiFlush(),
	 insert here.  */
      draw_window_fringes (w, 1);
    }

  /* If a row with mouse-face was overwritten, arrange for
     MW32_frame_up_to_date to redisplay the mouse highlight.  */
  if (mouse_face_overwritten_p)
    {
      dpyinfo->mouse_face_beg_row = dpyinfo->mouse_face_beg_col = -1;
      dpyinfo->mouse_face_end_row = dpyinfo->mouse_face_end_col = -1;
      dpyinfo->mouse_face_window = Qnil;
    }

  RELEASE_FRAME_HDC (f);

  updated_window = NULL;
}



/* This function is called from various places in xdisp.c whenever a
   complete update has been performed.  The global variable
   updated_window is not available here.  */

static void
MW32_frame_up_to_date (struct frame *f)
{
  if (FRAME_MW32_P (f))
    {
      struct mw32_display_info *dpyinfo = FRAME_MW32_DISPLAY_INFO (f);

      if (dpyinfo->mouse_face_deferred_gc
	  || f == dpyinfo->mouse_face_mouse_frame)
	{
	  BLOCK_INPUT;
	  GET_FRAME_HDC (f);

	  if (dpyinfo->mouse_face_mouse_frame)
	    note_mouse_highlight (dpyinfo->mouse_face_mouse_frame,
				  dpyinfo->mouse_face_mouse_x,
				  dpyinfo->mouse_face_mouse_y);
	  dpyinfo->mouse_face_deferred_gc = 0;

	  RELEASE_FRAME_HDC (f);
	  UNBLOCK_INPUT;
	}
    }
}


/* Draw truncation mark bitmaps, continuation mark bitmaps, overlay
   arrow bitmaps, or clear the areas where they would be displayed
   before DESIRED_ROW is made current.  The window being updated is
   found in updated_window.  This function It is called from
   update_window_line only if it is known that there are differences
   between bitmaps to be drawn between current row and DESIRED_ROW.  */

static void
mw32i_after_update_window_line (struct glyph_row *desired_row)
{
  struct window *w = updated_window;
  struct frame *f;
  int width, height;

  xassert (w);

  if (!desired_row->mode_line_p && !w->pseudo_window_p)
    desired_row->redraw_fringe_bitmaps_p = 1;

  /* When a window has disappeared, make sure that no rest of
     full-width rows stays visible in the internal border.  Could
     check here if updated_window is the leftmost/rightmost window,
     but I guess it's not worth doing since vertically split windows
     are almost never used, internal border is rarely set, and the
     overhead is very small.  */
  if (windows_or_buffers_changed
      && desired_row->full_width_p
      && (f = XFRAME (w->frame),
	  width = FRAME_INTERNAL_BORDER_WIDTH (f),
	  width != 0)
      && (height = desired_row->visible_height,
	  height > 0))
    {
      BLOCK_INPUT;
      {
	int y = WINDOW_TO_FRAME_PIXEL_Y (w, max (0, desired_row->y));

	mw32_clear_native_frame_area (f, 0, y, 0 + width, y + height);
	mw32_clear_native_frame_area (f, FRAME_PIXEL_WIDTH (f) - width, y,
				      FRAME_PIXEL_WIDTH (f), y + height);
      }
      /* If we should require GdiFlush(),
	 insert here.  */
      UNBLOCK_INPUT;
    }
}

/* Draw the bitmap WHICH in one of the left or right fringes of
   window W.  ROW is the glyph row for which to display the bitmap; it
   determines the vertical position at which the bitmap has to be
   drawn.  */

static void
mw32_draw_fringe_bitmap (struct window *w,
			 struct glyph_row *row,
			 struct draw_fringe_bitmap_params *p)
{
  struct frame *f = XFRAME (WINDOW_FRAME (w));
  HDC hdc;
  struct face *face = p->face;
  int rowY;

  hdc = GET_FRAME_HDC (f);
  SaveDC (hdc);
  /* Must clip because of partially visible lines.  */
  rowY = WINDOW_TO_FRAME_PIXEL_Y (w, row->y);
  if (p->y < rowY)
    {
      /* Adjust position of "bottom aligned" bitmap on partially
	 visible last row.  */
      int oldY = row->y;
      int oldVH = row->visible_height;
      row->visible_height = p->h;
      row->y -= rowY - p->y;
      mw32_clip_to_row (hdc, w, row, 1);
      row->y = oldY;
      row->visible_height = oldVH;
    }
  else
    mw32_clip_to_row (hdc, w, row, 1);

  if (p->bx >= 0 && !p->overlay_p)
    {
      mw32_fill_area_pix (f, face->background,
			  p->bx, p->by, p->bx + p->nx, p->by + p->ny);
    }

  if (p->which)
    {
      HBITMAP pixmap = fringe_bmp[p->which];
      HDC compat_hdc;
      HANDLE horig_obj;

      compat_hdc = CreateCompatibleDC (hdc);

      horig_obj = SelectObject (compat_hdc, pixmap);

      /* Paint overlays transparently.  */
      if (p->overlay_p)
	{
	  HBRUSH h_brush, h_orig_brush;

	  SetTextColor (hdc, BLACK_PIX_DEFAULT (f));
	  SetBkColor (hdc, WHITE_PIX_DEFAULT (f));
	  h_brush = CreateSolidBrush (face->foreground);
	  h_orig_brush = SelectObject (hdc, h_brush);

	  BitBlt (hdc, p->x, p->y, p->wd, p->h,
		  compat_hdc, 0, p->dh,
		  DSTINVERT);
	  BitBlt (hdc, p->x, p->y, p->wd, p->h,
		  compat_hdc, 0, p->dh,
		  0x2E064A);
	  BitBlt (hdc, p->x, p->y, p->wd, p->h,
		  compat_hdc, 0, p->dh,
		  DSTINVERT);

	  SelectObject (hdc, h_orig_brush);
	  DeleteObject (h_brush);
	}
      else
	{
	  SetTextColor (hdc, face->background);
	  SetBkColor (hdc, (p->cursor_p
			    ? f->output_data.mw32->cursor_pixel
			    : face->foreground));

	  BitBlt (hdc, p->x, p->y, p->wd, p->h,
		  compat_hdc, 0, p->dh,
		  SRCCOPY);
	}

      SelectObject (compat_hdc, horig_obj);
      DeleteDC (compat_hdc);
    }
  RestoreDC (hdc, -1);

  RELEASE_FRAME_HDC (f);
}

static void
mw32_define_fringe_bitmap (int which, unsigned short *bits, int h, int wd)
{
  fringe_bmp[which] = CreateBitmap (wd, h, 1, 1, bits);
}

static void
mw32_destroy_fringe_bitmap (int which)
{
  if (fringe_bmp[which])
    DeleteObject (fringe_bmp[which]);
  fringe_bmp[which] = 0;
}


/***********************************************************************
			  Line Highlighting
 ***********************************************************************/

/* This is called when starting Emacs and when restarting after
   suspend.  When starting Emacs, no X window is mapped.  And nothing
   must be done to Emacs's own window if it is suspended (though that
   rarely happens).  */

static void
MW32_set_terminal_modes ()
{
}

/* This is called when exiting or suspending Emacs.  Exiting will make
   the X-windows go away, and suspending requires no action.  */

static void
MW32_reset_terminal_modes ()
{
}



/***********************************************************************
			   Display Iterator
 ***********************************************************************/

/* Function prototypes of this page.  */

struct face *get_glyph_face_and_encoding P_ ((struct frame *, struct glyph *,
					      XChar2b *, int *));
int fill_composite_glyph_string P_ ((struct glyph_string *,
				     struct face **, int));
static int mw32_encode_char P_ ((int, XChar2b *, struct font_info *, int *));

XCharStruct *
mw32_per_char_metric (XFontStruct *font, XChar2b *char2b, int font_type)
{
  return MW32_INVOKE_METRICPROC (font, char2b, 0);
}

/* Encode CHAR2B using encoding information from FONT_INFO.  CHAR2B is
   the two-byte form of C.  Encoding is returned in *CHAR2B.  */

static int
mw32_encode_char (int c, XChar2b *char2b, struct font_info *font_info,
		  int *two_byte_p)
{
  int len = 0;
  int charset = CHAR_CHARSET (c);
  int c1, c2;
  MW32LogicalFont *plf = MW32_FONT_FROM_FONT_INFO (font_info);
  int charset_dimension;

  *char2b = 0;
  SPLIT_CHAR (c, charset, c1, c2);
  charset_dimension = CHARSET_DIMENSION (charset);

  /* We have to change code points in the following cases.  */
  if (font_info->font_encoder)
    {
      /* This font requires CCL program to calculate code
	 point of characters.  */
      struct ccl_program *ccl = font_info->font_encoder;
      int i;

      check_ccl_update (ccl);
      len = plf->encoding.font_unit_byte;

      if (charset_dimension == 1)
	{
	  ccl->reg[0] = charset;
	  ccl->reg[1] = c1;
	  ccl->reg[2] = -1;
	  ccl_driver (ccl, NULL, NULL, 0, 0, NULL);
	  for (i = 1; i <= len; i++)
	    *char2b = (ccl->reg[i] | (*char2b << 8));
	}
      else
	{
	  ccl->reg[0] = charset;
	  ccl->reg[1] = c1, ccl->reg[2] = c2;
	  ccl_driver (ccl, NULL, NULL, 0, 0, NULL);
	  for (i = 1; i <= len; i++)
	    *char2b = (ccl->reg[i] | (*char2b << 8));
	}
    }
  else
    {
      switch (plf->encoding.type)
	{
	case ENCODING_DIMENSION:
	  if (charset_dimension == 1)
	    *char2b = MAKEFONTCP (0, c1);
	  else
	    *char2b = MAKEFONTCP (c1, c2);
	  break;

	case ENCODING_BYTE1MSB1:
	  *char2b = MAKEFONTCP (0, (c1 | 0x80));
	  break;

	case ENCODING_BYTE2MSB11:
	  *char2b = MAKEFONTCP ((c1 | 0x80), (c2 | 0x80));
	  break;

	case ENCODING_SHIFTJIS:
	  if (charset_dimension == 2)
	    {
	      int s1, s2;
	      ENCODE_SJIS (c1, c2, s1, s2);
	      *char2b = MAKEFONTCP (s1, s2);
	    }
	  else
	    {
	      *char2b = MAKEFONTCP (0, (c1 | 0x80));
	    }
	  break;

	case ENCODING_UNICODE:
	  if (charset_dimension == 2)
	    {
	      *char2b = ((c1 - 32) * CHARSET_CHARS (charset)
			 + (c2 - 32));
	    }
	  else
	    {
	      *char2b = MAKEFONTCP (0, c1 - 32);
	    }
	  break;

	default:
	  abort ();
	}
    }
  if (two_byte_p)
    *two_byte_p = (*char2b > 0xFF);

  return 0;
}


/***********************************************************************
			    Glyph display
 ***********************************************************************/
static void mw32_insert_glyphs P_ ((struct glyph *, int));
static void mw32_clear_end_of_line P_ ((int));
static void mw32_set_glyph_string_clipping P_ ((struct glyph_string *));
static void mw32_draw_glyph_string_background P_ ((struct glyph_string *,
						int));
static void mw32_draw_glyph_string_foreground P_ ((struct glyph_string *));
static void mw32_draw_glyph_string_box P_ ((struct glyph_string *));
static void mw32_draw_glyph_string  P_ ((struct glyph_string *));
static PIX_TYPE mw32_get_lighter_color P_ ((struct frame *f, PIX_TYPE base,
					    double factor, int delta));
static void mw32_setup_relief_color P_ ((struct frame *, struct relief *,
					 double, int, unsigned long));
static void mw32_setup_relief_colors P_ ((struct glyph_string *));
static void mw32_draw_image_glyph_string P_ ((struct glyph_string *));
static void mw32_draw_image_relief P_ ((struct glyph_string *));
static void mw32_draw_image_foreground P_ ((struct glyph_string *));
static void mw32_clear_glyph_string_rect P_ ((struct glyph_string *, int,
					      int, int, int));
static void mw32_draw_relief_rect P_ ((struct frame *, int, int, int, int,
				       int, int, int, int, int, int, HRGN));
static void mw32_draw_box_rect P_ ((struct glyph_string *, int, int, int, int,
				    int, int, int, HRGN));
#if GLYPH_DEBUG
static void mw32_check_font P_ ((struct frame *, MW32LogicalFont *));
#endif

static void
mw32_generic_setup_face_hdc (struct glyph_string *s)
{
  SetTextColor (s->hdc, s->face->foreground);
  SetBkColor (s->hdc, s->face->background);
}

/* Set S->gc to a suitable GC for drawing glyph string S in cursor
   face.  */

static void
mw32_setup_cursor_hdc (struct glyph_string *s)
{
  COLORREF fg, bg;
  bg = s->f->output_data.mw32->cursor_pixel;
  fg = s->face->background;
  if (fg == bg)
    fg = s->face->foreground;
  if (fg == bg)
    fg = s->f->output_data.mw32->cursor_foreground_pixel;
  if (fg == bg)
    fg = s->face->foreground;
  /* Make sure the cursor is distinct from text in this face.  */
  if (bg == s->face->background
      && fg == s->face->foreground)
    {
      bg = s->face->foreground;
      fg = s->face->background;
    }

  SetTextColor (s->hdc, fg);
  SetBkColor (s->hdc, bg);
}


/* Set up S->gc of glyph string S for drawing text in mouse face.  */

static mw32_setup_mouse_face_hdc (struct glyph_string *s)
{
  int face_id;
  struct face *face;

  /* What face has to be used last for the mouse face?  */
  face_id = FRAME_MW32_DISPLAY_INFO (s->f)->mouse_face_face_id;
  face = FACE_FROM_ID (s->f, face_id);
  if (face == NULL)
    face = FACE_FROM_ID (s->f, MOUSE_FACE_ID);

  if (s->first_glyph->type == CHAR_GLYPH)
    face_id = FACE_FOR_CHAR (s->f, face, s->first_glyph->u.ch);
  else
    face_id = FACE_FOR_CHAR (s->f, face, 0);
  s->face = FACE_FROM_ID (s->f, face_id);
  PREPARE_FACE_FOR_DISPLAY (s->f, s->face);

  mw32_generic_setup_face_hdc (s);
}



static void
mw32_write_glyphs (struct glyph *start, int len)
{
  x_write_glyphs (start, len);
}

static void
mw32_insert_glyphs (struct glyph *start, int len)
{
  x_insert_glyphs (start, len);
}

static void
mw32_clear_end_of_line (int to_x)
{
  x_clear_end_of_line (to_x);
}

/* Set S->gc of glyph string S to a GC suitable for drawing a mode line.
   Faces to use in the mode line have already been computed when the
   matrix was built, so there isn't much to do, here.  */

static INLINE void
mw32_setup_mode_line_face_hdc (struct glyph_string *s)
{
  mw32_generic_setup_face_hdc (s);
}


/* Set S->gc of glyph string S for drawing that glyph string.  Set
   S->stippled_p to a non-zero value if the face of S has a stipple
   pattern.  */

static INLINE void
mw32_setup_glyph_string_hdc (struct glyph_string *s)
{
  PREPARE_FACE_FOR_DISPLAY (s->f, s->face);

  if (s->hl == DRAW_NORMAL_TEXT)
    {
      mw32_generic_setup_face_hdc (s);
      s->stippled_p = (s->face->stipple != 0);
    }
  else if (s->hl == DRAW_INVERSE_VIDEO)
    {
      mw32_setup_mode_line_face_hdc (s);
      s->stippled_p = s->face->stipple != 0;
    }
  else if (s->hl == DRAW_CURSOR)
    {
      mw32_setup_cursor_hdc (s);
      s->stippled_p = 0;
    }
  else if (s->hl == DRAW_MOUSE_FACE)
    {
      mw32_setup_mouse_face_hdc (s);
      s->stippled_p = s->face->stipple != 0;
    }
  else if (s->hl == DRAW_IMAGE_RAISED
	   || s->hl == DRAW_IMAGE_SUNKEN)
    {
      mw32_generic_setup_face_hdc (s);
      s->stippled_p = s->face->stipple != 0;
    }
  else
    {
      s->stippled_p = s->face->stipple != 0;
    }
}


/* Set clipping for output of glyph string S.  S may be part of a mode
   line or menu if we don't have X toolkit support.  */

static INLINE void
mw32_set_glyph_string_clipping (struct glyph_string *s)
{
  NativeRectangle r;
  HRGN region;
  get_glyph_string_clip_rect (s, &r);
  region = CreateRectRgnIndirect (&r);
  SelectClipRgn (s->hdc, region);
  DeleteObject (region);
}

/* Fill rectangle X, Y, W, H with background color of glyph string S.  */

static INLINE void
mw32_clear_glyph_string_rect (struct glyph_string *s,
			      int x, int y, int w, int h)
{
  PIX_TYPE pix;
  pix = GetBkColor (s->hdc);
  mw32_fill_area_pix (s->f, pix, x, y, x + w, y + h);
}


/* Draw the background of glyph_string S.  If S->background_filled_p
   is non-zero don't draw it.  FORCE_P non-zero means draw the
   background even if it wouldn't be drawn normally.  This is used
   when a string preceding S draws into the background of S, or S
   contains the first component of a composition.  */

static void
mw32_draw_glyph_string_background (struct glyph_string *s,
				   int force_p)
{
  /* Nothing to do if background has already been drawn or if it
     shouldn't be drawn in the first place.  */
  if (!s->background_filled_p)
    {
      int box_line_width = max (s->face->box_line_width, 0);

#if 0
      if (s->stippled_p)
	{
	  /* TODO: stipple support */
	  s->background_filled_p = 1;
	}
#endif
      if (FONT_HEIGHT (s->font) < s->height - 2 * box_line_width
	  || s->font_not_found_p
	  || s->extends_to_end_of_line_p
	  || force_p)
	{
	  mw32_clear_glyph_string_rect (s, s->x, s->y + box_line_width,
					s->background_width,
					s->height - 2 * box_line_width);
	  s->background_filled_p = 1;
	}
    }
}


/* Draw the foreground of glyph string S.  */

static void
mw32_draw_glyph_string_foreground (struct glyph_string *s)
{
  int i, x;

  /* If first glyph of S has a left box line, start drawing the text
     of S to the right of that box line.  */
  if (s->face->box != FACE_NO_BOX
      && s->first_glyph->left_box_line_p)
    x = s->x + abs (s->face->box_line_width);
  else
    x = s->x;

  /* Draw characters of S as rectangles if S's font could not be
     loaded.  */
  if (s->font_not_found_p)
    {
      for (i = 0; i < s->nchars; ++i)
	{
	  struct glyph *g = s->first_glyph + i;
	  mw32_draw_rectangle (s->hdc, x, s->y, g->pixel_width - 1,
			       s->height - 1);
	  x += g->pixel_width;
	}
    }
  else
    {
      RECT r;
      int boff = s->font_info->baseline_offset;
      char *char1b = (char *) s->char2b;
      int charset_dim = s->two_byte_p ? 2 : 1;
      int *pdx = (int*) alloca (s->nchars * sizeof (int) * MAXBYTES1FCP);
      int *p = pdx;
      int option;

      if (s->font_info->vertical_centering)
	boff = VCENTER_BASELINE_OFFSET (s->font, s->f) - boff;

      r.left = s->x;
      r.top = s->y;
      r.right = s->x + s->width;
      r.bottom = s->y + s->height;

      /* If we can use 8-bit functions, condense S->char2b.  */
      if (!s->two_byte_p)
	for (i = 0; i < s->nchars; ++i)
	  *char1b++ = XCHAR2B_BYTE2 (&s->char2b[i]);
      else
	for (i = 0; i < s->nchars; ++i)
	  {
	    int c = s->char2b[i];
	    *char1b++ = XCHAR2B_BYTE1 (&c);
	    *char1b++ = XCHAR2B_BYTE2 (&c);
	  }

      memset (pdx, 0, (s->nchars * sizeof (int) * MAXBYTES1FCP));
      for (i = 0; i < s->nchars; ++i)
	{
	  struct glyph *g = s->first_glyph + i;
	  *p = g->pixel_width;
	  /* Strip off extra width of a box line */
	  if (i == 0
	      && s->face->box != FACE_NO_BOX
	      && s->first_glyph->left_box_line_p)
	    *p -= abs (s->face->box_line_width);
	  p += charset_dim;
	}

      /* Draw text.  If background has already been filled, don't fill
	 background.  But when drawing the cursor, always fill background
	 bacause there is no chance that characters under a box cursor
	 are invisible.  */
      if (s->for_overlaps
	  || (s->background_filled_p && s->hl != DRAW_CURSOR))
	option = 0;
      else
	option = ETO_OPAQUE;

      MW32_INVOKE_OUTPUTPROC (s->font, s->hdc, (unsigned char *) s->char2b,
			      x, s->ybase - boff,
			      s->nchars * charset_dim, pdx, &r, option);
    }
}

/* Draw the foreground of composite glyph string S.  */

static void
mw32_draw_composite_glyph_string_foreground (struct glyph_string *s)
{
  int i, x;

  /* If first glyph of S has a left box line, start drawing the text
     of S to the right of that box line.  */
  if (s->face->box != FACE_NO_BOX
      && s->first_glyph->left_box_line_p)
    x = s->x + abs (s->face->box_line_width);
  else
    x = s->x;

  /* S is a glyph string for a composition.  S->gidx is the index of
     the first character drawn for glyphs of this composition.
     S->gidx == 0 means we are drawing the very first character of
     this composition.  */

  /* Draw a rectangle for the composition if the font for the very
     first character of the composition could not be loaded.  */
  if (s->font_not_found_p)
    {
      if (s->gidx == 0)
	mw32_draw_rectangle (s->hdc, x, s->y, s->width - 1, s->height - 1);
    }
  else
    {
      unsigned char str[MAXBYTES1FCP];
      unsigned char *pstr;
      RECT r;

      r.left = s->x;
      r.top = s->y;
      r.right = s->x + s->width;
      r.bottom = s->y + s->height;
      for (i = 0; i < s->nchars; i++, ++s->gidx)
	{
	  pstr = str;
	  SERIALIZE_FONTCP (pstr, (s->char2b)[i]);


	  MW32_INVOKE_OUTPUTPROC (s->font, s->hdc, str,
				  x + s->cmp->offsets[s->gidx * 2],
				  s->ybase - s->cmp->offsets[s->gidx * 2 + 1],
				  pstr - str, 0, &r, 0);
	}
    }
}

#if 0 /* We should create color management system and move it to
	 mw32color.c later. */

/* Value is an array of XColor structures for the contents of the
   color map of display DPY.  Set *NCELLS to the size of the array.
   Note that this probably shouldn't be called for large color maps,
   say a 24-bit TrueColor map.  */

static const XColor *
x_color_cells (dpy, ncells)
     Display *dpy;
     int *ncells;
{
  struct x_display_info *dpyinfo = x_display_info_for_display (dpy);

  if (dpyinfo->color_cells == NULL)
    {
      Screen *screen = dpyinfo->screen;
      int i;

      dpyinfo->ncolor_cells
	= XDisplayCells (dpy, XScreenNumberOfScreen (screen));
      dpyinfo->color_cells
	= (XColor *) xmalloc (dpyinfo->ncolor_cells
			      * sizeof *dpyinfo->color_cells);

      for (i = 0; i < dpyinfo->ncolor_cells; ++i)
	dpyinfo->color_cells[i].pixel = i;

      XQueryColors (dpy, dpyinfo->cmap,
		    dpyinfo->color_cells, dpyinfo->ncolor_cells);
    }

  *ncells = dpyinfo->ncolor_cells;
  return dpyinfo->color_cells;
}


/* On frame F, translate pixel colors to RGB values for the NCOLORS
   colors in COLORS.  Use cached information, if available.  */

void
x_query_colors (f, colors, ncolors)
     struct frame *f;
     XColor *colors;
     int ncolors;
{
  struct x_display_info *dpyinfo = FRAME_X_DISPLAY_INFO (f);

  if (dpyinfo->color_cells)
    {
      int i;
      for (i = 0; i < ncolors; ++i)
	{
	  unsigned long pixel = colors[i].pixel;
	  xassert (pixel < dpyinfo->ncolor_cells);
	  xassert (dpyinfo->color_cells[pixel].pixel == pixel);
	  colors[i] = dpyinfo->color_cells[pixel];
	}
    }
  else
    XQueryColors (FRAME_X_DISPLAY (f), FRAME_X_COLORMAP (f), colors, ncolors);
}


/* On frame F, translate pixel color to RGB values for the color in
   COLOR.  Use cached information, if available.  */

void
x_query_color (f, color)
     struct frame *f;
     XColor *color;
{
  x_query_colors (f, color, 1);
}


/* Allocate the color COLOR->pixel on DISPLAY, colormap CMAP.  If an
   exact match can't be allocated, try the nearest color available.
   Value is non-zero if successful.  Set *COLOR to the color
   allocated.  */

static int
x_alloc_nearest_color_1 (dpy, cmap, color)
     Display *dpy;
     Colormap cmap;
     XColor *color;
{
  int rc;

  rc = XAllocColor (dpy, cmap, color);
  if (rc == 0)
    {
      /* If we got to this point, the colormap is full, so we're going
	 to try to get the next closest color.  The algorithm used is
	 a least-squares matching, which is what X uses for closest
	 color matching with StaticColor visuals.  */
      int nearest, i;
      unsigned long nearest_delta = ~0;
      int ncells;
      const XColor *cells = x_color_cells (dpy, &ncells);

      for (nearest = i = 0; i < ncells; ++i)
	{
	  long dred   = (color->red   >> 8) - (cells[i].red   >> 8);
	  long dgreen = (color->green >> 8) - (cells[i].green >> 8);
	  long dblue  = (color->blue  >> 8) - (cells[i].blue  >> 8);
	  unsigned long delta = dred * dred + dgreen * dgreen + dblue * dblue;

	  if (delta < nearest_delta)
	    {
	      nearest = i;
	      nearest_delta = delta;
	    }
	}

      color->red   = cells[nearest].red;
      color->green = cells[nearest].green;
      color->blue  = cells[nearest].blue;
      rc = XAllocColor (dpy, cmap, color);
    }
  else
    {
      /* If allocation succeeded, and the allocated pixel color is not
         equal to a cached pixel color recorded earlier, there was a
         change in the colormap, so clear the color cache.  */
      struct x_display_info *dpyinfo = x_display_info_for_display (dpy);
      XColor *cached_color;

      if (dpyinfo->color_cells
	  && (cached_color = &dpyinfo->color_cells[color->pixel],
	      (cached_color->red != color->red
	       || cached_color->blue != color->blue
	       || cached_color->green != color->green)))
	{
	  xfree (dpyinfo->color_cells);
	  dpyinfo->color_cells = NULL;
	  dpyinfo->ncolor_cells = 0;
	}
    }

#ifdef DEBUG_X_COLORS
  if (rc)
    register_color (color->pixel);
#endif /* DEBUG_X_COLORS */

  return rc;
}


/* Allocate the color COLOR->pixel on frame F, colormap CMAP.  If an
   exact match can't be allocated, try the nearest color available.
   Value is non-zero if successful.  Set *COLOR to the color
   allocated.  */

int
x_alloc_nearest_color (f, cmap, color)
     struct frame *f;
     Colormap cmap;
     XColor *color;
{
  gamma_correct (f, color);
  return x_alloc_nearest_color_1 (FRAME_X_DISPLAY (f), cmap, color);
}


/* Allocate color PIXEL on frame F.  PIXEL must already be allocated.
   It's necessary to do this instead of just using PIXEL directly to
   get color reference counts right.  */

unsigned long
x_copy_color (f, pixel)
     struct frame *f;
     unsigned long pixel;
{
  XColor color;

  color.pixel = pixel;
  BLOCK_INPUT;
  x_query_color (f, &color);
  XAllocColor (FRAME_X_DISPLAY (f), FRAME_X_COLORMAP (f), &color);
  UNBLOCK_INPUT;
#ifdef DEBUG_X_COLORS
  register_color (pixel);
#endif
  return color.pixel;
}


/* Allocate color PIXEL on display DPY.  PIXEL must already be allocated.
   It's necessary to do this instead of just using PIXEL directly to
   get color reference counts right.  */

unsigned long
x_copy_dpy_color (dpy, cmap, pixel)
     Display *dpy;
     Colormap cmap;
     unsigned long pixel;
{
  XColor color;

  color.pixel = pixel;
  BLOCK_INPUT;
  XQueryColor (dpy, cmap, &color);
  XAllocColor (dpy, cmap, &color);
  UNBLOCK_INPUT;
#ifdef DEBUG_X_COLORS
  register_color (pixel);
#endif
  return color.pixel;
}

#endif /* #if 0 */


/* Brightness beyond which a color won't have its highlight brightness
   boosted.

   Nominally, highlight colors for `3d' faces are calculated by
   brightening an object's color by a constant scale factor, but this
   doesn't yield good results for dark colors, so for colors who's
   brightness is less than this value (on a scale of 0-65535) have an
   use an additional additive factor.

   The value here is set so that the default menu-bar/mode-line color
   (grey75) will not have its highlights changed at all.  */
#define HIGHLIGHT_COLOR_DARK_BOOST_LIMIT 190


/* Obtain a color which is lighter or darker than *PIXEL by FACTOR
   or DELTA.  Try a color with RGB values multiplied by FACTOR first.
   If this produces the same color as PIXEL, try a color where all RGB
   values have DELTA added.  Return the allocated color in *PIXEL.
   DISPLAY is the X display, CMAP is the colormap to operate on.
   Value is non-zero if successful.  */

static PIX_TYPE
mw32_get_lighter_color (struct frame *f, PIX_TYPE base,
			double factor, int delta)
{
  PIX_TYPE new;
  int red, green, blue;
  long bright;
  int success_p;
  HDC hdc;

  /* Change RGB values by specified FACTOR.  Avoid overflow!  */
  red = GetRValue (base);
  green = GetGValue (base);
  blue = GetBValue (base);

  xassert (factor >= 0);
  red = min (0xff, (int)(factor * red));
  green = min (0xff, (int)(factor * green));
  blue = min (0xff, (int)(factor * blue));

  /* Calculate brightness of COLOR.  */
  bright = (2 * red + 3 * green + blue) / 6;

  /* We only boost colors that are darker than
     HIGHLIGHT_COLOR_DARK_BOOST_LIMIT.  */
  if (bright < HIGHLIGHT_COLOR_DARK_BOOST_LIMIT)
    /* Make an additive adjustment to NEW, because it's dark enough so
       that scaling by FACTOR alone isn't enough.  */
    {
      /* How far below the limit this color is (0 - 1, 1 being darker).  */
      double dimness = 1 - (double)bright / HIGHLIGHT_COLOR_DARK_BOOST_LIMIT;
      /* The additive adjustment.  */
      int min_delta = (int)(delta * dimness * factor / 2.0);

      if (factor < 1)
	{
	  red =   max (0, red -   min_delta);
	  green = max (0, green - min_delta);
	  blue =  max (0, blue -  min_delta);
	}
      else
	{
	  red =   min (0xff, min_delta + red);
	  green = min (0xff, min_delta + green);
	  blue =  min (0xff, min_delta + blue);
	}
    }
  new = RGB (red, green, blue);
  hdc = GET_FRAME_HDC (f);
  new = GetNearestColor (hdc, new);
  if (new == CLR_INVALID)
    {
      red = min (0xff, delta + red);
      green = min (0xff, delta + green);
      blue = min (0xff, delta + blue);
      new = RGB (red, green, blue);
      new = GetNearestColor (hdc, new);
    }
  RELEASE_FRAME_HDC (f);

  return new;
}


/* Set up the foreground color for drawing relief lines of glyph
   string S.  RELIEF is a pointer to a struct relief containing the GC
   with which lines will be drawn.  Use a color that is FACTOR or
   DELTA lighter or darker than the relief's background which is found
   in S->f->output_data.mw32->relief_background.  If such a color cannot
   be allocated, use DEFAULT_PIXEL, instead.  */

static void
mw32_setup_relief_color (struct frame *f,
			 struct relief *relief,
			 double factor,
			 int delta,
			 PIX_TYPE default_pixel)
{
  struct mw32_output *di = f->output_data.mw32;
  PIX_TYPE background = di->relief_background;
  PIX_TYPE pixel;

  /* Free previously allocated color.  The color cell will be reused
     when it has been freed as many times as it was allocated, so this
     doesn't affect faces using the same colors.  */
  if (relief->allocated_p)
    {
      /* TODO: Do nothing because we have not
	 supported color pallete yet. */
      relief->allocated_p = 0;
    }

  /* Get ligher color.  */
  pixel = background;
  if (pixel = mw32_get_lighter_color (f, pixel, factor, delta))
    relief->foreground = pixel;
  else
    relief->foreground = default_pixel;

  relief->allocated_p = 1;
}


/* Set up colors for the relief lines around glyph string S.  */

static void
mw32_setup_relief_colors (struct glyph_string *s)
{
  struct mw32_output *di = s->f->output_data.mw32;
  PIX_TYPE color;

  if (s->face->use_box_color_for_shadows_p)
    color = s->face->box_color;
  else
    color = GetBkColor (s->hdc);

  if (!di->white_relief.allocated_p
      || color != di->relief_background)
    {
      di->relief_background = color;
      mw32_setup_relief_color (s->f, &di->white_relief, 1.2, 0x80,
			       WHITE_PIX_DEFAULT (s->f));
      mw32_setup_relief_color (s->f, &di->black_relief, 0.6, 0x40,
			       BLACK_PIX_DEFAULT (s->f));
    }
}


/* Draw a relief on frame F inside the rectangle given by LEFT_X,
   TOP_Y, RIGHT_X, and BOTTOM_Y.  WIDTH is the thickness of the relief
   to draw, it must be >= 0.  RAISED_P non-zero means draw a raised
   relief.  LEFT_P non-zero means draw a relief on the left side of
   the rectangle.  RIGHT_P non-zero means draw a relief on the right
   side of the rectangle.  CLIP_RECT is the clipping rectangle to use
   when drawing.  */

/* MW32 implementation.

(left_x, top_y)               width
  +-----------------------------^-----------------------------+
  | +---------------------------v----------------------------/|
  | |   Call Polygon() for upper-left edge and              | |
  < >  width                                          width < >
  | |                      lower-right edge.                | |
  |/ -------------------------------------------------------+ |
  +-----------------------------------------------------------+
                                                   (right_x, bottom_y)
*/

static void
mw32_draw_relief_rect (struct frame *f,
		       int left_x, int top_y, int right_x, int bottom_y,
		       int width, int raised_p, int top_p, int bot_p,
		       int left_p, int right_p, HRGN clip_rgn)
{
  int i;
  HDC hdc;
  struct mw32_output *di = f->output_data.mw32;
  LOGBRUSH logpenbrush;
  HPEN hp1, hp2, hptmp;
  HGDIOBJ hold1, hold2;

  hdc = GET_FRAME_HDC (f);
  logpenbrush.lbStyle = BS_SOLID;
  logpenbrush.lbHatch = 0;

  SaveDC (hdc);
  if (clip_rgn != INVALID_HANDLE_VALUE)
    SelectClipRgn (hdc, clip_rgn);

  logpenbrush.lbColor = di->white_relief.foreground;
  hp1 = ExtCreatePen (PS_COSMETIC | PS_SOLID, 1, &logpenbrush,
		      0, NULL);
  logpenbrush.lbColor = di->black_relief.foreground;
  hp2 = ExtCreatePen( PS_COSMETIC | PS_SOLID, 1, &logpenbrush,
		      0, NULL);

  /* Note that W32 GDI excludes bottom-right line.  */
  bottom_y--;
  right_x--;
  if (width == 1)
    {
      if (!raised_p)
	{
	  hptmp = hp1, hp1 = hp2, hp2 = hptmp;
	}

      hold1 = SelectObject (hdc, hp1);
      if (left_p)
	{
	  MoveToEx (hdc, left_x, bottom_y, NULL);
	  LineTo (hdc, left_x, top_y);
	}
      else
	MoveToEx (hdc, left_x, top_y, NULL);
      /* To draw the point (right_x, top_y), add 1 to the end.  */
      LineTo (hdc, right_x + 1, top_y);

      SelectObject (hdc, hp2);
      MoveToEx (hdc, right_x, top_y + 1, NULL);
      if (right_p)
	LineTo (hdc, right_x, bottom_y);
      else
	MoveToEx (hdc, right_x, bottom_y, NULL);
      /* To draw the point (left_x, bottom_y), sub 1 to the end.  */
      LineTo (hdc, left_x - 1, bottom_y);
      SelectObject (hdc, hold1);
    }
  else if (width > 1)
    {
      HBRUSH hb1, hb2;
      POINT points[4];

      if (raised_p)
	{
	  hb1 = CreateSolidBrush (di->white_relief.foreground);
	  hb2 = CreateSolidBrush (di->black_relief.foreground);
	}
      else
	{
	  hptmp = hp1, hp1 = hp2, hp2 = hptmp;
	  hb2 = CreateSolidBrush (di->white_relief.foreground);
	  hb1 = CreateSolidBrush (di->black_relief.foreground);
	}

      /* Top.  */
      if (top_p)
	{
	  hold1 = SelectObject (hdc, hp1);
	  hold2 = SelectObject (hdc, hb1);

	  points[0].x = left_x, points[0].y = top_y;
	  points[1].x = right_x, points[1].y = top_y;
	  points[2].x = right_x - (width - 1) * right_p,
	    points[2].y = top_y + width - 1;
	  points[3].x = left_x + (width - 1) * left_p,
	    points[3].y = top_y + width - 1;
	  Polygon (hdc, points, 4);
	}

      /* Left.  */
      if (left_p)
	{
	  points[0].x = left_x, points[0].y = top_y;
	  points[1].x = left_x, points[1].y = bottom_y - 1;
	  points[2].x = left_x + width - 1, points[2].y = bottom_y - width;
	  points[3].x = left_x + width - 1, points[3].y = top_y + width - 1;
	  Polygon (hdc, points, 4);
	}

      /* Bottom.  */
      if (bot_p)
	{
	  SelectObject (hdc, hp2);
	  SelectObject (hdc, hb2);

	  points[0].x = left_x, points[0].y = bottom_y;
	  points[1].x = right_x, points[1].y = bottom_y;
	  points[2].x = right_x - (width - 1) * right_p,
	    points[2].y = bottom_y - width + 1;
	  points[3].x = left_x + (width - 1) * left_p,
	    points[3].y = bottom_y - width + 1;
	  Polygon (hdc, points, 4);
	}

      /* Right.  */
      if (right_p)
	{
	  points[0].x = right_x, points[0].y = top_y + 1;
	  points[1].x = right_x, points[1].y = bottom_y - 1;
	  points[2].x = right_x - width + 1, points[2].y = bottom_y - width;
	  points[3].x = right_x - width + 1; points[3].y = top_y + width;
	  Polygon (hdc, points, 4);
	}

      SelectObject (hdc, hold2);
      SelectObject (hdc, hold1);
      DeleteObject (hb1);
      DeleteObject (hb2);
    }
  RestoreDC (hdc, -1);
  DeleteObject (hp1);
  DeleteObject (hp2);
  RELEASE_FRAME_HDC (f);
}

/* Draw a box on frame F inside the rectangle given by LEFT_X, TOP_Y,
   RIGHT_X, and BOTTOM_Y.  WIDTH is the thickness of the lines to
   draw, it must be >= 0.  LEFT_P non-zero means draw a line on the
   left side of the rectangle.  RIGHT_P non-zero means draw a line
   on the right side of the rectangle.  CLIP_RECT is the clipping
   rectangle to use when drawing.  */

static void
mw32_draw_box_rect (struct glyph_string *s, int left_x, int top_y,
		    int right_x, int bottom_y, int width,
		    int left_p, int right_p, HRGN clip_rgn)
{
  HDC hdc = s->hdc;
  RECT r;
  HRGN rr_rgn;
  HBRUSH hb;
  HGDIOBJ hold;

  SaveDC (hdc);
  hb = CreateSolidBrush (s->face->box_color);
  hold = SelectObject (hdc, hb);
  if (clip_rgn != INVALID_HANDLE_VALUE)
    SelectClipRgn (hdc, clip_rgn);

  if (left_p && right_p)
    rr_rgn = CreateRectRgn (left_x + width, top_y + width,
			    right_x - width, bottom_y - width);
  else if (left_p)
    rr_rgn = CreateRectRgn (left_x + width, top_y + width,
			    right_x, bottom_y - width);
  else if (right_p)
    rr_rgn = CreateRectRgn (left_x, top_y + width,
			    right_x - width, bottom_y - width);
  else
    rr_rgn = CreateRectRgn (left_x, top_y + width,
			    right_x, bottom_y - width);
  ExtSelectClipRgn (hdc, rr_rgn, RGN_DIFF);

  r.left = left_x;
  r.top = top_y;
  r.right = right_x;
  r.bottom = bottom_y;
  FillRect (hdc, &r, hb);

  SelectObject (hdc, hold);
  RestoreDC (hdc, -1);
  DeleteObject (hb);
  DeleteObject (rr_rgn);
}


/* Draw a box around glyph string S.  */

static void
mw32_draw_glyph_string_box (struct glyph_string *s)
{
  int width, left_x, right_x, top_y, bottom_y, last_x, raised_p;
  int left_p, right_p;
  struct glyph *last_glyph;
  NativeRectangle clip_rect;
  HRGN hcrr;

  last_x = window_box_right (s->w, s->area);
  if (s->row->full_width_p
      && !s->w->pseudo_window_p)
    {
      last_x += WINDOW_RIGHT_SCROLL_BAR_AREA_WIDTH (s->w);
      if (s->area != RIGHT_MARGIN_AREA
	  || WINDOW_HAS_FRINGES_OUTSIDE_MARGINS (s->w))
	last_x += WINDOW_RIGHT_FRINGE_WIDTH (s->w);
    }

  /* The glyph that may have a right box line.  */
  last_glyph = (s->cmp || s->img
		? s->first_glyph
		: s->first_glyph + s->nchars - 1);

  width = abs (s->face->box_line_width);
  raised_p = s->face->box == FACE_RAISED_BOX;
  left_x = s->x;
  right_x = (s->row->full_width_p && s->extends_to_end_of_line_p
	     ? last_x
	     : min (last_x, s->x + s->background_width));
  top_y = s->y;
  bottom_y = top_y + s->height;

  left_p = (s->first_glyph->left_box_line_p
	    || (s->hl == DRAW_MOUSE_FACE
		&& (s->prev == NULL
		    || s->prev->hl != s->hl)));
  right_p = (last_glyph->right_box_line_p
	     || (s->hl == DRAW_MOUSE_FACE
		 && (s->next == NULL
		     || s->next->hl != s->hl)));

  get_glyph_string_clip_rect (s, &clip_rect);
  hcrr = CreateRectRgnIndirect (&clip_rect);

  if (s->face->box == FACE_SIMPLE_BOX)
    mw32_draw_box_rect (s, left_x, top_y, right_x, bottom_y, width,
			left_p, right_p, hcrr);
  else
    {
      mw32_setup_relief_colors (s);
      mw32_draw_relief_rect (s->f, left_x, top_y, right_x, bottom_y,
			     width, raised_p, 1, 1, left_p, right_p, hcrr);
    }
  DeleteObject (hcrr);
}


int
mw32_setup_image_bitmap_handle (HDC hdc, struct image *img,
				HBITMAP *phbmp, HBITMAP *phbmpmask)
{
  unsigned char *pdata;
  mw32_image* pmimg = &(img->mw32_img);

  *phbmpmask = NULL;
  *phbmp = CreateDIBSection (hdc, pmimg->pbmpinfo, DIB_RGB_COLORS,
			     (void **) &pdata, NULL, 0);
  if (!(*phbmp)) return 0;
  memcpy (pdata, pmimg->pbmpdata, pmimg->size);

  /* prepare bitmap mask. */
  if (pmimg->pbmpmask)
    {
      struct {
	BITMAPINFOHEADER h;
	RGBQUAD c[2];
      } maskinfo;

      memset (&maskinfo, 0, sizeof (maskinfo));
      maskinfo.h.biSize = sizeof (BITMAPINFOHEADER);
      maskinfo.h.biWidth = pmimg->pbmpinfo->bmiHeader.biWidth;
      maskinfo.h.biHeight = pmimg->pbmpinfo->bmiHeader.biHeight;
      maskinfo.h.biPlanes = 1;
      maskinfo.h.biBitCount = 1;
      maskinfo.h.biCompression = BI_RGB;
      maskinfo.c[1].rgbRed = maskinfo.c[1].rgbGreen
	= maskinfo.c[1].rgbBlue = 255;

      *phbmpmask = CreateDIBSection (hdc, (LPBITMAPINFO)&maskinfo,
				     DIB_RGB_COLORS, (void **) &pdata,
				     NULL, 0);
      if (!(*phbmpmask))
	{
	  DeleteObject (*phbmp);
	  *phbmp = 0;
	  return 0;
	}
      memcpy (pdata, pmimg->pbmpmask, pmimg->mask_size);
    }

  return 1;
}

/* Draw foreground of image glyph string S.  */

static void
mw32_draw_image_foreground (struct glyph_string *s)
{
  HBITMAP hbmp, hbmpmask;
  int x = s->x;
  int y = s->ybase - image_ascent (s->img, s->face, &s->slice);

  /* If first glyph of S has a left box line, start drawing it to the
     right of that line.  */
  if (s->face->box != FACE_NO_BOX
      && s->first_glyph->left_box_line_p
      && s->slice.x == 0)
    x += abs (s->face->box_line_width);

  /* If there is a margin around the image, adjust x- and y-position
     by that margin.  */
  if (s->slice.x == 0)
    x += s->img->hmargin;
  if (s->slice.y == 0)
    y += s->img->vmargin;

  if (MW32_IMAGE_VALID_P (s->img->mw32_img)
      && mw32_setup_image_bitmap_handle (s->hdc, s->img, &hbmp, &hbmpmask))
    {
      XRectangle image_rect, clip_rect, r;
      NativeRectangle nr;
      HDC hdcimg = CreateCompatibleDC (s->hdc);

      if (!hdcimg) return;

      get_glyph_string_clip_rect (s, &nr);
      CONVERT_TO_XRECT (clip_rect, nr);

      image_rect.x = x;
      image_rect.y = y;
      image_rect.width = s->img->width;
      image_rect.height = s->img->height;

      if (x_intersect_rectangles (&clip_rect, &image_rect, &r))
	{
	  HGDIOBJ holdbmp = SelectObject (hdcimg, hbmp);
	  /* we ignore s->img->mask.  Use hbmpmask instead.  */
	  if (hbmpmask)
	    {
	      HDC hdc_fg = CreateCompatibleDC (s->hdc);
	      HDC hdc_bg = CreateCompatibleDC (s->hdc);
	      if (hdc_fg && hdc_bg)
		{
		  int w = s->slice.width;
		  int h = s->slice.height;
		  HBITMAP hfg = CreateCompatibleBitmap (s->hdc, w, h);
		  HBITMAP hbg = CreateCompatibleBitmap (s->hdc, w, h);
		  if (hfg && hbg)
		    {
		      HGDIOBJ hfg_old = SelectObject (hdc_fg, hfg);
		      HGDIOBJ hbg_old = SelectObject (hdc_bg, hbg);

		      /* Build up background */
		      BitBlt (hdc_bg, 0, 0, w, h, s->hdc, x, y, SRCCOPY);
		      holdbmp = SelectObject (hdcimg, hbmpmask);
		      BitBlt (hdc_bg, 0, 0, w, h, hdcimg, 0, 0, SRCAND);
		      /* Build up foreground */
		      BitBlt (hdc_fg, 0, 0, w, h, hdcimg, 0, 0, NOTSRCCOPY);
		      SelectObject (hdcimg, hbmp);
		      BitBlt (hdc_fg, 0, 0, w, h, hdcimg, 0, 0, SRCAND);
		      /* Merge background and foreground */
		      BitBlt (hdc_fg, 0, 0, w, h, hdc_bg, 0, 0, SRCPAINT);
		      /* Copy the result to HDC */
		      BitBlt (s->hdc, x, y, w, h, hdc_fg, 0, 0, SRCCOPY);
		      SelectObject (hdc_fg, hfg_old);
		      SelectObject (hdc_bg, hbg_old);
		    }
		  if (hfg) DeleteObject (hfg);
		  if (hbg) DeleteObject (hbg);
		}
	      if (hdc_fg) DeleteDC (hdc_fg);
	      if (hdc_bg) DeleteDC (hdc_bg);
	    }
	  else
	    {
	      BitBlt (s->hdc, x, y, s->slice.width, s->slice.height,
		      hdcimg, 0, 0, SRCCOPY);
	    }
	  SelectObject (hdcimg, holdbmp);
	}
      DeleteDC (hdcimg);
      DeleteObject (hbmp);
      DeleteObject (hbmpmask);

      /* When the image has a mask, we can expect that at
	 least part of a mouse highlight or a block cursor will
	 be visible.  If the image doesn't have a mask, make
	 a block cursor visible by drawing a rectangle around
	 the image.  I believe it's looking better if we do
	 nothing here for mouse-face.  */
      if (s->hl == DRAW_CURSOR)
	{
	  HANDLE hold;
	  HBRUSH hb = GetStockObject (NULL_BRUSH);
	  hold = SelectObject (s->hdc, hb);
	  Rectangle (s->hdc, x, y,
		     x + s->slice.width, y + s->slice.height);
	  SelectObject (s->hdc, hold);
	}
    }
  else
    /* Draw a rectangle if image could not be loaded.  */
    Rectangle (s->hdc, x, y,
	       x + s->slice.width, y + s->slice.height);
}


/* Draw a relief around the image glyph string S.  */

static void
mw32_draw_image_relief (struct glyph_string *s)
{
  int x0, y0, x1, y1, thick, raised_p;
  RECT r;
  HRGN hr;
  int x = s->x;
  int y = s->ybase - image_ascent (s->img, s->face, &s->slice);

  /* If first glyph of S has a left box line, start drawing it to the
     right of that line.  */
  if (s->face->box != FACE_NO_BOX
      && s->first_glyph->left_box_line_p
      && s->slice.x == 0)
    x += abs (s->face->box_line_width);

  /* If there is a margin around the image, adjust x- and y-position
     by that margin.  */
  if (s->slice.x == 0)
    x += s->img->hmargin;
  if (s->slice.y == 0)
    y += s->img->vmargin;

  if (s->hl == DRAW_IMAGE_SUNKEN
      || s->hl == DRAW_IMAGE_RAISED)
    {
      thick = (tool_bar_button_relief >= 0 ? tool_bar_button_relief
	       : DEFAULT_TOOL_BAR_BUTTON_RELIEF);
      raised_p = s->hl == DRAW_IMAGE_RAISED;
    }
  else
    {
      thick = abs (s->img->relief);
      raised_p = s->img->relief > 0;
    }

  x0 = x - thick;
  y0 = y - thick;
  x1 = x + s->slice.width + thick - 1;
  y1 = y + s->slice.height + thick - 1;

  mw32_setup_relief_colors (s);
  get_glyph_string_clip_rect (s, &r);
  hr = CreateRectRgnIndirect (&r);
  mw32_draw_relief_rect (s->f, x0, y0, x1, y1, thick, raised_p,
			 s->slice.y == 0,
			 s->slice.y + s->slice.height == s->img->height,
			 s->slice.x == 0,
			 s->slice.x + s->slice.width == s->img->width,
			 hr);
  DeleteObject (hr);
}

/* Draw part of the background of glyph string S.  X, Y, W, and H
   give the rectangle to draw.  */

static void
mw32_draw_glyph_string_bg_rect (struct glyph_string *s,
				int x, int y, int w, int h)
{
  if (s->stippled_p)
    {
      /* TODO: stipple support */
      mw32_clear_glyph_string_rect (s, x, y, w, h);
    }
  else
    mw32_clear_glyph_string_rect (s, x, y, w, h);
}


/* Draw image glyph string S.

            s->y
   s->x      +-------------------------
	     |   s->face->box
	     |
	     |     +-------------------------
	     |     |  s->img->margin
	     |     |
	     |     |       +-------------------
	     |     |       |  the image

 */

static void
mw32_draw_image_glyph_string (struct glyph_string *s)
{
  int x, y;
  int box_line_hwidth = abs (s->face->box_line_width);
  int box_line_vwidth = max (s->face->box_line_width, 0);
  int height;

  height = s->height - 2 * box_line_vwidth;

  /* Fill background with face under the image.  Do it only if row is
     taller than image or if image has a clip mask to reduce
     flickering.  */
  s->stippled_p = s->face->stipple != 0;
  if (height > s->img->height
      || s->img->hmargin
      || s->img->vmargin
      || MW32_IMAGE_HAS_MASK_P (s->img->mw32_img)
      || !MW32_IMAGE_VALID_P (s->img->mw32_img)
      || s->width != s->background_width)
    {
      if (box_line_hwidth && s->first_glyph->left_box_line_p)
	x = s->x + box_line_hwidth;
      else
	x = s->x;

      y = s->y + box_line_vwidth;

      mw32_draw_glyph_string_bg_rect (s, x, y, s->background_width, height);
      s->background_filled_p = 1;
    }

  mw32_draw_image_foreground (s);

  /* If we must draw a relief around the image, do it.  */
  if (s->img->relief
      || s->hl == DRAW_IMAGE_RAISED
      || s->hl == DRAW_IMAGE_SUNKEN)
    mw32_draw_image_relief (s);
}


/* Draw stretch glyph string S.  */

static void
mw32_draw_stretch_glyph_string (struct glyph_string *s)
{
  xassert (s->first_glyph->type == STRETCH_GLYPH);
  s->stippled_p = s->face->stipple != 0;

  if (s->hl == DRAW_CURSOR
      && !mw32_stretch_cursor_p)
    {
      /* If `x-stretch-block-cursor' is nil, don't draw a block cursor
	 as wide as the stretch glyph.  */
      int width = min (FRAME_COLUMN_WIDTH (s->f), s->background_width);

      /* Draw cursor.  */
      mw32_draw_glyph_string_bg_rect (s, s->x, s->y, width, s->height);

      /* Clear rest using the GC of the original non-cursor face.  */
      if (width < s->background_width)
	{
	  int x = s->x + width, y = s->y;
	  int w = s->background_width - width, h = s->height;
	  RECT r;
	  HRGN hr;

	  if (s->row->mouse_face_p
	      && cursor_in_mouse_face_p (s->w))
	    mw32_setup_mouse_face_hdc (s);

	  get_glyph_string_clip_rect (s, &r);
	  hr = CreateRectRgnIndirect (&r);
	  SaveDC (s->hdc);
	  SelectClipRgn (s->hdc, hr);
	  DeleteObject (hr);

	  if (s->face->stipple)
	    {
	      /* TODO: stipple support */
	      /* Fill background with a stipple pattern.  */
	    }
	  else
	    {
	      mw32_fill_area_pix (s->f, GetTextColor (s->hdc),
				  x, y, x + w, y + h);
	    }
	  RestoreDC (s->hdc, -1);
	}
    }
  else if (!s->background_filled_p)
    mw32_draw_glyph_string_bg_rect (s, s->x, s->y, s->background_width,
				    s->height);

  s->background_filled_p = 1;
}


/* Draw glyph string S.  */

static void
mw32_draw_glyph_string (struct glyph_string *s)
{
  int relief_drawn_p = 0;

  /* If S draws into the background of its successor, draw the
     background of the successor first so that S can draw into it.
     This makes S->next use XDrawString instead of XDrawImageString.  */
  if (s->next && s->right_overhang && !s->for_overlaps)
    {
      xassert (s->next->img == NULL);
      mw32_set_glyph_string_clipping (s->next);
      mw32_draw_glyph_string_background (s->next, 1);
    }

  /* Set up S->gc, set clipping and draw S.  */
  mw32_setup_glyph_string_hdc (s);

  /* Draw relief (if any) in advance for char/composition so that the
     glyph string can be drawn over it.  */
  if (!s->for_overlaps
      && s->face->box != FACE_NO_BOX
      && (s->first_glyph->type == CHAR_GLYPH
	  || s->first_glyph->type == COMPOSITE_GLYPH))

    {
      mw32_set_glyph_string_clipping (s);
      mw32_draw_glyph_string_background (s, 1);
      mw32_draw_glyph_string_box (s);
      relief_drawn_p = 1;
    }
  else
    mw32_set_glyph_string_clipping (s);


  switch (s->first_glyph->type)
    {
    case IMAGE_GLYPH:
      mw32_draw_image_glyph_string (s);
      break;

    case STRETCH_GLYPH:
      mw32_draw_stretch_glyph_string (s);
      break;

    case CHAR_GLYPH:
      if (s->for_overlaps)
	s->background_filled_p = 1;
      else
	mw32_draw_glyph_string_background (s, 0);
      mw32_draw_glyph_string_foreground (s);
      break;

    case COMPOSITE_GLYPH:
      if (s->for_overlaps || s->gidx > 0)
	s->background_filled_p = 1;
      else
	mw32_draw_glyph_string_background (s, 1);
      mw32_draw_composite_glyph_string_foreground (s);
      break;

    default:
      abort ();
    }

  if (!s->for_overlaps)
    {
      /* Draw underline.  */
      if (s->face->underline_p)
	{
	  unsigned long tem, h;
	  int y;

	  /* Always underline thickness is 1 on mw32 implementation. */
	  h = 1;

	  /* Get the underline position.  This is the recommended
	     vertical offset in pixels from the baseline to the top of
	     the underline.  This is a signed value according to the
	     specs, and its default is

	     ROUND ((maximum descent) / 2), with
	     ROUND(x) = floor (x + 0.5)  */

	  /* To make underline straight, the vertical offset has been
	     changed, that should be derived according to the
	     proportion of glyph_row descent. And its default is
	     ROUND ((descent of row) / 2), with
	     descent of row = height of row - ascent of row. */

	  if (s->row)
	    y = s->ybase + (s->row->height - s->row->ascent + 1) / 2;
	  else
	    y = s->y + s->height - h;

	  if (s->face->underline_defaulted_p)
	    mw32_fill_area_pix (s->f, s->face->foreground,
				s->x, y, s->x + s->width, y + h);
	  else
	    mw32_fill_area_pix (s->f, s->face->underline_color,
				s->x, y, s->x + s->width, y + h);
	}

      /* Draw overline.  */
      if (s->face->overline_p)
	{
	  unsigned long dy = 0, h = 1;

	  if (s->face->overline_color_defaulted_p)
	    mw32_fill_area_pix (s->f, s->face->foreground,
				s->x, s->y + dy, s->x + s->width,
				s->y + dy + h);
	  else
	    mw32_fill_area_pix (s->f, s->face->overline_color,
				s->x, s->y + dy, s->x + s->width,
				s->y + dy + h);
	}

      /* Draw strike-through.  */
      if (s->face->strike_through_p)
	{
	  unsigned long h = 1;
	  unsigned long dy = (s->height - h) / 2;

	  if (s->face->strike_through_color_defaulted_p)
	    mw32_fill_area_pix (s->f, s->face->foreground,
				s->x, s->y + dy, s->x + s->width,
				s->y + dy + h);
	  else
	    mw32_fill_area_pix (s->f, s->face->strike_through_color,
				s->x, s->y + dy, s->x + s->width,
				s->y + dy + h);
	}

      /* Draw relief if not yet drawn.  */
      if (!relief_drawn_p && s->face->box != FACE_NO_BOX)
	mw32_draw_glyph_string_box (s);
    }

  /* As for mw32 implementation, we don't have to
     reset clipping because all drawing functions restore
     DC state before exiting whenever setting clip region.  */
}

/* Shift display to make room for inserted glyphs.   */

void
mw32_shift_glyphs_for_insert (struct frame *f, int x, int y,
			      int width, int height, int shift_by)
{
  HDC hdc;
  hdc = GET_FRAME_HDC (f);

  BitBlt (hdc, x + shift_by, y, width, height, hdc, x, y, SRCCOPY);

  RELEASE_FRAME_HDC (f);
}

/* Delete N glyphs at the nominal cursor position.  Not implemented
   for X frames.  */

static void
MW32_delete_glyphs (int n)
{
  abort ();
}

/* Clear entire frame.  If updating_frame is non-null, clear that
   frame.  Otherwise clear the selected frame.  */

static void
MW32_clear_frame ()
{
  struct frame *f;

  if (updating_frame)
    f = updating_frame;
  else
    f = SELECTED_FRAME ();

  /* Clearing the frame will erase any cursor, so mark them all as no
     longer visible.  */
  mark_window_cursors_off (XWINDOW (FRAME_ROOT_WINDOW (f)));
  output_cursor.hpos = output_cursor.vpos = 0;
  output_cursor.x = -1;

  /* We don't set the output cursor here because there will always
     follow an explicit cursor_to.  */
  BLOCK_INPUT;

  {
    RECT rect;
    GetClientRect (FRAME_MW32_WINDOW (f), &rect);
    mw32_clear_native_frame_area (f, rect.left, rect.top,
				  rect.right, rect.bottom);
  }

  UNBLOCK_INPUT;
}




void
mw32_flash (struct frame *f)
{
  SEND_INFORM_MESSAGE (FRAME_MW32_WINDOW (f), WM_EMACS_FLASH_WINDOW,
		       (WPARAM) INVERT_EDGES_OF_FRAME, 0);
}

/* Make audible bell.  */

void
MW32_ring_bell (void)
{
  extern void w32_sys_ring_bell (void);

  if (visible_bell)
    {
      struct frame *f = SELECTED_FRAME ();

      if (EQ (Vmw32_visible_bell_type, intern ("x")))
	mw32_flash (f);
      else
	POST_INFORM_MESSAGE (FRAME_MW32_WINDOW (f),
			     WM_EMACS_FLASH_WINDOW, 0, 0);
    }
  else
    w32_sys_ring_bell ();
}


/* Specify how many text lines, from the top of the window,
   should be affected by insert-lines and delete-lines operations.
   This, and those operations, are used only within an update
   that is bounded by calls to x_update_begin and x_update_end.  */

static void
MW32_set_terminal_window (int n)
{
  /* This function intentionally left blank.  */
}



/***********************************************************************
			      Line Dance
 ***********************************************************************/

/* Perform an insert-lines or delete-lines operation, inserting N
   lines or deleting -N lines at vertical position VPOS.  */

static void
MW32_ins_del_lines (int vpos, int n)
{
  abort ();
}


/* Scroll part of the display as described by RUN.  */

static void
mw32i_scroll_run (struct window *w, struct run *run)
{
  struct frame *f = XFRAME (w->frame);
  int x, y, width, height, from_y, to_y, bottom_y;
  HWND hwnd = FRAME_MW32_WINDOW (f);
  HRGN expect_dirty;

  /* Get frame-relative bounding box of the text display area of W,
     without mode lines.  Include in this box the left and right
     fringes of W.  */
  window_box (w, -1, &x, &y, &width, &height);

  from_y = WINDOW_TO_FRAME_PIXEL_Y (w, run->current_y);
  to_y = WINDOW_TO_FRAME_PIXEL_Y (w, run->desired_y);
  bottom_y = y + height;

  if (to_y < from_y)
    {
      /* Scrolling up.  Make sure we don't copy part of the mode
	 line at the bottom.  */
      if (from_y + run->height > bottom_y)
	height = bottom_y - from_y;
      else
	height = run->height;
      expect_dirty = CreateRectRgn (x, y + height, x + width, bottom_y);
    }
  else
    {
      /* Scolling down.  Make sure we don't copy over the mode line.
	 at the bottom.  */
      if (to_y + run->height > bottom_y)
	height = bottom_y - to_y;
      else
	height = run->height;
      expect_dirty = CreateRectRgn (x, y, x + width, to_y);
    }

  BLOCK_INPUT;

  /* Cursor off.  Will be switched on again in x_update_window_end.  */
  updated_window = w;
  x_clear_cursor (w);

  {
    RECT from;
    RECT to;
    HRGN dirty = CreateRectRgn (0, 0, 0, 0);
    HRGN combined = CreateRectRgn (0, 0, 0, 0);

    from.left = to.left = x;
    from.right = to.right = x + width;
    from.top = from_y;
    from.bottom = from_y + height;
    to.top = y;
    to.bottom = bottom_y;

    ScrollWindowEx (hwnd, 0, to_y - from_y, &from, &to, dirty,
		    NULL, SW_INVALIDATE);

    /* Combine this with what we expect to be dirty. This covers the
       case where not all of the region we expect is actually dirty.  */
    CombineRgn (combined, dirty, expect_dirty, RGN_OR);

    /* If the dirty region is not what we expected, redraw the entire
       frame.  */
#if 0
    /* This causes unexpected "End of buffer".*/
    if (!EqualRgn (combined, expect_dirty))
      SET_FRAME_GARBAGED (f);
#endif
    DeleteObject (dirty);
    DeleteObject (combined);
  }
  DeleteObject (expect_dirty);

  UNBLOCK_INPUT;
}



/***********************************************************************
	    Expose operation (for WM_PAINT message)
 ***********************************************************************/

static void
frame_highlight (f)
     struct frame *f;
{
  /* Do nothing but update the cursor.  */
  x_update_cursor (f, 1);
}

static void
frame_unhighlight (f)
     struct frame *f;
{
  /* Do nothing but update the cursor.  */
  x_update_cursor (f, 1);
}

/* The focus has changed.  Update the frames as necessary to reflect
   the new situation.  Note that we can't change the selected frame
   here, because the Lisp code we are interrupting might become confused.
   Each event gets marked with the frame in which it occurred, so the
   Lisp code can tell when the switch took place by examining the events.  */

/* On MW32 implementation, also set focus_message_frame.  */

void
mw32_new_focus_frame (struct mw32_display_info *dpyinfo,
		      struct frame *frame)
{
  struct frame *old_highlight = dpyinfo->mw32_highlight_frame;

  dpyinfo->mw32_focus_message_frame = frame;

  if (frame != dpyinfo->mw32_focus_frame)
    {
      /* Set this before calling other routines, so that they see
	 the correct value of x_focus_frame.  */
      dpyinfo->mw32_focus_frame = frame;

      /* Only registering.  */
      if (dpyinfo->mw32_focus_frame && dpyinfo->mw32_focus_frame->auto_raise)
	pending_autoraise_frame = dpyinfo->mw32_focus_frame;
      else
	pending_autoraise_frame = 0;
    }

  mw32_frame_rehighlight_1 (dpyinfo);

  /* Update cursor status */
  if (old_highlight)
    POST_THREAD_INFORM_MESSAGE (main_thread_id,
				WM_EMACS_UPDATE_CURSOR,
				(WPARAM)old_highlight, 0);
  if (dpyinfo->mw32_highlight_frame)
    POST_THREAD_INFORM_MESSAGE (main_thread_id,
				WM_EMACS_UPDATE_CURSOR,
				(WPARAM)(dpyinfo->mw32_highlight_frame), 0);
}

/* Handle an event saying the mouse has moved out of an Emacs frame.  */

void
mw32_mouse_leave (struct mw32_display_info *dpyinfo)
{
  mw32_new_focus_frame (dpyinfo, dpyinfo->mw32_focus_message_frame);
}

/* The focus has changed, or we have redirected a frame's focus to
   another frame (this happens when a frame uses a surrogate
   mini-buffer frame).  Shift the highlight as appropriate.

   The FRAME argument doesn't necessarily have anything to do with which
   frame is being highlighted or un-highlighted; we only use it to find
   the appropriate X display info.  */

static void
MW32_frame_rehighlight (struct frame *frame)
{
  mw32_frame_rehighlight_1 (FRAME_MW32_DISPLAY_INFO (frame));
}

static void
mw32_frame_rehighlight_1 (struct mw32_display_info *dpyinfo)
{
  if (dpyinfo->mw32_focus_frame)
    {
      dpyinfo->mw32_highlight_frame
	= ((GC_FRAMEP (FRAME_FOCUS_FRAME (dpyinfo->mw32_focus_frame)))
	   ? XFRAME (FRAME_FOCUS_FRAME (dpyinfo->mw32_focus_frame))
	   : dpyinfo->mw32_focus_frame);
      if (! FRAME_LIVE_P (dpyinfo->mw32_highlight_frame))
	{
	  FRAME_FOCUS_FRAME (dpyinfo->mw32_focus_frame) = Qnil;
	  dpyinfo->mw32_highlight_frame = dpyinfo->mw32_focus_frame;
	}
    }
  else
    dpyinfo->mw32_highlight_frame = 0;
}


/************************************************************************
			Mouse Pointer Tracking
 ************************************************************************/

/* Function to report a mouse movement to the mainstream Emacs code.
   The input handler calls this.

   We have received a mouse movement event, which is given in *event.
   If the mouse is over a different glyph than it was last time, tell
   the mainstream emacs code by setting mouse_moved.  If not, ask for
   another motion event, so we can check again the next time it moves.  */

static void
note_mouse_movement (FRAME_PTR frame, MSG *msg)
{
  if (msg->hwnd != FRAME_MW32_WINDOW (frame))
    {
      frame->mouse_moved = 1;
      last_mouse_scroll_bar = Qnil;
      note_mouse_highlight (frame, -1, -1);
    }

  /* Has the mouse moved off the glyph it was on at the last sighting?  */
  else if (LOWORD (msg->lParam) < last_mouse_glyph.left
	   || LOWORD (msg->lParam) > last_mouse_glyph.right
	   || HIWORD (msg->lParam) < last_mouse_glyph.top
	   || HIWORD (msg->lParam) > last_mouse_glyph.bottom)
    {
      frame->mouse_moved = 1;
      last_mouse_scroll_bar = Qnil;
      note_mouse_highlight (frame, LOWORD (msg->lParam), HIWORD (msg->lParam));
    }
}


/************************************************************************
			      Mouse Face
 ************************************************************************/
static void
redo_mouse_highlight ()
{
  if (!NILP (last_mouse_motion_frame)
      && FRAME_LIVE_P (XFRAME (last_mouse_motion_frame)))
    note_mouse_highlight (XFRAME (last_mouse_motion_frame),
			  LOWORD (last_mouse_motion_message.lParam),
			  HIWORD (last_mouse_motion_message.wParam));
}




/* Try to determine frame pixel position and size of the glyph under
   frame pixel coordinates X/Y on frame F .  Return the position and
   size in *RECT.  Value is non-zero if we could compute these
   values.  */

static int
glyph_rect (struct frame *f, int x, int y, RECT *rect)
{
  Lisp_Object window;

  window = window_from_coordinates (f, x, y, 0, &x, &y, 0);

  if (!NILP (window))
    {
      struct window *w = XWINDOW (window);
      struct glyph_row *r = MATRIX_FIRST_TEXT_ROW (w->current_matrix);
      struct glyph_row *end = r + w->current_matrix->nrows - 1;

      for (; r < end && r->enabled_p; ++r)
	if (r->y <= y && r->y + r->height > y)
	  {
	    /* Found the row at y.  */
	    struct glyph *g = r->glyphs[TEXT_AREA];
	    struct glyph *end = g + r->used[TEXT_AREA];
	    int gx;

	    rect->top = WINDOW_TO_FRAME_PIXEL_Y (w, r->y);
	    rect->bottom = rect->top + r->height;

	    if (x < r->x)
	      {
		/* x is to the left of the first glyph in the row.  */
		/* Shouldn't this be a pixel value?
		   WINDOW_LEFT_EDGE_X (w) seems to be the right value.
		   ++KFS */
		rect->left = WINDOW_LEFT_EDGE_COL (w);
		rect->right = WINDOW_TO_FRAME_PIXEL_X (w, r->x);
		return 1;
	      }

	    for (gx = r->x; g < end; gx += g->pixel_width, ++g)
	      if (gx <= x && gx + g->pixel_width > x)
		{
		  /* x is on a glyph.  */
		  rect->left = WINDOW_TO_FRAME_PIXEL_X (w, gx);
		  rect->right = rect->left + g->pixel_width;
		  return 1;
		}
	    /* x is to the right of the last glyph in the row.  */
	    rect->left = WINDOW_TO_FRAME_PIXEL_X (w, gx);
	    /* Shouldn't this be a pixel value?
	       WINDOW_RIGHT_EDGE_X (w) seems to be the right value.
	       ++KFS */
	    rect->right = WINDOW_RIGHT_EDGE_COL (w);
	    return 1;
	  }
    }

  /* The y is not on any row.  */
  return 0;
}


/* Return the current position of the mouse.
   *FP should be a frame which indicates which display to ask about.

   If the mouse movement started in a scroll bar, set *FP, *BAR_WINDOW,
   and *PART to the frame, window, and scroll bar part that the mouse
   is over.  Set *X and *Y to the portion and whole of the mouse's
   position on the scroll bar.

   If the mouse movement started elsewhere, set *FP to the frame the
   mouse is on, *BAR_WINDOW to nil, and *X and *Y to the character cell
   the mouse is over.

   Set *TIME to the server time-stamp for the time at which the mouse
   was at this position.

   Don't store anything if we don't have a valid set of values to report.

   This clears the mouse_moved flag, so we can wait for the next mouse
   movement.  */

static void
MW32_mouse_position (FRAME_PTR *fp,
		     int insist,
		     Lisp_Object *bar_window,
		     enum scroll_bar_part *part,
		     Lisp_Object *x, Lisp_Object *y,
		     unsigned long *time)
{
  FRAME_PTR f1;
  struct frame *f = *fp;

  BLOCK_INPUT;
  GET_FRAME_HDC (f);

#if 0
  if (! NILP (last_mouse_scroll_bar))
    mw32_scroll_bar_report_motion (f, bar_window, part, x, y, time);
  else
#endif
    {
      POINT pt;

      Lisp_Object frame, tail;

      /* Clear the mouse-moved flag for every frame on this display.  */
      FOR_EACH_FRAME (tail, frame)
	if (FRAME_MW32_DISPLAY (XFRAME (frame)) == FRAME_MW32_DISPLAY (*fp))
	  XFRAME (frame)->mouse_moved = 0;
      last_mouse_scroll_bar = Qnil;

      GetCursorPos (&pt);
      /* Now we have a position on the root; find the innermost window
	 containing the pointer.  */
      {
	if (FRAME_MW32_DISPLAY_INFO (*fp)->grabbed && last_mouse_frame
	    && FRAME_LIVE_P (last_mouse_frame))
	  {
	    f1 = last_mouse_frame;
	  }
	else
	  {
	    /* Is the window one of our frames?  */
	    f1 = mw32_any_window_to_frame (FRAME_MW32_DISPLAY_INFO (*fp),
					   WindowFromPoint (pt));
	  }

#if 0
	/* If not, is it one of our scroll bars?  */
	if (! f1)
	  {
	    struct scroll_bar *bar = w32_window_to_scroll_bar (win);

	    if (bar)
	      {
		f1 = XFRAME (WINDOW_FRAME (XWINDOW (bar->window)));
#if 0
		win_x = parent_x;
		win_y = parent_y;
#endif
	      }
	  }

#endif
	if (f1 == 0 && insist > 0)
	  f1 = SELECTED_FRAME ();

	if (f1)
	  {
	    /* Ok, we found a frame.  Store all the values.
	       last_mouse_glyph is a rectangle used to reduce the
	       generation of mouse events.  To not miss any motion
	       events, we must divide the frame into rectangles of the
	       size of the smallest character that could be displayed
	       on it, i.e. into the same rectangles that matrices on
	       the frame are divided into.  */
	    int width, height, gx, gy;
	    RECT rect;

	    ScreenToClient (FRAME_MW32_WINDOW (f1), &pt);

	    if (glyph_rect (f1, pt.x, pt.y, &rect))
	      last_mouse_glyph = rect;
	    else
	      {
		width = FRAME_SMALLEST_CHAR_WIDTH (f1);
		height = FRAME_SMALLEST_FONT_HEIGHT (f1);
		gx = pt.x;
		gy = pt.y;

		/* Arrange for the division in FRAME_PIXEL_X_TO_COL etc. to
		   round down even for negative values.  */
		if (gx < 0)
		  gx -= width - 1;
		if (gy < 0)
		  gy -= height - 1;
		gx = (gx + width - 1) / width * width;
		gy = (gy + height - 1) / height * height;

		last_mouse_glyph.left = gx;
		last_mouse_glyph.top = gy;
		last_mouse_glyph.right = gx + width;
		last_mouse_glyph.bottom = gy + height;
	      }

	    *bar_window = Qnil;
	    *part = 0;
	    *fp = f1;
	    XSETINT (*x, pt.x);
	    XSETINT (*y, pt.y);
	    *time = last_mouse_movement_time;
	  }
      }
    }

  RELEASE_FRAME_HDC (f);
  UNBLOCK_INPUT;
}


/************************************************************************
		     Windows Control Scroll bar.
 ************************************************************************/

/* Given a window ID, find the struct scroll_bar which manages it.
   This can be called in GC, so we have to make sure to strip off mark
   bits.  */
struct scroll_bar *
mw32_window_to_scroll_bar (HWND hwnd)
{
  Lisp_Object tail, frame;

  for (tail = Vframe_list;
       XGCTYPE (tail) == Lisp_Cons;
       tail = XCONS (tail)->u.cdr)
    {
      Lisp_Object frame, bar, condemned;

      frame = XCAR (tail);
      /* All elements of Vframe_list should be frames.  */
      if (! GC_FRAMEP (frame))
	abort ();

      /* Scan this frame's scroll bar list for a scroll bar with the
         right window ID.  */
      condemned = FRAME_CONDEMNED_SCROLL_BARS (XFRAME (frame));
      for (bar = FRAME_SCROLL_BARS (XFRAME (frame));
	   /* This trick allows us to search both the ordinary and
              condemned scroll bar lists with one loop.  */
	   ! GC_NILP (bar) || (bar = condemned,
			       condemned = Qnil,
			       ! GC_NILP (bar));
	   bar = XSCROLL_BAR (bar)->next)
	if (SCROLL_BAR_MW32_WINDOW (XSCROLL_BAR (bar)) == hwnd)
	  return XSCROLL_BAR (bar);
    }
  return 0;
}

/* Open a new window to serve as a scroll bar, and return the
   scroll bar vector for it.  */
static struct scroll_bar *
mw32_scroll_bar_create (struct window *window,
			int top, int left,
			int width, int height)
{
  FRAME_PTR frame = XFRAME (WINDOW_FRAME (window));
  struct scroll_bar *bar =
    XSCROLL_BAR (Fmake_vector (make_number (SCROLL_BAR_VEC_SIZE), Qnil));

  {
    extern HINSTANCE hinst;
    MSG msg;
    RECT rect;
    HWND hwndScroll;
    SCROLLINFO scinfo;

    rect.top = top;
    rect.left = left;
    rect.bottom = height;
    rect.right = width;
    SEND_INFORM_MESSAGE (FRAME_MW32_WINDOW (frame),
			 WM_EMACS_CREATE_SCROLLBAR,
			 (WPARAM) &rect, (LPARAM) hinst);
    WAIT_REPLY_MESSAGE (&msg, WM_EMACS_CREATE_SCROLLBAR_REPLY);

    scinfo.cbSize = sizeof (SCROLLINFO);
    scinfo.fMask = SIF_ALL;
    scinfo.nMin = 0;
    scinfo.nMax = VERTICAL_SCROLL_BAR_TOP_RANGE (frame, height);
    scinfo.nPage = 0;
    scinfo.nPos = 0;
    SetScrollInfo ((HWND) msg.wParam, SB_CTL, &scinfo, TRUE);

    SET_SCROLL_BAR_MW32_WINDOW (bar, (HWND) msg.wParam);

  }

  XSETWINDOW (bar->window, window);
  XSETINT (bar->top, top);
  XSETINT (bar->left, left);
  XSETINT (bar->width, width);
  XSETINT (bar->height, height);
  XSETINT (bar->start, 0);
  XSETINT (bar->end, 0);
  bar->dragging = Qnil;

  /* Add bar to its frame's list of scroll bars.  */
  bar->next = FRAME_SCROLL_BARS (frame);
  bar->prev = Qnil;
  XSETVECTOR (FRAME_SCROLL_BARS (frame), bar);
  if (! NILP (bar->next))
    XSETVECTOR (XSCROLL_BAR (bar->next)->prev, bar);

  return bar;
}

/* Draw BAR's handle in the proper position.
   If the handle is already drawn from START to END, don't bother
   redrawing it, unless REBUILD is non-zero; in that case, always
   redraw it.  (REBUILD is handy for drawing the handle after expose
   events.)

   Normally, we want to constrain the start and end of the handle to
   fit inside its rectangle, but if the user is dragging the scroll bar
   handle, we want to let them drag it down all the way, so that the
   bar's top is as far down as it goes; otherwise, there's no way to
   move to the very end of the buffer.  */
static void
mw32_scroll_bar_set_handle (struct scroll_bar *bar,
			    int start, int end,
			    int rebuild)
{
  HWND hwndScroll;
  SCROLLINFO scinfo;

  if (! NILP (bar->dragging)) return;

  hwndScroll = SCROLL_BAR_MW32_WINDOW (bar);

  /* If the display is already accurate, do nothing.  */
  if (! rebuild
      && start == XINT (bar->start)
      && end == XINT (bar->end))
    return;

  scinfo.cbSize = sizeof (SCROLLINFO);
  scinfo.fMask = SIF_PAGE | SIF_POS;
  scinfo.nPage = end - start;
  scinfo.nPos = start;
  SetScrollInfo (hwndScroll, SB_CTL, &scinfo, TRUE);

  /* Store the adjusted setting in the scroll bar.  */
  XSETINT (bar->start, start);
  XSETINT (bar->end, end);
}

/* Move a scroll bar around on the screen, to accommodate changing
   window configurations.  */
static void
mw32_scroll_bar_move (struct scroll_bar *bar,
		      int top, int left, int width, int height)
{
  HWND hwndScroll;

  hwndScroll = SCROLL_BAR_MW32_WINDOW (bar);

  MoveWindow (hwndScroll, left, top, width, height, TRUE);

  XSETINT (bar->left,   left);
  XSETINT (bar->top,    top);
  XSETINT (bar->width,  width);
  XSETINT (bar->height, height);
}

/* Destroy the window for BAR, and set its Emacs window's scroll bar
   to nil.  */
static void
mw32_scroll_bar_remove (struct scroll_bar *bar)
{
  MSG msg;

  /* Destroy the window.  */
  SEND_INFORM_MESSAGE (SCROLL_BAR_MW32_WINDOW (bar), WM_CLOSE, 0, 0);

  /* Disassociate this scroll bar from its window.  */
  XWINDOW (bar->window)->vertical_scroll_bar = Qnil;
}

/* Set the handle of the vertical scroll bar for WINDOW to indicate
   that we are displaying PORTION characters out of a total of WHOLE
   characters, starting at POSITION.  If WINDOW has no scroll bar,
   create one.  */
static void
MW32_set_vertical_scroll_bar (struct window *w,
			      int portion, int whole, int position)
{
  struct frame *f = XFRAME (w->frame);
  struct scroll_bar *bar;
  int top, height, left, sb_left, width, sb_width;
  int window_y, window_height;

  GET_FRAME_HDC (f);

  /* Get window dimensions.  */
  window_box (w, -1, 0, &window_y, 0, &window_height);
  top = window_y;
  width = WINDOW_CONFIG_SCROLL_BAR_COLS (w) * FRAME_COLUMN_WIDTH (f);
  height = window_height;

  /* Compute the left edge of the scroll bar area.  */
  left = WINDOW_SCROLL_BAR_AREA_X (w);

  /* Compute the width of the scroll bar which might be less than
     the width of the area reserved for the scroll bar.  */
  if (WINDOW_CONFIG_SCROLL_BAR_WIDTH (w) > 0)
    sb_width = WINDOW_CONFIG_SCROLL_BAR_WIDTH (w);
  else
    sb_width = width;

  /* Compute the left edge of the scroll bar.  */
  if (WINDOW_HAS_VERTICAL_SCROLL_BAR_ON_RIGHT (w))
    sb_left = left + width - sb_width - (width - sb_width) / 2;
  else
    sb_left = left + (width - sb_width) / 2;

  /* Does the scroll bar exist yet?  */
  if (NILP (w->vertical_scroll_bar))
    {
      if (width > 0 && height > 0)
	mw32_clear_frame_area (f, left, top, width, height);

      bar = mw32_scroll_bar_create (w, top, sb_left, sb_width, height);
    }
  else
    {
      /* It may just need to be moved and resized.  */
      bar = XSCROLL_BAR (w->vertical_scroll_bar);

      if (width && height)
	{
	  /* Since toolkit scroll bars are smaller than the space reserved
	     for them on the frame, we have to clear "under" them.  */
	  mw32_clear_frame_area (f, left, top, width, height);
	}
      mw32_scroll_bar_move (bar, top, sb_left, sb_width, height);
    }

  /* Set the scroll bar's current state, unless we're currently being
     dragged.  */
  if (NILP (bar->dragging))
    {
      int top_range =
	VERTICAL_SCROLL_BAR_TOP_RANGE (f, height);

      if (whole == 0)
	mw32_scroll_bar_set_handle (bar, 0, top_range, 0);
      else
	{
	  int start = (int)(((double) position * top_range) / whole);
	  int end = (int)(((double) (position + portion) * top_range) / whole);

	  mw32_scroll_bar_set_handle (bar, start, end, 0);
	}
    }
  XSETVECTOR (w->vertical_scroll_bar, bar);

  RELEASE_FRAME_HDC (f);
}

/* The following three hooks are used when we're doing a thorough
   redisplay of the frame.  We don't explicitly know which scroll bars
   are going to be deleted, because keeping track of when windows go
   away is a real pain - "Can you say set-window-configuration, boys
   and girls?"  Instead, we just assert at the beginning of redisplay
   that *all* scroll bars are to be removed, and then save a scroll bar
   from the fiery pit when we actually redisplay its window.  */

/* Arrange for all scroll bars on FRAME to be removed at the next call
   to `*judge_scroll_bars_hook'.  A scroll bar may be spared if
   `*redeem_scroll_bar_hook' is applied to its window before the judgment.  */

static void
MW32_condemn_scroll_bars (FRAME_PTR frame)
{
  /* Transfer all the scroll bars to FRAME_CONDEMNED_SCROLL_BARS.  */
  while (! NILP (FRAME_SCROLL_BARS (frame)))
    {
      Lisp_Object bar;
      bar = FRAME_SCROLL_BARS (frame);
      FRAME_SCROLL_BARS (frame) = XSCROLL_BAR (bar)->next;
      XSCROLL_BAR (bar)->next = FRAME_CONDEMNED_SCROLL_BARS (frame);
      XSCROLL_BAR (bar)->prev = Qnil;
      if (! NILP (FRAME_CONDEMNED_SCROLL_BARS (frame)))
	XSCROLL_BAR (FRAME_CONDEMNED_SCROLL_BARS (frame))->prev = bar;
      FRAME_CONDEMNED_SCROLL_BARS (frame) = bar;
    }
}

/* Un-mark WINDOW's scroll bar for deletion in this judgment cycle.
   Note that WINDOW isn't necessarily condemned at all.  */

static void
MW32_redeem_scroll_bar (struct window *window)
{
  struct scroll_bar *bar;
  FRAME_PTR f;

  /* We can't redeem this window's scroll bar if it doesn't have one.  */
  if (NILP (window->vertical_scroll_bar))
    abort ();

  bar = XSCROLL_BAR (window->vertical_scroll_bar);

  /* Unlink it from the condemned list.  */
  f = XFRAME (WINDOW_FRAME (window));
  if (NILP (bar->prev))
    {
      /* If the prev pointer is nil, it must be the first in one of
	 the lists.  */
      if (EQ (FRAME_SCROLL_BARS (f), window->vertical_scroll_bar))
	/* It's not condemned.  Everything's fine.  */
	return;
      else if (EQ (FRAME_CONDEMNED_SCROLL_BARS (f),
		   window->vertical_scroll_bar))
	FRAME_CONDEMNED_SCROLL_BARS (f) = bar->next;
      else
	/* If its prev pointer is nil, it must be at the front of
	   one or the other!  */
	abort ();
    }
  else
    XSCROLL_BAR (bar->prev)->next = bar->next;

  if (! NILP (bar->next))
    XSCROLL_BAR (bar->next)->prev = bar->prev;

  bar->next = FRAME_SCROLL_BARS (f);
  bar->prev = Qnil;
  XSETVECTOR (FRAME_SCROLL_BARS (f), bar);
  if (! NILP (bar->next))
    XSETVECTOR (XSCROLL_BAR (bar->next)->prev, bar);
}

/* Remove all scroll bars on FRAME that haven't been saved since the
   last call to `*condemn_scroll_bars_hook'.  */
static void
MW32_judge_scroll_bars (FRAME_PTR f)
{
  Lisp_Object bar, next;

  bar = FRAME_CONDEMNED_SCROLL_BARS (f);

  /* Clear out the condemned list now so we won't try to process any
     more events on the hapless scroll bars.  */
  FRAME_CONDEMNED_SCROLL_BARS (f) = Qnil;

  for (; ! NILP (bar); bar = next)
    {
      struct scroll_bar *b = XSCROLL_BAR (bar);

      mw32_scroll_bar_remove (b);

      next = b->next;
      b->next = b->prev = Qnil;
    }

  /* Now there should be no references to the condemned scroll bars,
     and they should get garbage-collected.  */
}

/* Handle an Expose or GraphicsExpose event on a scroll bar.

   This may be called from a signal handler, so we have to ignore GC
   mark bits.  */
void
mw32_scroll_bar_expose (FRAME_PTR f)
{
#if 0 /* Needless for Windows control */
  Lisp_Object bar;

  for (bar = FRAME_SCROLL_BARS (f); VECTORP (bar);
       bar = XSCROLL_BAR (bar)->next)
    {
      HWND hwndScroll = SCROLL_BAR_MW32_WINDOW (XSCROLL_BAR (bar));
      UpdateWindow (hwndScroll);
    }
#endif
}

/* Handle a mouse click on the scroll bar BAR.  If *EMACS_EVENT's kind
   is set to something other than NO_EVENT, it is enqueued.

   This may be called from a signal handler, so we have to ignore GC
   mark bits.  */
static int
mw32_scroll_bar_handle_click (struct scroll_bar *bar, MSG *msg,
			      struct input_event *emacs_event)
{
  struct mw32_display_info *dpyinfo = GET_MW32_DISPLAY_INFO (msg->hwnd);
  if (!GC_WINDOWP (bar->window))
    abort ();

  EVENT_INIT (*emacs_event);
  MW32_INIT_EMACS_EVENT (*emacs_event);
  emacs_event->kind = W32_SCROLL_BAR_CLICK_EVENT;
  emacs_event->code = 0;
  emacs_event->modifiers = MW32GETMODIFIER (dpyinfo);
  emacs_event->frame_or_window = bar->window;
  emacs_event->timestamp = msg->time;
  {
    int y;
    int top_range =
      VERTICAL_SCROLL_BAR_TOP_RANGE (f, XINT (bar->height));

    y = HIWORD (msg -> wParam);
   /* copy from emacs 19.34 ....(himi) */
    switch (LOWORD (msg -> wParam))
      {
      case SB_THUMBTRACK:
	bar->dragging = Qt;
	emacs_event->part = scroll_bar_handle;
#if 0
	printf ("Scroll Bar Tracking...%d/%d\n", y, top_range);
	fflush (stdout);
#endif
	break;
      case SB_LINEDOWN:
	emacs_event->part = scroll_bar_down_arrow;
	break;
      case SB_LINEUP:
	emacs_event->part = scroll_bar_up_arrow;
	break;
      case SB_PAGEUP:
	emacs_event->part = scroll_bar_above_handle;
	break;
      case SB_PAGEDOWN:
	emacs_event->part = scroll_bar_below_handle;
	break;
      case SB_TOP:
	emacs_event->part = scroll_bar_handle;
	y = 0;
	break;
      case SB_BOTTOM:
	emacs_event->part = scroll_bar_handle;
	y = top_range;
	break;
      case SB_THUMBPOSITION:
	bar->dragging = Qnil;
	emacs_event->part = scroll_bar_handle_position;
#if 0
	printf ("Scroll Bar Tracking...%d/%d\n", y, top_range);
	fflush (stdout);
#endif
	break;
      case SB_ENDSCROLL:
      default:
	bar->dragging = Qnil;
	return 0;
	break;
      }
    XSETINT (emacs_event->x, y);

    XSETINT (emacs_event->y, max (y, top_range));
  }
  return 1;
}

void
mw32_scroll_bar_store_event (WPARAM wParam, LPARAM lParam)
{
  struct input_event event;
  MSG msg;
  struct scroll_bar *bar;

  bar = mw32_window_to_scroll_bar ((HWND) lParam);
  msg.time = GetTickCount ();
  msg.wParam = wParam;
  msg.lParam = lParam;
  if (bar && mw32_scroll_bar_handle_click (bar, &msg, &event))
    {
      kbd_buffer_store_event (&event);
      SetEvent (keyboard_handle);
    }
}

void
mw32_send_key_input (int vkeycode, int scancode, int flags)
{
  INPUT in;

  if (SendInputProc == NULL) return;

  in.type = INPUT_KEYBOARD;
  in.ki.wVk = vkeycode;
  in.ki.wScan = scancode;
  in.ki.dwFlags = flags;
  in.ki.time = 0;
  in.ki.dwExtraInfo = 0;
  SendInputProc (1, &in, sizeof (in));
}

DEFUN ("mw32-get-window-position", Fmw32_get_window_position,
       Smw32_get_window_position, 0, 2, 0,
       doc: /* Get the window position in screen coordinates.
CLASS-NAME is the window class name and WINDOW-NAME is the window's title.
Returns the dimensions as (LEFT TOP RIGHT BOTTOM) in screen coordinates,
or nil if specified window is not found, or failed to get the position. */)
     (class_name, window_name)
     Lisp_Object class_name, window_name;
{
  HWND hwnd;
  RECT rc;
  char *classname = NULL;
  char *windowname = NULL;

  if (STRINGP (class_name))
    classname = SDATA (class_name);

  if (STRINGP (window_name))
    windowname = SDATA (window_name);

  hwnd = FindWindow (classname, windowname);
  if (hwnd == NULL) return Qnil;

  if (! GetWindowRect (hwnd, &rc)) return Qnil;

  return Fcons (make_number (rc.left),
		Fcons (make_number (rc.top),
		       Fcons (make_number (rc.right),
			      Fcons (make_number (rc.bottom),
				     Qnil))));
}


DEFUN ("mw32-get-scroll-bar-info", Fmw32_get_scroll_bar_info,
       Smw32_get_scroll_bar_info, 0, 1, 0,
       doc: /* Get metrics of scroll bar for WINDOW.
Nil means selected window.
Return value is (MIN MAX PAGE POS TRACKPOS), or nil if window has no scroll-bar. */)
     (window)
     Lisp_Object window;
{
  struct scroll_bar *bar;
  struct window *w;
  SCROLLINFO scinfo;

  if (NILP (window))
    window = Fselected_window ();

  if (! WINDOWP (window))
    error ("invalid parameter");

  w = XWINDOW (window);
  if (NILP (w->vertical_scroll_bar))
    return Qnil;

  bar = XSCROLL_BAR (w->vertical_scroll_bar);

  scinfo.cbSize = sizeof (scinfo);
  scinfo.fMask = SIF_ALL;
  GetScrollInfo (SCROLL_BAR_MW32_WINDOW (bar), SB_CTL, &scinfo);

  return
    Fcons (make_number (scinfo.nMin),
	   Fcons (make_number (scinfo.nMax),
		  Fcons (make_number (scinfo.nPage),
			 Fcons (make_number (scinfo.nPos),
				Fcons (make_number (scinfo.nTrackPos),
				       Qnil)))));
}

#if 0
/* Return information to the user about the current position of the mouse
   on the scroll bar.  */

static void
mw32_scroll_bar_report_motion (FRAME_PTR *fp,
			       Lisp_Object *bar_window,
			       enum scroll_bar_part *part,
			       Lisp_Object *x, Lisp_Object *y,
			       unsigned long *time)
{
#if 0  /* TODO: not implemented yet on MW32 */
  struct scroll_bar *bar = XSCROLL_BAR (last_mouse_scroll_bar);
  Window w = SCROLL_BAR_X_WINDOW (bar);
  FRAME_PTR f = XFRAME (WINDOW_FRAME (XWINDOW (bar->window)));
  int win_x, win_y;
  Window dummy_window;
  int dummy_coord;
  unsigned int dummy_mask;

  BLOCK_INPUT;

  /* Get the mouse's position relative to the scroll bar window, and
     report that.  */
  if (! XQueryPointer (FRAME_X_DISPLAY (f), w,

		       /* Root, child, root x and root y.  */
		       &dummy_window, &dummy_window,
		       &dummy_coord, &dummy_coord,

		       /* Position relative to scroll bar.  */
		       &win_x, &win_y,

		       /* Mouse buttons and modifier keys.  */
		       &dummy_mask))
    ;
  else
    {
#if 0
      int inside_height
	= VERTICAL_SCROLL_BAR_INSIDE_HEIGHT (f, XINT (bar->height));
#endif
      int top_range
	= VERTICAL_SCROLL_BAR_TOP_RANGE (f, XINT (bar->height));

      win_y -= VERTICAL_SCROLL_BAR_TOP_BORDER;

      if (! NILP (bar->dragging))
	win_y -= XINT (bar->dragging);

      if (win_y < 0)
	win_y = 0;
      if (win_y > top_range)
	win_y = top_range;

      *fp = f;
      *bar_window = bar->window;

      if (! NILP (bar->dragging))
	*part = scroll_bar_handle;
      else if (win_y < XINT (bar->start))
	*part = scroll_bar_above_handle;
      else if (win_y < XINT (bar->end) + VERTICAL_SCROLL_BAR_MIN_HANDLE)
	*part = scroll_bar_handle;
      else
	*part = scroll_bar_below_handle;

      XSETINT (*x, win_y);
      XSETINT (*y, top_range);

      f->mouse_moved = 0;
      last_mouse_scroll_bar = Qnil;
    }

  *time = last_mouse_movement_time;

  UNBLOCK_INPUT;
#endif
}
#endif

/************************************************************************
            Message processing  Section 1 (Keyboard, Mouse, DnD, Menu)
 ************************************************************************/

/* internal variables */

int mw32_rbutton_to_emacs_button;
int mw32_mbutton_to_emacs_button;
int mw32_lbutton_to_emacs_button;
int mw32_hide_mouse_stickiness = 0;
Lisp_Object mw32_hide_mouse_timeout;
Lisp_Object mw32_hide_mouse_on_key;
Lisp_Object mw32_hide_mouse_by_wheel;

static void
mw32_mouse_button_cc (struct mw32_display_info *dpyinfo,
		      UINT mouse_event,
		      int *button,
		      int *up,
		      int *modifier)
{
  Lisp_Object modp = Qnil;

  switch (mouse_event)
    {
    case WM_LBUTTONUP:
      *button = mw32_lbutton_to_emacs_button;
      *up = 1;
      break;
    case WM_LBUTTONDOWN:
      *button = mw32_lbutton_to_emacs_button;
      *up = 0;
      break;
    case WM_MBUTTONUP:
      *button = mw32_mbutton_to_emacs_button;
      *up = 1;
      break;
    case WM_MBUTTONDOWN:
      *button = mw32_mbutton_to_emacs_button;
      *up = 0;
      break;
    case WM_RBUTTONUP:
      *button = mw32_rbutton_to_emacs_button;
      *up = 1;
      break;
    case WM_RBUTTONDOWN:
      *button = mw32_rbutton_to_emacs_button;
      *up = 0;
      break;
    }

  *modifier = MW32GETMOUSEMODIFIER (dpyinfo, *up);
}

/* Keyboard processing - modifier keys, etc. */
/* Convert a keysym to its name.  */
char *
x_get_keysym_name (int keysym)
{
  /* Make static so we can always return it */
  static char value[100];

  GetKeyNameText (keysym, value, 100);
  return value;
}

/* Modifier Key list.  ....
   0 is a normal key, 1 is a neglected key, 2 is meta modifier,
   4 is ctrl modifier, 8 is shift modifier, 16 is alt modifier,
   32 is super modifier. 64 is hyper modifier.
   128 is a special key that is translated by windows.
   (ToAscii translate it to normal key).
   It is noted to prevent it.  */
/*0   1   2   3   4   5   6   7   8   9   a   b   c   d   e   f   */
static BYTE keymodifier[256] =
{
  0,  0,  0,  0,  0,  0,  0,  0,128,128,  0,  0,  0,128,  0,  0,  /* 0x00-0x0f */
  8,  4,  2,  0,  0,  0,  0,  0,  0,  1,  1,128,  0,  0,  0,  0,  /* 0x10-0x1f */
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,128,  0,  /* 0x20-0x2f */
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  /* 0x30-0x3f */
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  /* 0x40-0x4f */
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  /* 0x50-0x5f */
128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,  /* 0x60-0x6f */
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  /* 0x70-0x7f */
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  /* 0x80-0x8f */
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  /* 0x90-0x9f */
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  /* 0xa0-0xaf */
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  /* 0xb0-0xbf */
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  /* 0xc0-0xcf */
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  /* 0xd0-0xdf */
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  /* 0xe0-0xef */
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  /* 0xf0-0xff */
};

static int
mw32_get_keymodifier_state ()
{
  BYTE keystate[256];
  int i;
  int mod_result = 0;
  PBYTE pks, pkm;
  BYTE tmp;

  pks = keystate;
  pkm = keymodifier;
  GetKeyboardState (keystate);
  for (i = 0; i < 256; i++, pks++, pkm++)
    {
      if ((*pkm == 0) || (*pkm == 128)) continue;    /* for speed.  */
      tmp = *pks >> 7;
      if (tmp & (*pkm >> 1)) mod_result |= meta_modifier;
      else if (tmp & (*pkm >> 2)) mod_result |= ctrl_modifier;
      else if (tmp & (*pkm >> 3)) mod_result |= shift_modifier;
      else if (tmp & (*pkm >> 4)) mod_result |= alt_modifier;
      else if (tmp & (*pkm >> 5)) mod_result |= super_modifier;
      else if (tmp & (*pkm >> 6)) mod_result |= hyper_modifier;
    }
  return mod_result;
}

static int
mw32_emacs_translate_message (struct mw32_display_info *dpyinfo,
			      UINT type, UINT virtkey,
			      UINT *keycode, int *modifier)
{
  BYTE keystate[256];
  static BYTE ansi_code[4];
  static int isdead = 0;
  extern char *lispy_function_keys[];

  static int cur_mod;

  if (!type)
    {
      if (isdead)
	{
	  *modifier = cur_mod;
	  isdead = 0;
	  *keycode = ansi_code[2];
	  return 1;
	}
      return 0;
    }

  cur_mod = MW32GETMODIFIER (dpyinfo);
  *modifier = cur_mod;

  if (keymodifier[virtkey] == 128)
    {
      *keycode = virtkey;
      return 2;
    }
  if (keymodifier[virtkey] == 1) return 3;
  if (keymodifier[virtkey]) return 0;

  GetKeyboardState (keystate);
  if (cur_mod)
    keystate[VK_KANA] = 0;
  keystate[VK_CONTROL] = 0;
  keystate[VK_MENU] = 0;
  keystate[VK_RCONTROL] = 0;
  keystate[VK_LCONTROL] = 0;
  keystate[VK_RMENU] = 0;
  keystate[VK_LMENU] = 0;
  if (keymodifier[VK_CAPITAL])
    keystate[VK_CAPITAL] = 0;
  if (cur_mod & shift_modifier)
    keystate[VK_SHIFT] = 0x80;
  else
    keystate[VK_SHIFT] = 0;

  /* Use ToAsciiEx() only! */
  isdead = ToAsciiEx (virtkey, 0, keystate, (LPWORD) ansi_code,
		      0, GetKeyboardLayout (0));
  if (isdead < 0) return 0;
  if (isdead >= 1)
    {
      isdead--;
      *keycode = ansi_code[0];
      return 1;
    }
  if (lispy_function_keys[virtkey])
    {
      *keycode = virtkey;
      return 2;
    }
  return 3;
}

/* Drop file Support */

int
mw32_drop_file_handler (FRAME_PTR frame,
			MSG* msg,
			struct input_event* emacs_event)
{
  struct mw32_display_info *dpyinfo = GET_MW32_DISPLAY_INFO (msg->hwnd);
  HDROP hDrop;
  POINT pt;

  hDrop = (HANDLE) msg->wParam;
  DragQueryPoint (hDrop, &pt);
  /* DragQueryPoint returns position based on window coordination */
  EVENT_INIT (*emacs_event);
  MW32_INIT_EMACS_EVENT (*emacs_event);
  emacs_event->kind = DRAG_N_DROP_EVENT;
  emacs_event->code = (int) hDrop;
  emacs_event->modifiers = MW32GETMODIFIER (dpyinfo);
  XSETINT (emacs_event->x, pt.x);
  XSETINT (emacs_event->y, pt.y);
  XSETFRAME (emacs_event->frame_or_window, frame);
  emacs_event->timestamp = msg->time;

  return 1;
}

/* IntelliMouse Support */

#ifdef W32_INTELLIMOUSE
int
mw32_mouse_wheel_handler (FRAME_PTR frame,
			  MSG* msg,
			  struct input_event* emacs_event)
{
  struct mw32_display_info *dpyinfo = FRAME_MW32_DISPLAY_INFO (frame);
  POINT pt;
  short delta = (short) HIWORD (msg->wParam);

#if 0 /* Due to these codes, coordinates_in_window()@window.c returns a
	 false position.  So I invalidate these.  MIYOSHI */
  /* Sony VAIO Jog Dial Utility sends WM_MOUSEWHEEL with the posotion
     guessed by active window. Here replace it by last mouse position
     stored as last_mouse_motion_message. There is no side effect,
     maybe.. 2001/03/16 K. Horiguchi */

  pt.x = (signed short) LOWORD (last_mouse_motion_message.lParam);
  pt.y = (signed short) HIWORD (last_mouse_motion_message.lParam);
#else
  pt.x = (signed short) LOWORD (msg->lParam);
  pt.y = (signed short) HIWORD (msg->lParam);
  ScreenToClient (msg->hwnd, &pt);
#endif

  emacs_event->kind = WHEEL_EVENT;
  emacs_event->code = (signed short) delta;
  emacs_event->modifiers = (MW32GETMODIFIER (dpyinfo)
			    | ((delta < 0 ) ? down_modifier : up_modifier));
  XSETINT (emacs_event->x, pt.x);
  XSETINT (emacs_event->y, pt.y);
  XSETFRAME (emacs_event->frame_or_window, frame);
  emacs_event->timestamp = msg->time;

  return 1;
}
#endif

/* We must construct menu structure, but only main thread
   can do it.  We store event menu_bar_activate_event to
   request main thread to reconstruct menu sturucture.
   Then, this thread must wait for the finish, but
   if main thread is busy, it cannot deal with the request.
   Nevertheless, we caputre any event mainly in order
   to store keyboard quit.
   Thus, I set timeout to 500 millisecond. */
void
mw32_menu_bar_store_activate_event (struct frame *f)
{
  extern Lisp_Object Vmenu_updating_frame;
  int result;

  struct input_event emacs_event;
  HANDLE ev = f->output_data.mw32->mainthread_to_frame_handle;

  EVENT_INIT (emacs_event);
  MW32_INIT_EMACS_EVENT (emacs_event);
  emacs_event.kind = MENU_BAR_ACTIVATE_EVENT;
  XSETFRAME (emacs_event.frame_or_window, f);
  emacs_event.timestamp = GetTickCount ();

  ResetEvent (ev);
  kbd_buffer_store_event (&emacs_event);
  SetEvent (keyboard_handle);

  message_loop_blocked_p = 1;
  result = WaitForSingleObject (ev, 500);
  /* main thread have already entered updating phase.
     Freeze until the finish! */
  if (!NILP (Vmenu_updating_frame))
    result = WaitForSingleObject (ev, 1000);
  message_loop_blocked_p = 0;

#if 0
  if (result == WAIT_OBJECT_0)
    fprintf (stderr, "Success!!\n");
  else if (result == WAIT_TIMEOUT)
    fprintf (stderr, "Timeout!!\n");
  else
    fprintf (stderr, "Unknown:%d!!\n", result);
#endif

  lock_mouse_cursor_visible (TRUE);
  DrawMenuBar (FRAME_MW32_WINDOW (f));

  f->output_data.mw32->disable_reconstruct_menubar = 1;

  return;
}

/************************************************************************
            Message processing  Section 2 (message loop)
 ************************************************************************/

static void
mw32_handle_tool_bar_click (FRAME_PTR f, MSG *pmsg, unsigned int modifiers)
{
  int x = GET_X_LPARAM (pmsg->lParam);
  int y = GET_Y_LPARAM (pmsg->lParam);

  handle_tool_bar_click (f, x, y,
			 pmsg->message == WM_EMACS_TOOL_BAR_DOWN, modifiers);
}

struct mw32_tool_bar_parm
{
  struct frame *f;
  int modifier;
};

int
mw32_process_main_thread_message (MSG *pwait_msg)
{
  MSG msg;

  for (;;)
    {
      if (pwait_msg)
	GetMessage (&msg, NULL, 0, 0);
      else
	if (!PeekMessage (&msg, NULL, 0, 0, PM_REMOVE)) break;

      switch (msg.message)
	{
#if 0
	case WM_EMACS_NOTE_MOUSE_MOVEMENT:
	  break;
#endif
	case WM_EMACS_TOOL_BAR_DOWN:
	case WM_EMACS_TOOL_BAR_UP:
	  {
	    struct mw32_tool_bar_parm *tbparm
	      = (struct mw32_tool_bar_parm *) msg.wParam;
	    mw32_handle_tool_bar_click (tbparm->f, &msg, tbparm->modifier);
	    xfree (tbparm);
	    break;
	  }
	case WM_EMACS_HIDE_TOOLTIP:
	  {
	    Fx_hide_tip ();
	    break;
	  }

	case WM_EMACS_UPDATE_CURSOR:
	  {
	    FRAME_PTR f = (FRAME_PTR) msg.wParam;
	    if (!NILP (f->root_window))
	      x_update_cursor (f, 1);
	    break;
	  }
	}
      if (pwait_msg && (pwait_msg->message == msg.message))
	{
	  *pwait_msg = msg;
	  break;
	}
    }

  return 1;
}

/* If you want to add synchronized operation for event,
   you may do in this.
   NOTICE THAT this function may be merged with MW32_read_socket()
   when blocking problem is solved!  */
void
note_sync_event (void)
{
  mw32_process_main_thread_message (NULL);

  if (!(inhibit_window_system || noninteractive)
      && !NILP (last_mouse_motion_frame))
    {
      struct frame *f = XFRAME (last_mouse_motion_frame);

      note_mouse_movement (f, &last_mouse_motion_message);

      /* If the contents of the global variable help_echo
	 has changed, generate a HELP_EVENT.  */
      if (!EQ (help_echo_string, previous_help_echo_string) ||
	  (!NILP (help_echo_string) && !STRINGP (help_echo_string)
	   && f->mouse_moved))
	{
	  if (NILP (help_echo_string))
	    {
	      help_echo_object = help_echo_window = Qnil;
	      help_echo_pos = -1;
	    }

	  any_help_event_p = 1;
	  gen_help_event (help_echo_string, last_mouse_motion_frame,
			  help_echo_window, help_echo_object,
			  help_echo_pos);
	}
      last_mouse_motion_frame = Qnil;
    }
  return;
}

/* Interface for read_avail_input@keyboard.c.
   This function only waits until the message thread
   store events if expected is true. */
static int
MW32_read_socket (int sd, int expected, struct input_event *bufp)
{
  if (expected)
    {
#if 0
      /* Ignore `expected'. Refer to [meadow-develop: 7443]. */
      ResetEvent (keyboard_handle);
      WaitForSingleObject (keyboard_handle, INFINITE);
#endif
    }
  return 0;
}


static void
mw32_clear_mouse_face (struct frame *f)
{
  struct mw32_display_info  *dpyinfo = FRAME_MW32_DISPLAY_INFO (f);

  MW32_BLOCK_FRAME_HDC (f);
  if (main_thread_hdc == INVALID_HANDLE_VALUE)
    {
      GET_FRAME_HDC (f);
      clear_mouse_face (dpyinfo);
      RELEASE_FRAME_HDC (f);
      f->output_data.mw32->pending_clear_mouse_face = FALSE;
    }
  else
    f->output_data.mw32->pending_clear_mouse_face = TRUE;
  /* Pending process will be donw in mw32_message_loop */

  MW32_UNBLOCK_FRAME_HDC (f);
}

int mw32_inhibit_hide_mouse = 0;

static void
show_or_hide_mouse_cursor (struct frame *f, MSG msg)
{
  static unsigned int mouse_hide_timer = 0;
  static POINT lastpos;
  static int insensitive_range = 0;
  static int last_timeout_obj_init_p = 0;
  static Lisp_Object last_timeout_obj;
  static timeout;
  struct mw32_display_info *dpyinfo = FRAME_MW32_DISPLAY_INFO (f);

  if (mw32_inhibit_hide_mouse)
    {
      if (dpyinfo->mouse_cursor_stat < 0)
	  ShowCursor (TRUE);
      dpyinfo->mouse_cursor_stat = 0;

      if (dpyinfo->mouse_face_hidden)
	{
	  dpyinfo->mouse_face_hidden = 0;
	  mw32_clear_mouse_face (f);
	}

      return;
    }

  if (! last_timeout_obj_init_p
      || !EQ (last_timeout_obj, mw32_hide_mouse_timeout))
    {
      last_timeout_obj = mw32_hide_mouse_timeout;
      last_timeout_obj_init_p = 1;

      if (NILP (mw32_hide_mouse_timeout))
	timeout = 0;
      else if (NUMBERP (mw32_hide_mouse_timeout))
	timeout = XINT (mw32_hide_mouse_timeout);
      else if (CONSP (mw32_hide_mouse_timeout)
	       && NUMBERP (CAR (mw32_hide_mouse_timeout)))
	timeout = XINT (CAR (mw32_hide_mouse_timeout));
    }

  if (timeout > 0
      && mouse_hide_timer == 0
      && dpyinfo->mouse_cursor_stat == 0
      && dpyinfo->mw32_highlight_frame == XFRAME (selected_frame)
      && FRAME_MW32_WINDOW (XFRAME (selected_frame)) == msg.hwnd)
    mouse_hide_timer = SetTimer (msg.hwnd, MW32_MOUSE_HIDE_TIMER_ID,
				 timeout, NULL);

  switch (msg.message)
    {
    case WM_MOUSEMOVE:
      SetCursor (FRAME_MW32_OUTPUT (XFRAME (selected_frame))->current_cursor);

    case WM_NCMOUSEMOVE:
      if (msg.lParam != last_mouse_motion_message.lParam)
	{
	  if (insensitive_range < 0)
	    {
	      lastpos.x = LOWORD (last_mouse_motion_message.lParam);
	      lastpos.y = HIWORD (last_mouse_motion_message.lParam);
	    }

	  last_mouse_motion_message = msg;
	  last_mouse_movement_time = msg.time;

	  if ((abs (LOWORD (msg.lParam) - lastpos.x)
	       > abs (insensitive_range))
	      || (abs (HIWORD (msg.lParam) - lastpos.y)
		  > abs (insensitive_range)))
	    {
	      if (dpyinfo->mouse_cursor_stat < 0)
		{
		  dpyinfo->mouse_cursor_stat = 0;
		  ShowCursor (TRUE);
		}
	      if (dpyinfo->mouse_face_hidden)
		{
		  dpyinfo->mouse_face_hidden = 0;
		  mw32_clear_mouse_face (f);
		}
	    }
	}
      break;

    case WM_TIMER:
      if (msg.wParam == mouse_hide_timer)
	{
	  /* If we've had mouse motion after last SetTimer, extend timeout. */
	  if (msg.time - last_mouse_movement_time < timeout
	      && dpyinfo->mouse_cursor_stat == 0)
	    {
	      int extend_mills =
		timeout - (msg.time - last_mouse_movement_time);

	      mouse_hide_timer = SetTimer (msg.hwnd, mouse_hide_timer,
					   extend_mills, NULL);
	    }
	  else
	    {
	      KillTimer (msg.hwnd, mouse_hide_timer);
	      mouse_hide_timer = 0;

	      if (dpyinfo->mouse_cursor_stat == 0)
		{
		  ShowCursor (FALSE);
		  dpyinfo->mouse_cursor_stat = -1;

		  insensitive_range = mw32_hide_mouse_stickiness;
		  if (CONSP (mw32_hide_mouse_timeout))
		    {
		      if (NUMBERP (CDR (mw32_hide_mouse_timeout)))
			insensitive_range =
			  XINT (CDR (mw32_hide_mouse_timeout));
		      else if (CONSP (CDR (mw32_hide_mouse_timeout)) &&
			       NUMBERP (CAR (CDR (mw32_hide_mouse_timeout))))
			insensitive_range =
			  XINT (CAR (CDR (mw32_hide_mouse_timeout)));
		    }

		  lastpos.x = LOWORD (last_mouse_motion_message.lParam);
		  lastpos.y = HIWORD (last_mouse_motion_message.lParam);

		  if (!dpyinfo->mouse_face_hidden
		      && INTEGERP (Vmouse_highlight)
		      && !EQ (f->tool_bar_window, dpyinfo->mouse_face_window))
		    {
		      mw32_clear_mouse_face (f);
		      dpyinfo->mouse_face_hidden = 1;
		    }
		  POST_THREAD_INFORM_MESSAGE (main_thread_id,
					      WM_EMACS_HIDE_TOOLTIP,
					      0, 0);
		}
	    }
	}
      break;

    case WM_MOUSELEAVE:
      if (mouse_hide_timer)
	{
	  KillTimer (msg.hwnd, mouse_hide_timer);
	  mouse_hide_timer = 0;
	}
      dpyinfo->mouse_face_hidden = 0;

      SetCursor (FRAME_MW32_OUTPUT (XFRAME (selected_frame))->nontext_cursor);

      break;

    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_RBUTTONDOWN:
      if (dpyinfo->mouse_cursor_stat < 0)
	{
	  dpyinfo->mouse_cursor_stat = 1;
	  ShowCursor (TRUE);
	}
      if (dpyinfo->mouse_face_hidden)
	{
	  dpyinfo->mouse_face_hidden = 0;
	  mw32_clear_mouse_face (f);
	}

      break;

    case WM_LBUTTONUP:
    case WM_MBUTTONUP:
    case WM_RBUTTONUP:
    case WM_NCLBUTTONDOWN:
    case WM_NCMBUTTONDOWN:
    case WM_NCRBUTTONDOWN:
      /* We may catch only WM_NC*BUTTONDOWN without WM_NC*BUTTONUP for
	 click on non client area . So use only WM_NC*BUTTONDOWN to
	 controlling mouse cursor on non client area. */

      if (dpyinfo->mouse_cursor_stat < 0)
	  ShowCursor (TRUE);

      dpyinfo->mouse_cursor_stat = 0;
      last_mouse_movement_time = msg.time;

      if (dpyinfo->mouse_face_hidden)
	{
	  dpyinfo->mouse_face_hidden = 0;
	  mw32_clear_mouse_face (f);
	}

      break;

    case WM_KEYDOWN:
      if (! NILP (mw32_hide_mouse_on_key) && dpyinfo->mouse_cursor_stat == 0)
	{
	  dpyinfo->mouse_cursor_stat = -1;
	  ShowCursor (FALSE);

	  insensitive_range = mw32_hide_mouse_stickiness;
	  if (NUMBERP (mw32_hide_mouse_on_key))
	    insensitive_range = XINT (mw32_hide_mouse_on_key);
	  lastpos.x = LOWORD (last_mouse_motion_message.lParam);
	  lastpos.y = HIWORD (last_mouse_motion_message.lParam);

	  if (!dpyinfo->mouse_face_hidden && INTEGERP (Vmouse_highlight)
	      && !EQ (f->tool_bar_window, dpyinfo->mouse_face_window))
	    {
	      mw32_clear_mouse_face (f);
	      dpyinfo->mouse_face_hidden = 1;
	    }

	  POST_THREAD_INFORM_MESSAGE (main_thread_id,
				      WM_EMACS_HIDE_TOOLTIP,
				      0, 0);
	}
      break;

#ifdef W32_INTELLIMOUSE
    case WM_MOUSEWHEEL:
      if (! NILP (mw32_hide_mouse_by_wheel))
	{
	  /* reset start point of measuring mouse movement */
	  lastpos.x = LOWORD (last_mouse_motion_message.lParam);
	  lastpos.y = HIWORD (last_mouse_motion_message.lParam);

	  if (dpyinfo->mouse_cursor_stat == 0)
	    {
	      dpyinfo->mouse_cursor_stat = -1;
	      ShowCursor (FALSE);

	      insensitive_range = mw32_hide_mouse_stickiness;
	      if (NUMBERP (mw32_hide_mouse_by_wheel))
		insensitive_range = XINT (mw32_hide_mouse_by_wheel);

	      if (!dpyinfo->mouse_face_hidden && INTEGERP (Vmouse_highlight)
		  && !EQ (f->tool_bar_window, dpyinfo->mouse_face_window))
		{
		  mw32_clear_mouse_face (f);
		  dpyinfo->mouse_face_hidden = 1;
		}

	      POST_THREAD_INFORM_MESSAGE (main_thread_id,
					  WM_EMACS_HIDE_TOOLTIP,
					  0, 0);
	    }
	}
      break;
#endif
    }
}


/* The main MW32 message loop - mw32_message_loop.  */
/* Read messages from the Windows, and process them.
   This routine is executed by the special thread, called message
   thread.  We return as soon as there are no more events
   to be read.

   Emacs Events representing keys are stored in buffer BUFP,
   which can hold up to NUMCHARS characters.
   We return the number of characters stored into the buffer,
   thus pretending to be `read'.

   EXPECTED is nonzero if the caller knows incoming message
   is available.  */

static int
mw32_message_loop (int sd, struct input_event *bufp,
		   int numchars, int expected, int *leftover)
{
  int count = 0;
  MSG msg;
  struct frame *f;
  static int lastmsgp = 0;
  static MSG lastmsg = {INVALID_HANDLE_VALUE, 0, 0, 0, 0, {0, 0}};
  struct mw32_display_info *dpyinfo = GET_MW32_DISPLAY_INFO (sd);

  *leftover = 0;

  if (interrupt_input_blocked)
    {
      interrupt_input_pending = 1;
      return -1;
    }

  interrupt_input_pending = 0;

  xassert (numchars > 0);

  if (lastmsgp)
    {
      msg = lastmsg;
    }
  while (1)
    {
      int button;
      int up;
      int modifier;
      int injected_key;

      if (lastmsgp) lastmsgp = 0;
      else if (expected)
	{
	  GetMessage (&msg, NULL, 0, 0);
	  expected = 0;
	}
      else if (!PeekMessage (&msg, NULL, 0, 0, PM_REMOVE)) break;

      SetEvent (keyboard_handle);

      if (msg.hwnd)
	{
	  f = mw32_window_to_frame (dpyinfo, msg.hwnd);

	  if (f)
	    {
	      if (f->output_data.mw32->pending_clear_mouse_face)
		mw32_clear_mouse_face (f);
	      show_or_hide_mouse_cursor (f, msg);
	    }

	  if ((!f) && (!IS_EMACS_PRIVATE_MESSAGE (msg.message)))
	    {
	      TranslateMessage (&msg);
	      DispatchMessage (&msg);
	      continue;
	    }
	}
      else
	f = NULL;

      injected_key = FALSE;
      switch (msg.message)
	{
	case WM_EMACS_FLUSH_MESSAGE:
	  return count;

	case WM_EMACS_KEYSTROKE:
	  injected_key = TRUE;
	  msg.message = WM_KEYDOWN;
	  /*  fall through */
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:

	  if (f && !f->iconified && f->visible)
	    {
	      UINT keycode;
	      int keymod;
	      int keyflag;
	      int ocount = count;

	      if (msg.wParam == VK_PROCESSKEY)
		{
		  int vkey = mw32_ime_get_virtual_key (msg.hwnd);
		  if (mw32_emacs_translate_message (dpyinfo, 1, vkey,
						    &keycode, &keymod) == 1)
		    {
		      mw32_ime_record_keycode (keycode, keymod);
		      if (mw32_ime_choke_keystroke (keycode, 0))
			break;
		    }
		}
	      keyflag = mw32_emacs_translate_message (dpyinfo, 1, msg.wParam,
						      &keycode, &keymod);

	      if (keyflag == 1
		  && ! injected_key
		  && mw32_ime_choke_keystroke (keycode, 1))
		break;

	      EVENT_INIT (*bufp);
	      MW32_INIT_EMACS_EVENT (*bufp);
	      while (1)
		{
		  switch (keyflag)
		    {
		    case 0:
		      goto dflt_no_translate;
		    case 1:
		      bufp->kind = ASCII_KEYSTROKE_EVENT;
		      break;
		    case 2:
		      bufp->kind = NON_ASCII_KEYSTROKE_EVENT;
		      break;
		    case 3:
		      goto dflt;
		    }
		  bufp->code = keycode;
		  bufp->modifiers = keymod;
		  XSETFRAME (bufp->frame_or_window, f);
		  bufp->timestamp = msg.time;
		  bufp++;
		  numchars--;
		  count++;

		  keyflag = mw32_emacs_translate_message (dpyinfo, 0, 0,
							  &keycode, &keymod);
		  if (!keyflag) break;

		  if (numchars == 0)
		    {
		      count = ocount;
		      *leftover = 1; /* no space, quit! */
		      break;
		    }
		}
	    }
	  break;

	case WM_KEYUP:
	case WM_SYSKEYUP:
	  f = mw32_window_to_frame (dpyinfo, msg.hwnd);
	  if (!f) goto dflt;

	    {
	      int keyflag;
	      UINT keycode;
	      int keymod;
	      keyflag = mw32_emacs_translate_message (dpyinfo, 1, msg.wParam,
						      &keycode, &keymod);
	      if (keyflag == 0) goto dflt_no_translate;
	      goto dflt;
	    }
	    break;

	case WM_MULE_IME_STATUS:
	  if (f && !f->iconified && f->visible && numchars > 0)
	    {
	      EVENT_INIT (*bufp);
	      MW32_INIT_EMACS_EVENT (*bufp);
	      bufp->kind = NON_ASCII_KEYSTROKE_EVENT;
	      bufp->code = VK_KANJI;
	      bufp->modifiers = 0;
	      XSETFRAME (bufp->frame_or_window, f);
	      bufp->timestamp = msg.time;
	      bufp++;
	      numchars--;
	      count++;
	    }
	  break;
	case WM_MULE_IME_REPORT:
	  {
	    LPTSTR lpStr;
	    struct input_event buf;
	    HANDLE hw32_ime_string = (HANDLE) msg.wParam;

	    if (count != 0)
	      {
		*leftover = 1;
		break;
	      }
	    f = (struct frame *) msg.lParam;
	    if (f && !f->iconified && f->visible)
	      {
		lpStr = GlobalLock (hw32_ime_string);
		while (lpStr)
		  {
		    EVENT_INIT (buf);
		    MW32_INIT_EMACS_EVENT (buf);
		    XSETFRAME (buf.frame_or_window, f);
		    buf.timestamp = msg.time;
		    buf.modifiers = 0;
		    if (*lpStr)
		      {
			buf.kind = ASCII_KEYSTROKE_EVENT;
			buf.code = *lpStr;
			kbd_buffer_store_event (&buf);
			lpStr++;
		      }
		    else
		      {
			buf.kind = NON_ASCII_KEYSTROKE_EVENT;
			buf.code = VK_COMPEND;
			kbd_buffer_store_event (&buf);
			break;
		      }
		  }
		GlobalUnlock (hw32_ime_string);
		GlobalFree (hw32_ime_string);
	      }
	  }
	  break;

	case WM_MOUSEMOVE:
	  /* If the mouse has just moved into the frame, start tracking
	     it, so we will be notified when it leaves the frame.  Mouse
	     tracking only works under W98 and NT4 and later. On earlier
	     versions, there is no way of telling when the mouse leaves the
	     frame, so we just have to put up with help-echo and mouse
	     highlighting remaining while the frame is not active.  */
	  if (track_mouse_event_fn && !track_mouse_window)
	    {
	      TRACKMOUSEEVENT tme;
	      tme.cbSize = sizeof (tme);
	      tme.dwFlags = TME_LEAVE;
	      tme.hwndTrack = msg.hwnd;

	      track_mouse_event_fn (&tme);
	      track_mouse_window = msg.hwnd;
	    }

	  /* Ignore non-movement.  */
	  {
	    int x = LOWORD (msg.lParam);
	    int y = HIWORD (msg.lParam);
	    if (x == last_mousemove_x && y == last_mousemove_y)
	      break;
	    last_mousemove_x = x;
	    last_mousemove_y = y;
	  }

	  previous_help_echo_string = help_echo_string;

	  if (last_mouse_frame
	      && FRAME_LIVE_P (last_mouse_frame)
	      && (dpyinfo->grabbed || !f))
	    f = last_mouse_frame;

	  if (f)
	    {
	      XSETFRAME (last_mouse_motion_frame, f);
	      dpyinfo->mouse_face_mouse_frame = f;
	    }

	  goto dflt;

	case WM_NCMOUSEMOVE:
	  mw32_clear_mouse_face (f);
	  dpyinfo->mouse_face_mouse_frame = NULL;
	  last_mouse_motion_frame = Qnil;

	  /* Generate a nil HELP_EVENT to cancel a help-echo.
	     Do it only if there's something to cancel.
	     Otherwise, the startup message is cleared when
	     the mouse leaves the frame.  */
	  if (msg.wParam && any_help_event_p)
	    {
	      Lisp_Object frame;

	      XSETFRAME (frame, f);
	      help_echo_string = Qnil;
	      gen_help_event (Qnil, frame, Qnil, Qnil, 0);
	      any_help_event_p = 0;
	    }

	  goto dflt;

	case WM_MOUSELEAVE:
	  if (f)
	    {
	      if (f == dpyinfo->mouse_face_mouse_frame)
		{
		  /* If we move outside the frame, then we're
		     certainly no longer on any text in the frame.  */
		  mw32_clear_mouse_face (f);
		  dpyinfo->mouse_face_mouse_frame = 0;
		  last_mouse_motion_frame = Qnil;
		}

	      /* Generate a nil HELP_EVENT to cancel a help-echo.
		 Do it only if there's something to cancel.
		 Otherwise, the startup message is cleared when
		 the mouse leaves the frame.  */
	      if (any_help_event_p)
		{
		  Lisp_Object frame;

		  XSETFRAME (frame, f);
		  help_echo_string = Qnil;
		  gen_help_event (Qnil, frame, Qnil, Qnil, 0);
		  any_help_event_p = 0;
		}
	    }
	  track_mouse_window = NULL;
	  goto dflt;

	case WM_LBUTTONDOWN:
	case WM_MBUTTONDOWN:
	case WM_RBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_MBUTTONUP:
	case WM_RBUTTONUP:
	  mw32_mouse_button_cc (dpyinfo, msg.message, &button, &up, &modifier);
	  {
	    /* If we decide we want to generate an event to be seen
	       by the rest of Emacs, we put it here.  */
	    int tool_bar_p = 0;

	    f = mw32_window_to_frame (dpyinfo, msg.hwnd);

	    if (f && WINDOWP (f->tool_bar_window)
		&& XFASTINT (XWINDOW (f->tool_bar_window)->total_lines))
	      {
		Lisp_Object window;
		enum window_part p;
		window = window_from_coordinates (f, LOWORD (msg.lParam),
						  HIWORD (msg.lParam), &p,
						  NULL, NULL, 1);
		if (EQ (window, f->tool_bar_window))
		  {
		    struct mw32_tool_bar_parm *tbparm
		      = ((struct mw32_tool_bar_parm *)
			 xmalloc (sizeof (struct mw32_tool_bar_parm)));

		    tbparm->f = f;
		    tbparm->modifier = MW32GETMODIFIER (dpyinfo);
		    tool_bar_p = 1;
		    if (msg.message == WM_LBUTTONUP)
		      {
			POST_THREAD_INFORM_MESSAGE (main_thread_id,
						    WM_EMACS_TOOL_BAR_UP,
						    (WPARAM) tbparm,
						    msg.lParam);
		      }
		    else if (msg.message == WM_LBUTTONDOWN)
		      {
			POST_THREAD_INFORM_MESSAGE (main_thread_id,
						    WM_EMACS_TOOL_BAR_DOWN,
						    (WPARAM) tbparm,
						    msg.lParam);
		      }
		  }
	      }
	    if (!tool_bar_p && f == dpyinfo->mw32_focus_frame)
	      {
		if (numchars >= 1)
		  {
		    EVENT_INIT (*bufp);
		    MW32_INIT_EMACS_EVENT (*bufp);
		    bufp->kind = MOUSE_CLICK_EVENT;
		    bufp->code = button;
		    bufp->timestamp = msg.time;
		    bufp->modifiers = modifier;
		    XSETINT (bufp->x, LOWORD (msg.lParam));
		    XSETINT (bufp->y, HIWORD (msg.lParam));
		    XSETFRAME (bufp->frame_or_window, f);
		    bufp++;
		    count++;
		    numchars--;
		  }
	      }

	    if (up == 1)
	      {
		dpyinfo->grabbed &= ~(1 << button);
		if (!dpyinfo->grabbed)
		  {
		    ReleaseCapture ();
		  }
	      }
	    else if (up == 0)
	      {
		dpyinfo->grabbed |= (1 << button);
		last_mouse_frame = f;
		SetCapture (msg.hwnd);
	      }
	    else
	      break;
	  }
	  goto dflt;

	case WM_DROPFILES:
	  {
	    if (f && !f->iconified && f->visible &&
		(mw32_drop_file_handler (f, &msg, bufp)))
	      {
		bufp++;
		count++;
		numchars--;
	      }
	  }
	 break;

#ifdef W32_INTELLIMOUSE
	case WM_MOUSEWHEEL:
	  {
	    if (f && !f->iconified && f->visible &&
		(mw32_mouse_wheel_handler (f, &msg, bufp)))
	      {
		bufp++;
		count++;
		numchars--;
	      }
	  }
	  break;
#endif
	case MM_MCINOTIFY:
	  bufp->kind = MW32_MCI_EVENT;
	  XSETFRAME (bufp->frame_or_window, f);
	  switch (msg.wParam)
	    {
	    case MCI_NOTIFY_ABORTED:
	      bufp->code = MW32_MCI_NOTIFY_ABORTED;
	      break;
	    case MCI_NOTIFY_FAILURE:
	      bufp->code = MW32_MCI_NOTIFY_FAILURE;
	      break;
	    case MCI_NOTIFY_SUCCESSFUL:
	      bufp->code = MW32_MCI_NOTIFY_SUCCESSFUL;
	      break;
	    case MCI_NOTIFY_SUPERSEDED:
	      bufp->code = MW32_MCI_NOTIFY_SUPERSEDED;
	      break;
	    default:
	      abort ();
	    }
	  bufp->arg = make_number (msg.lParam);
	  bufp->timestamp = msg.time;
	  bufp++;
	  count++;
	  numchars--;
	  break;

	  /*
	    WM_EMACS_CLEAR_MOUSE_FACE message is used for clearing
	    mouse face and help echo.  But when the WPARAM is zero,
	    don't clear help echo.  This feature is used by WM_NCMOUSEMOVE,
	    because non-client area controls should not interfere
	    the help echo message.
	  */
	case WM_EMACS_CLEAR_MOUSE_FACE:
	  if (f)
	    {
	      mw32_clear_mouse_face (f);

	      /* Generate a nil HELP_EVENT to cancel a help-echo.
		 Do it only if there's something to cancel.
		 Otherwise, the startup message is cleared when
		 the mouse leaves the frame.  */
	      if (msg.wParam && any_help_event_p)
		{
		  Lisp_Object frame;

		  XSETFRAME (frame, f);
		  help_echo_string = Qnil;
		  gen_help_event (Qnil, frame, Qnil, Qnil, 0);
		  any_help_event_p = 0;
		}
	    }
	  break;

	case WM_EMACS_DESTROY:
	  if (numchars <= 0)
	    abort ();

	  EVENT_INIT (*bufp);
	  MW32_INIT_EMACS_EVENT (*bufp);
	  bufp->kind = DELETE_WINDOW_EVENT;
	  XSETFRAME (bufp->frame_or_window, f);
	  bufp++;
	  count++;
	  numchars--;
	  break;

	case WM_EMACS_CLOSE_CONNECTION:
	  ExitThread (1);

	case WM_EMACS_CREATE_FRAME:
	  mw32m_create_frame_window ((struct frame*)msg.wParam,
				     (LPSTR)msg.lParam);
	  break;
#ifdef IME_CONTROL
	case WM_MULE_IME_CREATE_AGENT:
	  mw32m_ime_create_agent ();
	  break;
#endif

	case WM_EMACS_ACTIVATE:
	  if (f && numchars > 0)
	    {
	      EVENT_INIT (*bufp);
	      MW32_INIT_EMACS_EVENT (*bufp);
	      bufp->kind = MEADOW_PRIVATE_EVENT;
	      bufp->code = 0;
	      bufp->modifiers = 0;
	      XSETFRAME (bufp->frame_or_window, f);
	      bufp->timestamp = msg.time;
	      bufp++;
	      numchars--;
	      count++;
	    }
	  break;

	case WM_COMMAND:
	  f = mw32_window_to_frame (dpyinfo, msg.hwnd);
	  if ((msg.lParam == 0) && (HIWORD (msg.wParam) == 0))
	    {
	      /* Came from window menu */

	      if (count != 0)
		{
		  *leftover = 1;
		  break;
		}
	      menubar_selection_callback (msg.hwnd,
					  LOWORD (msg.wParam));
	    }
	  break;

	default:
	dflt:

#ifdef W32_INTELLIMOUSE
	    if (msg.message == mw32_wheel_message)
	      {
		WORD wp;

		wp = LOWORD (msg.wParam);
		msg.wParam = MAKELONG (0, wp);
		if (f && !f->iconified && f->visible &&
		    (mw32_mouse_wheel_handler (f, &msg, bufp)))
		  {
		    bufp++;
		    count++;
		    numchars--;
		  }
		break;
	      }
#endif

	  TranslateMessage (&msg);
	  DispatchMessage (&msg);

	dflt_no_translate:
	  continue;
	}
      if (*leftover)
	{
	  lastmsg = msg;
	  lastmsgp = 1;
	  break;
	}
    }

  /* If the focus was just given to an autoraising frame,
     raise it now.  */
  if (pending_autoraise_frame)
    {
      mw32_raise_frame (pending_autoraise_frame);
      pending_autoraise_frame = 0;
    }

  return count;
}

/* entry routine for the message thread. */
#ifndef KBD_BUFFER_SIZE
#define KBD_BUFFER_SIZE 8192
#endif
static DWORD WINAPI
mw32_async_handle_message (void *params)
{
  extern int quit_char;
  struct input_event buf[KBD_BUFFER_SIZE];
  int i, nread, leftover = 0;

  while (1)
    {
      if (!leftover)
	WaitMessage ();
      nread = mw32_message_loop (0, buf, KBD_BUFFER_SIZE, 0, &leftover);
      /* Scan the chars for C-g and store them in kbd_buffer.  */
      for (i = 0; i < nread; i++)
	{
	  kbd_buffer_store_event (&buf[i]);
	  /* Don't look at input that follows a C-g too closely.
	     This reduces lossage due to autorepeat on C-g.  */
	  if (buf[i].kind == ASCII_KEYSTROKE_EVENT
	      && buf[i].code == quit_char)
	    {
	      break;
	    }
	}
      SetEvent (keyboard_handle);
    }

  return 1;
}


/***********************************************************************
			     Text Cursor
 ***********************************************************************/

/* Set clipping for output in glyph row ROW.  W is the window in which
   we operate.  GC is the graphics context to set clipping in.
   WHOLE_LINE_P non-zero means include the areas used for truncation
   mark display and alike in the clipping rectangle.

   ROW may be a text row or, e.g., a mode line.  Text rows must be
   clipped to the interior of the window dedicated to text display,
   mode lines must be clipped to the whole window.  */

static void
mw32_clip_to_row (HDC hdc, struct window *w,
		  struct glyph_row *row,
		  int whole_line_p)
{
  struct frame *f = XFRAME (WINDOW_FRAME (w));
  int window_x, window_y, window_width, window_height;
  RECT clip_rect;
  HRGN hcr;

  window_box (w, -1, &window_x, &window_y, &window_width, &window_height);

  clip_rect.left = WINDOW_TO_FRAME_PIXEL_X (w, 0);
  clip_rect.top = WINDOW_TO_FRAME_PIXEL_Y (w, row->y);
  clip_rect.top = max (clip_rect.top, window_y);
  clip_rect.right = clip_rect.left + window_width;
  clip_rect.bottom = clip_rect.top + row->visible_height;

  /* If clipping to the whole line, including trunc marks, extend
     the rectangle to the left and increase its width.  */
  if (whole_line_p)
    {
      clip_rect.left -= FRAME_LEFT_FRINGE_WIDTH (f);
      clip_rect.right += FRAME_TOTAL_FRINGE_WIDTH (f);
    }
  hcr = CreateRectRgnIndirect (&clip_rect);
  SelectClipRgn (hdc, hcr);
  DeleteObject (hcr);
}

static int
mw32_compute_cursor_width (struct frame *f,
			   struct glyph *cursor_glyph)
{
  int wd;

  /* Compute the width of the rectangle to draw.  If on a stretch
     glyph, and `x-stretch-block-cursor' is nil, don't draw a
     rectangle as wide as the glyph, but use a canonical character
     width instead.  */
  wd = cursor_glyph->pixel_width;
  if (cursor_glyph->type == STRETCH_GLYPH
      && !mw32_stretch_cursor_p)
    wd = min (FRAME_COLUMN_WIDTH (f), wd);

  return wd;
}

/* Draw a hollow box cursor on window W in glyph row ROW.  */

static void
mw32_draw_hollow_cursor (struct window *w,
			 struct glyph_row *row)
{
  struct frame *f = XFRAME (WINDOW_FRAME (w));
  struct mw32_display_info *dpyinfo = FRAME_MW32_DISPLAY_INFO (f);
  int x, y, wd, h;
  struct glyph *cursor_glyph;
  PIX_TYPE fg;

  /* Compute frame-relative coordinates from window-relative
     coordinates.  */
  x = WINDOW_TEXT_TO_FRAME_PIXEL_X (w, w->phys_cursor.x);
  y = (WINDOW_TO_FRAME_PIXEL_Y (w, w->phys_cursor.y)
       + row->ascent - w->phys_cursor_ascent);
  h = row->height - 1;

  /* Get the glyph the cursor is on.  If we can't tell because
     the current matrix is invalid or such, give up.  */
  cursor_glyph = get_phys_cursor_glyph (w);
  if (cursor_glyph == NULL)
    return;
  wd = mw32_compute_cursor_width (f, cursor_glyph);

  /* The foreground of cursor_gc is typically the same as the normal
     background color, which can cause the cursor box to be invisible.  */
  fg = f->output_data.mw32->cursor_pixel;

  /* Set clipping, draw the rectangle, and reset clipping again.  */
  {
    HPEN hp;
    HBRUSH hb;
    LOGBRUSH logpenbrush;
    HDC hdc = GET_FRAME_HDC (f);

    logpenbrush.lbStyle = BS_SOLID;
    logpenbrush.lbColor = fg;
    hp = ExtCreatePen (PS_COSMETIC | PS_SOLID, 1, &logpenbrush,
		       0, NULL);
    hb = GetStockObject (NULL_BRUSH);
    SaveDC (hdc);
    SelectObject (hdc, hp);
    SelectObject (hdc, hb);
    mw32_clip_to_row (hdc, w, row, 0);
    Rectangle (hdc, x, y, x + wd, y + h);
    RestoreDC (hdc, -1);
    DeleteObject (hp);

    RELEASE_FRAME_HDC (f);
  }
}

void
mw32_draw_caret_cursor (struct frame *f,
			struct window *w,
			struct glyph_row *row)
{
  if (w->phys_cursor_type == CHECKERED_CARET_CURSOR)
    MW32_FRAME_CARET_BITMAP (f) = (HBITMAP) 1;
  else
    MW32_FRAME_CARET_BITMAP (f) = (HBITMAP) 0;

  if (w->phys_cursor_type != HAIRLINE_CARET_CURSOR)
    {
      struct glyph *cursor_glyph = get_phys_cursor_glyph (w);

      if (cursor_glyph != NULL)
	FRAME_CURSOR_WIDTH (f) = mw32_compute_cursor_width (f,
							    cursor_glyph);
    }
  w->cursor_off_p = 0;

  mw32_set_caret (f, SHOWN_CARET);
}

/* Draw a bar cursor on window W in glyph row ROW.

   Implementation note: One would like to draw a bar cursor with an
   angle equal to the one given by the font property XA_ITALIC_ANGLE.
   Unfortunately, I didn't find a font yet that has this property set.
   --gerd.  */

static void
mw32_draw_bar_cursor (struct window *w,
		      struct glyph_row *row,
		      int width,
		      enum text_cursor_kinds kind)
{
  struct frame *f = XFRAME (w->frame);
  struct glyph *cursor_glyph;
  int x;
  HDC hdc;

  /* If cursor is out of bounds, don't draw garbage.  This can happen
     in mini-buffer windows when switching between echo area glyphs
     and mini-buffer.  */
  cursor_glyph = get_phys_cursor_glyph (w);
  if (cursor_glyph == NULL)
    return;

  /* If on an image, draw like a normal cursor.  That's usually better
     visible than drawing a bar, esp. if the image is large so that
     the bar might not be in the window.  */
  if (cursor_glyph->type == IMAGE_GLYPH)
    {
      struct glyph_row *row;
      row = MATRIX_ROW (w->current_matrix, w->phys_cursor.vpos);
      draw_phys_cursor_glyph (w, row, DRAW_CURSOR);
    }
  else
    {
      COLORREF cursor_color = f->output_data.mw32->cursor_pixel;
      struct face *face = FACE_FROM_ID (f, cursor_glyph->face_id);

      /* If the glyph's background equals the color we normally draw
	 the bar cursor in, the bar cursor in its normal color is
	 invisible.  Use the glyph's foreground color instead in this
	 case, on the assumption that the glyph's colors are chosen so
	 that the glyph is legible.  */
      if (face->background == cursor_color)
	cursor_color = face->foreground;

      x = WINDOW_TEXT_TO_FRAME_PIXEL_X (w, w->phys_cursor.x);

      if (width < 0)
	width = FRAME_CURSOR_WIDTH (f);
      width = min (cursor_glyph->pixel_width, width);

      w->phys_cursor_width = width;

      hdc = GET_FRAME_HDC (f);
      SaveDC (hdc);
      mw32_clip_to_row (hdc, w, row, 0);

      if (kind == BAR_CURSOR)
	{
	  int y = WINDOW_TO_FRAME_PIXEL_Y (w, w->phys_cursor.y);
	  mw32_fill_area_pix (f, cursor_color, x, y,
			      x + width, y + row->height);
	}
      else
	{
	  int y = WINDOW_TO_FRAME_PIXEL_Y (w, w->phys_cursor.y +
					   row->height - width);
	  mw32_fill_area_pix (f, cursor_color, x, y,
			      x + cursor_glyph->pixel_width, y + width);
	}

      RestoreDC (hdc, -1);
      RELEASE_FRAME_HDC (f);
    }
}


/* RIF: Define cursor CURSOR on frame F.  */

static void
mw32_define_frame_cursor (struct frame *f, Cursor cursor)
{
  FRAME_MW32_OUTPUT (f)->current_cursor = cursor;
}

/* RIF: Draw or clear cursor on window W.  */

static void
mw32_draw_window_cursor (struct window *w, struct glyph_row *glyph_row,
			 int x, int y, int cursor_type, int cursor_width,
			 int on_p, int active_p)
{
  struct frame *f = XFRAME (w->frame);

  if (on_p)
    {
      w->phys_cursor_type = cursor_type;
      w->phys_cursor_on_p = 1;

      if (glyph_row->exact_window_width_line_p
	  && w->phys_cursor.hpos >= glyph_row->used[TEXT_AREA])
	{
	  glyph_row->cursor_in_fringe_p = 1;
	  draw_fringe_bitmap (w, glyph_row, 0);
	  return;
	}

      switch (cursor_type)
	{
	case HOLLOW_BOX_CURSOR:
	  mw32_draw_hollow_cursor (w, glyph_row);
	  break;

	case FILLED_BOX_CURSOR:
	  draw_phys_cursor_glyph (w, glyph_row, DRAW_CURSOR);
	  break;

	case BAR_CURSOR:
	  mw32_draw_bar_cursor (w, glyph_row, cursor_width, BAR_CURSOR);
	  break;

	case HBAR_CURSOR:
	  mw32_draw_bar_cursor (w, glyph_row, cursor_width, HBAR_CURSOR);
	  break;

	case NO_CURSOR:
	  break;

	case HAIRLINE_CARET_CURSOR:
	case CHECKERED_CARET_CURSOR:
	case CARET_CURSOR:
	  mw32_draw_caret_cursor (f, w, glyph_row);
	  break;

	default:
	  abort ();
	}
    }

#ifdef IME_CONTROL
  if (on_p
      && f->output_data.mw32->ime_composition_state
      && (XWINDOW (selected_window) == w)
      && (FRAME_MW32_DISPLAY_INFO (f)->mw32_highlight_frame == f))
    {
      /* Maybe, we're in critsec, so use POST_INFORM_MESSAGE.  */
      PostMessage (FRAME_MW32_WINDOW (f),
		   WM_MULE_IMM_SET_CONVERSION_WINDOW, (WPARAM) w, 0);
    }
#endif
}


/***********************************************************************
			    Frame/Window control
 ***********************************************************************/

/* Calculate the absolute position in frame F
   from its current recorded position values and gravity.  */

void
mw32_calc_absolute_position (struct frame *f)
{
  int flags = f->size_hint_flags;
  int working_area_width;
  int working_area_height;
  RECT working_area_rect;
  int width, height;
  RECT win_rect = {0, 0, FRAME_PIXEL_WIDTH (f), FRAME_PIXEL_HEIGHT (f)};

  /* Get size of frame including non-client region. */
  AdjustWindowRectEx (&win_rect, f->output_data.mw32->dwStyle,
		      FRAME_EXTERNAL_MENU_BAR (f),
		      f->output_data.mw32->dwStyleEx);
  width = win_rect.right - win_rect.left;
  height = win_rect.bottom - win_rect.top;

  /* We have nothing to do if the current position
     is already for the top-left corner.  */
  if ((flags & XNegative) || (flags & YNegative))
    {
      /* Calculate absolute position. */
      if (flags & XNegative)
	f->left_pos = FRAME_MW32_DISPLAY_INFO (f)->width - width + f->left_pos;

      if (flags & YNegative)
	f->top_pos = FRAME_MW32_DISPLAY_INFO (f)->height - height + f->top_pos;

      f->size_hint_flags &= ~ (XNegative | YNegative);
    }

  /* Relocate frame so as not to be overlapped by taskbar. */
  if (mw32_restrict_frame_position)
    {
      SystemParametersInfo (SPI_GETWORKAREA, 0, &working_area_rect, 0);

      /* First, adjust right-bottom edges. */
      if (f->left_pos + width > working_area_rect.right)
	f->left_pos = working_area_rect.right - width;
      if (f->top_pos + height > working_area_rect.bottom)
	f->top_pos = working_area_rect.bottom - height;

      /* Then, adjust left-top edges. */
      if (f->left_pos < working_area_rect.left)
	f->left_pos = working_area_rect.left;
      if (f->top_pos < working_area_rect.top)
	f->top_pos = working_area_rect.top;
    }
}

/* CHANGE_GRAVITY is 1 when calling from Fset_frame_position,
   to really change the position, and 0 when calling from
   x_make_frame_visible (in that case, XOFF and YOFF are the current
   position values).  It is -1 when calling from x_set_frame_parameters,
   which means, do adjust for borders but don't change the gravity.  */

void
x_set_offset (struct frame *f,
	      int xoff, int yoff,
	      int change_gravity)
{
  if (change_gravity > 0)
    {
      f->top_pos = yoff;
      f->left_pos = xoff;
      f->size_hint_flags &= ~ (XNegative | YNegative);

      if (xoff < 0)
	f->size_hint_flags |= XNegative;
      if (yoff < 0)
	f->size_hint_flags |= YNegative;
      f->win_gravity = NorthWestGravity;
    }
  mw32_calc_absolute_position (f);

  BLOCK_INPUT;

  SetWindowPos (FRAME_MW32_WINDOW (f),
		HWND_NOTOPMOST,
		f->left_pos, f->top_pos,
		0,0,
		SWP_NOZORDER | SWP_NOSIZE);
  UNBLOCK_INPUT;
}

/* Call this to change the size of frame F's x-window.
   If CHANGE_GRAVITY is 1, we change to top-left-corner window gravity
   for this size change and subsequent size changes.
   Otherwise we leave the window gravity unchanged.  */

void
x_set_window_size (struct frame *f,
		   int change_gravity,
		   int cols, int rows)
{
  int pixelwidth, pixelheight;
  RECT rect;

  BLOCK_INPUT;

  check_frame_size (f, &rows, &cols);
  f->scroll_bar_actual_width
    = FRAME_SCROLL_BAR_COLS (f) * FRAME_COLUMN_WIDTH (f);

  compute_fringe_widths (f, 0);

  pixelwidth = FRAME_TEXT_COLS_TO_PIXEL_WIDTH (f, cols);
  pixelheight = FRAME_TEXT_LINES_TO_PIXEL_HEIGHT (f, rows);

  f->win_gravity = NorthWestGravity;
  rect.left = rect.top = 0;
  rect.right = pixelwidth;
  rect.bottom = pixelheight;

  AdjustWindowRectEx (&rect, f->output_data.mw32->dwStyle,
		      FRAME_EXTERNAL_MENU_BAR (f),
		      f->output_data.mw32->dwStyleEx);

  /* All windows have an extra pixel */

  SetWindowPos (FRAME_MW32_WINDOW (f),
		HWND_NOTOPMOST,
		0, 0,
		rect.right - rect.left,
		rect.bottom - rect.top,
		SWP_NOZORDER | SWP_NOMOVE);

  if (IsIconic (FRAME_MW32_WINDOW (f)) || IsZoomed (FRAME_MW32_WINDOW (f)))
    {
      WINDOWPLACEMENT placement;

      placement.length = sizeof placement;

      GetWindowPlacement (FRAME_MW32_WINDOW (f), &placement);

      placement.rcNormalPosition.right = (placement.rcNormalPosition.left +
					  (rect.right - rect.left));
      placement.rcNormalPosition.bottom = (placement.rcNormalPosition.top +
					   (rect.bottom - rect.top));

      SetWindowPlacement (FRAME_MW32_WINDOW (f), &placement);
    }

  /* Now, strictly speaking, we can't be sure that this is accurate,
     but the window manager will get around to dealing with the size
     change request eventually, and we'll hear how it went when the
     ConfigureNotify event gets here.

     We could just not bother storing any of this information here,
     and let the ConfigureNotify event set everything up, but that
     might be kind of confusing to the lisp code, since size changes
     wouldn't be reported in the frame parameters until some random
     point in the future when the ConfigureNotify event arrives.  */
  change_frame_size (f, rows, cols, 0, 1, 0);
  FRAME_PIXEL_WIDTH (f) = pixelwidth;
  FRAME_PIXEL_HEIGHT (f) = pixelheight;

  /* We've set {FRAME,PIXEL}_{WIDTH,HEIGHT} to the values we hope to
     receive in the ConfigureNotify event; if we get what we asked
     for, then the event won't cause the screen to become garbaged, so
     we have to make sure to do it here.  */
  SET_FRAME_GARBAGED (f);

  /* Clear out any recollection of where the mouse highlighting was,
     since it might be in a place that's outside the new frame size.
     Actually checking whether it is outside is a pain in the neck,
     so don't try--just let the highlighting be done afresh with new size.  */
  cancel_mouse_face (f);

  UNBLOCK_INPUT;
}

/* Raise frame F.  */

static void
mw32_raise_frame (struct frame *f)
{
  if (f->async_visible)
    {
      BLOCK_INPUT;
      SEND_INFORM_MESSAGE (FRAME_MW32_WINDOW (f),
			   WM_EMACS_SETFOREGROUND,
			   0, 0);
      UNBLOCK_INPUT;
    }
}

/* Lower frame F.  */

static void
mw32_lower_frame (struct frame *f)
{
  if (f->async_visible)
    {
      BLOCK_INPUT;
      SetWindowPos (FRAME_MW32_WINDOW (f),
		    HWND_BOTTOM,
		    0,0,0,0,
		    SWP_NOSIZE | SWP_NOMOVE);
      UNBLOCK_INPUT;
    }
}

static void
MW32_frame_raise_lower (FRAME_PTR f, int raise_flag)
{
  if (raise_flag)
    mw32_raise_frame (f);
  else
    mw32_lower_frame (f);
}


/* Change of visibility.  */

/* This tries to wait until the frame is really visible.
   However, if the window manager asks the user where to position
   the frame, this will return before the user finishes doing that.
   The frame will not actually be visible at that time,
   but it will become visible later when the window manager
   finishes with it.  */

void
x_make_frame_visible (struct frame *f)
{
  int mask;

  BLOCK_INPUT;

  if (! FRAME_VISIBLE_P (f))
    {
      if (! FRAME_ICONIFIED_P (f))
	x_set_offset (f, f->left_pos, f->top_pos, 0);
#if 1
      ShowWindow (FRAME_MW32_WINDOW (f), SW_RESTORE);
      ShowWindow (FRAME_MW32_WINDOW (f), SW_SHOW);
#else
      PostMessage (FRAME_MW32_WINDOW (f), WM_EMACS_SHOWWINDOW,
		   (WPARAM) SW_SHOWNORMAL, 0);
#endif
    }

  UNBLOCK_INPUT;

  /* Synchronize to ensure Emacs knows the frame is visible
     before we do anything else.  We do this loop with input not blocked
     so that incoming events are handled.  */
  {
    Lisp_Object frame;
    XSETFRAME (frame, f);
#if 0
    while (! f->async_visible)
      {
	/* Machines that do polling rather than SIGIO have been observed
	   to go into a busy-wait here.  So we'll fake an alarm signal
	   to let the handler know that there's something to be read.
	   We used to raise a real alarm, but it seems that the handler
	   isn't always enabled here.  This is probably a bug.  */
	if (input_polling_used ())
	  {
	    /* It could be confusing if a real alarm arrives while processing
	       the fake one.  Turn it off and let the handler reset it.  */
	    alarm (0);
	    input_poll_signal ();
	  }
      }
#endif
    FRAME_SAMPLE_VISIBILITY (f);
  }
}

/* Change from mapped state to withdrawn state.  */

/* Make the frame visible (mapped and not iconified).  */

void
x_make_frame_invisible (f)
     struct frame *f;
{
  /* Don't keep the highlight on an invisible frame.  */
  if (FRAME_MW32_DISPLAY_INFO (f)->mw32_highlight_frame == f)
    FRAME_MW32_DISPLAY_INFO (f)->mw32_highlight_frame = 0;

  BLOCK_INPUT;

#if 1
  ShowWindow (FRAME_MW32_WINDOW (f), SW_HIDE);
#else
  PostMessage (FRAME_MW32_WINDOW (f),
	       WM_EMACS_SHOWWINDOW, (WPARAM) SW_HIDE, 0);
#endif

  /* We can't distinguish this from iconification
     just by the event that we get from the server.
     So we can't win using the usual strategy of letting
     FRAME_SAMPLE_VISIBILITY set this.  So do it by hand,
     and synchronize with the server to make sure we agree.  */
  f->visible = 0;
  FRAME_ICONIFIED_P (f) = 0;
  f->async_visible = 0;
  f->async_iconified = 0;

  UNBLOCK_INPUT;
}

/* Change window state from mapped to iconified.  */

void
x_iconify_frame (f)
     struct frame *f;
{
  /* Don't keep the highlight on an invisible frame.  */
  if (FRAME_MW32_DISPLAY_INFO (f)->mw32_highlight_frame == f)
    FRAME_MW32_DISPLAY_INFO (f)->mw32_highlight_frame = 0;

  if (f->async_iconified)
    return;

  BLOCK_INPUT;

  /* Make sure the X server knows where the window should be positioned,
     in case the user deiconifies with the window manager.  */
  if (! FRAME_VISIBLE_P (f) && !FRAME_ICONIFIED_P (f))
    x_set_offset (f, f->left_pos, f->top_pos, 0);

#if 0
  ShowWindow (FRAME_MW32_WINDOW (f), SW_SHOWMINIMIZED);
#else
  PostMessage (FRAME_MW32_WINDOW (f), WM_SYSCOMMAND, SC_ICON, 0);
#endif

  cancel_mouse_face (f);
  f->async_iconified = 1;
  f->async_visible = 0;

  UNBLOCK_INPUT;

  return;
}


/***********************************************************************
		         Locate Mouse Pointer
 ***********************************************************************/

void
x_set_mouse_position (struct frame *f, int x, int y)
{
  POINT pt;

  pt.x = FRAME_COL_TO_PIXEL_X (f, x) + FONT_WIDTH  (FRAME_FONT (f)) / 2;
  pt.y = FRAME_LINE_TO_PIXEL_Y (f, y) + FRAME_LINE_HEIGHT (f) / 2;

  if (pt.x < 0) pt.x = 0;
  if (pt.x > FRAME_PIXEL_WIDTH (f)) pt.x = FRAME_PIXEL_WIDTH (f);

  if (pt.y < 0) pt.y = 0;
  if (pt.y > FRAME_PIXEL_HEIGHT (f)) pt.y = FRAME_PIXEL_HEIGHT (f);

  BLOCK_INPUT;

  ClientToScreen (FRAME_MW32_WINDOW (f), &pt);
  SetCursorPos (pt.x, pt.y);
  UNBLOCK_INPUT;
}

/* Move the mouse to position pixel PIX_X, PIX_Y relative to frame F.  */

void
x_set_mouse_pixel_position (struct frame *f,
			    int pix_x, int pix_y)
{
  POINT org;

  BLOCK_INPUT;
  org.x = org.y = 0;
  ClientToScreen (FRAME_MW32_WINDOW (f), &org);
  SetCursorPos (pix_x + org.x, pix_y + org.y);
  UNBLOCK_INPUT;
}


/***********************************************************************
			          Icon
 ***********************************************************************/
/* currently nothing in this section*/


/* Window manager things */
void
x_wm_set_icon_position (f, icon_x, icon_y)
     struct frame *f;
     int icon_x, icon_y;
{
#if 0
  Window window = FRAME_W32_WINDOW (f);

  f->display.x->wm_hints.flags |= IconPositionHint;
  f->display.x->wm_hints.icon_x = icon_x;
  f->display.x->wm_hints.icon_y = icon_y;

  XSetWMHints (FRAME_X_DISPLAY (f), window, &f->display.x->wm_hints);
#endif
}


/***********************************************************************
				Fonts
 ***********************************************************************/

/* Changing the font of the frame.  */
/* Give frame F the font named FONTNAME as its default font, and
   return the full name of that font.  FONTNAME may be a wildcard
   pattern; in that case, we choose some font that fits the pattern.
   The return value shows which font we chose.  */
Lisp_Object
mw32_new_font (struct frame *f, char *fontname)
{
  struct font_info *fontp
    = FS_LOAD_FONT (f, 0, fontname, -1);

  if (!fontp)
    return Qnil;

  FRAME_FONT (f) = (MW32LogicalFont *) (fontp->font);
  FRAME_BASELINE_OFFSET (f) = fontp->baseline_offset;
  FRAME_FONTSET (f) = -1;

  FRAME_COLUMN_WIDTH (f) = FONT_WIDTH (FRAME_FONT (f));
  FRAME_SPACE_WIDTH (f) = fontp->space_width;
  FRAME_LINE_HEIGHT (f) = FONT_HEIGHT (FRAME_FONT (f));

  compute_fringe_widths (f, 1);

  /* Compute the scroll bar width in character columns.  */
  if (f->config_scroll_bar_width > 0)
    {
      int wid = FRAME_DEFAULT_FONT_WIDTH (f);
      FRAME_CONFIG_SCROLL_BAR_COLS (f)
	= (FRAME_CONFIG_SCROLL_BAR_WIDTH (f) + wid-1) / wid;
    }
  else
    {
      int wid = FRAME_DEFAULT_FONT_WIDTH (f);
      FRAME_CONFIG_SCROLL_BAR_COLS (f) = (14 + wid - 1) / wid;
    }

  /* Now make the frame display the given font.  */
  if (FRAME_MW32_WINDOW (f) != 0)
    {
      /* Don't change the size of a tip frame; there's no point in
	 doing it because it's done in Fx_show_tip, and it leads to
	 problems because the tip frame has no widget.  */
      if (NILP (tip_frame) || XFRAME (tip_frame) != f)
	x_set_window_size (f, 0, f->text_cols, f->text_lines);
    }

  return build_string (fontp->full_name);
}

/* Give frame F the fontset named FONTSETNAME as its default font, and
   return the full name of that fontset.  FONTSETNAME may be a wildcard
   pattern; in that case, we choose some fontset that fits the pattern.
   The return value shows which fontset we chose.  */

Lisp_Object
mw32_new_fontset (struct frame *f, char *fontsetname)
{
  int fontset = fs_query_fontset (build_string (fontsetname), 0);
  Lisp_Object result;

  if (fontset < 0)
    return Qnil;

  if (f->output_data.mw32->fontset == fontset)
    /* This fontset is already set in frame F.  There's nothing more
       to do.  */
    return fontset_name (fontset);

  result = mw32_new_font (f, (SDATA (fontset_ascii (fontset))));

  if (!STRINGP (result))
    /* Can't load ASCII font.  */
    return Qnil;

  /* Since x_new_font doesn't update any fontset information, do it now.  */
  FRAME_FONTSET (f) = fontset;

  return build_string (fontsetname);
}


/***********************************************************************
	                Frame Resource Manager
 ***********************************************************************/

/* Free resources of frame F.  */
void
mw32_free_frame_resources (struct frame *f)
{
  struct mw32_display_info *dpyinfo = FRAME_MW32_DISPLAY_INFO (f);

  BLOCK_INPUT;

  free_frame_menubar (f);

  /* Never call main thread for this frame. */
  f->output_data.mw32->window_desc = INVALID_HANDLE_VALUE;

  if (FRAME_FACE_CACHE (f))
    free_frame_faces (f);

  mw32_destroy_frame_hdc (f);
  CloseHandle (f->output_data.mw32->mainthread_to_frame_handle);
  CloseHandle (f->output_data.mw32->setcaret_event);
  DeleteCriticalSection (&(f->output_data.mw32->hdc_critsec));
  xfree (f->output_data.mw32);
  f->output_data.mw32 = NULL;

  if (f == dpyinfo->mw32_focus_frame)
    dpyinfo->mw32_focus_frame = 0;
  if (f == dpyinfo->mw32_focus_message_frame)
    dpyinfo->mw32_focus_message_frame = 0;
  if (f == dpyinfo->mw32_highlight_frame)
    dpyinfo->mw32_highlight_frame = 0;

  if (f == dpyinfo->mouse_face_mouse_frame)
    {
      dpyinfo->mouse_face_beg_row
	= dpyinfo->mouse_face_beg_col = -1;
      dpyinfo->mouse_face_end_row
	= dpyinfo->mouse_face_end_col = -1;
      dpyinfo->mouse_face_window = Qnil;
      dpyinfo->mouse_face_mouse_frame = 0;
    }

  UNBLOCK_INPUT;
}


/* Destroy the X window of frame F.  */

void
x_destroy_window (FRAME_PTR f)
{
  MSG msg;
  struct mw32_display_info *dpyinfo = FRAME_MW32_DISPLAY_INFO (f);
  HWND hwnd = FRAME_MW32_WINDOW (f);

  mw32_free_frame_resources (f);
  last_mouse_motion_frame = Qnil;

  SEND_INFORM_MESSAGE (hwnd, WM_EMACS_DESTROY_FRAME, 0, 0);
  WAIT_REPLY_MESSAGE (&msg, WM_EMACS_DESTROY_FRAME_REPLY);

  dpyinfo->reference_count--;
}


/***********************************************************************
			    Message Thread
 ***********************************************************************/


/***********************************************************************
			    Initialization
 ***********************************************************************/

/* mw32fns.c */
extern Cursor mw32_load_cursor (LPCTSTR name);

static int mw32_initialized;

struct mw32_display_info *
mw32_term_init (Lisp_Object display_name,
		char *xrm_option, char *resource_name)
{
  int connection;
  struct mw32_display_info *dpyinfo;

  BLOCK_INPUT;

  if (!mw32_initialized)
    {
      mw32_initialize ();
      mw32_initialized = 1;
    }

  /* We have definitely succeeded.  Record the new connection.  */

  dpyinfo = ((struct mw32_display_info *)
	     xmalloc (sizeof (struct mw32_display_info)));
  bzero (dpyinfo, sizeof *dpyinfo);

#ifdef MULTI_KBOARD
  {
    struct x_display_info *share;
    Lisp_Object tail;

    for (share = x_display_list, tail = x_display_name_list; share;
	 share = share->next, tail = XCDR (tail))
      if (same_x_server (SDATA (XCAR (XCAR (tail))),
			 SDATA (display_name)))
	break;
    if (share)
      dpyinfo->kboard = share->kboard;
    else
      {
	dpyinfo->kboard = (KBOARD *) xmalloc (sizeof (KBOARD));
	init_kboard (dpyinfo->kboard);
	if (!EQ (XSYMBOL (Qvendor_specific_keysyms)->function, Qunbound))
	  {
	    char *vendor = ServerVendor (dpy);
	    UNBLOCK_INPUT;
	    dpyinfo->kboard->Vsystem_key_alist
	      = call1 (Qvendor_specific_keysyms,
		       build_string (vendor ? vendor : ""));
	    BLOCK_INPUT;
	  }

	dpyinfo->kboard->next_kboard = all_kboards;
	all_kboards = dpyinfo->kboard;
	/* Don't let the initial kboard remain current longer than necessary.
	   That would cause problems if a file loaded on startup tries to
	   prompt in the mini-buffer.  */
	if (current_kboard == initial_kboard)
	  current_kboard = dpyinfo->kboard;
      }
    dpyinfo->kboard->reference_count++;
  }
#endif

  /* Put this display on the chain.  */
  dpyinfo->next = mw32_display_list;
  mw32_display_list = dpyinfo;

  /* Put it on x_display_name_list as well, to keep them parallel.  */
  x_display_name_list = Fcons (Fcons (display_name, Qnil),
			       x_display_name_list);
  dpyinfo->name_list_element = XCAR (x_display_name_list);

  dpyinfo->mw32_id_name
    = (char *) xmalloc (SBYTES (Vinvocation_name)
			+ SBYTES (Vsystem_name)
			+ 2);
  sprintf (dpyinfo->mw32_id_name, "%s@%s",
	   SDATA (Vinvocation_name), SDATA (Vsystem_name));

#if 0
  /* Get the scroll bar cursor.  */
  dpyinfo->vertical_scroll_bar_cursor
    = XCreateFontCursor (dpyinfo->display, XC_sb_v_double_arrow);
#endif

  {
    RECT r;
    HDC hdc;
    double h, w;
    dpyinfo->root_window = GetDesktopWindow ();
    GetWindowRect (dpyinfo->root_window, &r);
    dpyinfo->height = r.bottom - r.top;
    dpyinfo->width = r.right - r.left;

    hdc = GetDC (dpyinfo->root_window);
    dpyinfo->n_planes = (GetDeviceCaps (hdc, BITSPIXEL)
			 * GetDeviceCaps (hdc, PLANES));
    w = GetDeviceCaps (hdc, HORZSIZE);
    h = GetDeviceCaps (hdc, VERTSIZE);
    dpyinfo->pixel_width = w / dpyinfo->width;
    dpyinfo->pixel_height = h / dpyinfo->height;
    dpyinfo->resx = GetDeviceCaps (hdc, LOGPIXELSX);
    dpyinfo->resy = GetDeviceCaps (hdc, LOGPIXELSY);

    ReleaseDC (dpyinfo->root_window, hdc);
  }

  dpyinfo->grabbed = 0;
  dpyinfo->reference_count = 0;
  dpyinfo->font_table = NULL;
  dpyinfo->n_fonts = 0;
  dpyinfo->font_table_size = 0;
  dpyinfo->bitmaps = 0;
  dpyinfo->bitmaps_size = 0;
  dpyinfo->bitmaps_last = 0;
  dpyinfo->mouse_face_mouse_frame = 0;
  dpyinfo->mouse_face_beg_row = dpyinfo->mouse_face_beg_col = -1;
  dpyinfo->mouse_face_end_row = dpyinfo->mouse_face_end_col = -1;
  dpyinfo->mouse_face_face_id = DEFAULT_FACE_ID;
  dpyinfo->mouse_face_window = Qnil;
  dpyinfo->mouse_face_overlay = Qnil;
  dpyinfo->mouse_face_hidden = 0;
  dpyinfo->mouse_face_mouse_x = dpyinfo->mouse_face_mouse_y = 0;
  dpyinfo->mouse_face_defer = 0;
  dpyinfo->mw32_focus_frame = 0;
  dpyinfo->mw32_focus_message_frame = 0;
  dpyinfo->mw32_highlight_frame = 0;
  dpyinfo->image_cache = make_image_cache ();

  dpyinfo->vertical_scroll_bar_cursor = mw32_load_cursor (IDC_SIZENS);

#if 0
  connection = ConnectionNumber (dpyinfo->display);
  dpyinfo->connection = connection;
#endif

  /* Initialize HDC cache.  */
  {
    int i;
    for (i = 0;i < MAX_DC_NUM;i++)
      dpyinfo->alloced_DCs[i] = INVALID_HANDLE_VALUE;
  }

  /* Create Fringe Bitmaps and store them for later use.

     On W32, bitmaps are all unsigned short, as Windows requires
     bitmap data to be Word aligned.  For some reason they are
     horizontally reflected compared to how they appear on X, so we
     need to bitswap and convert to unsigned shorts before creating
     the bitmaps.  */
  w32_init_fringe ();

  UNBLOCK_INPUT;

  return dpyinfo;
}

/* Get rid of display DPYINFO, assuming all frames are already gone,
   and without sending any more commands to the X server.  */

void
mw32_delete_display (dpyinfo)
     struct mw32_display_info *dpyinfo;
{
  if (mw32_display_list == dpyinfo)
    mw32_display_list = dpyinfo->next;
  else
    {
      struct mw32_display_info *tail;

      for (tail = mw32_display_list; tail; tail = tail->next)
	if (tail->next == dpyinfo)
	  tail->next = tail->next->next;
    }

  /* Release DCs */
  {
    int i;
    HDC *phdc = dpyinfo->alloced_DCs;
    struct frame **pf = dpyinfo->alloced_DC_frames;
    for (i = 0;i < MAX_DC_NUM;i++)
      {
	if (*phdc != INVALID_HANDLE_VALUE)
	  {
	    abort ();
	    RestoreDC (*phdc, -1);
	    ReleaseDC (FRAME_MW32_WINDOW (pf[i]), *phdc);
	  }
	phdc++;
      }
  }

  if (dpyinfo->font_table)
    xfree (dpyinfo->font_table);
  xfree (dpyinfo->mw32_id_name);
  /* xfree (dpyinfo->color_cells); */
  xfree (dpyinfo);

  w32_reset_fringes ();
}


/* Set up use of X before we make the first connection.  */

extern frame_parm_handler mw32i_frame_parm_handlers[];

static struct redisplay_interface mw32_redisplay_interface =
{
  mw32i_frame_parm_handlers,
  x_produce_glyphs,
  mw32_write_glyphs,
  mw32_insert_glyphs,
  mw32_clear_end_of_line,
  mw32i_scroll_run,
  mw32i_after_update_window_line,
  mw32i_update_window_begin,
  mw32i_update_window_end,
  x_cursor_to,
  mw32i_flush,
  0, /* flush_display_optional */
  x_clear_window_mouse_face,
  x_get_glyph_overhangs,
  x_fix_overlapping_area,
  mw32_draw_fringe_bitmap,
  mw32_define_fringe_bitmap,
  mw32_destroy_fringe_bitmap,
  mw32_per_char_metric,
  mw32_encode_char,
  NULL, /* compute_glyph_string_overhangs */
  mw32_draw_glyph_string,
  mw32_define_frame_cursor,
  mw32_clear_frame_area,
  mw32_draw_window_cursor,
  mw32_draw_vertical_window_border,
  mw32_shift_glyphs_for_insert
};

void
mw32_initialize ()
{
  Lisp_Object frame;
  char *defaultvalue;
  int argc = 0;
  char** argv = 0;

  rif = &mw32_redisplay_interface;

  clear_frame_hook = MW32_clear_frame;
  ins_del_lines_hook = MW32_ins_del_lines;
  delete_glyphs_hook = MW32_delete_glyphs;
  ring_bell_hook = MW32_ring_bell;
  reset_terminal_modes_hook = MW32_reset_terminal_modes;
  set_terminal_modes_hook = MW32_set_terminal_modes;
  update_begin_hook = MW32_update_begin;
  update_end_hook = MW32_update_end;
  set_terminal_window_hook = MW32_set_terminal_window;
  read_socket_hook = MW32_read_socket;
  frame_up_to_date_hook = MW32_frame_up_to_date;
  mouse_position_hook = MW32_mouse_position;
  frame_rehighlight_hook = MW32_frame_rehighlight;
  frame_raise_lower_hook = MW32_frame_raise_lower;
  set_vertical_scroll_bar_hook = MW32_set_vertical_scroll_bar;
  condemn_scroll_bars_hook = MW32_condemn_scroll_bars;
  redeem_scroll_bar_hook = MW32_redeem_scroll_bar;
  judge_scroll_bars_hook = MW32_judge_scroll_bars;

  scroll_region_ok = 1;		/* we'll scroll partial frames */
  char_ins_del_ok = 1;
  line_ins_del_ok = 1;		/* we'll just blt 'em */
  fast_clear_end_of_line = 1;	/* X does this well */
  memory_below_frame = 0;	/* we don't remember what scrolls
				   off the bottom */
  baud_rate = 19200;

  last_tool_bar_item = -1;
  any_help_event_p = 0;

  /* Try to use interrupt input; if we can't, then start polling.  */
  Fset_input_mode (Qt, Qnil, Qt, Qnil);

#ifdef IME_CONTROL
  mw32_ime_control_init ();
#endif

#ifdef W32_INTELLIMOUSE
  mw32_wheel_message = RegisterWindowMessage ("MSWHEEL_ROLLMSG");
#endif

  next_message_block_event = CreateEvent (0, TRUE, TRUE, NULL);
  keyboard_handle = CreateEvent (0, TRUE, TRUE, NULL);

  if (!DuplicateHandle (GetCurrentProcess (),
			GetCurrentThread (),
			GetCurrentProcess (),
			&main_thread,
			0, FALSE,
			DUPLICATE_SAME_ACCESS))
    abort ();

  main_thread_id = GetCurrentThreadId ();

  mw32_init_frame_hdc ();

  /* Create message thread.  */
  /* Caution!!!!! inherited thread can't make Lisp Object directly
     in stack.  */
  /* message thread might consume lots of stack
     because of draw_glyphs().  We prepare 4MB.  */
  msg_thread = CreateThread (NULL, 4 * 1024 * 1024,
			     mw32_async_handle_message,
			     0, 0, &msg_thread_id);
  /* SetThreadPriority(msg_thread, THREAD_PRIORITY_ABOVE_NORMAL); */
  message_loop_blocked_p = 0;
  mw32_frame_window = INVALID_HANDLE_VALUE;

  mw32_display_list = NULL;

  track_mouse_window = NULL;

  {
    HMODULE user32_lib = GetModuleHandle ("user32.dll");
    /*
      TrackMouseEvent not available in all versions of Windows, so must load
      it dynamically.  Do it once, here, instead of every time it is used.
    */
    track_mouse_event_fn
      = (TrackMouseEvent_Proc) GetProcAddress (user32_lib, "TrackMouseEvent");

    /* Layered Window */
    SetLayeredWindowAttributesProc = (SETLAYEREDWINDOWATTRPROC)
      GetProcAddress (user32_lib, "SetLayeredWindowAttributes");

    SendInputProc = (SENDINPUTPROC)
      GetProcAddress (user32_lib, "SendInput");
  }
}



/***********************************************************************
	      Emacs Lisp Interfaces / Symbol Registration
 ***********************************************************************/

DEFUN ("w32-get-system-metrics", Fw32_get_system_metrics,
       Sw32_get_system_metrics,
       1, 1, 0,
       doc: /* Retrieve system metrics. This function only calls GetSystemMetrics.  */)
     (index)
     Lisp_Object index;
{
  Lisp_Object ret;
  CHECK_NUMBER (index);
  XSETINT (ret, GetSystemMetrics (XFASTINT (index)));
  return ret;
}

DEFUN ("w32-set-modifier-key", Fw32_set_modifier_key,
       Sw32_set_modifier_key, 2, 2, 0,
       doc: /* Set modifier key.
KEY must be a virtual key code or string that describe a key.
MODIFIER is one of these.
'nil....normal key. 'none...neglected key.
'meta...meta modifier. 'ctrl...ctrl modifier.
'shift...shift modifier(it works modifier key only.)
'alt...alt modifier. 'super...super modifier.
'hyper...hyper modifier.  */)
     (key, modifier)
     Lisp_Object key, modifier;
{
  int virtkey;

  if (NUMBERP (key))
    {
      virtkey = XFASTINT (key);
      if (!((virtkey <= 255) && (virtkey >= 0)))
	error ("invalid key %d", virtkey);
    }
  else if (STRINGP (key))
    {
      extern char *lispy_function_keys[];
      int i;

      virtkey = -1;
      for (i = 0;i < 256;i++)
	{
	  if (lispy_function_keys[i]
	      && (strcmp (lispy_function_keys[i], SDATA (key)) == 0))
	    {
	      virtkey = i;
	      break;
	    }
	}

      if (virtkey == -1)
	error ("Can't find the key:%s", SDATA (key));
    }
  else
    error ("KEY must be a string or number.");


  if (EQ (modifier, Qnil))
    {
      keymodifier[virtkey] &= 0x80;
    }
  else if (EQ (modifier, intern ("none")))
    {
      keymodifier[virtkey] |= 0x01;
    }
  else if (EQ (modifier, intern ("meta")))
    {
      keymodifier[virtkey] |= 0x02;
    }
  else if (EQ (modifier, intern ("ctrl")))
    {
      keymodifier[virtkey] |= 0x04;
    }
  else if (EQ (modifier, intern ("shift")))
    {
      keymodifier[virtkey] |= 0x08;
    }
  else if (EQ (modifier, intern ("alt")))
    {
      keymodifier[virtkey] |= 0x10;
    }
  else if (EQ (modifier, intern ("super")))
    {
      keymodifier[virtkey] |= 0x20;
    }
  else if (EQ (modifier, intern ("hyper")))
    {
      keymodifier[virtkey] |= 0x40;
    }
  else
    {
      error ("unknown modifier type!!");
    }
  return Qnil;
}

DEFUN ("w32-get-key-state", Fw32_get_key_state, Sw32_get_key_state,
       1, 1, 0,
       doc: /* Retrieve a key state when the previous message was received;
not the current state. KEY is a virtual key code to get a state.  */)
     (key)
     Lisp_Object key;
{
  int state;
  Lisp_Object ret;
  CHECK_NUMBER (key);
  state = GetKeyState (XFASTINT (key));
  if (state & 0x8000) ret = Qt;
  else ret = Qnil;
  return ret;
}

DEFUN ("w32-get-mouse-wheel-scroll-lines",
       Fw32_get_mouse_wheel_scroll_lines,
       Sw32_get_mouse_wheel_scroll_lines,
       1, 1, 0,
       doc: /* Retrieve a number of scroll lines from delta number of mouse wheel.  */)
     (delta)
     Lisp_Object delta;
{
#ifdef W32_INTELLIMOUSE
  UINT lines;
  int dt;

  CHECK_NUMBER (delta);

  dt = XINT (delta);
  if (!SystemParametersInfo (SPI_GETWHEELSCROLLLINES, 0, &lines, 0))
    return Qnil;

  if (lines == WHEEL_PAGESCROLL)
    if (dt > 0) return intern ("above-handle");
    else if (dt < 0) return intern ("below-handle");
    else dt = 0;

  return make_number (-((signed int) (lines) * dt / WHEEL_DELTA));
#else
  return make_number (0);
#endif
}

DEFUN ("w32-keyboard-type",
       Fw32_keyboard_type,
       Sw32_keyboard_type,
       0, 0, 0,
       doc: /* Retrieve w32 keyboard type with string.  */)
     ()
{
  int keyboardtypenum, oemkeyboardtype, functionkeys;
  char keyboardlayout[KL_NAMELENGTH];
  char buf[KL_NAMELENGTH + 20 + 20 + 20 +1];
  char *oemtype;
  static char* keyboardtype[] =
    {
      "unknown",
      "PC/XT#83",
      "Olivetti/ICO#102",
      "PC/AT#84",
      "PC/AT#101",
      "Nokia1050",
      "Nokia9140",
      "Japanese",
    };
  static char* japanoemname[] =
    {
      "Microsoft",
      "AX", 0, 0,
      "EPSON",
      "Fujitsu", 0,
      "IBM@Japan", 0, 0,
      "Matsushita", 0, 0,
      "NEC", 0, 0, 0, 0,
      "Toshiba",
    };

  keyboardtypenum = GetKeyboardType (0);
  functionkeys    = GetKeyboardType (2);
  oemkeyboardtype = GetKeyboardType (1);
  if (!GetKeyboardLayoutName (keyboardlayout)) return Qnil;
#if 0
  if (!((functionkeytype >= 1) && (functionkeytype <= 6)))
    functionkeytype = 0;
#endif
  if (!((keyboardtypenum >= 1) && (keyboardtypenum <= 7)))
    keyboardtypenum = 0;

  if (keyboardtypenum == 7)
    {
      if (!((oemkeyboardtype >= 0) && (oemkeyboardtype <= 18)))
	oemtype = 0;
      else
	oemtype = japanoemname[oemkeyboardtype];
    }
  else
    oemtype = 0;

  if (oemtype)
    sprintf (buf, "%s-%s-%d-%s",
	     keyboardtype[keyboardtypenum],
	     oemtype,
	     functionkeys,
	     keyboardlayout);
  else
    sprintf (buf, "%s-OEMNo.0x%08x-%d-%s",
	     keyboardtype[keyboardtypenum],
	     oemkeyboardtype,
	     functionkeys,
	     keyboardlayout);

  return build_string (buf);
}

Lisp_Object
unwind_get_device_capability (data)
     Lisp_Object data;
{
  RELEASE_FRAME_HDC (XFRAME (data));

  return Qnil;
}

DEFUN ("mw32-get-device-capability",
       Fmw32_get_device_capability,
       Smw32_get_device_capability,
       1, 2, 0,
       doc: /* Retrieve system metrics. This function only calls GetSystemMetrics.  */)
     (item, target)
     Lisp_Object item, target;
{
  HDC hdc;
  Lisp_Object ret;
  Lisp_Object frame;
  int count = SPECPDL_INDEX ();

  if (NILP (target))
    frame = selected_frame;
  else
    frame = target;

  if (! FRAMEP (frame))
    wrong_type_argument(Qframep, frame);

  record_unwind_protect (unwind_get_device_capability, frame);

  hdc = GET_FRAME_HDC (XFRAME (frame));

  if (EQ (item, intern ("width-in-mm")))
    {
      int val = GetDeviceCaps (hdc, HORZSIZE);
      XSETINT (ret, val);
    }
  else if (EQ (item, intern ("height-in-mm")))
    {
      int val = GetDeviceCaps (hdc, VERTSIZE);
      XSETINT (ret, val);
    }
  else if (EQ (item, intern ("width")))
    {
      int val = GetDeviceCaps (hdc, HORZRES);
      XSETINT (ret, val);
    }
  else if (EQ (item, intern ("height")))
    {
      int val = GetDeviceCaps (hdc, VERTRES);
      XSETINT (ret, val);
    }
  else if (EQ (item, intern ("pixel-width-in-mm")))
    {
      int val = GetDeviceCaps (hdc, LOGPIXELSX);
      ret = make_float (25.4 / val);
    }
  else if (EQ (item, intern ("pixel-height-in-mm")))
    {
      int val = GetDeviceCaps (hdc, LOGPIXELSY);
      ret = make_float (25.4 / val);
    }
  else if (EQ (item, intern ("color-bits")))
    {
      int val = (GetDeviceCaps (hdc, BITSPIXEL)
		 * GetDeviceCaps (hdc, PLANES));
      XSETINT (ret, val);
    }
  else if (EQ (item, intern ("colors")))
    {
      int val = (GetDeviceCaps (hdc, BITSPIXEL)
		 * GetDeviceCaps (hdc, PLANES));
      if (val >= 24)
	ret = intern ("full");
      else
	XSETINT (ret, 1 << val);
    }
  else if (EQ (item, intern ("clipping")))
    {
      int val = GetDeviceCaps (hdc, CLIPCAPS);
      if (val)
	ret = Qt;
      else
	ret = Qnil;
    }
  else if (EQ (item, intern ("text-capabilities")))
    {
      int val = GetDeviceCaps (hdc, TEXTCAPS);

      ret = Qnil;
      if (val & TC_OP_CHARACTER)
	ret = Fcons (intern ("character"), ret);
      if (val & TC_OP_STROKE)
	ret = Fcons (intern ("stroke"), ret);
      if (val & TC_CP_STROKE)
	ret = Fcons (intern ("clipping"), ret);
      if (val & TC_CR_90)
	ret = Fcons (intern ("rotate-90"), ret);
      if (val & TC_CR_ANY)
	ret = Fcons (intern ("rotate"), ret);
      if (val & TC_SA_DOUBLE)
	ret = Fcons (intern ("scale"), ret);
      if (val & TC_SA_INTEGER)
	ret = Fcons (intern ("scale-in-integer"), ret);

      if (val & TC_IA_ABLE)
	ret = Fcons (intern ("italic"), ret);
      if (val & TC_UA_ABLE)
	ret = Fcons (intern ("underline"), ret);
      if (val & TC_SO_ABLE)
	ret = Fcons (intern ("strikeout"), ret);

      if (val & TC_RA_ABLE)
	ret = Fcons (intern ("raster"), ret);
      if (val & TC_VA_ABLE)
	ret = Fcons (intern ("vector"), ret);
    }
  else if (NUMBERP (item))
    {
      int val = GetDeviceCaps (hdc, XINT (item));
      XSETINT (ret, val);
    }
  else
    {
      Fsignal (Qwrong_type_argument,
	       Fcons (build_string ("Invalid item."),
		      Fcons (item, Qnil)));
    }

  return unbind_to (count, ret);
}

DEFUN ("Meadow-version",
       FMeadow_version,
       SMeadow_version,
       0, 1, 0,
       doc: /* return the Meadow's version in string.
The optional argument DUMMY is not currently used.  */)
     (dummy)
     Lisp_Object dummy;
{
  return (build_string (MEADOW_VERSION_STRING));
}


static int
get_file_attributes (Lisp_Object symbol)
{
  char *string = SDATA (SYMBOL_NAME (symbol));

  if (strcmp (string, "FILE_ATTRIBUTE_READONLY") == 0)
    return FILE_ATTRIBUTE_READONLY;
  else if (strcmp (string, "FILE_ATTRIBUTE_HIDDEN") == 0)
    return FILE_ATTRIBUTE_HIDDEN;
  else if (strcmp (string, "FILE_ATTRIBUTE_SYSTEM") == 0)
    return FILE_ATTRIBUTE_SYSTEM;
  else if (strcmp (string, "FILE_ATTRIBUTE_DIRECTORY") == 0)
    return FILE_ATTRIBUTE_DIRECTORY;
  else if (strcmp (string, "FILE_ATTRIBUTE_ARCHIVE") == 0)
    return FILE_ATTRIBUTE_ARCHIVE;
  else if (strcmp (string, "FILE_ATTRIBUTE_ENCRYPTED") == 0)
    return FILE_ATTRIBUTE_ENCRYPTED;
  else if (strcmp (string, "FILE_ATTRIBUTE_NORMAL") == 0)
    return FILE_ATTRIBUTE_NORMAL;
  else if (strcmp (string, "FILE_ATTRIBUTE_TEMPORARY") == 0)
    return FILE_ATTRIBUTE_TEMPORARY;
  else if (strcmp (string, "FILE_ATTRIBUTE_SPARSE_FILE") == 0)
    return FILE_ATTRIBUTE_SPARSE_FILE;
  else if (strcmp (string, "FILE_ATTRIBUTE_REPARSE_POINT") == 0)
    return FILE_ATTRIBUTE_REPARSE_POINT;
  else if (strcmp (string, "FILE_ATTRIBUTE_COMPRESSED") == 0)
    return FILE_ATTRIBUTE_COMPRESSED;
  else if (strcmp (string, "FILE_ATTRIBUTE_OFFLINE") == 0)
    return FILE_ATTRIBUTE_OFFLINE;
  else if (strcmp (string, "FILE_ATTRIBUTE_NOT_CONTENT_INDEXED") == 0)
    return FILE_ATTRIBUTE_NOT_CONTENT_INDEXED;
  return 0;
}

static int
get_info_to_retrieve (Lisp_Object symbol)
{
  char *string = SDATA (SYMBOL_NAME (symbol));

  if (strcmp (string, "SHGFI_ATTR_SPECIFIED") == 0)
    return SHGFI_ATTR_SPECIFIED;
  else if (strcmp (string, "SHGFI_ATTRIBUTES") == 0)
    return SHGFI_ATTRIBUTES;
  else if (strcmp (string, "SHGFI_DISPLAYNAME") == 0)
    return SHGFI_DISPLAYNAME;
  else if (strcmp (string, "SHGFI_EXETYPE") == 0)
    return SHGFI_EXETYPE;
  else if (strcmp (string, "SHGFI_ICON") == 0)
    return SHGFI_ICON;
  else if (strcmp (string, "SHGFI_ICONLOCATION") == 0)
    return SHGFI_ICONLOCATION;
  else if (strcmp (string, "SHGFI_LARGEICON") == 0)
    return SHGFI_LARGEICON;
  else if (strcmp (string, "SHGFI_LINKOVERLAY") == 0)
    return SHGFI_LINKOVERLAY;
  else if (strcmp (string, "SHGFI_OPENICON") == 0)
    return SHGFI_OPENICON;
  else if (strcmp (string, "SHGFI_PIDL") == 0)
    return SHGFI_PIDL;
  else if (strcmp (string, "SHGFI_SELECTED") == 0)
    return SHGFI_SELECTED;
  else if (strcmp (string, "SHGFI_SHELLICONSIZE") == 0)
    return SHGFI_SHELLICONSIZE;
  else if (strcmp (string, "SHGFI_SMALLICON") == 0)
    return SHGFI_SMALLICON;
  else if (strcmp (string, "SHGFI_SYSICONINDEX") == 0)
    return SHGFI_SYSICONINDEX;
  else if (strcmp (string, "SHGFI_TYPENAME") == 0)
    return SHGFI_TYPENAME;
  else if (strcmp (string, "SHGFI_USEFILEATTRIBUTES") == 0)
    return SHGFI_USEFILEATTRIBUTES;
  else
    return 0;
}

DEFUN ("mw32-sh-get-file-info",
       Fmw32_sh_get_file_info,
       Smw32_sh_get_file_info,
       3, 3, 0,
       doc: /* Return a list of information about an object in the file system.

Elements of the information list are:
 0. Image blob of the icon related with the file. The image is filled
    with the background color of the current frame.
 1. Index of the icon image within the system image list.
 2. Flags that indicates the attributes of the file object.
 3. File path that contains the icon representing the file.
 4. Type name of the file.

PATH is a file path about which you want to retrieve information.

ATTRIB is a list of file attribute flags.  The following flags are
valid for this list: FILE_ATTRIBUTE_READONLY, FILE_ATTRIBUTE_HIDDEN,
FILE_ATTRIBUTE_SYSTEM, FILE_ATTRIBUTE_DIRECTORY,
FILE_ATTRIBUTE_ARCHIVE, FILE_ATTRIBUTE_ENCRYPTED,
FILE_ATTRIBUTE_NORMAL, FILE_ATTRIBUTE_TEMPORARY,
FILE_ATTRIBUTE_SPARSE_FILE, FILE_ATTRIBUTE_REPARSE_POINT,
FILE_ATTRIBUTE_COMPRESSED, FILE_ATTRIBUTE_OFFLINE and
FILE_ATTRIBUTE_NOT_CONTENT_INDEXED.

RETRIEVE is a list of flags that specify the file information to
retrieve.  The following flags are valid for this list:
SHGFI_ATTR_SPECIFIED, SHGFI_ATTRIBUTES, SHGFI_DISPLAYNAME,
SHGFI_EXETYPE, SHGFI_ICON, SHGFI_ICONLOCATION, SHGFI_LARGEICON,
SHGFI_LINKOVERLAY, SHGFI_OPENICON, SHGFI_PIDL, SHGFI_SELECTED,
SHGFI_SHELLICONSIZE, SHGFI_SMALLICON, SHGFI_SYSICONINDEX and
SHGFI_TYPENAME.

This function simply calls SHGetFileInfo().  Please refer to the
specification of the function.  */)
    (path, attrib, retrieve)
     Lisp_Object path, attrib, retrieve;
{
  LPCTSTR path_string;
  DWORD attribute = 0;
  UINT retrieve_value = 0;
  SHFILEINFO info;
  Lisp_Object tail;
  Lisp_Object blob = Qnil;

  if (STRINGP (path))
    path_string = mw32_encode_lispy_string (Vlocale_coding_system,
					    (Funix_to_dos_filename (path)),
					    NULL);
  else
    return Qnil;

  if (CONSP (attrib))
    {
      for (tail = attrib; CONSP (tail); tail = XCDR (tail))
	{
	  if (SYMBOLP (XCAR (tail)))
	    attribute |= get_file_attributes (XCAR (tail));
	}
    }
  else if (SYMBOLP (attrib))
    attribute = get_file_attributes (attrib);
  else
    return Qnil;

  if (CONSP (retrieve))
    {
      for (tail = retrieve; CONSP (tail); tail = XCDR (tail))
	{
	  if (SYMBOLP (XCAR (tail)))
	    retrieve_value |= get_info_to_retrieve (XCAR (tail));
	}
    }
  else if (SYMBOLP (retrieve))
    retrieve_value = get_info_to_retrieve (retrieve);
  else
    return Qnil;

  SHGetFileInfo (path_string, attribute, &info, sizeof (info), retrieve_value);

  if (info.hIcon)
    {
      blob = mw32_create_image_blob_from_icon (info.hIcon);
      DestroyIcon (info.hIcon);
    }

  return list5 (blob,
		make_number (info.iIcon),
		make_number (info.dwAttributes),
		make_string (info.szDisplayName, strlen (info.szDisplayName)),
		make_string (info.szTypeName, strlen (info.szTypeName)));
}

void
syms_of_mw32term ()
{
  staticpro (&x_display_name_list);
  x_display_name_list = Qnil;

  staticpro (&last_mouse_scroll_bar);
  last_mouse_scroll_bar = Qnil;

  staticpro (&last_mouse_press_frame);
  last_mouse_press_frame = Qnil;
  staticpro (&last_mouse_motion_frame);
  last_mouse_motion_frame = Qnil;

  DEFVAR_BOOL ("mw32-stretch-cursor", &mw32_stretch_cursor_p,
	       doc: /* *Non-nil means draw block cursor as wide as the glyph under it.
For example, if a block cursor is over a tab, it will be drawn as
wide as that tab on the display.  */);
  mw32_stretch_cursor_p = 0;

  DEFVAR_LISP ("mw32-visible-bell-type", &Vmw32_visible_bell_type,
	       doc: /* Type of visible bell.
The value is a symbol, `x' for X window style. Otherwise MS Windows style. */);
  Vmw32_visible_bell_type = Qnil;

  DEFVAR_INT ("w32-lbutton-to-emacs-button", &mw32_lbutton_to_emacs_button,
	      doc: /* Position of a mouse button sent to emacs, when the w32 left button
is changed.  */);
  DEFVAR_INT ("w32-mbutton-to-emacs-button", &mw32_mbutton_to_emacs_button,
	      doc: /* Position of a mouse button sent to emacs, when the w32 middle button
is changed.  */);
  DEFVAR_INT ("w32-rbutton-to-emacs-button", &mw32_rbutton_to_emacs_button,
	      doc: /* Position of a mouse button sent to emacs, when the w32 right button
is changed.  */);
  DEFVAR_INT ("w32-hide-mouse-stickiness", &mw32_hide_mouse_stickiness,
	      doc: /* Insensitive range of mouse motion to restore cursor hidden by
`w32-hide-mouse-timeout' or `w32-hide-mouse-on-key'
or `w32-hide-mouse-by-wheel'.
Positive number means movement from where the cursor has been hidden,
and negative means movement between two successive WM_MOUSEMOVE messages. */);
  DEFVAR_LISP ("w32-hide-mouse-timeout", &mw32_hide_mouse_timeout,
	      doc: /* Mouse cursor will hide after some rest. (in milliseconds)
Cursor will not hide if 0. (default)
If value is cons of two numbers, the car is timeout and the cdr is insensitive
range of mouse motion for restoring cursor which overrides
`w32-hide-mouse-stickiness'.*/);
  DEFVAR_LISP ("w32-hide-mouse-by-wheel", &mw32_hide_mouse_by_wheel,
	       doc: /* Non nil means mouse cursor will hide on mouse wheel rotation.
Value of a number means insensitive range of mouse motion for restoring
cursor which overrides `w32-hide-mouse-stickiness'. */);
  DEFVAR_LISP ("w32-hide-mouse-on-key", &mw32_hide_mouse_on_key,
	       doc: /* Non nil means mouse cursor will hide on key input.
Value of a number means insensitive range of mouse motion for restoring
cursor which overrides `w32-hide-mouse-stickiness'. */);

  DEFVAR_BOOL ("mw32-restrict-frame-position", &mw32_restrict_frame_position,
	       doc: /* If non-nil, frame position is restricted within screen when frame position
is set from lisp code or commandline. */);


  mw32_hide_mouse_timeout = make_number(0);    /* infinite */
  mw32_hide_mouse_on_key = Qnil;
  mw32_hide_mouse_by_wheel = Qnil;
  mw32_lbutton_to_emacs_button = 0;
  mw32_mbutton_to_emacs_button = 2;
  mw32_rbutton_to_emacs_button = 1;

  defsubr (&Smw32_get_window_position);
  defsubr (&Smw32_get_scroll_bar_info);
  defsubr (&Sw32_get_system_metrics);
  defsubr (&Sw32_set_modifier_key);
  defsubr (&Sw32_get_key_state);
  defsubr (&Sw32_get_mouse_wheel_scroll_lines);
  defsubr (&Sw32_keyboard_type);
  defsubr (&Smw32_get_device_capability);
  defsubr (&SMeadow_version);
  defsubr (&Smw32_sh_get_file_info);
}
