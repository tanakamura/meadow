/* MW32 Functions for Windows.
   Copyright (C) 1989, 92, 93, 94, 95, 96, 1997, 1998, 1999, 2000, 2001
     Free Software Foundation.

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

/* MW32 implementation by MIYASHITA Hisashi <himi@meadowy.org> */

#include <config.h>
#include <signal.h>
#include <stdio.h>
#include <math.h>
#include <windows.h>
#include <shellapi.h>
#include <imm.h>
#include <commdlg.h>
#include <winspool.h>

#include "lisp.h"
#include "mw32term.h"
#include "mw32reg.h"
#include "frame.h"
#include "window.h"
#include "buffer.h"
#include "intervals.h"
#include "dispextern.h"
#include "keyboard.h"
#include "blockinput.h"
#include "mw32sync.h"
#include <epaths.h>
#include "charset.h"
#include "coding.h"
#include "fontset.h"
#include "systime.h"
#include "termhooks.h"
#include "atimer.h"

/* mw32menu.c */
extern void free_frame_menubar (FRAME_PTR);
extern void mw32_menu_display_help P_ ((HWND, HMENU, UINT, UINT));

/* internal functions */
static struct mw32_display_info *mw32_display_info_for_name P_ ((Lisp_Object));

LRESULT CALLBACK mw32_WndProc P_((HWND, UINT, WPARAM , LPARAM));

/* The shape when over mouse-sensitive text.  */
Lisp_Object Vmw32_sensitive_text_pointer_shape;

/* Color of chars displayed in cursor box. */
Lisp_Object Vmw32_cursor_fore_pixel;

/* The colormap for converting color names to RGB values */
Lisp_Object Vmw32_color_map;

/* W32 Psudo(?) terminal connection flag.  */
static int mw32_open = 0;

/* The name we're using in resource queries.  Most often "emacs".  */

Lisp_Object Vx_resource_name;

/* The application class we're using in resource queries.
   Normally "Emacs".  */

Lisp_Object Vx_resource_class;

/* Non-zero means we're allowed to display an hourglass cursor.  */

int display_hourglass_p;

/* The background and shape of the mouse pointer, and shape when not
   over text or in the modeline.  */

Lisp_Object Vmw32_hourglass_pointer_shape;

/* If non-nil, the pointer shape to indicate that windows can be
   dragged horizontally.  */

Lisp_Object Vmw32_window_horizontal_drag_shape;

Lisp_Object Qbar, Qcaret, Qcheckered_caret, Qhairline_caret;
Lisp_Object Qbox;
Lisp_Object Qcursor_height;
Lisp_Object Qalpha;
Lisp_Object Qnone;
Lisp_Object Qouter_window_id;
extern Lisp_Object Qparent_id;
Lisp_Object Qsuppress_icon;
extern Lisp_Object Qtop;
Lisp_Object Qundefined_color;
extern Lisp_Object Qx_resource_name;
Lisp_Object Qime_font;
extern Lisp_Object Qdisplay;
Lisp_Object Qcenter;
Lisp_Object Qcompound_text, Qcancel_timer;

/* The ANSI codepage.  */
int w32_ansi_code_page;

/* Lower limit of alpha value of frame. */
int mw32_frame_alpha_lower_limit;

#if GLYPH_DEBUG
int image_cache_refcount, dpyinfo_refcount;
#endif

EXFUN (Funix_to_dos_filename, 1);
EXFUN (Fdos_to_unix_filename, 1);

/* Error if we are not connected to X.  */

void
check_mw32 (void)
{
  if (! mw32_open)
    error ("Windows have not been initialized yet.");
}

/* Nonzero if we can use mouse menus.
   You should not call this unless HAVE_MENUS is defined.  */

int
have_menus_p (void)
{
  return mw32_open;
}

/* Extract a frame as a FRAME_PTR, defaulting to the selected frame
   and checking validity for X.  */

FRAME_PTR
check_mw32_frame (Lisp_Object frame)
{
  FRAME_PTR f;

  if (NILP (frame))
    frame = selected_frame;
  CHECK_LIVE_FRAME (frame);
  f = XFRAME (frame);
  if (! FRAME_MW32_P (f))
    error ("Non-mw32 frame used");
  return f;
}


/* Store the screen positions of frame F into XPTR and YPTR.
   These are the positions of the containing window manager window,
   not Emacs's own window.  */

void
mw32_real_positions (FRAME_PTR f, int *xptr, int *yptr)
{
  POINT pt;
  RECT inner, outer;

  GetClientRect ((HWND) FRAME_MW32_WINDOW (f), &inner);
  outer = inner;
  AdjustWindowRectEx (&outer, f->output_data.mw32->dwStyle,
		      FRAME_EXTERNAL_MENU_BAR (f),
		      f->output_data.mw32->dwStyleEx);

  pt.x = outer.left;
  pt.y = outer.top;

  ClientToScreen (FRAME_MW32_WINDOW (f), &pt);

  /* Remember x_pixels_diff and y_pixels_diff.  */
  f->x_pixels_diff = inner.left - outer.left;
  f->y_pixels_diff = inner.top - outer.top;

  *xptr = pt.x;
  *yptr = pt.y;
}

/* Check if we need to resize the frame due to a fullscreen request.
   If so needed, resize the frame. */
static void
mw32_check_fullscreen (struct frame *f)
{
  if (f->want_fullscreen & FULLSCREEN_BOTH)
    {
      int width, height, ign;

      mw32_real_positions (f, &f->left_pos, &f->top_pos);

      x_fullscreen_adjust (f, &width, &height, &f->top_pos, &f->left_pos);

      /* We do not need to move the window, it shall be taken care of
	 when setting WM manager hints.  */
      if (FRAME_COLS (f) != width || FRAME_LINES (f) != height)
	{
	  if (!f->async_iconified && !f->async_visible)
	    {
	      change_frame_size (f, height, width, 0, 1, 0);
	      SET_FRAME_GARBAGED (f);
	      cancel_mouse_face (f);

	      /* Wait for the change of frame size to occur */
	      f->want_fullscreen |= FULLSCREEN_WAIT;
	      x_set_window_size (f, 0, width, height);
	    }
	}
    }
}

/* Let the user specify an X display with a frame.
   nil stands for the selected frame--or, if that is not an X frame,
   the first X display on the list.  */

struct mw32_display_info *
check_x_display_info (Lisp_Object frame)
{
  struct mw32_display_info *dpyinfo = NULL;

  if (NILP (frame))
    {
      struct frame *sf = XFRAME (selected_frame);

      if (FRAME_MW32_P (sf) && FRAME_LIVE_P (sf))
	dpyinfo = FRAME_MW32_DISPLAY_INFO (sf);
      else if (mw32_display_list != 0)
	dpyinfo = mw32_display_list;
      else
	error ("MW32 is not in use or not initialized");
    }
  else if (STRINGP (frame))
    dpyinfo = mw32_display_info_for_name (frame);
  else
    {
      FRAME_PTR f;

      CHECK_LIVE_FRAME (frame);
      f = XFRAME (frame);
      if (! FRAME_MW32_P (f))
	error ("Non-X frame used");
      dpyinfo = FRAME_MW32_DISPLAY_INFO (f);
    }

  return dpyinfo;
}


/* Return the Emacs frame-object corresponding to an X window.
   It could be the frame's main window or an icon window.  */

/* This function can be called during GC, so use GC_xxx type test macros.  */

struct frame *
mw32_window_to_frame (struct mw32_display_info *dpyinfo,
		      HWND hwnd)
{
  Lisp_Object tail, frame;
  struct frame *f;

  for (tail = Vframe_list; GC_CONSP (tail); tail = XCDR (tail))
    {
      frame = XCAR (tail);
      if (!GC_FRAMEP (frame))
	continue;
      f = XFRAME (frame);
      if (!FRAME_MW32_P (f) || FRAME_MW32_DISPLAY_INFO (f) != dpyinfo)
	continue;
#if 0
      if ((f->output_data.x->edit_widget
	   && XtWindow (f->output_data.x->edit_widget) == wdesc)
	  /* A tooltip frame?  */
	  || (!f->output_data.x->edit_widget
	      && FRAME_X_WINDOW (f) == wdesc)
	  || f->output_data.x->icon_desc == wdesc)
	return f;
#endif
      if (FRAME_MW32_WINDOW (f) == hwnd)
	return f;
    }
  return 0;
}

/* Like x_window_to_frame but also compares the window with the widget's
   windows.  */

struct frame *
mw32_any_window_to_frame (struct mw32_display_info *dpyinfo,
			  HWND hwnd)
{
  do {
    if (hwnd == dpyinfo->root_window)
      return NULL;
    if ((WNDPROC) GetClassLong (hwnd, GCL_WNDPROC) == mw32_WndProc)
      return mw32_window_to_frame (dpyinfo, hwnd);
  } while (hwnd = GetParent (hwnd));

  return NULL;
}


/***********************************************************************
	    Color table (should be moved into mw32color.c later)
 ***********************************************************************/

/* The default colors for the color map */
typedef struct ColorMap_t {
    char * name ;
    COLORREF colorref ;
} ColorMap_t ;

ColorMap_t mw32_color_map[] = {
#include "mw32rgb.h"
} ;

DEFUN ("mw32-default-color-map", Fmw32_default_color_map,
       Smw32_default_color_map, 0, 0, 0,
       doc: /* Return the default color map.  */)
  ()
{
    int i;
    int n = sizeof (mw32_color_map) / sizeof (mw32_color_map[0]);
    ColorMap_t * pc = mw32_color_map;
    Lisp_Object cmap;

    cmap = Qnil;

    for (i = 0; i < n; pc++,i++)
      cmap = Fcons (Fcons (build_string (pc->name),
			   make_number (pc->colorref)),
		    cmap);
    return (cmap);
}

Lisp_Object
mw32_to_x_color (Lisp_Object rgb)
{
  Lisp_Object color;

  CHECK_NUMBER (rgb);
  color = Frassq (rgb, Vmw32_color_map);
  if (!NILP (color))
    return (XCAR (color));
  else
    return Qnil;
}

static int
get_hex1 (int numstr)
{
  if (isalpha (numstr))
    {
      numstr = toupper (numstr);
      if (!(numstr >= 'A' && numstr <= 'F'))
	return -1;
      numstr -= ('A' - 10);
      return numstr;
    }
  if (isdigit (numstr))
    {
      numstr -= '0';
      return numstr;
    }
  return -1;
}

static int
color_radix_change (TCHAR *colstr, int size)
{
  int i;
  int ret, tmp;

  ret = 0;
  if ((size <= 0) || (size > 4)) return -1;
  for (i = 0;i < size;i++)
    {
      ret <<= 4;
      tmp = get_hex1 (*colstr);
      if (tmp == -1)
	return -1;
      ret += tmp;
      colstr++;
    }
  return ret;
}

static COLORREF
x_old_rgb_names (TCHAR *colstr, int len)
{
  int red, green, blue;

  switch (len)
    {
    case 4:			/* #RGB */
      red = color_radix_change (colstr + 1, 1) << 4;
      green = color_radix_change (colstr + 2, 1) << 4;
      blue = color_radix_change (colstr + 3, 1) << 4;
      break;

    case 7:			/* #RRGGBB */
      red = color_radix_change (colstr + 1, 2);
      green = color_radix_change (colstr + 3, 2);
      blue = color_radix_change (colstr + 5, 2);
      break;
    case 9:			/* #RRGGBBAA */
      red = color_radix_change (colstr + 1, 2);
      green = color_radix_change (colstr + 3, 2);
      blue = color_radix_change (colstr + 5, 2);
      break;
    case 10:			/* #RRRGGGBBB */
      red = color_radix_change (colstr + 1, 3) >> 4;
      green = color_radix_change (colstr + 4, 3) >> 4;
      blue = color_radix_change (colstr + 7, 3) >> 4;
      break;
    case 13:			/* #RRRRGGGGBBBB */
      red = color_radix_change (colstr + 1, 4) >> 8;
      green = color_radix_change (colstr + 5, 4) >> 8;
      blue = color_radix_change (colstr + 9, 4) >> 8;
      break;

    default :
      return CLR_INVALID;
    }
  if ((red == -1) || (green == -1) || (blue == -1))
    return CLR_INVALID;

  return RGB (red, green, blue);
}

static COLORREF
x_rgb_names (TCHAR *colstr, int len)
{
  TCHAR *colstrend;
  Lisp_Object ret;
  int red, green, blue;
  int redchars, greenchars, bluechars;

  colstr += 4;
  len -= 4;
  colstrend = memchr (colstr, '/', len);
  if (!colstrend)
    return CLR_INVALID;
  redchars = (int) (colstrend - colstr);
  red = color_radix_change (colstr, redchars);
  if (red == -1) return CLR_INVALID;

  len -= (int) (colstrend - colstr + 1);
  colstr = colstrend + 1;
  colstrend = memchr (colstr, '/', len);
  if (!colstrend)
    return CLR_INVALID;
  greenchars = (int) (colstrend - colstr);
  green = color_radix_change (colstr, greenchars);
  if (green == -1) return CLR_INVALID;

  len -= (int) (colstrend - colstr + 1);
  colstr = colstrend + 1;
  bluechars = len;
  blue = color_radix_change (colstr, bluechars);
  if (blue == -1) return CLR_INVALID;

  /*
     We rescale color values in 4bit * <number of chars>,
     then normalize them in 8bit.
  */
  if (redchars > 2)
    red >>= -(8 - redchars * 4);
  else if (redchars < 2)
    red <<= (8 - redchars * 4);
  if (greenchars > 2)
    green >>= -(8 - greenchars * 4);
  else if (greenchars < 2)
    green <<= (8 - greenchars * 4);
  if (bluechars > 2)
    blue >>= -(8 - bluechars * 4);
  else if (bluechars < 2)
    blue <<= (8 - bluechars * 4);

  return RGB (red, green, blue);
}

static COLORREF
x_to_mw32_color (TCHAR *colorname)
{
  COLORREF ret;
  TCHAR *colstr;
  int colstrlen;
  Lisp_Object tail;

  colstr = colorname;
  colstrlen = strlen (colstr);
  if (*colstr == '#')
    {
      return x_old_rgb_names (colstr, colstrlen);
    }

  if (colstrlen > 4) {
    if (memcmp (colstr, "rgb:", 4) == 0)
      {
	return x_rgb_names (colstr, colstrlen);
      }
#if 0
    if (memcmp (colstr, "rgbi:", 5) == 0)
      {
	return x_rgbi_names (arg, colstr, colstrlen);
      }
#endif
  }

  ret = CLR_INVALID;
  for (tail = Vmw32_color_map; CONSP (tail); tail = XCDR (tail))
    {
      register Lisp_Object elt, tem;

      elt = XCAR (tail);
      if (!CONSP (elt)) continue;

      tem = CAR (elt);

      if (colstrlen == LISPY_STRING_BYTES (tem))
	if (memicmp (colstr, SDATA (tem), colstrlen) == 0)
	  {
	    ret = XINT (CDR (elt));
	    break;
	  }
    }

  return ret;
}


/* Gamma-correct COLOR on frame F.  */

COLORREF
gamma_correct (FRAME_PTR f, COLORREF color)
{
  if (f->gamma)
    {
      int r, g, b;
      r = (int) (pow (GetRValue (color) / 256.0, f->gamma) * 256.0 + 0.5);
      g = (int) (pow (GetGValue (color) / 256.0, f->gamma) * 256.0 + 0.5);
      b = (int) (pow (GetBValue (color) / 256.0, f->gamma) * 256.0 + 0.5);
      return RGB (r, g, b);
    }
  return color;
}


/* Decide if color named COLOR_NAME is valid for use on frame F.  If
   so, return the RGB values in COLOR.  If ALLOC_P is non-zero,
   allocate the color.  Value is zero if COLOR_NAME is invalid, or
   no color could be allocated.  */

static int
mw32_defined_color (FRAME_PTR f, char *color_name,
		    COLORREF *color_def, int alloc_p)
{
  COLORREF col;
  col = x_to_mw32_color (color_name);

  if (col == CLR_INVALID) return 0;
  *color_def = col;

  return 1;
}

int
x_defined_color (FRAME_PTR f, char *color_name, XColor *color_def, int alloc_p)
{
  COLORREF col;

  col = x_to_mw32_color (color_name);
  if (col == CLR_INVALID) return 0;

  if (f)
    col = gamma_correct (f, col);

  memset (color_def, 0, sizeof (XColor));
  /* pixel value is used only for specifying GC or window attribute
     in terms of X semantics.  We don't have to emulate it on MW32. */
  color_def->pixel = col;
  color_def->red = GetRValue (col) << 8;
  color_def->green = GetGValue (col) << 8;
  color_def->blue = GetBValue (col) << 8;

  return 1;
}

/* Return the pixel color value for color COLOR_NAME on frame F.  If F
   is a monochrome frame, return MONO_COLOR regardless of what ARG says.
   Signal an error if color can't be allocated.  */

COLORREF
mw32_decode_color (FRAME_PTR f,
		   Lisp_Object color_name,
		   COLORREF mono_color)
{
  COLORREF ret;
  CHECK_STRING (color_name);

#if 0 /* Don't do this.  It's wrong when we're not using the default
	 colormap, it makes freeing difficult, and it's probably not
	 an important optimization.  */
  if (strcmp (SDATA (color_name), "black") == 0)
    return BLACK_PIX_DEFAULT (f);
  else if (strcmp (SDATA (color_name), "white") == 0)
    return WHITE_PIX_DEFAULT (f);
#endif

  /* Return MONO_COLOR for monochrome frames.  */
  if (FRAME_MW32_DISPLAY_INFO (f)->n_planes == 1)
    return mono_color;

  if (mw32_defined_color (f, SDATA (color_name), &ret, 1))
    return ret;

  Fsignal (Qerror, Fcons (build_string ("Undefined color"),
			  Fcons (color_name, Qnil)));
  return 0;
}



/***
    Frame parameter setting functions.
 ***/

/* Functions called only from `x_set_frame_param'
   to set individual parameters.

   If FRAME_X_WINDOW (f) is 0,
   the frame is being created and its X-window does not exist yet.
   In that case, just record the parameter's new value
   in the standard place; do not attempt to change the window.  */

void
mw32_set_foreground_color (FRAME_PTR f, Lisp_Object arg, Lisp_Object old_value)
{
  struct mw32_output *x = f->output_data.mw32;
  COLORREF fg, old_fg;

  fg = mw32_decode_color (f, arg, BLACK_PIX_DEFAULT (f));
  old_fg = FRAME_FOREGROUND_PIXEL (f);
  FRAME_FOREGROUND_PIXEL (f) = fg;

  if (FRAME_MW32_WINDOW (f) != 0)
    {
      update_face_from_frame_parameter (f, Qforeground_color, arg);

      if (FRAME_VISIBLE_P (f))
	redraw_frame (f);
    }

#if 0 /* TODO: color */
  unload_color (f, old_fg);
#endif
}

static void
mw32_set_background_color (FRAME_PTR f, Lisp_Object arg, Lisp_Object old_value)
{
  struct mw32_output *x = f->output_data.mw32;
  COLORREF bg;

  bg = mw32_decode_color (f, arg, WHITE_PIX_DEFAULT (f));
#if 0 /* TODO: color */
  unload_color (f,   FRAME_BACKGROUND_PIXEL (f));
#endif
  FRAME_BACKGROUND_PIXEL (f) = bg;

  if (FRAME_MW32_WINDOW (f) != 0)
    {
      update_face_from_frame_parameter (f, Qbackground_color, arg);

      if (FRAME_VISIBLE_P (f))
	redraw_frame (f);
    }
}

static void
mw32_set_mouse_color (FRAME_PTR f, Lisp_Object arg, Lisp_Object oldval)
{
#if 1

  COLORREF mask_color;
  if (!EQ (Qnil, arg))
    f->output_data.mw32->mouse_pixel
      = mw32_decode_color (f, arg, BLACK_PIX_DEFAULT (f));
  mask_color = FRAME_BACKGROUND_PIXEL (f);
				/* No invisible pointers. */
  if (mask_color == f->output_data.mw32->mouse_pixel
      && mask_color == FRAME_BACKGROUND_PIXEL (f))
    f->output_data.mw32->mouse_pixel = FRAME_FOREGROUND_PIXEL (f);

#else
  struct mw32_output *x = f->output_data.mw32;
  Cursor cursor, nontext_cursor, mode_cursor, cross_cursor;
  Cursor hourglass_cursor, horizontal_drag_cursor;
  int count;
  unsigned long pixel = x_decode_color (f, arg, BLACK_PIX_DEFAULT (f));
  unsigned long mask_color = FRAME_BACKGROUND_PIXEL (f);

  /* Don't let pointers be invisible.  */
  if (mask_color == pixel)
    {
      x_free_colors (f, &pixel, 1);
      pixel = x_copy_color (f, FRAME_FOREGROUND_PIXEL (f))
    }

  unload_color (f, x->mouse_pixel);
  x->mouse_pixel = pixel;

  BLOCK_INPUT;

  /* It's not okay to crash if the user selects a screwy cursor.  */
  count = x_catch_errors (dpy);

  if (!NILP (Vx_pointer_shape))
    {
      CHECK_NUMBER (Vx_pointer_shape);
      cursor = XCreateFontCursor (dpy, XINT (Vx_pointer_shape));
    }
  else
    cursor = XCreateFontCursor (dpy, XC_xterm);
  x_check_errors (dpy, "bad text pointer cursor: %s");

  if (!NILP (Vx_nontext_pointer_shape))
    {
      CHECK_NUMBER (Vx_nontext_pointer_shape);
      nontext_cursor
	= XCreateFontCursor (dpy, XINT (Vx_nontext_pointer_shape));
    }
  else
    nontext_cursor = XCreateFontCursor (dpy, XC_left_ptr);
  x_check_errors (dpy, "bad nontext pointer cursor: %s");

  if (!NILP (Vx_hourglass_pointer_shape))
    {
      CHECK_NUMBER (Vx_hourglass_pointer_shape);
      hourglass_cursor
	= XCreateFontCursor (dpy, XINT (Vx_hourglass_pointer_shape));
    }
  else
    hourglass_cursor = XCreateFontCursor (dpy, XC_watch);
  x_check_errors (dpy, "bad hourglass pointer cursor: %s");

  x_check_errors (dpy, "bad nontext pointer cursor: %s");
  if (!NILP (Vx_mode_pointer_shape))
    {
      CHECK_NUMBER (Vx_mode_pointer_shape);
      mode_cursor = XCreateFontCursor (dpy, XINT (Vx_mode_pointer_shape));
    }
  else
    mode_cursor = XCreateFontCursor (dpy, XC_xterm);
  x_check_errors (dpy, "bad modeline pointer cursor: %s");

  if (!NILP (Vx_sensitive_text_pointer_shape))
    {
      CHECK_NUMBER (Vx_sensitive_text_pointer_shape);
      cross_cursor
	= XCreateFontCursor (dpy, XINT (Vx_sensitive_text_pointer_shape));
    }
  else
    cross_cursor = XCreateFontCursor (dpy, XC_crosshair);

  if (!NILP (Vx_window_horizontal_drag_shape))
    {
      CHECK_NUMBER (Vx_window_horizontal_drag_shape);
      horizontal_drag_cursor
	= XCreateFontCursor (dpy, XINT (Vx_window_horizontal_drag_shape));
    }
  else
    horizontal_drag_cursor
      = XCreateFontCursor (dpy, XC_sb_h_double_arrow);

  /* Check and report errors with the above calls.  */
  x_check_errors (dpy, "can't set cursor shape: %s");
  x_uncatch_errors (dpy, count);

  {
    XColor fore_color, back_color;

    fore_color.pixel = x->mouse_pixel;
    x_query_color (f, &fore_color);
    back_color.pixel = mask_color;
    x_query_color (f, &back_color);

    XRecolorCursor (dpy, cursor, &fore_color, &back_color);
    XRecolorCursor (dpy, nontext_cursor, &fore_color, &back_color);
    XRecolorCursor (dpy, mode_cursor, &fore_color, &back_color);
    XRecolorCursor (dpy, cross_cursor, &fore_color, &back_color);
    XRecolorCursor (dpy, hourglass_cursor, &fore_color, &back_color);
    XRecolorCursor (dpy, horizontal_drag_cursor, &fore_color, &back_color);
  }

  if (FRAME_X_WINDOW (f) != 0)
    XDefineCursor (dpy, FRAME_X_WINDOW (f), cursor);

  if (cursor != x->text_cursor
      && x->text_cursor != 0)
    XFreeCursor (dpy, x->text_cursor);
  x->text_cursor = cursor;

  if (nontext_cursor != x->nontext_cursor
      && x->nontext_cursor != 0)
    XFreeCursor (dpy, x->nontext_cursor);
  x->nontext_cursor = nontext_cursor;

  if (hourglass_cursor != x->hourglass_cursor
      && x->hourglass_cursor != 0)
    XFreeCursor (dpy, x->hourglass_cursor);
  x->hourglass_cursor = hourglass_cursor;

  if (mode_cursor != x->modeline_cursor
      && x->modeline_cursor != 0)
    XFreeCursor (dpy, f->output_data.x->modeline_cursor);
  x->modeline_cursor = mode_cursor;

  if (cross_cursor != x->cross_cursor
      && x->cross_cursor != 0)
    XFreeCursor (dpy, x->cross_cursor);
  x->cross_cursor = cross_cursor;

  if (horizontal_drag_cursor != x->horizontal_drag_cursor
      && x->horizontal_drag_cursor != 0)
    XFreeCursor (dpy, x->horizontal_drag_cursor);
  x->horizontal_drag_cursor = horizontal_drag_cursor;

  XFlush (dpy);
  UNBLOCK_INPUT;
#endif
  update_face_from_frame_parameter (f, Qmouse_color, arg);
}

Cursor
mw32_load_cursor (LPCTSTR name)
{
  /* Try first to load cursor from application resource.  */
  Cursor cursor = LoadImage ((HINSTANCE) GetModuleHandle(NULL),
			     name, IMAGE_CURSOR, 0, 0,
			     LR_DEFAULTCOLOR | LR_DEFAULTSIZE | LR_SHARED);
  if (!cursor)
    {
      /* Then try to load a shared predefined cursor.  */
      cursor = LoadImage (NULL, name, IMAGE_CURSOR, 0, 0,
			  LR_DEFAULTCOLOR | LR_DEFAULTSIZE | LR_SHARED);
    }
  return cursor;
}

static void
mw32_set_cursor_color (FRAME_PTR f, Lisp_Object arg, Lisp_Object oldval)
{
#if 1
  unsigned long fore_pixel;
  struct mw32_output *out = FRAME_MW32_OUTPUT (f);

  if (!EQ (Vmw32_cursor_fore_pixel, Qnil))
    fore_pixel = mw32_decode_color (f, Vmw32_cursor_fore_pixel,
				    WHITE_PIX_DEFAULT (f));
  else
    fore_pixel = FRAME_BACKGROUND_PIXEL (f);
  out->cursor_pixel = mw32_decode_color (f, arg, BLACK_PIX_DEFAULT (f));

  /* Make sure that the cursor color differs from the background color.  */
  if (out->cursor_pixel == FRAME_BACKGROUND_PIXEL (f))
    {
      out->cursor_pixel = out->mouse_pixel;
      if (out->cursor_pixel == fore_pixel)
	fore_pixel = FRAME_BACKGROUND_PIXEL (f);
    }
  out->cursor_foreground_pixel = fore_pixel;

  if (FRAME_MW32_WINDOW (f) != 0)
    {
      if (FRAME_VISIBLE_P (f))
	{
	  x_update_cursor (f, 0);
	  x_update_cursor (f, 1);
	}
    }
#else
  unsigned long fore_pixel, pixel;
  int fore_pixel_allocated_p = 0, pixel_allocated_p = 0;
  struct mw32_output *x = f->output_data.mw32;

  if (!NILP (Vx_cursor_fore_pixel))
    {
      fore_pixel = x_decode_color (f, Vmw32_cursor_fore_pixel,
				   WHITE_PIX_DEFAULT (f));
      fore_pixel_allocated_p = 1;
    }
  else
    fore_pixel = FRAME_BACKGROUND_PIXEL (f);

  pixel = x_decode_color (f, arg, BLACK_PIX_DEFAULT (f));
  pixel_allocated_p = 1;

  /* Make sure that the cursor color differs from the background color.  */
  if (pixel == FRAME_BACKGROUND_PIXEL (f))
    {
      if (pixel_allocated_p)
	{
	  x_free_colors (f, &pixel, 1);
	  pixel_allocated_p = 0;
	}

      pixel = x->mouse_pixel;
      if (pixel == fore_pixel)
	{
	  if (fore_pixel_allocated_p)
	    {
	      x_free_colors (f, &fore_pixel, 1);
	      fore_pixel_allocated_p = 0;
	    }
	  fore_pixel = FRAME_BACKGROUND_PIXEL (f);
	}
    }

  unload_color (f, x->cursor_foreground_pixel);
  if (!fore_pixel_allocated_p)
    fore_pixel = x_copy_color (f, fore_pixel);
  x->cursor_foreground_pixel = fore_pixel;

  unload_color (f, x->cursor_pixel);
  if (!pixel_allocated_p)
    pixel = x_copy_color (f, pixel);
  x->cursor_pixel = pixel;

  if (FRAME_X_WINDOW (f) != 0)
    {
      BLOCK_INPUT;
      XSetBackground (FRAME_X_DISPLAY (f), x->cursor_gc, x->cursor_pixel);
      XSetForeground (FRAME_X_DISPLAY (f), x->cursor_gc, fore_pixel);
      UNBLOCK_INPUT;

      if (FRAME_VISIBLE_P (f))
	{
	  x_update_cursor (f, 0);
	  x_update_cursor (f, 1);
	}
    }
#endif

  update_face_from_frame_parameter (f, Qcursor_color, arg);
}

/* Set the border-color of frame F to value described by ARG.
   ARG can be a string naming a color.
   The border-color is used for the border that is drawn by the X server.
   Note that this does not fully take effect if done before
   F has an x-window; it must be redone when the window is created.

   Note: this is done in two routines because of the way X10 works.

   Note: under X11, this is normally the province of the window manager,
   and so emacs' border colors may be overridden.  */

static void
mw32_set_border_color (FRAME_PTR f, Lisp_Object arg, Lisp_Object oldval)
{
  COLORREF pix;

  CHECK_STRING (arg);

  pix = mw32_decode_color (f, arg, BLACK_PIX_DEFAULT (f));
  f->output_data.mw32->border_pixel = pix;

  if ((FRAME_MW32_WINDOW (f) != 0)
      && (f->border_width > 0))
    {
      if (FRAME_VISIBLE_P (f))
	redraw_frame (f);
    }

  update_face_from_frame_parameter (f, Qborder_color, arg);
}

static void
mw32_set_cursor_type (FRAME_PTR f, Lisp_Object arg, Lisp_Object oldval)
{
  set_frame_cursor_types (f, arg);

  /* Make sure the cursor gets redrawn.  */
  cursor_type_changed = 1;
}

static void
mw32_set_cursor_height (FRAME_PTR f, Lisp_Object arg, Lisp_Object oldval)
{
  int old= f->output_data.mw32->cursor_height;

  CHECK_NUMBER (arg);

  if (MW32_FRAME_CARET_HEIGHT (f) == XINT (arg))
    return;

  MW32_FRAME_CARET_HEIGHT (f) = XINT (arg);
}

/* defined in mw32term.c */
extern void mw32_update_frame_alpha (struct frame *f);
extern SETLAYEREDWINDOWATTRPROC SetLayeredWindowAttributesProc;

#define CHECK_ALPHA_RANGE(alpha) if (alpha < 0 || alpha > 100)	\
    args_out_of_range (make_number (0), make_number (100));

static void
mw32_set_frame_alpha (FRAME_PTR f, Lisp_Object arg, Lisp_Object oldval)
{
  int newalpha[NUM_OF_ALPHAS];
  int i, tmp;
  Lisp_Object obj;

  if (SetLayeredWindowAttributesProc == NULL)
    return;

  if (NILP (arg)
      || (CONSP (arg) && NILP (CAR (arg))))
    tmp = -1;
  else if (NUMBERP (arg))
    {
      tmp = XINT (arg);
      CHECK_ALPHA_RANGE (tmp);
    }
  else if (CONSP (arg) && NUMBERP (CAR (arg)))
    {
      tmp = XINT (CAR (arg));
      CHECK_ALPHA_RANGE (tmp);
    }
  else
    wrong_type_argument (Qnumberp, (arg));

  for (i = 0 ; i < NUM_OF_ALPHAS ; i++)
    newalpha[i] = tmp;

  if (CONSP (arg))
    {
      obj = CDR (arg);
      for (i = ALPHA_INACTIVE ;
	   i < NUM_OF_ALPHAS && CONSP (obj) ;
	   i++, obj = CDR (obj))
	{
	  if (NILP (CAR (obj)))
	    tmp = -1;
	  else if (NUMBERP (CAR (obj)))
	    {
	      tmp = XINT (CAR (obj));
	      CHECK_ALPHA_RANGE (tmp);
	    }
	  else
	    wrong_type_argument (Qnumberp, (CAR (obj)));

	  newalpha[i] = tmp;
	}
    }

  for (i = 0 ; i < NUM_OF_ALPHAS ; i++)
    {
      /* Apply lower limit silently */
      if (newalpha[i] != -1 && newalpha[i] < mw32_frame_alpha_lower_limit)
	newalpha[i] = mw32_frame_alpha_lower_limit;

      f->output_data.mw32->alpha[i] = newalpha[i];
    }

  mw32_update_frame_alpha (f);
}


#ifdef IME_CONTROL
void
mw32_set_frame_ime_font (FRAME_PTR f, Lisp_Object arg, Lisp_Object oldval)
{
#if 0				/* ??? */
  MSG msg;
#endif

  if (! NILP (arg))
    {
      CHECK_LIST (arg);
      {
	extern Lisp_Object Qw32_logfont; /* mw32font.c */
	Lisp_Object tmpcar = CAR (arg);
	CHECK_SYMBOL (tmpcar);
	if (!EQ (tmpcar, Qw32_logfont))
	  error ("ime-font: invalid logfont `%s'.",
		 SDATA (Fprin1_to_string (arg, Qt)));
      }
    }
  mw32font_set_frame_ime_font_by_llogfont (f, arg);

#if 0				/* ??? */
  WAIT_REPLY_MESSAGE (&msg, WM_MULE_IMM_SET_COMPOSITION_FONT_REPLY);
#endif
}
#endif


void
x_set_menu_bar_lines (FRAME_PTR f, Lisp_Object value, Lisp_Object oldval)
{
  int nlines;
  int olines = FRAME_MENU_BAR_LINES (f);

  /* Right now, menu bars don't work properly in minibuf-only frames;
     most of the commands try to apply themselves to the minibuffer
     frame itself, and get an error because you can't switch buffers
     in or split the minibuffer window.  */
  if (FRAME_MINIBUF_ONLY_P (f))
    return;

  if (INTEGERP (value))
    nlines = XINT (value);
  else
    nlines = 0;

  FRAME_MENU_BAR_LINES (f) = 0;
  if (nlines)
    FRAME_EXTERNAL_MENU_BAR (f) = 1;
  else
    {
      if (FRAME_EXTERNAL_MENU_BAR (f) == 1)
	free_frame_menubar (f);
      FRAME_EXTERNAL_MENU_BAR (f) = 0;

      /* Adjust the frame size so that the client (text) dimensions
	 remain the same.  This depends on FRAME_EXTERNAL_MENU_BAR being
	 set correctly.  */
      x_set_window_size (f, 0, FRAME_COLS (f), FRAME_LINES (f));
      do_pending_window_change (0);
    }
  adjust_glyphs (f);
}


/* Set the number of lines used for the tool bar of frame F to VALUE.
   VALUE not an integer, or < 0 means set the lines to zero.  OLDVAL
   is the old number of tool bar lines.  This function changes the
   height of all windows on frame F to match the new tool bar height.
   The frame's height doesn't change.  */

void
x_set_tool_bar_lines (FRAME_PTR f, Lisp_Object value, Lisp_Object oldval)
{
  int delta, nlines, root_height;
  Lisp_Object root_window;

  /* Treat tool bars like menu bars.  */
  if (FRAME_MINIBUF_ONLY_P (f))
    return;

  /* Use VALUE only if an integer >= 0.  */
  if (INTEGERP (value) && XINT (value) >= 0)
    nlines = XFASTINT (value);
  else
    nlines = 0;

  /* Make sure we redisplay all windows in this frame.  */
  ++windows_or_buffers_changed;

  delta = nlines - FRAME_TOOL_BAR_LINES (f);

  GET_FRAME_HDC (f);

  /* Don't resize the tool-bar to more than we have room for.  */
  root_window = FRAME_ROOT_WINDOW (f);
  root_height = WINDOW_TOTAL_LINES (XWINDOW (root_window));
  if (root_height - delta < 1)
    {
      delta = root_height - 1;
      nlines = FRAME_TOOL_BAR_LINES (f) + delta;
    }

  FRAME_TOOL_BAR_LINES (f) = nlines;
  change_window_heights (root_window, delta);
  adjust_glyphs (f);

  /* We also have to make sure that the internal border at the top of
     the frame, below the menu bar or tool bar, is redrawn when the
     tool bar disappears.  This is so because the internal border is
     below the tool bar if one is displayed, but is below the menu bar
     if there isn't a tool bar.  The tool bar draws into the area
     below the menu bar.  */
  if (FRAME_MW32_WINDOW (f) && FRAME_TOOL_BAR_LINES (f) == 0)
    {
      updating_frame = f;
      clear_frame ();
      clear_current_matrices (f);
      updating_frame = NULL;
    }

  /* If the tool bar gets smaller, the internal border below it
     has to be cleared.  It was formerly part of the display
     of the larger tool bar, and updating windows won't clear it.  */

  /* The condition to clear the internal border below was changed,
     because it is not cleared when the tool bar gets larger.  */
  if (delta != 0)
    {
      int height = FRAME_INTERNAL_BORDER_WIDTH (f);
      int width = FRAME_PIXEL_WIDTH (f);
      int y = nlines * FRAME_LINE_HEIGHT (f);
      struct frame *f2;

      f2 = (updating_frame ? updating_frame : SELECTED_FRAME ());
      if (FRAME_WINDOW_P (f2))
	{
	  GET_FRAME_HDC (f2);

	  BLOCK_INPUT;
	  mw32_clear_native_frame_area (f2, 0, y, width, y + height);
	  UNBLOCK_INPUT;

	  if (WINDOWP (f2->tool_bar_window))
	    clear_glyph_matrix (XWINDOW (f2->tool_bar_window)->current_matrix);

	  RELEASE_FRAME_HDC (f2);
	}
    }
  RELEASE_FRAME_HDC (f);
}


/* Set the foreground color for scroll bars on frame F to VALUE.
   VALUE should be a string, a color name.  If it isn't a string or
   isn't a valid color name, do nothing.  OLDVAL is the old value of
   the frame parameter.  */

static void
mw32_set_scroll_bar_foreground (FRAME_PTR f, Lisp_Object value,
				Lisp_Object oldval)
{
  COLORREF pixel;

  if (STRINGP (value))
    pixel = mw32_decode_color (f, value, BLACK_PIX_DEFAULT (f));
  else
    pixel = CLR_INVALID;

#if 0
  if (f->output_data.mw32->scroll_bar_foreground_pixel != CLR_INVALID)
    unload_color (f, f->output_data.mw32->scroll_bar_foreground_pixel);
#endif

  f->output_data.mw32->scroll_bar_foreground_pixel = pixel;
  if (FRAME_MW32_WINDOW (f) && FRAME_VISIBLE_P (f))
    {
      /* Remove all scroll bars because they have wrong colors.  */
      if (condemn_scroll_bars_hook)
	(*condemn_scroll_bars_hook) (f);
      if (judge_scroll_bars_hook)
	(*judge_scroll_bars_hook) (f);

      update_face_from_frame_parameter (f, Qscroll_bar_foreground, value);
      redraw_frame (f);
    }
}


/* Set the background color for scroll bars on frame F to VALUE VALUE
   should be a string, a color name.  If it isn't a string or isn't a
   valid color name, do nothing.  OLDVAL is the old value of the frame
   parameter.  */

static void
mw32_set_scroll_bar_background (FRAME_PTR f, Lisp_Object value,
				Lisp_Object oldval)
{
  COLORREF pixel;

  if (STRINGP (value))
    pixel = mw32_decode_color (f, value, WHITE_PIX_DEFAULT (f));
  else
    pixel = CLR_INVALID;

#if 0
  if (f->output_data.mw32->scroll_bar_background_pixel != CLR_INVALID)
    unload_color (f, f->output_data.mw32->scroll_bar_background_pixel);
#endif

  f->output_data.mw32->scroll_bar_background_pixel = pixel;
  if (FRAME_MW32_WINDOW (f) && FRAME_VISIBLE_P (f))
    {
      /* Remove all scroll bars because they have wrong colors.  */
      if (condemn_scroll_bars_hook)
	(*condemn_scroll_bars_hook) (f);
      if (judge_scroll_bars_hook)
	(*judge_scroll_bars_hook) (f);

      update_face_from_frame_parameter (f, Qscroll_bar_background, value);
      redraw_frame (f);
    }
}


/* Prepare to encode Lisp string as a text in a format appropriate for
   Windows.
   Not to recommend to use this function alone.  Use with the macro,
   MW32_ENCODE_TEXT().  */

int
mw32_encode_text_prepare (Lisp_Object coding_system,
			  struct coding_system *coding,
			  int bytes)
{
  setup_coding_system (coding_system, coding);
  coding->mode |= CODING_MODE_LAST_BLOCK;
  if (coding->type == coding_type_iso2022)
    coding->flags |= CODING_FLAG_ISO_SAFE;
  /* We suppress producing escape sequences for composition.  */
  coding->composing = COMPOSITION_DISABLED;
  return encoding_buffer_size (coding, bytes);
}

/*
   Encode str in Lisp String to string for external systems,
   which include Windows APIs.
   This function returns a string in LPTSTR, and set the size
   in byte to *psize only if psize is not NULL.
*/
LPTSTR
mw32_encode_lispy_string (Lisp_Object coding_system,
			  Lisp_Object str,
			  int *psize)
{
  str = code_convert_string_norecord (str, coding_system, 1);
  if (psize) *psize = LISPY_STRING_BYTES (str);

  return (LPTSTR) SDATA (str);
}

/*
   Decode tstr in LPTSTR to Lisp String for Emacs system.
   This function returns a Lisp String.  If size is not 0,
   it is regarded as size of tstr in byte.  If size is 0,
   this function call lstrlen to count the byte size of tstr.
*/
Lisp_Object
mw32_decode_lispy_string (Lisp_Object coding_system,
			  LPTSTR tstr, int size)
{
  Lisp_Object str;
  if (size == 0) {
    size = lstrlen (tstr);
  }
  str = make_string (tstr, size);
  return code_convert_string_norecord (str, coding_system, 0);
}


/* Change the name of frame F to NAME.  If NAME is nil, set F's name to
       x_id_name.

   If EXPLICIT is non-zero, that indicates that lisp code is setting the
       name; if NAME is a string, set F's name to NAME and set
       F->explicit_name; if NAME is Qnil, then clear F->explicit_name.

   If EXPLICIT is zero, that indicates that Emacs redisplay code is
       suggesting a new name, which lisp code should override; if
       F->explicit_name is set, ignore the new name; otherwise, set it.  */

static void
mw32_set_name (FRAME_PTR f, Lisp_Object name, int explicit)
{
  /* Make sure that requests from lisp code override requests from
     Emacs redisplay code.  */
  if (explicit)
    {
      /* If we're switching from explicit to implicit, we had better
	 update the mode lines and thereby update the title.  */
      if (f->explicit_name && NILP (name))
	update_mode_lines = 1;

      f->explicit_name = ! NILP (name);
    }
  else if (f->explicit_name)
    return;

  /* If NAME is nil, set the name to the x_id_name.  */
  if (NILP (name))
    {
      /* Check for no change needed in this very common case
	 before we do any consing.  */
      if (!strcmp (FRAME_MW32_DISPLAY_INFO (f)->mw32_id_name, SDATA (f->name)))
	return;
      name = build_string (FRAME_MW32_DISPLAY_INFO (f)->mw32_id_name);
    }
  else
    CHECK_STRING (name);

  /* Don't change the name if it's already NAME.  */
  if (! NILP (Fstring_equal (name, f->name)))
    return;

  f->name = name;

  /* For setting the frame title, the title parameter should override
     the name parameter.  */
  if (! NILP (f->title))
    name = f->title;

  if (FRAME_MW32_WINDOW (f))
    {
      int size;
      char *ttext;
      MW32_ENCODE_TEXT (name, Vlocale_coding_system, &ttext, &size);
      BLOCK_INPUT;
      SetWindowText (FRAME_MW32_WINDOW (f), ttext);
      UNBLOCK_INPUT;
    }
}

/* This function should be called when the user's lisp code has
   specified a name for the frame; the name will override any set by the
   redisplay code.  */
static void
mw32_explicitly_set_name (FRAME_PTR f, Lisp_Object arg, Lisp_Object oldval)
{
  mw32_set_name (f, arg, 1);
}

/* This function should be called by Emacs redisplay code to set the
   name; names set this way will never override names set by the user's
   lisp code.  */
void
mw32_implicitly_set_name (FRAME_PTR f, Lisp_Object arg, Lisp_Object oldval)
{
  mw32_set_name (f, arg, 0);
}


/* Change the title of frame F to NAME.
   If NAME is nil, use the frame name as the title.

   If EXPLICIT is non-zero, that indicates that lisp code is setting the
       name; if NAME is a string, set F's name to NAME and set
       F->explicit_name; if NAME is Qnil, then clear F->explicit_name.

   If EXPLICIT is zero, that indicates that Emacs redisplay code is
       suggesting a new name, which lisp code should override; if
       F->explicit_name is set, ignore the new name; otherwise, set it.  */

static void
mw32_set_title (FRAME_PTR f, Lisp_Object name, Lisp_Object old_name)
{
  /* Don't change the title if it's already NAME.  */
  if (EQ (name, f->title))
    return;

  update_mode_lines = 1;

  f->title = name;

  if (NILP (name))
    name = f->name;
  else
    CHECK_STRING (name);

  if (FRAME_MW32_WINDOW (f))
    {
      int size;
      char *ttext;
      MW32_ENCODE_TEXT (name, Vlocale_coding_system, &ttext, &size);
      BLOCK_INPUT;
      SetWindowText (FRAME_MW32_WINDOW (f), ttext);
      UNBLOCK_INPUT;
    }
}



/***********************************************************************
			X resource emulation.
 ***********************************************************************/
void
x_set_scroll_bar_default_width (struct frame *f)
{
  int wid = FRAME_COLUMN_WIDTH (f);

#ifdef USE_TOOLKIT_SCROLL_BARS
  /* A minimum width of 14 doesn't look good for toolkit scroll bars.  */
  int width = 16 + 2 * VERTICAL_SCROLL_BAR_WIDTH_TRIM;
  FRAME_CONFIG_SCROLL_BAR_COLS (f) = (width + wid - 1) / wid;
  FRAME_CONFIG_SCROLL_BAR_WIDTH (f) = width;
#else
  /* Make the actual width at least 14 pixels and a multiple of a
     character width.  */
  FRAME_CONFIG_SCROLL_BAR_COLS (f) = (14 + wid - 1) / wid;

  /* Use all of that space (aside from required margins) for the
     scroll bar.  */
  FRAME_CONFIG_SCROLL_BAR_WIDTH (f) = 0;
#endif
}



/***********************************************************************
			Create Window of Frame
 ***********************************************************************/

void mw32m_create_frame_window (struct frame *f, LPSTR title)
{
  HWND hwnd;

  hwnd = CreateWindowEx (f->output_data.mw32->dwStyleEx,
			 EMACS_CLASS,
			 (LPSTR) title,
			 f->output_data.mw32->dwStyle,
			 f->left_pos,
			 f->top_pos,
			 FRAME_PIXEL_WIDTH (f),
			 FRAME_PIXEL_HEIGHT (f),
			 NULL,
			 NULL,
			 hinst,
			 NULL);
  POST_THREAD_INFORM_MESSAGE (main_thread_id, WM_EMACS_CREATE_FRAME_REPLY,
			      (WPARAM) hwnd, (LPARAM) 0);
  DragAcceptFiles (hwnd, TRUE);

  if (mw32_frame_window == INVALID_HANDLE_VALUE)
    mw32_frame_window = hwnd;
}

static void
mw32m_create_tip_frame_window (struct frame *f)
{
  HWND hwnd;

  hwnd = CreateWindowEx (WS_EX_TOOLWINDOW, /* hide icon on task bar */
			 EMACS_CLASS,
			 f->namebuf,
			 f->output_data.mw32->dwStyle,
			 f->left_pos,
			 f->top_pos,
			 FRAME_PIXEL_WIDTH (f),
			 FRAME_PIXEL_HEIGHT (f),
			 NULL, /* root window is owner */
			 NULL,
			 hinst,
			 NULL);

  POST_THREAD_INFORM_MESSAGE (main_thread_id,
			      WM_EMACS_CREATE_TIP_FRAME_REPLY,
			      (WPARAM) hwnd, (LPARAM) 0);
}

static void
mw32m_create_scrollbar (HWND hwnd_parent,
			LPRECT lprect, HINSTANCE hinst)
{
  HWND hwnd;

  hwnd = CreateWindowEx (0L,
			 "SCROLLBAR",
			 (LPSTR) NULL,
			 WS_CHILD | WS_VISIBLE | SBS_VERT,
			 lprect->left,
			 lprect->top,
			 lprect->right,
			 lprect->bottom,
			 hwnd_parent,
			 (HMENU) NULL,
			 hinst, (LPVOID) NULL);
  POST_THREAD_INFORM_MESSAGE (main_thread_id,
			      WM_EMACS_CREATE_SCROLLBAR_REPLY,
			      (WPARAM) hwnd, (LPARAM) 0);
  return;
}

static void
mw32m_destroy_frame (HWND hwnd)
{
  Lisp_Object tail, frame;
  HWND hnextwnd;
  struct frame *f;

  ReplyMessage (1);
  DestroyWindow (hwnd);

  /* When mw32_frame_window is destroyed,
     set the other window handle if possible. */
  if (hwnd == mw32_frame_window)
    {
      mw32_frame_window = INVALID_HANDLE_VALUE;
      for (tail = Vframe_list; CONSP (tail);
	   tail = XCONS (tail)->u.cdr)
	{
	  frame = XCONS (tail)->car;
	  if (!FRAMEP (frame)) continue;
	  f = XFRAME (frame);
	  if (f->output_data.nothing == 1)
	    break;
	  hnextwnd = FRAME_MW32_WINDOW (f);
	  if ((hnextwnd != hwnd)
	      && IsWindow (hnextwnd))
	    {
	      mw32_frame_window = hnextwnd;
	      break;
	    }
	}
    }

  POST_THREAD_INFORM_MESSAGE (main_thread_id,
			      WM_EMACS_DESTROY_FRAME_REPLY,
			      (WPARAM) 0, (LPARAM) 0);
  return;
}

static void
mw32m_create_clpbd (void)
{
  static const char CLASSNAME[] = "Emacs Clipboard";
  HWND hwnd;
 
  hwnd = CreateWindow (CLASSNAME, CLASSNAME, 0, 0, 0, 0, 0, NULL, NULL,
		       NULL, NULL);

  POST_THREAD_INFORM_MESSAGE (main_thread_id, WM_EMACS_CREATE_CLPBD_REPLY,
			      (WPARAM) hwnd, (LPARAM) 0);
}

static void
mw32m_track_popup_menu (HWND parent, HANDLE hmenu, LPPOINT lppos)
{
  int flag;
  UINT track_flag;
  MSG msg2;

  track_flag = TPM_LEFTALIGN;
  if (GetAsyncKeyState (VK_LBUTTON) &0x8000) track_flag |= TPM_LEFTBUTTON;
  if (GetAsyncKeyState (VK_RBUTTON) &0x8000) track_flag |= TPM_RIGHTBUTTON;

  lock_mouse_cursor_visible (TRUE);

  flag = TrackPopupMenu (hmenu,
			 track_flag,
			 lppos->x, lppos->y,
			 0,
			 parent,
			 NULL);

  lock_mouse_cursor_visible (FALSE);

  if (!flag)
    POST_THREAD_INFORM_MESSAGE (main_thread_id,
				WM_EMACS_POPUP_MENU_REPLY,
				(WPARAM) 0, (LPARAM) 0);
  else
    {
      flag = PeekMessage (&msg2, parent, WM_COMMAND, WM_COMMAND, PM_REMOVE);
      if (flag
	  && ((msg2.message == WM_COMMAND)
	      && (HIWORD (msg2.wParam) == 0)))
	{
	  POST_THREAD_INFORM_MESSAGE (main_thread_id,
				      WM_EMACS_POPUP_MENU_REPLY,
				      (WPARAM) msg2.wParam, (LPARAM) 0);
	}
      else
	{
	  POST_THREAD_INFORM_MESSAGE (main_thread_id,
				      WM_EMACS_POPUP_MENU_REPLY,
				      (WPARAM) 0, (LPARAM) 0);
	}
    }
}

#ifdef IME_CONTROL
void mw32m_ime_create_agent ()
{
  HWND hwnd;
  hwnd = CreateWindowEx (0L,
			 CONVAGENT_CLASS, "Agent",
			 0, /* STYLE */
			 0, 0, 0, 0,
			 NULL,
			 NULL,
			 hinst,
			 NULL);
  POST_THREAD_INFORM_MESSAGE (main_thread_id, WM_MULE_IME_CREATE_AGENT_REPLY,
			      (WPARAM) hwnd, (LPARAM) 0);
}

static void
mw32m_ime_destroy_agent (HWND hwnd)
{
  DestroyWindow (hwnd);
  POST_THREAD_INFORM_MESSAGE (main_thread_id,
			      WM_MULE_IME_DESTROY_AGENT_REPLY,
			      (WPARAM) 0, (LPARAM) 0);
}
#endif /* IME_CONTROL */

#ifndef W32_VER4
static HANDLE
mw32_ime_string_handle (HANDLE hStr)
{
  HANDLE hw32_ir_string;
  LPTSTR lpStr;
  LPTSTR lpCode;

  if (!hStr) return 0;
  lpStr = GlobalLock (hStr);
  if (!lpStr) return 0;
  hw32_ir_string =
    GlobalAlloc (GMEM_MOVEABLE | GMEM_SHARE,
		 strlen (lpStr) + 1);
  if (!hw32_ir_string) abort ();
  lpCode = GlobalLock (hw32_ir_string);
  strcpy (lpCode, lpStr);
  GlobalUnlock (hw32_ir_string);
  GlobalUnlock (hStr);
  return hw32_ir_string;
}
#endif

static void
mw32m_flash_window (struct frame *f)
{
  PIX_TYPE pixel = FRAME_FOREGROUND_PIXEL (f) ^ FRAME_BACKGROUND_PIXEL (f);
  HDC hdc = GetDC (FRAME_MW32_WINDOW (f));
  HBRUSH hbrush = CreateSolidBrush (pixel);
  HBRUSH old_brush = SelectObject (hdc, hbrush);

  /* Get the height not including a menu bar widget.  */
  int height = FRAME_TEXT_LINES_TO_PIXEL_HEIGHT (f, FRAME_LINES (f));
  /* Height of each line to flash.  */
  int flash_height = FRAME_LINE_HEIGHT (f);
  /* These will be the left and right margins of the rectangles.  */
  int flash_left = FRAME_INTERNAL_BORDER_WIDTH (f);
  int flash_right = FRAME_PIXEL_WIDTH (f) - FRAME_INTERNAL_BORDER_WIDTH (f);
  int width;

  /* Don't flash the area between a scroll bar and the frame
     edge it is next to.  */
  switch (FRAME_VERTICAL_SCROLL_BAR_TYPE (f))
    {
    case vertical_scroll_bar_left:
      flash_left += VERTICAL_SCROLL_BAR_WIDTH_TRIM;
      break;

    case vertical_scroll_bar_right:
      flash_right -= VERTICAL_SCROLL_BAR_WIDTH_TRIM;
      break;

    default:
      break;
    }

  width = flash_right - flash_left;

  /* If window is tall, flash top and bottom line.  */
  if (height > 3 * FRAME_LINE_HEIGHT (f))
    {
      int x = flash_left;
      int y = FRAME_INTERNAL_BORDER_WIDTH (f)
	      + FRAME_TOOL_BAR_LINES (f) * FRAME_LINE_HEIGHT (f);

      BitBlt (hdc, x, y, width, flash_height, hdc, x, y, PATINVERT);

      y = height - flash_height - FRAME_INTERNAL_BORDER_WIDTH (f);

      BitBlt (hdc, x, y, width, flash_height, hdc, x, y, PATINVERT);
    }
  else
    {
      /* If it is short, flash it all.  */
      int x = flash_left;
      int y = FRAME_INTERNAL_BORDER_WIDTH (f);
      int reverse_height = height - 2 * FRAME_INTERNAL_BORDER_WIDTH (f);

      BitBlt (hdc, x, y, width, reverse_height, hdc, x, y, PATINVERT);
    }

  GdiFlush ();

  Sleep(100);

  /* If window is tall, flash top and bottom line.  */
  if (height > 3 * FRAME_LINE_HEIGHT (f))
    {
      int x = flash_left;
      int y = FRAME_INTERNAL_BORDER_WIDTH (f)
	      + FRAME_TOOL_BAR_LINES (f) * FRAME_LINE_HEIGHT (f);

      BitBlt (hdc, x, y, width, flash_height, hdc, x, y, PATINVERT);

      y = height - flash_height - FRAME_INTERNAL_BORDER_WIDTH (f);

      BitBlt (hdc, x, y, width, flash_height, hdc, x, y, PATINVERT);
    }
  else
    {
      /* If it is short, flash it all.  */
      int x = flash_left;
      int y = FRAME_INTERNAL_BORDER_WIDTH (f);
      int reverse_height = height - 2 * FRAME_INTERNAL_BORDER_WIDTH (f);

      BitBlt (hdc, x, y, width, reverse_height, hdc, x, y, PATINVERT);
    }

  GdiFlush ();
  SelectObject (hdc, old_brush);
  DeleteObject (hbrush);
  ReleaseDC (FRAME_MW32_WINDOW (f), hdc);
}

/* Multi monitor APIs */
#if WINVER < 0x0500 && !defined (__MINGW32__)
typedef struct tagMONITORINFO
{
  DWORD   cbSize;
  RECT    rcMonitor;
  RECT    rcWork;
  DWORD   dwFlags;
} MONITORINFO, *LPMONITORINFO;
#endif /* WINVER < 0x0500 && !defined (__MINGW32__) */

#if !defined (MONITOR_DEFAULTTONEAREST)
#define MONITOR_DEFAULTTONEAREST    0x00000002
#endif /* MONITOR_DEFAULTTONEAREST */

#if !defined (HMONITOR_DECLARED) && WINVER < 0x0500
DECLARE_HANDLE (HMONITOR);
#endif /* not HMONITOR_DECLARED && WINVER < 0x0500 */

typedef HMONITOR (WINAPI *MONITORFROMWINDOWPROC) (HWND, DWORD);
static MONITORFROMWINDOWPROC MonitorFromWindowProc = NULL;

typedef BOOL (WINAPI *GETMONITORINFOPROC) (HMONITOR, LPMONITORINFO);
static GETMONITORINFOPROC GetMonitorInfoProc = NULL;

static int multi_monitor_api_valid_p = 0;

static void
initialze_multi_monitor_api (void)
{
  HMODULE hUser32 = LoadLibrary ("USER32.DLL");

  if (hUser32)
    {
      MonitorFromWindowProc = (MONITORFROMWINDOWPROC)
	GetProcAddress (hUser32, "MonitorFromWindow");

      GetMonitorInfoProc = (GETMONITORINFOPROC)
	GetProcAddress (hUser32, "GetMonitorInfoA");

      if (MonitorFromWindowProc && GetMonitorInfoProc)
	multi_monitor_api_valid_p = 1;
    }
}

static void
get_working_area_size (HWND hwnd, int *width, int *height)
{
  if (multi_monitor_api_valid_p)
    {
      /* If multi monitor APIs are implemented, get size of the
	 working area on the current display. */
      MONITORINFO info;
      HMONITOR hmon = (MonitorFromWindowProc) (hwnd, MONITOR_DEFAULTTONEAREST);

      bzero (&info, sizeof (MONITORINFO));
      info.cbSize = sizeof (MONITORINFO);
      (GetMonitorInfoProc) (hmon, &info);
      *height = info.rcWork.bottom - info.rcWork.top;
      *width = info.rcWork.right - info.rcWork.left;
    }
  else
    {
      /* If multi monitor APIs are *NOT* implemented, get size of the
	 working area on the primary display. */
      *width = GetSystemMetrics (SM_CXFULLSCREEN);
      *height = GetSystemMetrics (SM_CYFULLSCREEN);
    }
}

static Lisp_Object
get_face_height (Lisp_Object face)
{
  Lisp_Object height = Qnil;
  extern Lisp_Object QCheight;

  if (!NILP (face) && SYMBOLP (face))
    {
      height = Finternal_get_lisp_face_attribute (face, QCheight, Qnil);
      if (!INTEGERP (height) && !FLOATP (height))
	height = Qnil;
    }
  else if (CONSP (face))
    {
      height = get_face_height (XCAR (face));
      if (NILP (height))
	height = get_face_height (XCDR (face));
    }
  return height;
}

static void
mw32_get_ime_font_property (FRAME_PTR f)
{
  /* set logfont for IME */
  LOGFONT lf = f->output_data.mw32->ime_logfont;

  if (lf.lfHeight == 0)
    {
      extern Lisp_Object Qface, QCheight, Qdefault;
      Lisp_Object p, face, height;

      if (!NILP (Feolp ()))
	XSETFASTINT (p, max (XINT (Fline_beginning_position (Qnil)), PT - 1));
      else
	XSETFASTINT (p, PT);
      face = Fget_text_property (p, Qface, Qnil);

      height = get_face_height (face);

      if (NILP (height))
	height = Finternal_get_lisp_face_attribute (Qdefault, QCheight, Qnil);
      if (INTEGERP (height))
	lf.lfHeight = (int) (- XINT (height)
			     * FRAME_MW32_DISPLAY_INFO (f)->resy / 720);
      else if (FLOATP (height))
	lf.lfHeight = (int) (XFLOATINT (height) * FRAME_LINE_HEIGHT (f));
      mw32_set_ime_font (FRAME_MW32_WINDOW (f), &lf);
    }
}

/* MW32 Window procedure.  */

LRESULT CALLBACK
mw32_WndProc (HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  struct mw32_display_info *dpyinfo = GET_MW32_DISPLAY_INFO (hwnd);
#ifdef IME_CONTROL
  extern int IME_event_off_count;
  extern HANDLE fep_switch_event;
#endif
  extern void mw32_scroll_bar_store_event (WPARAM wParam, LPARAM lParam);
  extern void mw32_menu_bar_store_activate_event (struct frame *f);

  struct frame *f;
  WPARAM SCParam;
  LRESULT ret = 1;

  f = mw32_window_to_frame (dpyinfo, hwnd);
  if (!f && !IS_EMACS_PRIVATE_MESSAGE (msg))
    return DefWindowProc (hwnd, msg, wParam, lParam);

  switch (msg)
    {
    case WM_ERASEBKGND:
      {
	RECT rect;
	int orefcount;
	HDC ohdc;

	orefcount = f->output_data.mw32->hdc_nestlevel;
	ohdc = f->output_data.mw32->hdc;

	f->output_data.mw32->hdc = (HDC) wParam;
	f->output_data.mw32->hdc_nestlevel = 1;

	if (!GetUpdateRect (FRAME_MW32_WINDOW (f), &rect, FALSE))
	  GetClientRect (FRAME_MW32_WINDOW (f), &rect);
	mw32_clear_native_frame_area (f, rect.left, rect.top,
				      rect.right, rect.bottom);

	f->output_data.mw32->hdc = ohdc;
	f->output_data.mw32->hdc_nestlevel = orefcount;

	return 1;
      }

    case WM_PAINT:
      if (hwnd == tip_window) /* don't redraw tooltip */
	goto dflt;
      if (f->async_visible == 0)
	{
	  f->async_visible = IsWindowVisible (hwnd);
	  f->async_iconified = IsIconic (hwnd);
	  if (f->async_visible && !f->async_iconified)
	    SET_FRAME_GARBAGED (f);
	}
      else
	{
	  PAINTSTRUCT ps;

	  /* Avoid conflicting with drawing in main thread. */
	  if (INPUT_BLOCKED_P)
	    return 0;

	  MW32_BLOCK_FRAME_HDC (f);
	  if (main_thread_hdc == INVALID_HANDLE_VALUE)
	    {
	      BeginPaint (FRAME_MW32_WINDOW (f), &ps);
	      GET_FRAME_HDC (f);

	      mw32_clear_native_frame_area (f,
					    ps.rcPaint.left,
					    ps.rcPaint.top,
					    ps.rcPaint.right,
					    ps.rcPaint.bottom);

	      expose_frame (f,
			    ps.rcPaint.left, ps.rcPaint.top,
			    ps.rcPaint.right - ps.rcPaint.left,
			    ps.rcPaint.bottom - ps.rcPaint.top);

	      RELEASE_FRAME_HDC (f);
	      EndPaint (FRAME_MW32_WINDOW (f), &ps);
	    }
	  else
	    {
	      /* Sleep message thread in a few minutes to run main thread */
	      Sleep (2);
	    }
	  MW32_UNBLOCK_FRAME_HDC (f);

	  return 0;
	}

      goto dflt;

    case WM_SETFOCUS:

      mw32_new_focus_frame (dpyinfo, f);

      /* This code is for switching selected-frame.  In order to
	 generate no events before Emacs is set up, check whether
	 selected_frame is initial terminal frame.  This is because
	 input_pending should be false at the startup to show startup
	 message. */
      if (SELECTED_FRAME ()
	  && (SELECTED_FRAME () != XFRAME (Vterminal_frame))
	  && (SELECTED_FRAME () != f))
	PostMessage (hwnd, WM_EMACS_ACTIVATE, (WPARAM) 0, (LPARAM) 0);

      mw32_update_frame_alpha (f);

      /* Synchronize caret state */
      if (CARET_CURSOR_P (XWINDOW (f->selected_window)->phys_cursor_type)
	  && MW32_FRAME_CARET_SHOWN (f))
	{
	  DestroyCaret ();
	  MW32_FRAME_CARET_STATE (f) = NO_CARET;
	  MW32_FRAME_CARET_BLOCKED (f) = FALSE;
	  PostMessage (hwnd, WM_EMACS_SETCARET,
		       (WPARAM)SHOWN_CARET, (LPARAM)0);
	}
      return 0;

    case WM_KILLFOCUS:

      if (MW32_FRAME_CARET_STATE (f) > NO_CARET)
	{
	  DestroyCaret ();
	  MW32_FRAME_CARET_STATE (f) = NO_CARET;
	  MW32_FRAME_CARET_BLOCKED (f) = FALSE;
	}

      if (f == FRAME_MW32_DISPLAY_INFO (f)->mw32_focus_frame)
	mw32_new_focus_frame (dpyinfo, NULL);

      /* reset mouse face and help echo.  */
      mw32_update_frame_alpha (f);

      PostMessage (hwnd, WM_EMACS_CLEAR_MOUSE_FACE, (WPARAM) 1, (LPARAM) 0);

      return 0;

    case WM_MOVING:
      f->output_data.mw32->frame_moving_or_sizing = 1;
      mw32_update_frame_alpha (f);
      return TRUE;

    case WM_SIZING:
      f->output_data.mw32->frame_moving_or_sizing = 2;
      mw32_update_frame_alpha (f);
      return TRUE;

    case WM_EXITSIZEMOVE:
      f->output_data.mw32->frame_moving_or_sizing = 0;
      mw32_update_frame_alpha (f);

      return TRUE;

    case WM_MOVE:

      if (!f->async_iconified && f->async_visible)
	{
	  RECT rect;

	  GetWindowRect (hwnd, &rect);

	  mw32_real_positions (f, &f->left_pos, &f->top_pos);
	}

      break;

    case WM_SIZE:

      switch (wParam)
	{
	case SIZE_MINIMIZED:
	  f->async_visible = 0;
	  f->async_iconified = 1;
	  break;
	case SIZE_MAXIMIZED:
	  f->want_fullscreen |= FULLSCREEN_BOTH;
	  /* fall through */
	case SIZE_RESTORED:
	  {
	    RECT rect;

	    GetWindowRect (hwnd, &rect);

	    f->async_visible = IsWindowVisible (hwnd);
	    f->async_iconified = IsIconic (hwnd);
	    mw32_real_positions (f, &f->left_pos, &f->top_pos);
	    SET_FRAME_GARBAGED (f);
	    mw32_update_frame_alpha (f);
	  }
	}

      break;

    case WM_SHOWWINDOW:
      if (!wParam)
	{
	  f->async_visible = 0;
	  f->async_iconified = 1;
	}
      goto dflt;

    case WM_NCMOUSEMOVE:
      /* reset mouse face only.  */
      PostMessage (hwnd, WM_EMACS_CLEAR_MOUSE_FACE, (WPARAM) 0, (LPARAM) 0);
      goto dflt;

    case WM_WINDOWPOSCHANGED:
      if (!f->async_iconified && f->async_visible)
	{
	  int call_defprocp = 1;
	  RECT rect;
	  LPWINDOWPOS lppos = (LPWINDOWPOS) lParam;

	  if (!(lppos->flags & SWP_NOMOVE))
	    {
	      GetWindowRect (hwnd, &rect);
	      /* position change */
	      mw32_real_positions (f, &f->left_pos, &f->top_pos);
	      call_defprocp = 0;
	    }

	  GetClientRect (hwnd, &rect);
	  if (!(lppos->flags & SWP_NOSIZE)
	      || lppos->flags & SWP_DRAWFRAME)
	    {
	      /* size change */
	      int width, height;
	      int rows, cols;
	      int wdiff = 0, hdiff = 0;

	      height = rect.bottom - rect.top;
	      width = rect.right - rect.left;

	      if ((width < (2 * (FRAME_INTERNAL_BORDER_WIDTH (f)
				 + FRAME_TOTAL_FRINGE_WIDTH (f))
			    + f->scroll_bar_actual_width))
		  || (height < (2 * FRAME_INTERNAL_BORDER_WIDTH (f))))
		{
		  /* This case MUST not happen.  Because it means that the
		     window size is smaller than required size.  Normally,
		     this case is caused by iconification on Windows.  So
		     we MUST not regard it as a normal window size changing
		     message. (by himi)  Call DefWindowProc() to generate
		     WM_SIZE message.
		  */
		  goto dflt;
		}

	      if (!(f->want_fullscreen & FULLSCREEN_WIDTH)
		  || !(f->want_fullscreen & FULLSCREEN_HEIGHT))
		{
		  wdiff = ((width
			    - 2 * (FRAME_INTERNAL_BORDER_WIDTH (f)
				   + FRAME_TOTAL_FRINGE_WIDTH (f))
			    - f->scroll_bar_actual_width)
			   % FRAME_DEFAULT_FONT_WIDTH (f));
		  hdiff = ((height
			    - 2 * FRAME_INTERNAL_BORDER_WIDTH (f))
			   % FRAME_LINE_HEIGHT (f));
		  if ((2 * wdiff) > FRAME_DEFAULT_FONT_WIDTH (f))
		    wdiff -= FRAME_DEFAULT_FONT_WIDTH (f);
		  if ((2 * hdiff) > FRAME_LINE_HEIGHT (f))
		    hdiff -= FRAME_LINE_HEIGHT (f);

		  width -= wdiff;
		  height -= hdiff;

		  {
		    /*
		      Rearrange the frame size, which should be
		      smaller than the display size.
		    */
		    HDC hdc = GetDC (hwnd);
		    RECT nrect;
		    int work_area_w, work_area_h;

		    nrect.left = nrect.top = 0;
		    nrect.right = width;
		    nrect.bottom = height;
		    AdjustWindowRectEx (&nrect, f->output_data.mw32->dwStyle,
					FRAME_EXTERNAL_MENU_BAR (f),
					f->output_data.mw32->dwStyleEx);

		    get_working_area_size (hwnd, &work_area_w, &work_area_h);
		    if (nrect.bottom - nrect.top > work_area_h)
		      {
			hdiff += FRAME_LINE_HEIGHT (f);
			height -= FRAME_LINE_HEIGHT (f);
		      }
		    if (nrect.right - nrect.left > work_area_w)
		      {
			wdiff += FRAME_DEFAULT_FONT_WIDTH (f);
			width -= FRAME_DEFAULT_FONT_WIDTH (f);
		      }
		    ReleaseDC (hwnd, hdc);
		  }
		}

	      rows = FRAME_PIXEL_HEIGHT_TO_TEXT_LINES (f, height);
	      cols = FRAME_PIXEL_WIDTH_TO_TEXT_COLS (f, width);

	      if (cols != f->text_cols
		  || rows != f->text_lines
		  || width != FRAME_PIXEL_WIDTH (f)
		  || height != FRAME_PIXEL_HEIGHT (f))
		{
		  change_frame_size (f, rows, cols, 0, 1, 0);
		  SET_FRAME_GARBAGED (f);
		  cancel_mouse_face (f);
		  FRAME_PIXEL_WIDTH (f) = width;
		  FRAME_PIXEL_HEIGHT (f) = height;
		  f->win_gravity = NorthWestGravity;
		}

	      /* To adjust window correctly,
		 we must check size of the window twice
		 (strictly speaking number of dimention),
		 thus, width and height. */
	      if ((f->output_data.mw32->frame_change_state < 2)
		  && (wdiff || hdiff))
		{
		  f->output_data.mw32->frame_change_state++;
		  SetWindowPos (hwnd, NULL, 0, 0, lppos->cx - wdiff,
				lppos->cy - hdiff, SWP_NOZORDER | SWP_NOMOVE);
		}
	      else
		f->output_data.mw32->frame_change_state = 0;

	      call_defprocp = 0;
	    }
	  else
	    f->output_data.mw32->frame_change_state = 0;

	  if (!call_defprocp)
	    return 0;
	}

      goto dflt;

    case WM_ACTIVATE:
    case WM_ACTIVATEAPP:
      mw32_check_fullscreen (f);

      goto dflt;

    case WM_CLOSE:
      PostMessage (hwnd, WM_EMACS_DESTROY, wParam, lParam);
      return 0;

    case WM_CREATE:
      return 0;

    case WM_INITMENU:
      if (f->output_data.mw32->menubar_handle == (HMENU) wParam)
	mw32_menu_bar_store_activate_event (f);
      return 0;

    case WM_MENUSELECT:
      /* Direct handling of help_echo in menus.  Should be safe now
	 that we generate the help_echo by placing a help event in the
	 keyboard buffer.  */
      {
	HMENU menu = (HMENU) lParam;
	UINT menu_item = (UINT) LOWORD (wParam);
	UINT flags = (UINT) HIWORD (wParam);

	mw32_menu_display_help (hwnd, menu, menu_item, flags);
      }
      return 0;

    case WM_MEASUREITEM:
      if (f)
	{
	  MEASUREITEMSTRUCT * pMis = (MEASUREITEMSTRUCT *) lParam;

	  if (pMis->CtlType == ODT_MENU)
	    {
	      /* Work out dimensions for popup menu titles. */
	      char * title = (char *) pMis->itemData;
	      HDC hdc = GetDC (hwnd);
	      HFONT menu_font = GetCurrentObject (hdc, OBJ_FONT);
	      LOGFONT menu_logfont;
	      HFONT old_font;
	      SIZE size;

	      GetObject (menu_font, sizeof (menu_logfont), &menu_logfont);
	      menu_logfont.lfWeight = FW_BOLD;
	      menu_font = CreateFontIndirect (&menu_logfont);
	      old_font = SelectObject (hdc, menu_font);

	      pMis->itemHeight = GetSystemMetrics (SM_CYMENUSIZE);
	      if (title)
		{
		  GetTextExtentPoint32 (hdc, title, strlen (title), &size);
		  pMis->itemWidth = size.cx;
		  if (pMis->itemHeight < size.cy)
		    pMis->itemHeight = size.cy;
		}
	      else
		pMis->itemWidth = 0;

	      SelectObject (hdc, old_font);
	      DeleteObject (menu_font);
	      ReleaseDC (hwnd, hdc);
	      return TRUE;
	    }
	}
      return 0;

    case WM_DRAWITEM:
      if (f)
	{
	  DRAWITEMSTRUCT * pDis = (DRAWITEMSTRUCT *) lParam;

	  if (pDis->CtlType == ODT_MENU)
	    {
	      /* Draw popup menu title. */
	      char * title = (char *) pDis->itemData;
	      if (title)
		{
		  HDC hdc = pDis->hDC;
		  HFONT menu_font = GetCurrentObject (hdc, OBJ_FONT);
		  LOGFONT menu_logfont;
		  HFONT old_font;

		  GetObject (menu_font, sizeof (menu_logfont), &menu_logfont);
		  menu_logfont.lfWeight = FW_BOLD;
		  menu_font = CreateFontIndirect (&menu_logfont);
		  old_font = SelectObject (hdc, menu_font);

		  /* Always draw title as if not selected.  */
		  ExtTextOut (hdc,
			      pDis->rcItem.left
			      + GetSystemMetrics (SM_CXMENUCHECK),
			      pDis->rcItem.top,
			      ETO_OPAQUE, &pDis->rcItem,
			      title, strlen (title), NULL);

		  SelectObject (hdc, old_font);
		  DeleteObject (menu_font);
		}
	      return TRUE;
	    }
	}
      return 0;

    case WM_ENTERMENULOOP:
      if (dpyinfo->mouse_cursor_stat < 0)
	{
	  ShowCursor (TRUE);
	  dpyinfo->mouse_cursor_stat = 0;
	}
      return 0;

    case WM_EXITMENULOOP:
      if (!wParam) /* not track popup menu */
	f->output_data.mw32->disable_reconstruct_menubar = 0;
      lock_mouse_cursor_visible (FALSE);
      return 0;

    case WM_VSCROLL:
      mw32_scroll_bar_store_event (wParam, lParam);
      return 0;

    case WM_SYSCOMMAND:
      switch (wParam & 0xFFF0)
	{
	case SC_MAXIMIZE:
	  f->want_fullscreen |= FULLSCREEN_BOTH;
	  break;

	case SC_RESTORE:
	  f->want_fullscreen &= ~FULLSCREEN_BOTH;
	  break;
	}
      goto dflt;

    case WM_EMACS_FLASH_WINDOW:
      if ((enum emacs_flash_window_type) wParam == INVERT_EDGES_OF_FRAME)
	mw32m_flash_window (f);
      else
	{
	  FlashWindow (hwnd, TRUE);
	  Sleep (100);
	  FlashWindow (hwnd, FALSE);
	}
      return 0;
#if defined(MEADOW) && defined(IME_CONTROL)
#ifdef W32_VER4
    case WM_IME_NOTIFY:
      if (wParam == IMN_SETOPENSTATUS)
	{
	  if (!IME_event_off_count)
	    PostMessage (hwnd, WM_MULE_IME_STATUS, 0, 0);
	  else
	    IME_event_off_count--;
	  SetEvent (fep_switch_event);
	}
      goto dflt;

#ifdef IME_RECONVERSION
    case WM_IME_REQUEST:
      if (wParam == IMR_RECONVERTSTRING)
	return mw32_get_ime_reconversion_string (hwnd, wParam,
						 (RECONVERTSTRING*)lParam);
      goto dflt;
#endif
    case WM_IME_STARTCOMPOSITION:
      mw32_set_ime_conv_window (hwnd, XWINDOW (f->selected_window));
      f->output_data.mw32->ime_composition_state = 1;
      mw32_get_ime_font_property (f);
      goto dflt;

    case WM_IME_COMPOSITION:
      if (lParam & GCS_RESULTSTR)
	{
	  extern BOOL mw32_get_ime_result_string (HWND);
	  if (mw32_get_ime_result_string (hwnd))
	    return 0;
	  else
	    break;
	}

      if (mw32_get_ime_undetermined_string_length (hwnd) == 0)
	{ /* Cancelling composition string */
	  mw32_ime_cancel_input_function ();
	}

      goto dflt;

    case WM_IME_ENDCOMPOSITION:
      /* To erase garbage image of system caret set update_mode_lines */
      /* I can not find smart solution for results of asynchronisity of
	 IME events posted onto main thread. */
      if (CARET_CURSOR_P (XWINDOW (f->selected_window)->phys_cursor_type))
	update_mode_lines++;

      f->output_data.mw32->ime_composition_state = 0;

      goto dflt;

#else /* not W32_VER4 */

    case WM_IME_REPORT:
      switch (wParam)
	{
	case IR_STRING:
	  {
	    HANDLE himestr;

	    himestr = mw32_ime_string_handle ((HANDLE) lParam);
	    if (!himestr) break;

	    PostMessage (NULL, WM_MULE_IME_REPORT,
			 (WPARAM) himestr, (LPARAM) f);
	    return 1;
	  }
	case IR_OPENCONVERT:
	case IR_CLOSECONVERT:
	  if (!IME_event_off_count)
	    PostMessage (hwnd, WM_MULE_IME_STATUS, 0, 0);
	  else
	    IME_event_off_count--;
	  SetEvent (fep_switch_event);
	  return 0;
	}
#endif /* not W32_VER4 */
    case WM_MULE_IMM_SET_COMPOSITION_FONT:
      f->output_data.mw32->ime_logfont = *((LPLOGFONT) lParam);
      mw32_set_ime_font (hwnd, (LPLOGFONT) lParam);
      POST_THREAD_INFORM_MESSAGE (main_thread_id,
				  WM_MULE_IMM_SET_COMPOSITION_FONT_REPLY,
				  (WPARAM) 0, (LPARAM) 0);
      break;
    case WM_MULE_IMM_SET_CONVERSION_WINDOW:
      mw32_set_ime_conv_window (hwnd, (struct window *) wParam);
      break;

    case WM_MULE_IMM_SET_COMPOSITION_STRING:
      mw32_ime_set_composition_string (hwnd, (char*) wParam);
      break;

    case WM_MULE_IMM_GET_COMPOSITION_STRING:
      mw32_ime_get_composition_string (hwnd);
      break;

#endif /* not MEADOW and IME_CONTROL */

      /* Emacs private message entries. */
    case WM_EMACS_CREATE_FRAME:
      mw32m_create_frame_window ((struct frame*) wParam, (LPSTR) lParam);
      break;

    case WM_EMACS_CREATE_TIP_FRAME:
      mw32m_create_tip_frame_window ((struct frame *) wParam);
      break;

    case WM_EMACS_CREATE_SCROLLBAR:
      mw32m_create_scrollbar (hwnd, (LPRECT) wParam, (HINSTANCE) lParam);
      break;

#ifdef IME_CONTROL
    case WM_MULE_IME_CREATE_AGENT:
      mw32m_ime_create_agent ();
      break;
    case WM_MULE_IME_DESTROY_AGENT:
      mw32m_ime_destroy_agent (hwnd);
      break;
#endif
    case WM_EMACS_DESTROY_FRAME:
      mw32m_destroy_frame (hwnd);
      break;

    case WM_EMACS_POPUP_MENU:
      /* Use menubar_active to indicate that WM_INITMENU is from
	 TrackPopupMenu below, and should be ignored.  */
      f = mw32_window_to_frame (dpyinfo, hwnd);
      if (f)
	f->output_data.mw32->menubar_active = 1;

      mw32m_track_popup_menu (hwnd, (HANDLE) wParam, (LPPOINT) lParam);
      dpyinfo->grabbed = 0;
      break;

    case WM_EMACS_SETCARET:
      {
	static int last_phys_cursor_height;
	static int last_cursor_width;
	static int last_cursor_height;
	static struct frame *last_cursor_frame = NULL;
	static HBITMAP last_bitmap;
	struct window *w = XWINDOW (f->selected_window);
	int count;
	int caret_spec_changed = 0;

	if (last_cursor_frame
	    && last_cursor_frame != f
	    && MW32_FRAME_CARET_STATE2 (last_cursor_frame) != NO_CARET)
	  {
	    /* Destroy caret in previous frame on sudden switch of
	       message frame. */
	    DestroyCaret ();
	    MW32_FRAME_CARET_STATE (last_cursor_frame) = NO_CARET;
	    MW32_FRAME_CARET_BLOCKED (last_cursor_frame) = FALSE;
	  }
	last_cursor_frame = f;

	if (last_phys_cursor_height != w->phys_cursor_height
	    || last_cursor_width != FRAME_CURSOR_WIDTH (f)
	    || last_cursor_height != MW32_FRAME_CARET_HEIGHT (f)
	    || last_bitmap != MW32_FRAME_CARET_BITMAP (f))
	  caret_spec_changed = 1;

	if (cursor_in_echo_area
	    && FRAME_HAS_MINIBUF_P (f)
	    && EQ (FRAME_MINIBUF_WINDOW (f), echo_area_window))
	  w = XWINDOW (echo_area_window);

	/* Force to hide when defocused */
	if (MW32_FRAME_CARET_STATE (f) != NO_CARET &&
	    (f != FRAME_MW32_DISPLAY_INFO (f)->mw32_focus_frame ||
	     ! w->phys_cursor_on_p ))
	  {
	    if (MW32_FRAME_CARET_BLOCKED (f) && wParam == UNBLOCK_CARET)
	      MW32_FRAME_CARET_STATE (f) = HIDDEN_CARET;
	    else
	      wParam = HIDDEN_CARET;
	  }

	if (wParam == BLOCK_CARET)
	  {
	    if (MW32_FRAME_CARET_BLOCKED (f))
	      goto setcaret_end;

	    MW32_FRAME_CARET_BLOCKED (f) = TRUE;

	    if (MW32_FRAME_CARET_SHOWN (f))
	      HideCaret (hwnd);
	    else if (MW32_FRAME_CARET_STATE (f) == TOBESHOWN_CARET)
	      MW32_FRAME_CARET_STATE (f) = SHOWN_CARET;
	    goto setcaret_end;
	  }

	if (wParam == UNBLOCK_CARET)
	  {
	    if (!MW32_FRAME_CARET_BLOCKED (f))
	      goto setcaret_end;

	    MW32_FRAME_CARET_BLOCKED (f) = FALSE;
	    wParam = MW32_FRAME_CARET_SHOWN (f) ? SHOWN_CARET : HIDDEN_CARET;
	    MW32_FRAME_CARET_STATE (f) = HIDDEN_CARET;
	  }

	if (wParam == SHOWN_CARET)
	  {
	    int caret_height =
	      MW32_FRAME_CARET_HEIGHT (f) * w->phys_cursor_height / 4;
	    int caret_xpos =
	      WINDOW_TEXT_TO_FRAME_PIXEL_X (w, w->phys_cursor.x);
	    int caret_ypos =
	      WINDOW_TO_FRAME_PIXEL_Y (w, w->phys_cursor.y)
	      + w->phys_cursor_height - caret_height;
	    int retry_count = 2;  /* Magic number */

	    do {
	      if (caret_spec_changed)
		{
		  if (MW32_FRAME_CARET_STATE (f) > NO_CARET)
		    {
		      DestroyCaret ();
		      MW32_FRAME_CARET_STATE (f) = NO_CARET;
		      /* Caret is immediately re-created, so it should not
			 unblock caret. */
		    }
		  caret_spec_changed = FALSE;
		}

	      if (MW32_FRAME_CARET_STATE (f) == NO_CARET)
		{
		  CreateCaret (hwnd,
			       MW32_FRAME_CARET_BITMAP (f),
			       FRAME_CURSOR_WIDTH (f),
			       caret_height);
		  MW32_FRAME_CARET_STATE (f) = HIDDEN_CARET;
		  MW32_FRAME_CARET_BLOCKED (f) = FALSE;
		  last_phys_cursor_height = w->phys_cursor_height;
		  last_cursor_width = FRAME_CURSOR_WIDTH (f);
		  last_cursor_height = MW32_FRAME_CARET_HEIGHT (f);
		  last_bitmap = MW32_FRAME_CARET_BITMAP (f);
		}

	      SetCaretPos (caret_xpos, caret_ypos);

	      if (MW32_FRAME_CARET_SHOWN (f))
		goto setcaret_end;
	      MW32_FRAME_CARET_STATE (f) = SHOWN_CARET;
	      if (! MW32_FRAME_CARET_BLOCKED (f) && ShowCaret (hwnd) == 0)
		{
		  if (GetLastError () == ERROR_ACCESS_DENIED)
		    caret_spec_changed = TRUE;	/* Retry showcaret */
		  else
		    MW32_FRAME_CARET_STATE (f) = TOBESHOWN_CARET;
		}
	    } while (caret_spec_changed && retry_count-- > 0);
	  }
	else
	  {
	    if (MW32_FRAME_CARET_SHOWN (f))
	      {
		if (! MW32_FRAME_CARET_BLOCKED (f)) HideCaret (hwnd);
		MW32_FRAME_CARET_STATE (f) = HIDDEN_CARET;
	      }
	  }
      setcaret_end:
	SetEvent (f->output_data.mw32->setcaret_event);
      }
      break;
    case WM_EMACS_SETFOREGROUND:
      {
	HWND foreground_window;
	DWORD foreground_thread, timeout;
	HDWP hdwp;

	/* On NT 5.0, and apparently Windows 98, it is necessary to
	   attach to the thread that currently has focus in order to
	   pull the focus away from it.  */
	foreground_window = GetForegroundWindow ();
	foreground_thread = GetWindowThreadProcessId (foreground_window, NULL);
	if (!foreground_window
	    || foreground_thread == GetCurrentThreadId ()
	    || !AttachThreadInput (GetCurrentThreadId (),
				   foreground_thread, TRUE))
	  foreground_thread = 0;

	/* On Windows 98 or later, it is necessary to set foreground
	   lock time to zero in order to set foreground window
	   successfully. */
	SystemParametersInfo (SPI_GETFOREGROUNDLOCKTIMEOUT, 0, &timeout, 0);
	SystemParametersInfo (SPI_SETFOREGROUNDLOCKTIMEOUT, 0, (LPVOID) 0, 0);

	/* Set foreground window */
	SetForegroundWindow (hwnd);

	/* Modify Z order */
	BringWindowToTop (hwnd);

	/* Restore foreground lock time. */
	SystemParametersInfo (SPI_SETFOREGROUNDLOCKTIMEOUT,
			      0, (LPVOID) timeout, 0);

	/* Detach from the previous foreground thread.  */
	if (foreground_thread)
	  AttachThreadInput (GetCurrentThreadId (),
			     foreground_thread, FALSE);
      }
      break;

    case WM_EMACS_UPDATE_ALPHA:
      mw32_update_frame_alpha ((struct frame *) wParam);
      break;

    case WM_EMACS_CREATE_CLPBD:
      mw32m_create_clpbd ();
      break;

      /* end of Emacs private message entries. */

    default:
#if defined(MEADOW) && defined(IME_CONTROL) && defined(W32_VER4)
      {
	extern LRESULT CALLBACK
	  conversion_agent_wndproc (HWND hwnd, UINT message,
				    WPARAM wparam, LPARAM lparam);
	if (MESSAGE_IMM_COM_P (msg))
	  return conversion_agent_wndproc (hwnd, msg, wParam, lParam);
      }
#endif

    dflt:
      return DefWindowProc (hwnd, msg, wParam, lParam);
    }

  return 1;
}

static BOOL
mw32_init_app (HINSTANCE hinst)
{
    WNDCLASS wc;

    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = (WNDPROC) mw32_WndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hinst;
    wc.hIcon = LoadIcon (hinst, EMACS_CLASS);
    wc.hCursor = NULL;
    wc.hbrBackground = GetStockObject (WHITE_BRUSH);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = EMACS_CLASS;

    return (RegisterClass (&wc));
}

/* Create a tooltip window. Unlike my_create_window, we do not do this
   indirectly via the Window thread, as we do not need to process Window
   messages for the tooltip.  Creating tooltips indirectly also creates
   deadlocks when tooltips are created for menu items.  */
static Window
mw32_create_tip_window (struct frame *f)
{
  MSG msg;
  Window hwnd;

  SEND_MSGTHREAD_INFORM_MESSAGE (WM_EMACS_CREATE_TIP_FRAME,
				 (WPARAM) f, (LPARAM) NULL);
  WAIT_REPLY_MESSAGE (&msg, WM_EMACS_CREATE_TIP_FRAME_REPLY);

  hwnd = (HWND) msg.wParam;

  if (hwnd == NULL)
    error ("Unable to create window");

  return hwnd;
}

/* Create and set up the X window for frame F.  */

static void
mw32_window (struct frame *f, int minibuffer_only)
{
  struct mw32_display_info *dpyinfo = FRAME_MW32_DISPLAY_INFO (f);
  HWND hwnd;
  char *name;
  MSG msg;

  if (STRINGP (f->name))
    name = (char*) SDATA (f->name);
  else
    name = dpyinfo->mw32_id_name;

  SEND_MSGTHREAD_INFORM_MESSAGE (WM_EMACS_CREATE_FRAME,
				 (WPARAM) f, (LPARAM) name);
  WAIT_REPLY_MESSAGE (&msg, WM_EMACS_CREATE_FRAME_REPLY);

  hwnd = (HWND) msg.wParam;
  FRAME_MW32_WINDOW (f) = hwnd;

  f->output_data.mw32->mainthread_to_frame_handle = CreateEvent (0, TRUE,
								 TRUE, NULL);

  validate_x_resource_name ();

  if (!minibuffer_only && FRAME_EXTERNAL_MENU_BAR (f))
    initialize_frame_menubar (f);

  mw32_calc_absolute_position (f);

  /* w32_set_name normally ignores requests to set the name if the
     requested name is the same as the current name.  This is the one
     place where that assumption isn't correct; f->name is set, but
     the X server hasn't been told.  */
  {
    Lisp_Object name;
    int explicit = f->explicit_name;

    f->explicit_name = 0;
    name = f->name;
    f->name = Qnil;
    mw32_set_name (f, name, explicit);
  }

  if (FRAME_MW32_WINDOW (f) == 0)
    error ("Unable to create window");

  mw32_update_frame_alpha (f);
}


/* Handler for signals raised during x_create_frame and
   x_create_top_frame.  FRAME is the frame which is partially
   constructed.  */

static Lisp_Object
unwind_create_frame (Lisp_Object frame)
{
  struct frame *f = XFRAME (frame);

  /* If frame is ``official'', nothing to do.  */
  if (!CONSP (Vframe_list) || !EQ (XCAR (Vframe_list), frame))
    {
#if GLYPH_DEBUG
      struct mw32_display_info *dpyinfo = FRAME_MW32_DISPLAY_INFO (f);
#endif

      mw32_free_frame_resources (f);

      /* Check that reference counts are indeed correct.  */
      xassert (dpyinfo->reference_count == dpyinfo_refcount);
      xassert (dpyinfo->image_cache->refcount == image_cache_refcount);
      return Qt;
    }

  return Qnil;
}


DEFUN ("x-create-frame", Fx_create_frame, Sx_create_frame,
       1, 1, 0,
       doc: /* Make a new window, which is called a \"frame\" in Emacs terms.
Returns an Emacs frame object.
PARAMETERS is an alist of frame parameters.
If the parameters specify that the frame should not have a minibuffer,
and do not specify a specific minibuffer window to use,
then `default-minibuffer-frame' must be a frame whose minibuffer can
be shared by the new frame.

This function is an internal primitive--use `make-frame' instead.  */)
  (parameters)
     Lisp_Object parameters;
{
  struct frame *f;
  Lisp_Object frame, tem;
  Lisp_Object name;
  int minibuffer_only = 0;
  long window_prompting = 0;
  int width, height;
  int count = SPECPDL_INDEX ();
  struct gcpro gcpro1, gcpro2, gcpro3, gcpro4;
  Lisp_Object display;
  struct mw32_display_info *dpyinfo = NULL;
  Lisp_Object parent;
  struct kboard *kb;

  check_mw32 ();

  /* Use this general default value to start with
     until we know if this frame has a specified name.  */
  Vx_resource_name = Vinvocation_name;

  display = x_get_arg (dpyinfo, parameters, Qdisplay, 0, 0, RES_TYPE_STRING);
  if (EQ (display, Qunbound))
    display = Qnil;
  /* Get mw32 display info.  Curretly, arg is a dummy argument.  */
  dpyinfo = GET_MW32_DISPLAY_INFO (arg);
#ifdef MULTI_KBOARD
  kb = dpyinfo->kboard;
#else
  kb = &the_only_kboard;
#endif

  name = x_get_arg (dpyinfo, parameters, Qname, "name", "Name",
		    RES_TYPE_STRING);
  if (!STRINGP (name)
      && ! EQ (name, Qunbound)
      && ! NILP (name))
    error ("Invalid frame name--not a string or nil");

  if (STRINGP (name))
    Vx_resource_name = name;

#if 0
  /* See if parent window is specified.  */
  parent = x_get_arg (dpyinfo, parameters, Qparent_id, NULL, NULL,
		      RES_TYPE_NUMBER);
  if (EQ (parent, Qunbound))
    parent = Qnil;
  if (! NILP (parent))
    CHECK_NUMBER (parent);
#else
  parent = Qnil;
#endif

  /* make_frame_without_minibuffer can run Lisp code and garbage collect.  */
  /* No need to protect DISPLAY because that's not used after passing
     it to make_frame_without_minibuffer.  */
  frame = Qnil;
  GCPRO4 (parameters, parent, name, frame);
  tem = x_get_arg (dpyinfo, parameters, Qminibuffer, "minibuffer",
		   "Minibuffer", RES_TYPE_SYMBOL);
  if (EQ (tem, Qnone) || NILP (tem))
    f = make_frame_without_minibuffer (Qnil, kb, display);
  else if (EQ (tem, Qonly))
    {
      f = make_minibuffer_frame ();
      minibuffer_only = 1;
    }
  else if (WINDOWP (tem))
    f = make_frame_without_minibuffer (tem, kb, display);
  else
    f = make_frame (1);

  XSETFRAME (frame, f);

  /* Note that MW32 supports scroll bars.  */
  FRAME_CAN_HAVE_SCROLL_BARS (f) = 1;
  /* Scroll bar locates on the right side.  */
  FRAME_VERTICAL_SCROLL_BAR_TYPE (f) = vertical_scroll_bar_right;

  f->output_method = output_mw32;
  f->output_data.mw32 = (struct mw32_output *) xmalloc (sizeof (struct mw32_output));
  bzero (f->output_data.mw32, sizeof (struct mw32_output));
  f->output_data.mw32->icon_bitmap = -1;
  f->output_data.mw32->fontset = -1;
  f->output_data.mw32->scroll_bar_foreground_pixel = -1;
  f->output_data.mw32->scroll_bar_background_pixel = -1;
  /* all handles must be set to INVALID_HANLE_VALUE.  */
  f->output_data.mw32->menubar_handle = INVALID_HANDLE_VALUE;
  f->output_data.mw32->hdc = INVALID_HANDLE_VALUE;
  f->output_data.mw32->hdc_nestlevel = 0;
  f->output_data.mw32->pending_clear_mouse_face = 0;
  f->output_data.mw32->frame_moving_or_sizing = 0;
  f->output_data.mw32->setcaret_event = CreateEvent (NULL, TRUE, FALSE, NULL);
  if (f->output_data.mw32->setcaret_event == NULL)
    error ("Cannot create event object");
  InitializeCriticalSection (&(f->output_data.mw32->hdc_critsec));

  {
    int i;

    f->output_data.mw32->current_alpha = -1;
    for (i = 0 ; i < NUM_OF_ALPHAS ; i++)
      f->output_data.mw32->alpha[i] = -1;
  }
  {
    LOGFONT lf;

    mw32_initialize_default_logfont (&lf);
    f->output_data.mw32->ime_logfont = lf;
  }

  record_unwind_protect (unwind_create_frame, frame);

  f->icon_name
    = x_get_arg (dpyinfo, parameters, Qicon_name, "iconName", "Title",
		    RES_TYPE_STRING);
  if (! STRINGP (f->icon_name))
    f->icon_name = Qnil;

  FRAME_MW32_DISPLAY_INFO (f) = dpyinfo;
#if GLYPH_DEBUG
  image_cache_refcount = FRAME_MW32_IMAGE_CACHE (f)->refcount;
  dpyinfo_refcount = dpyinfo->reference_count;
#endif /* GLYPH_DEBUG */
#ifdef MULTI_KBOARD
  FRAME_KBOARD (f) = kb;
#endif

  f->output_data.mw32->text_cursor
    = mw32_load_cursor (IDC_IBEAM);
  f->output_data.mw32->nontext_cursor
    = mw32_load_cursor (IDC_ARROW);
  f->output_data.mw32->modeline_cursor
    = mw32_load_cursor (IDC_SIZENS);
  f->output_data.mw32->hourglass_cursor
    = mw32_load_cursor (IDC_WAIT);
  f->output_data.mw32->horizontal_drag_cursor
    = mw32_load_cursor (IDC_SIZEWE);

  f->output_data.mw32->hand_cursor
    = mw32_load_cursor ((LPCTSTR) IDC_HAND);
  if (f->output_data.mw32->hand_cursor == NULL)
    f->output_data.mw32->hand_cursor
      = mw32_load_cursor (IDC_ARROW);

  f->output_data.mw32->current_cursor
    = f->output_data.mw32->nontext_cursor;

#if 0
  /* These colors will be set anyway later, but it's important
     to get the color reference counts right, so initialize them!  */
  {
    Lisp_Object black;
    struct gcpro gcpro1;

    /* Function x_decode_color can signal an error.  Make
       sure to initialize color slots so that we won't try
       to free colors we haven't allocated.  */
    f->output_data.mw32->foreground_pixel = -1;
    f->output_data.mw32->background_pixel = -1;
    f->output_data.mw32->cursor_pixel = -1;
    f->output_data.mw32->cursor_foreground_pixel = -1;
    f->output_data.mw32->border_pixel = -1;
    f->output_data.mw32->mouse_pixel = -1;

    black = build_string ("black");
    GCPRO1 (black);
    f->output_data.mw32->foreground_pixel
      = mw32_decode_color (f, black, BLACK_PIX_DEFAULT (f));
    f->output_data.mw32->background_pixel
      = mw32_decode_color (f, black, BLACK_PIX_DEFAULT (f));
    f->output_data.mw32->cursor_pixel
      = mw32_decode_color (f, black, BLACK_PIX_DEFAULT (f));
    f->output_data.mw32->cursor_foreground_pixel
      = mw32_decode_color (f, black, BLACK_PIX_DEFAULT (f));
    f->output_data.mw32->border_pixel
      = mw32_decode_color (f, black, BLACK_PIX_DEFAULT (f));
    f->output_data.mw32->mouse_pixel
      = mw32_decode_color (f, black, BLACK_PIX_DEFAULT (f));
    UNGCPRO;
  }
#endif

  /* Specify the parent under which to make this X window.  */

  if (!NILP (parent))
    {
      f->output_data.mw32->explicit_parent = 1;
    }
  else
    {
      f->output_data.mw32->parent_desc = FRAME_MW32_DISPLAY_INFO (f)->root_window;
      f->output_data.mw32->explicit_parent = 0;
    }

  /* Set the name; the functions to which we pass f expect the name to
     be set.  */
  if (EQ (name, Qunbound) || NILP (name))
    {
      f->name = build_string (dpyinfo->mw32_id_name);
      f->explicit_name = 0;
    }
  else
    {
      f->name = name;
      f->explicit_name = 1;
      /* use the frame's title when getting resources for this frame.  */
      specbind (Qx_resource_name, name);
    }

  /* Extract the window parameters from the supplied values
     that are needed to determine window geometry.  */
  {
    Lisp_Object font, fontset;

    font = x_get_arg (dpyinfo, parameters, Qfont, "font", "Font",
		      RES_TYPE_STRING);
    if (! STRINGP (font))
      font = build_string ("default");

    fontset = Fquery_fontset (font, Qnil);
    if (STRINGP (fontset))
      font = mw32_new_fontset (f, SDATA (fontset));
    else
      font = mw32_new_font (f, SDATA (font));

    if (!STRINGP (font))
      {
	/* Initial font cannot be created,
	   we should display detail informations
	   as far as possible. */
	if (SYMBOLP (font))
	  error ("Cannot select initial font:%s",
		 SDATA (SYMBOL_NAME (font)));
	else
	  error ("Cannot select initial font");
      }

    x_default_parameter (f, parameters, Qfont, font,
			 "font", "Font", RES_TYPE_STRING);
#ifdef IME_CONTROL
    x_default_parameter (f, parameters, Qime_font, Qnil,
			 "ime-font", "IME-Font", RES_TYPE_STRING);
#endif
  }

  x_default_parameter (f, parameters, Qborder_width, make_number (2),
		       "borderWidth", "BorderWidth", RES_TYPE_NUMBER);
  /* This defaults to 2 in order to match xterm.  We recognize either
     internalBorderWidth or internalBorder (which is what xterm calls
     it).  */
  if (NILP (Fassq (Qinternal_border_width, parameters)))
    {
      Lisp_Object value;

      value = x_get_arg (dpyinfo, parameters, Qinternal_border_width,
			 "internalBorder", "internalBorder", RES_TYPE_NUMBER);
      if (! EQ (value, Qunbound))
	parameters = Fcons (Fcons (Qinternal_border_width, value),
			    parameters);
    }
  x_default_parameter (f, parameters, Qinternal_border_width, make_number (1),
		       "internalBorderWidth", "internalBorderWidth",
		       RES_TYPE_NUMBER);
  x_default_parameter (f, parameters, Qvertical_scroll_bars, Qleft,
		       "verticalScrollBars", "ScrollBars",
		       RES_TYPE_SYMBOL);

  /* Also do the stuff which must be set before the window exists.  */
  x_default_parameter (f, parameters, Qforeground_color,
		       build_string ("black"),
		       "foreground", "Foreground", RES_TYPE_STRING);
  x_default_parameter (f, parameters, Qbackground_color,
		       build_string ("white"),
		       "background", "Background", RES_TYPE_STRING);
  x_default_parameter (f, parameters, Qmouse_color, build_string ("black"),
		       "pointerColor", "Foreground", RES_TYPE_STRING);
  x_default_parameter (f, parameters, Qcursor_color, build_string ("black"),
		       "cursorColor", "Foreground", RES_TYPE_STRING);
  x_default_parameter (f, parameters, Qborder_color, build_string ("black"),
		       "borderColor", "BorderColor", RES_TYPE_STRING);
  x_default_parameter (f, parameters, Qscreen_gamma, Qnil,
		       "screenGamma", "ScreenGamma", RES_TYPE_FLOAT);
  x_default_parameter (f, parameters, Qline_spacing, Qnil,
		       "lineSpacing", "LineSpacing", RES_TYPE_NUMBER);
  x_default_parameter (f, parameters, Qleft_fringe, Qnil,
		       "leftFringe", "LeftFringe", RES_TYPE_NUMBER);
  x_default_parameter (f, parameters, Qright_fringe, Qnil,
		       "rightFringe", "RightFringe", RES_TYPE_NUMBER);

#if 0
  x_default_parameter (f, parameters, Qscroll_bar_foreground,
		       build_string ("LightBlue"),
		       "scrollBarForeground", "ScrollBarForeground",
		       RES_TYPE_STRING);
  x_default_parameter (f, parameters, Qscroll_bar_background,
		       build_string ("black"),
		       "scrollBarForeground", "ScrollBarForeground",
		       RES_TYPE_STRING);
#endif

  /* Init faces before x_default_parameter is called for scroll-bar
     parameters because that function calls x_set_scroll_bar_width,
     which calls change_frame_size, which calls Fset_window_buffer,
     which runs hooks, which call Fvertical_motion.  At the end, we
     end up in init_iterator with a null face cache, which should not
     happen.  */
  init_frame_faces (f);

  x_default_parameter (f, parameters, Qmenu_bar_lines, make_number (1),
		       "menuBar", "MenuBar", RES_TYPE_NUMBER);
  x_default_parameter (f, parameters, Qtool_bar_lines, make_number (1),
		       "toolBar", "ToolBar", RES_TYPE_NUMBER);

  x_default_parameter (f, parameters, Qbuffer_predicate, Qnil,
		       "bufferPredicate", "BufferPredicate", RES_TYPE_SYMBOL);
  x_default_parameter (f, parameters, Qtitle, Qnil,
		       "title", "Title", RES_TYPE_STRING);
  x_default_parameter (f, parameters, Qfullscreen, Qnil,
		       "fullscreen", "Fullscreen", RES_TYPE_SYMBOL);

  f->output_data.mw32->dwStyle = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
  f->output_data.mw32->dwStyleEx = WS_EX_CLIENTEDGE;

  f->output_data.mw32->parent_desc = FRAME_MW32_DISPLAY_INFO (f)->root_window;

  window_prompting = x_figure_window_size (f, parameters, 1);

  tem = x_get_arg (dpyinfo, parameters, Qunsplittable, 0, 0, RES_TYPE_BOOLEAN);
  f->no_split = minibuffer_only || EQ (tem, Qt);

  /* Create the X widget or window.  */
  mw32_window (f, minibuffer_only);

  GET_FRAME_HDC (f);

  /* Now consider the frame official.  */
  FRAME_MW32_DISPLAY_INFO (f)->reference_count++;
  Vframe_list = Fcons (frame, Vframe_list);

  /* We need to do this after creating the window, so that the
     icon-creation functions can say whose icon they're describing.  */
  x_default_parameter (f, parameters, Qicon_type, Qnil,
		       "bitmapIcon", "BitmapIcon", RES_TYPE_SYMBOL);

  x_default_parameter (f, parameters, Qauto_raise, Qnil,
		       "autoRaise", "AutoRaiseLower", RES_TYPE_BOOLEAN);
  x_default_parameter (f, parameters, Qauto_lower, Qnil,
		       "autoLower", "AutoRaiseLower", RES_TYPE_BOOLEAN);
  x_default_parameter (f, parameters, Qcursor_type, Qbox,
		       "cursorType", "CursorType", RES_TYPE_SYMBOL);
  x_default_parameter (f, parameters, Qcursor_height, make_number (4),
		       "cursorHeight", "CursorHeight", RES_TYPE_NUMBER);
  x_default_parameter (f, parameters, Qscroll_bar_width, Qnil,
		       "scrollBarWidth", "ScrollBarWidth", RES_TYPE_NUMBER);
  x_default_parameter (f, parameters, Qalpha, Qnil,
		       "alpha", "Alpha", RES_TYPE_NUMBER);

  /* Dimensions, especially FRAME_LINES (f), must be done via
     change_frame_size.  Change will not be effected unless different
     from the current FRAME_LINES (f).  */
  width = FRAME_COLS (f);
  height = FRAME_LINES (f);

  FRAME_LINES (f) = 0;
  SET_FRAME_COLS (f, 0);
  change_frame_size (f, height, width, 1, 0, 0);

  /* Set up faces after all frame parameters are known.  This call
     also merges in face attributes specified for new frames.  If we
     don't do this, the `menu' face for instance won't have the right
     colors, and the menu bar won't appear in the specified colors for
     new frames.  */
  call1 (Qface_set_after_frame_default, frame);

#ifdef USE_X_TOOLKIT
  /* Create the menu bar.  */
  if (!minibuffer_only && FRAME_EXTERNAL_MENU_BAR (f))
    {
      /* If this signals an error, we haven't set size hints for the
	 frame and we didn't make it visible.  */
      initialize_frame_menubar (f);

      /* This is a no-op, except under Motif where it arranges the
	 main window for the widgets on it.  */
      lw_set_main_areas (f->output_data.x->column_widget,
			 f->output_data.x->menubar_widget,
			 f->output_data.x->edit_widget);
    }
#endif /* USE_X_TOOLKIT */

  /* Make the window appear on the frame and enable display, unless
     the caller says not to.  However, with explicit parent, Emacs
     cannot control visibility, so don't try.  */
  if (! f->output_data.mw32->explicit_parent)
    {
      Lisp_Object visibility;

      visibility = x_get_arg (dpyinfo, parameters, Qvisibility, 0, 0,
				 RES_TYPE_SYMBOL);
      if (EQ (visibility, Qunbound))
	visibility = Qt;

      if (EQ (visibility, Qicon))
	x_iconify_frame (f);
      else
	{
	  if (! NILP (visibility))
	    {
	      x_make_frame_visible (f);
	      mw32_new_focus_frame (dpyinfo, f);
	    }
	  SetForegroundWindow (FRAME_MW32_WINDOW (f));
	}
    }
  UNGCPRO;

  /* Make sure windows on this frame appear in calls to next-window
     and similar functions.  */
  Vwindow_list = Qnil;

  RELEASE_FRAME_HDC (f);
  return unbind_to (count, frame);
}


/***********************************************************************
			   Lisp Functions.
 ***********************************************************************/

/* FRAME is used only to get a handle on the X display.  We don't pass the
   display info directly because we're called from frame.c, which doesn't
   know about that structure.  */

Lisp_Object
x_get_focus_frame (struct frame *frame)
{
  struct mw32_display_info *dpyinfo = FRAME_MW32_DISPLAY_INFO (frame);
  Lisp_Object xfocus;
  if (! dpyinfo->mw32_focus_frame)
    return Qnil;

  XSETFRAME (xfocus, dpyinfo->mw32_focus_frame);
  return xfocus;
}


/* In certain situations, when the window manager follows a
   click-to-focus policy, there seems to be no way around calling
   XSetInputFocus to give another frame the input focus .

   In an ideal world, XSetInputFocus should generally be avoided so
   that applications don't interfere with the window manager's focus
   policy.  But I think it's okay to use when it's clearly done
   following a user-command.  */

DEFUN ("x-focus-frame", Fx_focus_frame, Sx_focus_frame, 1, 1, 0,
       doc: /* Set the input focus to FRAME.
FRAME nil means use the selected frame.  */)
  (frame)
     Lisp_Object frame;
{
  struct frame *f = check_mw32_frame (frame);

  SetFocus (FRAME_MW32_WINDOW (f));

  return Qnil;
}


/***********************************************************************
           Color (later we move this part ot mw32color.c)
 ***********************************************************************/

DEFUN ("xw-color-defined-p", Fxw_color_defined_p, Sxw_color_defined_p, 1, 2, 0,
       doc: /* Internal function called by `color-defined-p', which see.  */)
  (color, frame)
     Lisp_Object color, frame;
{
  COLORREF pixel;
  FRAME_PTR f = check_mw32_frame (frame);

  CHECK_STRING (color);

  if (mw32_defined_color (f, SDATA (color), &pixel, 0))
    return Qt;
  else
    return Qnil;
}

DEFUN ("xw-color-values", Fxw_color_values, Sxw_color_values, 1, 2, 0,
       doc: /* Internal function called by `color-values', which see.  */)
  (color, frame)
     Lisp_Object color, frame;
{
  XColor pixel;
  FRAME_PTR f = check_mw32_frame (frame);

  CHECK_STRING (color);

  if (x_defined_color (f, SDATA (color), &pixel, 0))
    {
      Lisp_Object rgb[3];

      rgb[0] = make_number (pixel.red);
      rgb[1] = make_number (pixel.green);
      rgb[2] = make_number (pixel.blue);
      return Flist (3, rgb);
    }
  else
    return Qnil;
}

DEFUN ("xw-display-color-p", Fxw_display_color_p, Sxw_display_color_p, 0, 1, 0,
       doc: /* Internal function called by `display-color-p', which see.  */)
  (display)
     Lisp_Object display;
{
  struct mw32_display_info *dpyinfo = check_x_display_info (display);

  if (dpyinfo->n_planes <= 2)
    return Qnil;

  return Qt;
}

DEFUN ("x-display-grayscale-p", Fx_display_grayscale_p, Sx_display_grayscale_p,
       0, 1, 0,
       doc: /* Return t if the X display supports shades of gray.
Note that color displays do support shades of gray.
The optional argument DISPLAY specifies which display to ask about.
DISPLAY should be either a frame or a display name (a string).
If omitted or nil, that stands for the selected frame's display.  */)
  (display)
     Lisp_Object display;
{
  struct mw32_display_info *dpyinfo = check_x_display_info (display);

  if (dpyinfo->n_planes <= 1)
    return Qnil;

  return Qt;
}

DEFUN ("x-display-color-cells", Fx_display_color_cells, Sx_display_color_cells,
       0, 1, 0,
       doc: /* Returns the number of color cells of DISPLAY.
The optional argument DISPLAY specifies which display to ask about.
DISPLAY should be either a frame or a display name (a string).
If omitted or nil, that stands for the selected frame's display.  */)
  (display)
     Lisp_Object display;
{
  struct mw32_display_info *dpyinfo = check_x_display_info (display);
  HDC hdc;
  int cap = -1;

  hdc = GetDC (dpyinfo->root_window);
  if (GetDeviceCaps (hdc, RASTERCAPS) & RC_PALETTE)
    cap = GetDeviceCaps (hdc, SIZEPALETTE);

  if (cap < 0)
    {
      /* GetDeviceCaps (hdc, NUMCOLORS) returns unexpected value when
	 Emacs runs on Remote Desktop, so use BITSPIXEL and PLANES to
	 culculate the number of available colors */
      int bpp = GetDeviceCaps (hdc, BITSPIXEL) * GetDeviceCaps (hdc, PLANES);
      cap = 1 << min (bpp, 24);
    }

  ReleaseDC (dpyinfo->root_window, hdc);

  return make_number (cap);
}


/***********************************************************************
  external interface part mainly for frame.c
                                (Why are these functions here?;_;)
 ***********************************************************************/

int
x_pixel_width (f)
     register struct frame *f;
{
  return FRAME_PIXEL_WIDTH (f);
}

int
x_pixel_height (f)
     register struct frame *f;
{
  return FRAME_PIXEL_HEIGHT (f);
}

int
x_char_width (f)
     register struct frame *f;
{
  return FRAME_DEFAULT_FONT_WIDTH (f);
}

int
x_char_height (f)
     register struct frame *f;
{
  return FRAME_LINE_HEIGHT (f);
}

int
x_screen_planes (f)
     register struct frame *f;
{
  return FRAME_MW32_DISPLAY_INFO (f)->n_planes;
}

/*
 *    XParseGeometry parses strings of the form
 *   "=<width>x<height>{+-}<xoffset>{+-}<yoffset>", where
 *   width, height, xoffset, and yoffset are unsigned integers.
 *   Example:  "=80x24+300-49"
 *   The equal sign is optional.
 *   It returns a bitmask that indicates which of the four values
 *   were actually found in the string.  For each value found,
 *   the corresponding argument is updated;  for each value
 *   not found, the corresponding argument is left unchanged.
 */

static int
read_integer (string, NextString)
     register char *string;
     char **NextString;
{
  register int Result = 0;
  int Sign = 1;

  if (*string == '+')
    string++;
  else if (*string == '-')
    {
      string++;
      Sign = -1;
    }
  for (; (*string >= '0') && (*string <= '9'); string++)
    {
      Result = (Result * 10) + (*string - '0');
    }
  *NextString = string;
  if (Sign >= 0)
    return (Result);
  else
    return (-Result);
}

int
XParseGeometry (string, x, y, width, height)
     char *string;
     int *x, *y;
     unsigned int *width, *height;    /* RETURN */
{
  int mask = NoValue;
  register char *strind;
  unsigned int tempWidth, tempHeight;
  int tempX, tempY;
  char *nextCharacter;

  if ((string == NULL) || (*string == '\0')) return (mask);
  if (*string == '=')
    string++;  /* ignore possible '=' at beg of geometry spec */

  strind = (char *)string;
  if (*strind != '+' && *strind != '-' && *strind != 'x')
    {
      tempWidth = read_integer (strind, &nextCharacter);
      if (strind == nextCharacter)
	return (0);
      strind = nextCharacter;
      mask |= WidthValue;
    }

  if (*strind == 'x' || *strind == 'X')
    {
      strind++;
      tempHeight = read_integer (strind, &nextCharacter);
      if (strind == nextCharacter)
	return (0);
      strind = nextCharacter;
      mask |= HeightValue;
    }

  if ((*strind == '+') || (*strind == '-'))
    {
      if (*strind == '-')
	{
	  strind++;
	  tempX = -read_integer (strind, &nextCharacter);
	  if (strind == nextCharacter)
	    return (0);
	  strind = nextCharacter;
	  mask |= XNegative;

	}
      else
	{
	  strind++;
	  tempX = read_integer (strind, &nextCharacter);
	  if (strind == nextCharacter)
	    return (0);
	  strind = nextCharacter;
	}
      mask |= XValue;
      if ((*strind == '+') || (*strind == '-'))
	{
	  if (*strind == '-')
	    {
	      strind++;
	      tempY = -read_integer (strind, &nextCharacter);
	      if (strind == nextCharacter)
		return (0);
	      strind = nextCharacter;
	      mask |= YNegative;

	    }
	  else
	    {
	      strind++;
	      tempY = read_integer (strind, &nextCharacter);
	      if (strind == nextCharacter)
		return (0);
	      strind = nextCharacter;
	    }
	  mask |= YValue;
	}
    }

  /* If strind isn't at the end of the string the it's an invalid
     geometry specification. */

  if (*strind != '\0') return (0);

  if (mask & XValue)
    *x = tempX;
  if (mask & YValue)
    *y = tempY;
  if (mask & WidthValue)
    *width = tempWidth;
  if (mask & HeightValue)
    *height = tempHeight;
  return (mask);
}

void
x_sync (FRAME_PTR f)
{
  /* do nothing. */
}



/***********************************************************************
              Open/Close connection (This style is from xterm.c).
 ***********************************************************************/

/* Return the X display structure for the display named NAME.
   Open a new connection if necessary.  */

static struct mw32_display_info *
mw32_display_info_for_name (Lisp_Object name)
{
  Lisp_Object names;
  struct mw32_display_info *dpyinfo;

  CHECK_STRING (name);

  if (! EQ (Vwindow_system, intern ("w32")))
    error ("Not using MW32");

  if (mw32_display_list)
    return mw32_display_list;

  /* Use this general default value to start with.  */
  Vx_resource_name = Vinvocation_name;

  validate_x_resource_name ();

  dpyinfo = mw32_term_init (name, (char *)0,
			    (char *) SDATA (Vx_resource_name));

  if (dpyinfo == 0)
    {
      fatal ("Cannot initialize MW32 window system %s.",
	     SDATA (name));
    }

  mw32_open = 1;

  XSETFASTINT (Vwindow_system_version, 1);

  return dpyinfo;
}

DEFUN ("x-open-connection", Fx_open_connection, Sx_open_connection,
       1, 3, 0, doc: /* Open a connection to an X server.
DISPLAY is the name of the display to connect to.
Optional second arg XRM-STRING is a string of resources in xrdb format.
If the optional third arg MUST-SUCCEED is non-nil,
terminate Emacs if we can't open the connection.  */)
  (display, xrm_string, must_succeed)
     Lisp_Object display, xrm_string, must_succeed;
{
  unsigned char *xrm_option;
  struct mw32_display_info *dpyinfo;

  CHECK_STRING (display);
  if (! NILP (xrm_string))
    CHECK_STRING (xrm_string);

  if (! EQ (Vwindow_system, intern ("w32")))
    error ("Not using MW32");

  mw32_init_app (hinst);
  Vmw32_color_map = Fmw32_default_color_map ();

  if (! NILP (xrm_string))
    xrm_option = (unsigned char *) SDATA (xrm_string);
  else
    xrm_option = (unsigned char *) 0;

  validate_x_resource_name ();

  /* This is what opens the connection and sets x_current_display.
     This also initializes many symbols, such as those used for input.  */
  dpyinfo = mw32_term_init (display, xrm_option,
			    (char *) SDATA (Vx_resource_name));

  if (dpyinfo == 0)
    {
      fatal ("Cannot initialize MW32 window system %s.",
	     SDATA (display));
    }

  mw32_open = 1;

  XSETFASTINT (Vwindow_system_version, 1);
  return Qnil;
}

DEFUN ("x-close-connection", Fx_close_connection,
       Sx_close_connection, 1, 1, 0,
       doc: /* Close the connection to DISPLAY's X server.
For DISPLAY, specify either a frame or a display name (a string).
If DISPLAY is nil, that stands for the selected frame's display.  */)
  (display)
  Lisp_Object display;
{
  struct mw32_display_info *dpyinfo = check_x_display_info (display);
  int i;

  if (dpyinfo->reference_count > 0)
    error ("Display still has frames on it");

#if 0
  /* Free the fonts in the font table.  */
  for (i = 0; i < dpyinfo->n_fonts; i++)
    if (dpyinfo->font_table[i].name)
      {
	if (dpyinfo->font_table[i].name != dpyinfo->font_table[i].full_name)
	  xfree (dpyinfo->font_table[i].full_name);
	xfree (dpyinfo->font_table[i].name);
	XFreeFont (dpyinfo->display, dpyinfo->font_table[i].font);
      }

  x_destroy_all_bitmaps (dpyinfo);
  XSetCloseDownMode (dpyinfo->display, DestroyAll);
#endif

  mw32_delete_display (dpyinfo);

  POST_THREAD_INFORM_MESSAGE (msg_thread_id,
			      WM_EMACS_CLOSE_CONNECTION,
			      (WPARAM) 0, (LPARAM) 0);
  mw32_open = 0;

  return Qnil;
}

DEFUN ("x-display-list", Fx_display_list, Sx_display_list, 0, 0, 0,
       doc: /* Return the list of display names that Emacs has connections to.  */)
  ()
{
  return Qnil;
}

/* Wait for responses to all X commands issued so far for frame F.  */


/***********************************************************************
				Busy cursor
 ***********************************************************************/

/* If non-null, an asynchronous timer that, when it expires, displays
   an hourglass cursor on all frames.  */

static struct atimer *hourglass_atimer;

/* Non-zero means an hourglass cursor is currently shown.  */

static int hourglass_shown_p;

/* Number of seconds to wait before displaying an hourglass cursor.  */

static Lisp_Object Vhourglass_delay;

/* Default number of seconds to wait before displaying an hourglass
   cursor.  */

#define DEFAULT_HOURGLASS_DELAY 1

/* Function prototypes.  */

static void show_hourglass P_ ((struct atimer *));
static void hide_hourglass P_ ((void));


/* Cancel a currently active hourglass timer, and start a new one.  */

void
start_hourglass (void)
{
  EMACS_TIME delay;
  int secs, usecs = 0;

  cancel_hourglass ();

  if (INTEGERP (Vhourglass_delay)
      && XINT (Vhourglass_delay) > 0)
    secs = XFASTINT (Vhourglass_delay);
  else if (FLOATP (Vhourglass_delay)
	   && XFLOAT_DATA (Vhourglass_delay) > 0)
    {
      Lisp_Object tem;
      tem = Ftruncate (Vhourglass_delay, Qnil);
      secs = XFASTINT (tem);
      usecs = (int) ((XFLOAT_DATA (Vhourglass_delay) - secs) * 1000000);
    }
  else
    secs = DEFAULT_HOURGLASS_DELAY;

  EMACS_SET_SECS_USECS (delay, secs, usecs);
  hourglass_atimer = start_atimer (ATIMER_RELATIVE, delay,
				   show_hourglass, NULL);
}


/* Cancel the hourglass cursor timer if active, hide a busy cursor if
   shown.  */

void
cancel_hourglass (void)
{
  if (hourglass_atimer)
    {
      cancel_atimer (hourglass_atimer);
      hourglass_atimer = NULL;
    }

  if (hourglass_shown_p)
    hide_hourglass ();
}


/* Timer function of hourglass_atimer.  TIMER is equal to
   hourglass_atimer.

   Display an hourglass pointer on all frames by mapping the frames'
   hourglass_window.  Set the hourglass_p flag in the frames'
   output_data.mw32 structure to indicate that an hourglass cursor is
   shown on the frames.  */

static void
show_hourglass (struct atimer *timer)
{
  /* The timer implementation will cancel this timer automatically
     after this function has run.  Set hourglass_atimer to null
     so that we know the timer doesn't have to be canceled.  */
  hourglass_atimer = NULL;

  if (!hourglass_shown_p)
    {
      Lisp_Object rest, frame;

      BLOCK_INPUT;

      FOR_EACH_FRAME (rest, frame)
	{
	  struct frame *f = XFRAME (frame);

	  f->output_data.mw32->hourglass_p = 1;
	  if (FRAME_LIVE_P (f) && FRAME_MW32_P (f))
	    {
	      if (FRAME_OUTER_WINDOW (f))
		{
		  /* TODO: SHOW hourglass mouse cursor.  */
		}
	    }
	}

      hourglass_shown_p = 1;
      UNBLOCK_INPUT;
    }
}


/* Hide the hourglass pointer on all frames, if it is currently
   shown.  */

static void
hide_hourglass (void)
{
  if (hourglass_shown_p)
    {
      Lisp_Object rest, frame;

      BLOCK_INPUT;
      FOR_EACH_FRAME (rest, frame)
	{
	  struct frame *f = XFRAME (frame);

	  if (FRAME_MW32_P (f)
	      /* Watch out for newly created frames.  */
	      && f->output_data.mw32->hourglass_p)
	    {
	      /* hide hourglass mouse cursor.  */
	      f->output_data.mw32->hourglass_p = 0;
	    }
	}

      hourglass_shown_p = 0;
      UNBLOCK_INPUT;
    }
}



/***********************************************************************
				Tool tips
 ***********************************************************************/

static Lisp_Object mw32_create_tip_frame P_ ((struct mw32_display_info *,
					      Lisp_Object, Lisp_Object));
static void compute_tip_xy P_ ((struct frame *, Lisp_Object, Lisp_Object,
				Lisp_Object, int, int, int *, int *));

/* The frame of a currently visible tooltip.  */

Lisp_Object tip_frame;

/* If non-nil, a timer started that hides the last tooltip when it
   fires.  */

Lisp_Object tip_timer;
Window tip_window;

/* If non-nil, a vector of 3 elements containing the last args
   with which x-show-tip was called.  See there.  */

Lisp_Object last_show_tip_args;

/* Maximum size for tooltips; a cons (COLUMNS . ROWS).  */

Lisp_Object Vmw32_max_tooltip_size;


static Lisp_Object
unwind_create_tip_frame (Lisp_Object frame)
{
  Lisp_Object deleted;

  deleted = unwind_create_frame (frame);
  if (EQ (deleted, Qt))
    {
      tip_window = NULL;
      tip_frame = Qnil;
    }

  return deleted;
}


/* Create a frame for a tooltip on the display described by DPYINFO.
   PARMS is a list of frame parameters.  TEXT is the string to
   display in the tip frame.  Value is the frame.

   Note that functions called here, esp. x_default_parameter can
   signal errors, for instance when a specified color name is
   undefined.  We have to make sure that we're in a consistent state
   when this happens.  */

static Lisp_Object
mw32_create_tip_frame (struct mw32_display_info *dpyinfo,
		       Lisp_Object parms, Lisp_Object text)
{
  struct frame *f;
  Lisp_Object frame, tem;
  Lisp_Object name;
  long window_prompting = 0;
  int width, height;
  int count = SPECPDL_INDEX ();
  struct gcpro gcpro1, gcpro2, gcpro3;
  struct kboard *kb;
  int face_change_count_before = face_change_count;
  Lisp_Object buffer;
  struct buffer *old_buffer;

  check_mw32 ();

  /* Use this general default value to start with until we know if
     this frame has a specified name.  */
  Vx_resource_name = Vinvocation_name;

#ifdef MULTI_KBOARD
  kb = dpyinfo->kboard;
#else
  kb = &the_only_kboard;
#endif

  /* Get the name of the frame to use for resource lookup.  */
  name = x_get_arg (dpyinfo, parms, Qname, "name", "Name", RES_TYPE_STRING);
  if (!STRINGP (name)
      && !EQ (name, Qunbound)
      && !NILP (name))
    error ("Invalid frame name--not a string or nil");
  Vx_resource_name = name;

  frame = Qnil;
  GCPRO3 (parms, name, frame);
  /* Make a frame without minibuffer nor mode-line.  */
  f = make_frame (0);
  f->wants_modeline = 0;
  XSETFRAME (frame, f);

  buffer = Fget_buffer_create (build_string (" *tip*"));
  Fset_window_buffer (FRAME_ROOT_WINDOW (f), buffer, Qnil);
  old_buffer = current_buffer;
  set_buffer_internal_1 (XBUFFER (buffer));
  current_buffer->truncate_lines = Qnil;
  Ferase_buffer ();
  Finsert (1, &text);
  set_buffer_internal_1 (old_buffer);

  FRAME_CAN_HAVE_SCROLL_BARS (f) = 0;
  record_unwind_protect (unwind_create_tip_frame, frame);

  /* By setting the output method, we're essentially saying that
     the frame is live, as per FRAME_LIVE_P.  If we get a signal
     from this point on, x_destroy_window might screw up reference
     counts etc.  */
  f->output_method = output_mw32;
  f->output_data.mw32 =
    (struct mw32_output *) xmalloc (sizeof (struct mw32_output));
  bzero (f->output_data.mw32, sizeof (struct mw32_output));

  f->output_data.mw32->icon_bitmap = -1;
  f->output_data.mw32->fontset = -1;
  f->output_data.mw32->scroll_bar_foreground_pixel = -1;
  f->output_data.mw32->scroll_bar_background_pixel = -1;
  /* all handles must be set to INVALID_HANLE_VALUE.  */
  f->output_data.mw32->menubar_handle = INVALID_HANDLE_VALUE;
  f->output_data.mw32->hdc = INVALID_HANDLE_VALUE;
  f->output_data.mw32->hdc_nestlevel = 0;
  f->output_data.mw32->pending_clear_mouse_face = 0;
  InitializeCriticalSection (&(f->output_data.mw32->hdc_critsec));

  FRAME_MW32_DISPLAY_INFO (f) = dpyinfo;
  f->icon_name = Qnil;

#if 0 /* GLYPH_DEBUG TODO: image support.  */
  image_cache_refcount = FRAME_X_IMAGE_CACHE (f)->refcount;
  dpyinfo_refcount = dpyinfo->reference_count;
#endif /* GLYPH_DEBUG */
#ifdef MULTI_KBOARD
  FRAME_KBOARD (f) = kb;
#endif

  f->output_data.mw32->parent_desc = FRAME_MW32_DISPLAY_INFO (f)->root_window;
  f->output_data.mw32->explicit_parent = 0;
  f->output_data.mw32->dwStyle = WS_BORDER | WS_POPUP | WS_DISABLED;

  /* Set the name; the functions to which we pass f expect the name to
     be set.  */
  if (EQ (name, Qunbound) || NILP (name))
    {
      f->name = build_string (dpyinfo->mw32_id_name);
      f->explicit_name = 0;
    }
  else
    {
      f->name = name;
      f->explicit_name = 1;
      /* use the frame's title when getting resources for this frame.  */
      specbind (Qx_resource_name, name);
    }

  /* Extract the window parameters from the supplied values
     that are needed to determine window geometry.  */
  {
    Lisp_Object font, fontset;
    int use_default_font_p = 1;

    tem = Qnil;

    font = x_get_arg (dpyinfo, parms, Qfont,
			 "font", "Font", RES_TYPE_STRING);

    BLOCK_INPUT;
    /* First, try whatever font the caller has specified.  */
    if (STRINGP (font))
      {
	fontset = Fquery_fontset (font, Qnil);
	if (STRINGP (fontset))
	  tem = mw32_new_fontset (f, SDATA (fontset));
	else
	  tem = mw32_new_font (f, SDATA (font));

	if (STRINGP (tem))
	  use_default_font_p = 0;
	else
	  message ("Cannot select tooltip font: `%s'", SDATA (font));
      }

    /* Next, try default font.  */
    if (!STRINGP (tem))
      {
	font = build_string ("default");
	fontset = Fquery_fontset (font, Qnil);
	if (STRINGP (fontset))
	  tem = mw32_new_fontset (f, SDATA (fontset));
	else
	  tem = mw32_new_font (f, SDATA (font));
      }
    UNBLOCK_INPUT;
    /* In case of default font, second argument must be nil.  */
    x_default_parameter (f, use_default_font_p ? Qnil : parms,
			 Qfont, tem, "font", "Font", RES_TYPE_STRING);
  }

  x_default_parameter (f, parms, Qborder_width, make_number (2),
		       "borderWidth", "BorderWidth", RES_TYPE_NUMBER);
  /* This defaults to 2 in order to match xterm.  We recognize either
     internalBorderWidth or internalBorder (which is what xterm calls
     it).  */
  if (NILP (Fassq (Qinternal_border_width, parms)))
    {
      Lisp_Object value;

      value = x_get_arg (dpyinfo, parms, Qinternal_border_width,
			    "internalBorder", "internalBorder",
			    RES_TYPE_NUMBER);
      if (! EQ (value, Qunbound))
	parms = Fcons (Fcons (Qinternal_border_width, value),
		       parms);
    }
  x_default_parameter (f, parms, Qinternal_border_width, make_number (1),
		       "internalBorderWidth", "internalBorderWidth",
		       RES_TYPE_NUMBER);

  /* Also do the stuff which must be set before the window exists.  */
  x_default_parameter (f, parms, Qforeground_color, build_string ("black"),
		       "foreground", "Foreground", RES_TYPE_STRING);
  x_default_parameter (f, parms, Qbackground_color, build_string ("white"),
		       "background", "Background", RES_TYPE_STRING);
  x_default_parameter (f, parms, Qmouse_color, build_string ("black"),
		       "pointerColor", "Foreground", RES_TYPE_STRING);
  x_default_parameter (f, parms, Qcursor_color, build_string ("black"),
		       "cursorColor", "Foreground", RES_TYPE_STRING);
  x_default_parameter (f, parms, Qborder_color, build_string ("black"),
		       "borderColor", "BorderColor", RES_TYPE_STRING);

  /* Init faces before x_default_parameter is called for scroll-bar
     parameters because that function calls x_set_scroll_bar_width,
     which calls change_frame_size, which calls Fset_window_buffer,
     which runs hooks, which call Fvertical_motion.  At the end, we
     end up in init_iterator with a null face cache, which should not
     happen.  */
  init_frame_faces (f);

  window_prompting = x_figure_window_size (f, parms, 0);

  /* No fringes on tip frame.  */
  f->fringe_cols = 0;
  f->left_fringe_width = 0;
  f->right_fringe_width = 0;

  if (window_prompting & XNegative)
    {
      if (window_prompting & YNegative)
	f->win_gravity = SouthEastGravity;
      else
	f->win_gravity = NorthEastGravity;
    }
  else
    {
      if (window_prompting & YNegative)
	f->win_gravity = SouthWestGravity;
      else
	f->win_gravity = NorthWestGravity;
    }

  f->size_hint_flags = window_prompting;

  BLOCK_INPUT;
  tip_window = FRAME_MW32_WINDOW (f) = mw32_create_tip_window (f);
  UNBLOCK_INPUT;

  x_default_parameter (f, parms, Qauto_raise, Qnil,
		       "autoRaise", "AutoRaiseLower", RES_TYPE_BOOLEAN);
  x_default_parameter (f, parms, Qauto_lower, Qnil,
		       "autoLower", "AutoRaiseLower", RES_TYPE_BOOLEAN);

  /* Dimensions, especially FRAME_LINES (f), must be done via change_frame_size.
     Change will not be effected unless different from the current
     FRAME_LINES (f).  */
  width = FRAME_COLS (f);
  height = FRAME_LINES (f);
  FRAME_LINES (f) = 0;
  SET_FRAME_COLS (f, 0);
  change_frame_size (f, height, width, 1, 0, 0);

  /* Add `tooltip' frame parameter's default value. */
  if (NILP (Fframe_parameter (frame, intern ("tooltip"))))
    Fmodify_frame_parameters (frame, Fcons (Fcons (intern ("tooltip"), Qt),
					    Qnil));

  /* Set up faces after all frame parameters are known.  This call
     also merges in face attributes specified for new frames.

     Frame parameters may be changed if .Xdefaults contains
     specifications for the default font.  For example, if there is an
     `Emacs.default.attributeBackground: pink', the `background-color'
     attribute of the frame get's set, which let's the internal border
     of the tooltip frame appear in pink.  Prevent this.  */
  {
    Lisp_Object bg = Fframe_parameter (frame, Qbackground_color);

    call1 (Qface_set_after_frame_default, frame);

    if (!EQ (bg, Fframe_parameter (frame, Qbackground_color)))
      Fmodify_frame_parameters (frame, Fcons (Fcons (Qbackground_color, bg),
					      Qnil));
  }

  f->no_split = 1;

  UNGCPRO;

  /* It is now ok to make the frame official even if we get an error
     below.  And the frame needs to be on Vframe_list or making it
     visible won't work.  */
  Vframe_list = Fcons (frame, Vframe_list);

  /* Now that the frame is official, it counts as a reference to
     its display.  */
  FRAME_MW32_DISPLAY_INFO (f)->reference_count++;

  /* Setting attributes of faces of the tooltip frame from resources
     and similar will increment face_change_count, which leads to the
     clearing of all current matrices.  Since this isn't necessary
     here, avoid it by resetting face_change_count to the value it
     had before we created the tip frame.  */
  face_change_count = face_change_count_before;

  /* Discard the unwind_protect.  */
  return unbind_to (count, frame);
}


/* Compute where to display tip frame F.  PARMS is the list of frame
   parameters for F.  DX and DY are specified offsets from the current
   location of the mouse.  WIDTH and HEIGHT are the width and height
   of the tooltip.  Return coordinates relative to the root window of
   the display in *ROOT_X, and *ROOT_Y.  */

static void
compute_tip_xy (struct frame *f, Lisp_Object parms, Lisp_Object dx,
		Lisp_Object dy, int width, int height,
		int *root_x, int *root_y)
{
  Lisp_Object left, top;

  /* User-specified position?  */
  left = Fcdr (Fassq (Qleft, parms));
  top  = Fcdr (Fassq (Qtop, parms));

  /* Move the tooltip window where the mouse pointer is.  Resize and
     show it.  */
  if (!INTEGERP (left) || !INTEGERP (top))
    {
      POINT pt;

      BLOCK_INPUT;
      GetCursorPos (&pt);
      *root_x = pt.x;
      *root_y = pt.y;
      UNBLOCK_INPUT;
    }

  if (INTEGERP (top))
    *root_y = XINT (top);
  else if (*root_y + XINT (dy) <= 0)
    *root_y = 0; /* Can happen for negative dy */
  else if (*root_y + XINT (dy) + height <= FRAME_MW32_DISPLAY_INFO (f)->height)
    /* It fits below the pointer */
      *root_y += XINT (dy);
  else if (height + XINT (dy) <= *root_y)
    /* It fits above the pointer.  */
    *root_y -= height + XINT (dy);
  else
    /* Put it on the top.  */
    *root_y = 0;

  if (INTEGERP (left))
    *root_x = XINT (left);
  else if (*root_x + XINT (dx) <= 0)
    *root_x = 0; /* Can happen for negative dx */
  else if (*root_x + XINT (dx) + width <= FRAME_MW32_DISPLAY_INFO (f)->width)
    /* It fits to the right of the pointer.  */
    *root_x += XINT (dx);
  else if (width + XINT (dx) <= *root_x)
    /* It fits to the left of the pointer.  */
    *root_x -= width + XINT (dx);
  else
    /* Put it left justified on the screen -- it ought to fit that way.  */
    *root_x = 0;
}


DEFUN ("x-show-tip", Fx_show_tip, Sx_show_tip, 1, 6, 0,
       doc: /* Show STRING in a \"tooltip\" window on frame FRAME.
A tooltip window is a small window displaying a string.

FRAME nil or omitted means use the selected frame.

PARMS is an optional list of frame parameters which can be
used to change the tooltip's appearance.

Automatically hide the tooltip after TIMEOUT seconds.  TIMEOUT nil
means use the default timeout of 5 seconds.

If the list of frame parameters PARAMS contains a `left' parameter,
the tooltip is displayed at that x-position.  Otherwise it is
displayed at the mouse position, with offset DX added (default is 5 if
DX isn't specified).  Likewise for the y-position; if a `top' frame
parameter is specified, it determines the y-position of the tooltip
window, otherwise it is displayed at the mouse position, with offset
DY added (default is -10).

A tooltip's maximum size is specified by `x-max-tooltip-size'.
Text larger than the specified size is clipped.  */)
  (string, frame, parms, timeout, dx, dy)
     Lisp_Object string, frame, parms, timeout, dx, dy;
{
  struct frame *f;
  struct window *w;
  int root_x, root_y;
  struct buffer *old_buffer;
  struct text_pos pos;
  int i, width, height;
  struct gcpro gcpro1, gcpro2, gcpro3, gcpro4;
  int old_windows_or_buffers_changed = windows_or_buffers_changed;
  int count = SPECPDL_INDEX ();

  specbind (Qinhibit_redisplay, Qt);

  GCPRO4 (string, parms, frame, timeout);

  CHECK_STRING (string);
  f = check_mw32_frame (frame);
  if (NILP (timeout))
    timeout = make_number (5);
  else
    CHECK_NATNUM (timeout);

  if (NILP (dx))
    dx = make_number (5);
  else
    CHECK_NUMBER (dx);

  if (NILP (dy))
    dy = make_number (-10);
  else
    CHECK_NUMBER (dy);

  if (NILP (last_show_tip_args))
    last_show_tip_args = Fmake_vector (make_number (3), Qnil);

  if (!NILP (tip_frame))
    {
      Lisp_Object last_string = AREF (last_show_tip_args, 0);
      Lisp_Object last_frame = AREF (last_show_tip_args, 1);
      Lisp_Object last_parms = AREF (last_show_tip_args, 2);

      if (EQ (frame, last_frame)
	  && !NILP (Fequal (last_string, string))
	  && !NILP (Fequal (last_parms, parms)))
	{
	  struct frame *f = XFRAME (tip_frame);

	  /* Only DX and DY have changed.  */
	  if (!NILP (tip_timer))
	    {
	      Lisp_Object timer = tip_timer;
	      tip_timer = Qnil;
	      call1 (Qcancel_timer, timer);
	    }

	  BLOCK_INPUT;
	  compute_tip_xy (f, parms, dx, dy, FRAME_PIXEL_WIDTH (f),
			  FRAME_PIXEL_HEIGHT (f), &root_x, &root_y);

	  /* Put tooltip in topmost group and in position.  */
	  SetWindowPos (FRAME_MW32_WINDOW (f), HWND_TOPMOST,
			root_x, root_y, 0, 0,
			SWP_NOSIZE | SWP_NOACTIVATE);

          /* Ensure tooltip is on top of other topmost windows (eg menus).  */
          SetWindowPos (FRAME_MW32_WINDOW (f), HWND_TOP,
			0, 0, 0, 0,
                        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

	  UNBLOCK_INPUT;
	  goto start_timer;
	}
    }

  /* Hide a previous tip, if any.  */
  Fx_hide_tip ();

  ASET (last_show_tip_args, 0, string);
  ASET (last_show_tip_args, 1, frame);
  ASET (last_show_tip_args, 2, parms);

  /* Add default values to frame parameters.  */
  if (NILP (Fassq (Qname, parms)))
    parms = Fcons (Fcons (Qname, build_string ("tooltip")), parms);
  if (NILP (Fassq (Qinternal_border_width, parms)))
    parms = Fcons (Fcons (Qinternal_border_width, make_number (3)), parms);
  if (NILP (Fassq (Qborder_width, parms)))
    parms = Fcons (Fcons (Qborder_width, make_number (1)), parms);
  if (NILP (Fassq (Qborder_color, parms)))
    parms = Fcons (Fcons (Qborder_color, build_string ("lightyellow")), parms);
  if (NILP (Fassq (Qbackground_color, parms)))
    parms = Fcons (Fcons (Qbackground_color, build_string ("lightyellow")),
		   parms);

  /* Block input until the tip has been fully drawn, to avoid crashes
     when drawing tips in menus.  */
  BLOCK_INPUT;

  /* Create a frame for the tooltip, and record it in the global
     variable tip_frame.  */
  frame = mw32_create_tip_frame (FRAME_MW32_DISPLAY_INFO (f), parms, string);
  f = XFRAME (frame);

  /* Set up the frame's root window.  */
  w = XWINDOW (FRAME_ROOT_WINDOW (f));
  w->left_col = w->top_line = make_number (0);

  if (CONSP (Vmw32_max_tooltip_size)
      && INTEGERP (XCAR (Vmw32_max_tooltip_size))
      && XINT (XCAR (Vmw32_max_tooltip_size)) > 0
      && INTEGERP (XCDR (Vmw32_max_tooltip_size))
      && XINT (XCDR (Vmw32_max_tooltip_size)) > 0)
    {
      w->total_cols = XCAR (Vmw32_max_tooltip_size);
      w->total_lines = XCDR (Vmw32_max_tooltip_size);
    }
  else
    {
      w->total_cols = make_number (80);
      w->total_lines = make_number (40);
    }

  f->total_cols = XINT (w->total_cols);
  adjust_glyphs (f);
  w->pseudo_window_p = 1;

  /* Display the tooltip text in a temporary buffer.  */
  old_buffer = current_buffer;
  set_buffer_internal_1 (XBUFFER (XWINDOW (FRAME_ROOT_WINDOW (f))->buffer));
  current_buffer->truncate_lines = Qnil;
  clear_glyph_matrix (w->desired_matrix);
  clear_glyph_matrix (w->current_matrix);
  SET_TEXT_POS (pos, BEGV, BEGV_BYTE);
  try_window (FRAME_ROOT_WINDOW (f), pos, 0);

  /* Compute width and height of the tooltip.  */
  width = height = 0;
  for (i = 0; i < w->desired_matrix->nrows; ++i)
    {
      struct glyph_row *row = &w->desired_matrix->rows[i];
      struct glyph *last;
      int row_width;

      /* Stop at the first empty row at the end.  */
      if (!row->enabled_p || !row->displays_text_p)
	break;

      /* Let the row go over the full width of the frame.  */
      row->full_width_p = 1;

      row_width = row->pixel_width;

      /* There's a glyph at the end of rows that is use to place
	 the cursor there.  Don't include the width of this glyph.  */
      if (row->used[TEXT_AREA] && i != w->desired_matrix->nrows - 1)
	{
	  /* Check if the next row is empty.  */
	  struct glyph_row *row_next = &w->desired_matrix->rows[i + 1];
	  if (!row_next->enabled_p || !row_next->displays_text_p)
	    {
	      last = &row->glyphs[TEXT_AREA][row->used[TEXT_AREA] - 1];
	      row_width -= last->pixel_width;
	    }
	}

      /* TODO: find why tips do not draw along baseline as instructed.  */
      height += row->height;
      width = max (width, row_width);
    }

  /* Add the frame's internal border to the width and height the X
     window should have.  */
  height += 2 * FRAME_INTERNAL_BORDER_WIDTH (f);
  width += 2 * FRAME_INTERNAL_BORDER_WIDTH (f);

  /* Move the tooltip window where the mouse pointer is.  Resize and
     show it.  */
  compute_tip_xy (f, parms, dx, dy, width, height, &root_x, &root_y);

  {
    /* Adjust Window size to take border into account.  */
    RECT rect;
    rect.left = rect.top = 0;
    rect.right = width;
    rect.bottom = height;
    AdjustWindowRect (&rect, f->output_data.mw32->dwStyle,
		      FRAME_EXTERNAL_MENU_BAR (f));

    /* Position and size tooltip, and put it in the topmost group.
       The add-on of 3 to the 5th argument is a kludge: without it,
       some fonts cause the last character of the tip to be truncated,
       for some obscure reason.  */
    SetWindowPos (FRAME_MW32_WINDOW (f), HWND_TOPMOST,
		  root_x, root_y, rect.right - rect.left + 3,
		  rect.bottom - rect.top, SWP_NOACTIVATE);

    /* Ensure tooltip is on top of other topmost windows (eg menus).  */
    SetWindowPos (FRAME_MW32_WINDOW (f), HWND_TOP,
		  0, 0, 0, 0,
		  SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    /* Let redisplay know that we have made the frame visible already.  */
    /* f->async_visible = 1; /* CAUTION: Don't make this flag on here */

    ShowWindow (FRAME_MW32_WINDOW (f), SW_SHOWNOACTIVATE);
  }

  /* Draw into the window.  */
  w->must_be_updated_p = 1;
  update_single_window (w, 1);

  UNBLOCK_INPUT;

  /* Restore original current buffer.  */
  set_buffer_internal_1 (old_buffer);
  windows_or_buffers_changed = old_windows_or_buffers_changed;

  if (tip_frame != Qnil)		/* Is this happen ? */
    Fdelete_frame (frame, Qnil);	/* No room to exist */
  else
    tip_frame = frame;

 start_timer:
  /* Let the tip disappear after timeout seconds.  */
  tip_timer = call3 (intern ("run-at-time"), timeout, Qnil,
		     intern ("x-hide-tip"));

  UNGCPRO;
  return unbind_to (count, Qnil);
}


DEFUN ("x-hide-tip", Fx_hide_tip, Sx_hide_tip, 0, 0, 0,
       doc: /* Hide the current tooltip window, if there is any.
Value is t if tooltip was open, nil otherwise.  */)
  ()
{
  int count;
  Lisp_Object deleted, frame, timer;
  struct gcpro gcpro1, gcpro2;

  /* Return quickly if nothing to do.  */
  if (NILP (tip_timer) && NILP (tip_frame))
    return Qnil;

  frame = tip_frame;
  timer = tip_timer;
  GCPRO2 (frame, timer);
  tip_frame = tip_timer = deleted = Qnil;

  count = SPECPDL_INDEX ();
  specbind (Qinhibit_redisplay, Qt);
  specbind (Qinhibit_quit, Qt);

  if (!NILP (timer))
    call1 (Qcancel_timer, timer);

  if (FRAMEP (frame))
    {
      Fdelete_frame (frame, Qnil);
      deleted = Qt;
    }

  UNGCPRO;
  return unbind_to (count, deleted);
}

/***********************************************************************
			File selection dialog
 ***********************************************************************/
#include <dlgs.h>
#define FILE_NAME_TEXT_FIELD edt1

extern Lisp_Object Qfile_name_history;

/* Callback for altering the behaviour of the Open File dialog.
   Makes the Filename text field contain "Current Directory" and be
   read-only when "Directories" is selected in the filter.  This
   allows us to work around the fact that the standard Open File
   dialog does not support directories.  */
static UINT CALLBACK
file_dialog_callback (HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  if (msg == WM_NOTIFY)
    {
      OFNOTIFY * notify = (OFNOTIFY *) lParam;
      /* Detect when the Filter dropdown is changed.  */
      if (notify->hdr.code == CDN_TYPECHANGE
	  || notify->hdr.code == CDN_INITDONE)
	{
	  HWND dialog = GetParent (hwnd);
	  HWND edit_control = GetDlgItem (dialog, FILE_NAME_TEXT_FIELD);

	  /* Directories is in index 2.  */
	  if (notify->lpOFN->nFilterIndex == 2)
	    {
	      if (notify->hdr.code == CDN_TYPECHANGE)
		notify->lpOFN->Flags &= ~OFN_FILEMUSTEXIST;

	      CommDlg_OpenSave_SetControlText (dialog, FILE_NAME_TEXT_FIELD,
					       "Current Directory");
	      EnableWindow (edit_control, FALSE);
	    }
	  else
	    {
	      if (notify->hdr.code == CDN_TYPECHANGE)
		{
		  if (notify->lpOFN->Flags & OFN_PATHMUSTEXIST)
		    notify->lpOFN->Flags |= OFN_FILEMUSTEXIST;

		  /* Don't override default filename on init done.  */
		  CommDlg_OpenSave_SetControlText (dialog,
						   FILE_NAME_TEXT_FIELD, "");
		}
	      EnableWindow (edit_control, TRUE);
	    }
	}
    }
  return 0;
}

extern int mw32_inhibit_hide_mouse;

DEFUN ("mw32-file-dialog", Fmw32_file_dialog, Smw32_file_dialog, 2, 5, 0,
       doc: /* Read file name, prompting with PROMPT in directory DIR.
Use a file selection dialog.
Select DEFAULT-FILENAME in the dialog's file selection box, if
specified.  Don't let the user enter a file name in the file
selection dialog's entry field, if MUSTMATCH is non-nil.  */)
  (prompt, dir, default_filename, mustmatch, only_dir_p)
     Lisp_Object prompt, dir, default_filename, mustmatch, only_dir_p;
{
  struct frame *f = SELECTED_FRAME ();
  Lisp_Object file = Qnil;
  int count = specpdl_ptr - specpdl;
  struct gcpro gcpro1, gcpro2, gcpro3, gcpro4, gcpro5, gcpro6;
  LPTSTR init_dir;
  TCHAR filename[MAX_PATH + 1];
  int filter_index = 1;
  OPENFILENAME file_details;
  int ret;

  GCPRO6 (prompt, dir, default_filename, mustmatch, only_dir_p, file);
  CHECK_STRING (prompt);
  CHECK_STRING (dir);

  /* Create the dialog with PROMPT as title, using DIR as initial
     directory and using "*" as pattern.  */
  dir = Fexpand_file_name (dir, Qnil);
  init_dir = mw32_encode_lispy_string (Vlocale_coding_system,
				       Funix_to_dos_filename (dir), NULL);

  if (STRINGP (default_filename))
    {
      file = Ffile_name_nondirectory (Funix_to_dos_filename (default_filename));
      if (LISPY_STRING_BYTES (file) > 0)
	{
	  int size;
	  LPTSTR filename_tmp;

	  filename_tmp = mw32_encode_lispy_string (Vlocale_coding_system,
						   file, &size);
	  memcpy (filename, filename_tmp, size);
	  filename[size / sizeof (filename[0])] = '\0';
	}
      else
	filename[0] = '\0';
    }
  else
    filename[0] = '\0';

  /* Prevent redisplay.  */
  specbind (Qinhibit_redisplay, Qt);
  BLOCK_INPUT;

  bzero (&file_details, sizeof (file_details));
  file_details.lStructSize = sizeof (file_details);
  file_details.hwndOwner = FRAME_MW32_WINDOW (f);
  /* Undocumented Bug in Common File Dialog:
     If a filter is not specified, shell links are not resolved.  */
  file_details.lpstrFilter = "All Files (*.*)\0*.*\0Directories\0*|*\0\0";

  if (!NILP (only_dir_p))
    filter_index = 2;

  file_details.nFilterIndex = filter_index;
  file_details.lpstrFile = filename;
  file_details.nMaxFile = sizeof (filename) / sizeof (filename[0]);
  file_details.lpstrInitialDir = init_dir;
  file_details.lpstrTitle
    = mw32_encode_lispy_string (Vlocale_coding_system,
				prompt, NULL);
  file_details.Flags = (OFN_HIDEREADONLY | OFN_NOCHANGEDIR
			| OFN_EXPLORER | OFN_ENABLEHOOK);

  if (!NILP (mustmatch))
    {
      /* Require that the path to the parent directory exists.  */
      file_details.Flags |= OFN_PATHMUSTEXIST;
      /* If we are looking for a file, require that it exists.  */
      if (NILP (only_dir_p))
	file_details.Flags |= OFN_FILEMUSTEXIST;
    }

  file_details.lpfnHook = (LPOFNHOOKPROC) file_dialog_callback;

  mw32_inhibit_hide_mouse = TRUE;
  ret = GetOpenFileName (&file_details);
  mw32_inhibit_hide_mouse = FALSE;
  if (ret)
    {
      file = mw32_decode_lispy_string (Vlocale_coding_system,
				       filename, 0);
      file = Fdos_to_unix_filename (file);
      if (file_details.nFilterIndex == 2)
	{
	  /* "Folder Only" selected - strip dummy file name.  */
	  file = Ffile_name_directory (file);
	}
    }
  else
    file = Qnil;

  UNBLOCK_INPUT;
  file = unbind_to (count, file);

  UNGCPRO;

  /* Make "Cancel" equivalent to C-g.  */
  if (NILP (file))
    Fsignal (Qquit, Qnil);

  return unbind_to (count, file);
}

/***********************************************************************
                         w32 specialized functions
 ***********************************************************************/
static Lisp_Object Qwm_syscommand;
static Lisp_Object Qsc_keymenu;
static Lisp_Object Qsc_monitorpower;
static Lisp_Object Qsc_tasklist;
static Lisp_Object Qsc_maximize;
static Lisp_Object Qsc_restore;
static Lisp_Object Qlow_power;
static Lisp_Object Qshut_off;

DEFUN ("w32-access-windows-intrinsic-facility",
       Fw32_access_windows_intrinsic_facility,
       Sw32_access_windows_intrinsic_facility, 2, 4, 0,
       doc: /* This function is internal use only.  Don't call it directly.

When CATEGORY is WM-SYSCOMMAND, you can choose
   SC-KEYMENU,
   SC-MONITORPOWER,
   SC-TASKLIST,
   SC-MAXIMIZE,
   SC-RESTORE,
as <CATEGORY-DEPENDENT-ARG>.  */)
  (category, category_dependent_args, optional_args, frame)
     Lisp_Object category, category_dependent_args, optional_args, frame;
{
  FRAME_PTR f = check_mw32_frame (NILP (frame) ? selected_frame : frame);

  if (EQ (category, Qwm_syscommand))
    {
      WPARAM command;
      LPARAM arg = 0;

      if (EQ (category_dependent_args, Qsc_keymenu))
	command = SC_KEYMENU;
      else if (EQ (category_dependent_args, Qsc_monitorpower))
	{
	  command = SC_MONITORPOWER;

	  if (SYMBOLP (optional_args))
	    {
	      if (EQ (optional_args, Qlow_power))
		arg = 1;
	      else if (EQ (optional_args, Qshut_off))
		arg = 2;
	    }
	  if (arg == 0)
	    error ("Invalid optional argument for sc-monitorpower: %s",
		   SDATA (SYMBOL_NAME (optional_args)));
	}
      else if (EQ (category_dependent_args, Qsc_tasklist))
	command = SC_TASKLIST;
      else if (EQ (category_dependent_args, Qsc_maximize))
	command = SC_MAXIMIZE;
      else if (EQ (category_dependent_args, Qsc_restore))
	command = SC_RESTORE;
      else
	error ("Invalid command for wm-syscommand: `%s'",
	       SDATA (SYMBOL_NAME (category_dependent_args)));

      PostMessage (FRAME_MW32_WINDOW (f), WM_SYSCOMMAND, command, arg);
    }
  else
    error ("Invalid category: `%s'", SDATA (SYMBOL_NAME (category)));

  return Qnil;
}

DEFUN ("file-system-info", Ffile_system_info, Sfile_system_info, 1, 1, 0,
       doc: /* Return storage information about the file system FILENAME is on.
Value is a list of floats (TOTAL FREE AVAIL), where TOTAL is the total
storage of the file system, FREE is the free storage, and AVAIL is the
storage available to a non-superuser.  All 3 numbers are in bytes.
If the underlying system call fails, value is nil.  */)
  (filename)
  Lisp_Object filename;
{
  Lisp_Object encoded, value;

  CHECK_STRING (filename);
  filename = Fexpand_file_name (filename, Qnil);
  encoded = ENCODE_FILE (filename);

  value = Qnil;

  /* Determining the required information on Windows turns out, sadly,
     to be more involved than one would hope.  The original Win32 api
     call for this will return bogus information on some systems, but we
     must dynamically probe for the replacement api, since that was
     added rather late on.  */
  {
    HMODULE hKernel = GetModuleHandle ("kernel32");
    BOOL (*pfn_GetDiskFreeSpaceEx)
      (char *, PULARGE_INTEGER, PULARGE_INTEGER, PULARGE_INTEGER)
      = (void *) GetProcAddress (hKernel, "GetDiskFreeSpaceEx");

    /* On Windows, we may need to specify the root directory of the
       volume holding FILENAME.  */
    char rootname[MAX_PATH];
    char *name = SDATA (encoded);

    /* find the root name of the volume if given */
    if (isalpha (name[0]) && name[1] == ':')
      {
	rootname[0] = name[0];
	rootname[1] = name[1];
	rootname[2] = '\\';
	rootname[3] = 0;
      }
    else if (IS_DIRECTORY_SEP (name[0]) && IS_DIRECTORY_SEP (name[1]))
      {
	char *str = rootname;
	int slashes = 4;
	do
	  {
	    if (IS_DIRECTORY_SEP (*name) && --slashes == 0)
	      break;
	    *str++ = *name++;
	  }
	while ( *name );

	*str++ = '\\';
	*str = 0;
      }

    if (pfn_GetDiskFreeSpaceEx)
      {
	LARGE_INTEGER availbytes;
	LARGE_INTEGER freebytes;
	LARGE_INTEGER totalbytes;

	if (pfn_GetDiskFreeSpaceEx (rootname,
				    (ULARGE_INTEGER *) &availbytes,
				    (ULARGE_INTEGER *) &totalbytes,
				    (ULARGE_INTEGER *) &freebytes))
	  value = list3 (make_float ((double) totalbytes.QuadPart),
			 make_float ((double) freebytes.QuadPart),
			 make_float ((double) availbytes.QuadPart));
      }
    else
      {
	DWORD sectors_per_cluster;
	DWORD bytes_per_sector;
	DWORD free_clusters;
	DWORD total_clusters;

	if (GetDiskFreeSpace (rootname,
			      &sectors_per_cluster,
			      &bytes_per_sector,
			      &free_clusters,
			      &total_clusters))
	  value = list3 (make_float ((double) total_clusters
				     * sectors_per_cluster * bytes_per_sector),
			 make_float ((double) free_clusters
				     * sectors_per_cluster * bytes_per_sector),
			 make_float ((double) free_clusters
				     * sectors_per_cluster
				     * bytes_per_sector));
      }
  }

  return value;
}

DEFUN ("default-printer-name", Fdefault_printer_name, Sdefault_printer_name,
       0, 0, 0, doc: /* Return the name of Windows default printer device.  */)
     ()
{
  static char pname_buf[256];
  int err;
  HANDLE hPrn;
  PRINTER_INFO_2 *ppi2 = NULL;
  DWORD dwNeeded = 0, dwReturned = 0;

  /* Retrieve the default string from Win.ini (the registry).
   * String will be in form "printername,drivername,portname".
   * This is the most portable way to get the default printer. */
  if (GetProfileString ("windows", "device", ",,", pname_buf, sizeof (pname_buf)) <= 0)
    return Qnil;
  /* printername precedes first "," character */
  strtok (pname_buf, ",");
  /* We want to know more than the printer name */
  if (!OpenPrinter (pname_buf, &hPrn, NULL))
    return Qnil;
  GetPrinter (hPrn, 2, NULL, 0, &dwNeeded);
  if (dwNeeded == 0)
    {
      ClosePrinter (hPrn);
      return Qnil;
    }
  /* Allocate memory for the PRINTER_INFO_2 struct */
  ppi2 = (PRINTER_INFO_2 *) xmalloc (dwNeeded);
  if (!ppi2)
    {
      ClosePrinter (hPrn);
      return Qnil;
    }
  /* Call GetPrinter() again with big enouth memory block */
  err = GetPrinter (hPrn, 2, (LPBYTE)ppi2, dwNeeded, &dwReturned);
  ClosePrinter (hPrn);
  if (!err)
    {
      xfree(ppi2);
      return Qnil;
    }

  if (ppi2)
    {
      if (ppi2->Attributes & PRINTER_ATTRIBUTE_SHARED && ppi2->pServerName)
        {
	  /* a remote printer */
	  if (*ppi2->pServerName == '\\')
	    _snprintf(pname_buf, sizeof (pname_buf), "%s\\%s", ppi2->pServerName,
		      ppi2->pShareName);
	  else
	    _snprintf(pname_buf, sizeof (pname_buf), "\\\\%s\\%s", ppi2->pServerName,
		      ppi2->pShareName);
	  pname_buf[sizeof (pname_buf) - 1] = '\0';
	}
      else
        {
	  /* a local printer */
	  strncpy(pname_buf, ppi2->pPortName, sizeof (pname_buf));
	  pname_buf[sizeof (pname_buf) - 1] = '\0';
	  /* `pPortName' can include several ports, delimited by ','.
	   * we only use the first one. */
	  strtok(pname_buf, ",");
	}
      xfree(ppi2);
    }

  return build_string (pname_buf);
}

/***********************************************************************
			    Initialization
 ***********************************************************************/

/* Keep this list in the same order as frame_parms in frame.c.
   Use 0 for unsupported frame parameters.  */

frame_parm_handler mw32i_frame_parm_handlers[] =
{
  x_set_autoraise,
  x_set_autolower,
  mw32_set_background_color,
  mw32_set_border_color,
  x_set_border_width,
  mw32_set_cursor_color,
  mw32_set_cursor_type,
  x_set_font,
  mw32_set_foreground_color,
  0, /* x_set_icon_name, */
  0, /* x_set_icon_type, */
  x_set_internal_border_width,
  x_set_menu_bar_lines,
  mw32_set_mouse_color,
  mw32_explicitly_set_name,
  x_set_scroll_bar_width,
  mw32_set_title,
  x_set_unsplittable,
  x_set_vertical_scroll_bars,
  x_set_visibility,
  x_set_tool_bar_lines,
  mw32_set_scroll_bar_foreground,
  mw32_set_scroll_bar_background,
  x_set_screen_gamma,
  x_set_line_spacing,
  x_set_fringe_width,
  x_set_fringe_width,
  0, /* x_set_wait_for_wm, */
  x_set_fullscreen,
#ifdef IME_CONTROL
  mw32_set_frame_ime_font,
#endif
  mw32_set_cursor_height,
  mw32_set_frame_alpha
};

void
syms_of_mw32fns (void)
{
  /* This is zero if not using X windows.  */
  mw32_open = 0;

  /* The section below is built by the lisp expression at the top of the file,
     just above where these variables are declared.  */
  /*&&& init symbols here &&&*/
  Qbar = intern ("bar");
  staticpro (&Qbar);
  Qcaret = intern ("caret");
  staticpro (&Qcaret);
  Qcheckered_caret = intern ("checkered-caret");
  staticpro (&Qcheckered_caret);
  Qhairline_caret = intern ("hairline-caret");
  staticpro (&Qhairline_caret);
  Qbox = intern ("box");
  staticpro (&Qbox);
  Qcursor_height = intern ("cursor-height");
  staticpro (&Qcursor_height);
  Qalpha = intern ("alpha");
  staticpro (&Qalpha);
  Qnone = intern ("none");
  staticpro (&Qnone);
  Qsuppress_icon = intern ("suppress-icon");
  staticpro (&Qsuppress_icon);
  Qundefined_color = intern ("undefined-color");
  staticpro (&Qundefined_color);
  Qouter_window_id = intern ("outer-window-id");
  staticpro (&Qouter_window_id);
  Qime_font = intern ("ime-font");
  staticpro (&Qime_font);
  Qcenter = intern ("center");
  staticpro (&Qcenter);
  Qcompound_text = intern ("compound-text");
  staticpro (&Qcompound_text);
  Qcancel_timer = intern ("cancel-timer");
  staticpro (&Qcancel_timer);
  /* This is the end of symbol initialization.  */

  /* Text property `display' should be nonsticky by default.  */
  Vtext_property_default_nonsticky
    = Fcons (Fcons (Qdisplay, Qt), Vtext_property_default_nonsticky);

  Fput (Qundefined_color, Qerror_conditions,
	Fcons (Qundefined_color, Fcons (Qerror, Qnil)));
  Fput (Qundefined_color, Qerror_message,
	build_string ("Undefined color"));

  DEFVAR_LISP ("w32-color-map", &Vmw32_color_map,
	       doc: /* A array of color name mappings for windows.  */);
  Vmw32_color_map = Qnil;

  DEFVAR_LISP ("x-resource-name", &Vx_resource_name,
	       doc: /* The name Emacs uses to look up X resources.
`x-get-resource' uses this as the first component of the instance name
when requesting resource values.
Emacs initially sets `x-resource-name' to the name under which Emacs
was invoked, or to the value specified with the `-name' or `-rn'
switches, if present.

It may be useful to bind this variable locally around a call
to `x-get-resource'.  See also the variable `x-resource-class'.  */);
  Vx_resource_name = Qnil;

  DEFVAR_LISP ("x-resource-class", &Vx_resource_class,
	       doc: /* The class Emacs uses to look up X resources.
`x-get-resource' uses this as the first component of the instance class
when requesting resource values.
Emacs initially sets `x-resource-class' to \"Emacs\".

Setting this variable permanently is not a reasonable thing to do,
but binding this variable locally around a call to `x-get-resource'
is a reasonable practice.  See also the variable `x-resource-name'.  */);
  Vx_resource_class = build_string (EMACS_CLASS);

  DEFVAR_LISP ("mw32-hourglass-pointer-shape", &Vmw32_hourglass_pointer_shape,
	       doc: /* The shape of the pointer when Emacs is busy.
This variable takes effect when you create a new frame
or when you set the mouse color.  */);
  Vmw32_hourglass_pointer_shape = Qnil;

  DEFVAR_BOOL ("display-hourglass", &display_hourglass_p,
	       doc: /* Non-zero means Emacs displays an hourglass pointer on window systems.  */);
  display_hourglass_p = 1;

  DEFVAR_LISP ("hourglass-delay", &Vhourglass_delay,
	       doc: /* *Seconds to wait before displaying an hourglass pointer.
Value must be an integer or float.  */);
  Vhourglass_delay = make_number (DEFAULT_HOURGLASS_DELAY);

  DEFVAR_LISP ("mw32-sensitive-text-pointer-shape",
	      &Vmw32_sensitive_text_pointer_shape,
	       doc: /* The shape of the pointer when over mouse-sensitive text.
This variable takes effect when you create a new frame
or when you set the mouse color.  */);
  Vmw32_sensitive_text_pointer_shape = Qnil;

  DEFVAR_LISP ("mw32-window-horizontal-drag-cursor",
	      &Vmw32_window_horizontal_drag_shape,
	       doc: /* Pointer shape to use for indicating a window can be dragged horizontally.
This variable takes effect when you create a new frame
or when you set the mouse color.  */);
  Vmw32_window_horizontal_drag_shape = Qnil;

  DEFVAR_LISP ("mw32-cursor-fore-pixel", &Vmw32_cursor_fore_pixel,
	       doc: /* A string indicating the foreground color of the cursor box.  */);
  Vmw32_cursor_fore_pixel = Qnil;

  DEFVAR_LISP ("mw32-max-tooltip-size", &Vmw32_max_tooltip_size,
	       doc: /* Maximum size for tooltips.  Value is a pair (COLUMNS . ROWS).
Text larger than this is clipped.  */);
  Vmw32_max_tooltip_size = Fcons (make_number (80), make_number (40));

  DEFVAR_INT ("w32-ansi-code-page",
	      &w32_ansi_code_page,
	      doc: /* The ANSI code page used by the system.  */);
  w32_ansi_code_page = GetACP ();

  DEFVAR_INT ("mw32-frame-alpha-lower-limit", &mw32_frame_alpha_lower_limit,
	      doc: /* Lower limit of alpha value of frame. */);
  mw32_frame_alpha_lower_limit = 20;

  defsubr (&Sxw_display_color_p);
  defsubr (&Sx_display_grayscale_p);
  defsubr (&Sxw_color_defined_p);
  defsubr (&Sxw_color_values);

  defsubr (&Sx_display_list);

  defsubr (&Sx_create_frame);
  defsubr (&Sx_open_connection);
  defsubr (&Sx_close_connection);
  defsubr (&Sx_focus_frame);

#if 0
  defsubr (&Sx_display_pixel_width);
  defsubr (&Sx_display_pixel_height);
  defsubr (&Sx_display_planes);
#endif
  defsubr (&Sx_display_color_cells);
#if 0
  defsubr (&Sx_server_max_request_size);
  defsubr (&Sx_server_vendor);
  defsubr (&Sx_server_version);
  defsubr (&Sx_display_screens);
  defsubr (&Sx_display_mm_height);
  defsubr (&Sx_display_mm_width);
  defsubr (&Sx_display_backing_store);
  defsubr (&Sx_display_visual_class);
  defsubr (&Sx_display_save_under);
#endif

  defsubr (&Sw32_access_windows_intrinsic_facility);
  Qwm_syscommand = intern ("WM-SYSCOMMAND");
  staticpro (&Qwm_syscommand);
  Qsc_keymenu = intern ("SC-KEYMENU");
  staticpro (&Qsc_keymenu);
  Qsc_monitorpower = intern ("SC-MONITORPOWER");
  staticpro (&Qsc_monitorpower);
  Qsc_tasklist = intern ("SC-TASKLIST");
  staticpro (&Qsc_tasklist);
  Qsc_maximize = intern ("SC-MAXIMIZE");
  staticpro (&Qsc_maximize);
  Qsc_restore = intern ("SC-RESTORE");
  staticpro (&Qsc_restore);
  Qlow_power = intern ("low-power");
  staticpro (&Qlow_power);
  Qshut_off = intern ("shut-off");
  staticpro (&Qshut_off);

  defsubr (&Sfile_system_info);
  defsubr (&Sdefault_printer_name);

  hourglass_atimer = NULL;
  hourglass_shown_p = 0;

  defsubr (&Sx_show_tip);
  defsubr (&Sx_hide_tip);

  tip_timer = Qnil;
  staticpro (&tip_timer);
  tip_frame = Qnil;
  staticpro (&tip_frame);

  last_show_tip_args = Qnil;
  staticpro (&last_show_tip_args);

  defsubr (&Smw32_file_dialog);
}


void
init_mw32fns (void)
{
  /* When dumping, don't call it. */
  if (initialized)
    {
      initialze_multi_monitor_api ();
    }
}

#undef abort

void
w32_abort (void)
{
  int button;
  button = MessageBox (NULL,
		       "A fatal error has occurred!\n\n"
		       "Select Abort to exit, Retry to debug, Ignore to continue",
		       "Emacs Abort Dialog",
		       MB_ICONEXCLAMATION | MB_TASKMODAL
		       | MB_SETFOREGROUND | MB_ABORTRETRYIGNORE);
  switch (button)
    {
    case IDRETRY:
      DebugBreak ();
      break;
    case IDIGNORE:
      break;
    case IDABORT:
    default:
      abort ();
      break;
    }
#if defined (__MINGW32__)
  exit (1);
#endif /* __MINGW32__ */
}
