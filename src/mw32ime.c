/* IME specific staff for later W32 version4.
   Copyright (C) 1992, 1995 Free Software Foundation, Inc.

This file is part of Meadow.

Meadow is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

Meadow is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Meadow; see the file COPYING.  If not, write to
the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

/* 97.10.13 written by himi */

#include <windows.h>
#include "config.h"
#include "lisp.h"
#include "intervals.h"
#include <imm.h>
#ifdef IME_RECONVERSION
#include "buffer.h"
#endif

#ifdef HAVE_NTGUI
#include "frame.h"
#include "window.h"
#include "charset.h"
#include "coding.h"
#include "mw32term.h"
#endif /* not HAVE_NTGUI */

#include <dde.h>

#ifndef HAVE_NTGUI
extern HWND hwndConsole;
#endif /* not HAVE_NTGUI */
extern HINSTANCE hinst;
extern HINSTANCE hprevinst;
extern LPSTR lpCmdLine;
extern int nCmdShow;

#define CHECK_IME_FACILITY \
  if (!fIME) error ("System have no IME facility.")

#define IMMCONTEXTCAR(imc) \
  (make_number ((((unsigned long) (imc)) >> 16) & 0xffff))

#define IMMCONTEXTCDR(imc) \
  (make_number (((unsigned long) (imc)) & 0xffff))

#ifdef IME_CONTROL

#ifndef IME_SETOPEN
#define IME_SETOPEN 4
#endif
#ifndef IME_GETOPEN
#define IME_GETOPEN 5
#endif
#ifndef IME_SETCONVERSIONFONTEX
#define IME_SETCONVERSIONFONTEX 25
#endif
#ifndef IME_SETCONVERSIONWINDOW
#define IME_SETCONVERSIONWINDOW 8
#endif
#ifndef MCW_WINDOW
#define MCW_WINDOW 2
#endif

#if 0
#define IMC_SETCOMPOSITIONWINDOW	0x000C
typedef struct COMPOSITIONFORM {
  DWORD dwStyle;
  POINT ptCurrentPos;
  RECT  rcArea;
} COMPOSITIONFORM;
#define CFS_FORCE_POSITION		0x20
#define CFS_POINT			0x02
#endif

#define MAX_CONVAGENT 100

typedef struct conversion_agent {
  HIMC himc;
  HWND hwnd;
} conversion_agent;

static conversion_agent agent[MAX_CONVAGENT];
static int conversion_agent_num = -1;

BOOL fIME = FALSE;
typedef LRESULT (WINAPI *SENDIMEMESSAGEEXPROC)(HWND, LPARAM);
SENDIMEMESSAGEEXPROC SendIMEMessageExProc;

typedef BOOL (WINAPI *IMMGETOPENSTATUSPROC)(HIMC);
IMMGETOPENSTATUSPROC ImmGetOpenStatusProc;
typedef BOOL (WINAPI *IMMSETOPENSTATUSPROC)(HIMC, BOOL);
IMMSETOPENSTATUSPROC ImmSetOpenStatusProc;

typedef HWND (WINAPI *IMMGETDEFAULTIMEWNDPROC)(HWND);
IMMGETDEFAULTIMEWNDPROC ImmGetDefaultIMEWndProc;
typedef LONG (WINAPI *IMMGETCOMPOSITIONSTRINGPROC)
     (HIMC, DWORD, LPVOID, DWORD);
IMMGETCOMPOSITIONSTRINGPROC ImmGetCompositionStringProc;
typedef BOOL (WINAPI *IMMSETCOMPOSITIONSTRINGPROC)
     (HIMC, DWORD, LPCVOID, DWORD, LPCVOID, DWORD);
IMMSETCOMPOSITIONSTRINGPROC ImmSetCompositionStringProc;
typedef BOOL (WINAPI *IMMSETCOMPOSITIONFONTPROC) (HIMC, LPLOGFONTA);
IMMSETCOMPOSITIONFONTPROC ImmSetCompositionFontProc;
typedef HIMC (WINAPI *IMMGETCONTEXTPROC)(HWND);
IMMGETCONTEXTPROC ImmGetContextProc;
typedef BOOL (WINAPI *IMMGETCONVERSIONSTATUSPROC)(HIMC, LPDWORD, LPDWORD);
IMMGETCONVERSIONSTATUSPROC ImmGetConversionStatusProc;
typedef BOOL (WINAPI *IMMSETCONVERSIONSTATUSPROC)(HIMC, DWORD, DWORD);
IMMSETCONVERSIONSTATUSPROC ImmSetConversionStatusProc;
typedef DWORD (WINAPI *IMMGETCONVERSIONLISTPROC)
     (HKL, HIMC, LPCTSTR, LPCANDIDATELIST, DWORD, UINT);
IMMGETCONVERSIONLISTPROC ImmGetConversionListProc;
typedef BOOL (WINAPI *IMMCONFIGUREIMEPROC)(HKL, HWND, DWORD, LPVOID);
IMMCONFIGUREIMEPROC ImmConfigureIMEProc;
typedef BOOL (WINAPI *IMMNOTIFYIMEPROC)(HIMC, DWORD, DWORD, DWORD);
IMMNOTIFYIMEPROC ImmNotifyIMEProc;
typedef BOOL (WINAPI *IMMRELEASECONTEXTPROC)(HWND, HIMC);
IMMRELEASECONTEXTPROC ImmReleaseContextProc;
typedef HIMC (WINAPI *IMMCREATECONTEXTPROC)(void);
IMMCREATECONTEXTPROC ImmCreateContextProc;
typedef BOOL (WINAPI *IMMDESTROYCONTEXTPROC)(HIMC);
IMMDESTROYCONTEXTPROC ImmDestroyContextProc;
typedef HIMC (WINAPI *IMMASSOCIATECONTEXTPROC) (HWND, HIMC);
IMMASSOCIATECONTEXTPROC ImmAssociateContextProc;
typedef DWORD (WINAPI *IMMGETCANDIDATELISTPROC)
(HIMC, DWORD, LPCANDIDATELIST, DWORD);
IMMGETCANDIDATELISTPROC ImmGetCandidateListProc;
typedef DWORD (WINAPI *IMMGETCANDIDATELISTCOUNTPROC) (HIMC, LPDWORD);
IMMGETCANDIDATELISTCOUNTPROC ImmGetCandidateListCountProc;
typedef DWORD (WINAPI *IMMGETHOTKEYPROC)(DWORD , LPUINT, LPUINT, LPHKL);
IMMGETHOTKEYPROC ImmGetHotKeyProc;
typedef BOOL (WINAPI *IMMISIMEPROC)(HKL);
IMMISIMEPROC ImmIsIMEProc;
typedef UINT (WINAPI *IMMGETVIRTUALKEYPROC)(HWND);
IMMGETVIRTUALKEYPROC ImmGetVirtualKeyProc;

extern Lisp_Object Vime_control;

static WPARAM wIMEOpen;

static HANDLE input_method_function_event = INVALID_HANDLE_VALUE;
static int waiting_on_main_thread = FALSE;
static HANDLE input_method_function_string = INVALID_HANDLE_VALUE;
HANDLE fep_switch_event = INVALID_HANDLE_VALUE;

Lisp_Object Vime_control;
Lisp_Object VIME_command_off_flag;
Lisp_Object Qim_info;
Lisp_Object Vmw32_ime_composition_window;
int mw32_fep_switch_by_key_event;

#define CHOKEDKEYBUFLEN 16
int IME_event_off_count;
static int last_ime_vkeycode;
static int last_ime_vkeymod;
static int choke_keystroke = FALSE;
static int choked_key_index = 0;
static char choked_key_buf [CHOKEDKEYBUFLEN];

EXFUN (Fsubstring_no_properties, 3);
EXFUN (Ffep_get_mode, 0);

int
mw32_ime_get_virtual_key (HWND hwnd)
{
  return (ImmGetVirtualKeyProc)(hwnd);
}

void
mw32_ime_record_keycode (int keycode, int keymod)
{
  if (choke_keystroke)
    return;

  last_ime_vkeycode = keycode;
  last_ime_vkeymod  = keymod;
}

void
mw32_set_ime_conv_window (HWND hwnd, struct window *w)
{
  if (fIME && !NILP (Vime_control))
    {
      HWND IMEhwnd;
      COMPOSITIONFORM cf;

      /* If Vmw32_ime_composition_window is set, try it. */
      if (!NILP (Vmw32_ime_composition_window)
	  && WINDOWP (Vmw32_ime_composition_window)
	  && WINDOW_FRAME (XWINDOW (Vmw32_ime_composition_window))
	  == WINDOW_FRAME (w))
	w = XWINDOW (Vmw32_ime_composition_window);

      IMEhwnd = (ImmGetDefaultIMEWndProc) (hwnd);
      cf.dwStyle = CFS_RECT;
      cf.ptCurrentPos.x = WINDOW_TEXT_TO_FRAME_PIXEL_X (w, w->phys_cursor.x);
      cf.ptCurrentPos.y = WINDOW_TO_FRAME_PIXEL_Y (w, w->phys_cursor.y);
      cf.rcArea.left = (WINDOW_BOX_LEFT_EDGE_X (w)
			+ WINDOW_LEFT_MARGIN_WIDTH (w)
			+ WINDOW_LEFT_FRINGE_WIDTH (w));
      cf.rcArea.top = WINDOW_TOP_EDGE_Y (w) + WINDOW_HEADER_LINE_HEIGHT (w);
      cf.rcArea.right = (WINDOW_BOX_RIGHT_EDGE_X (w)
			 - WINDOW_RIGHT_MARGIN_WIDTH (w)
			 - WINDOW_RIGHT_FRINGE_WIDTH (w));
      cf.rcArea.bottom = (WINDOW_BOTTOM_EDGE_Y (w)
			  - WINDOW_MODE_LINE_HEIGHT (w));

      SendMessage (IMEhwnd, WM_IME_CONTROL, (WPARAM) IMC_SETCOMPOSITIONWINDOW,
		   (LPARAM) (&cf));
    }
  return;
}

void
mw32_ime_set_composition_string (HWND hwnd, char* str)
{
  HIMC himc;

  himc = (ImmGetContextProc) (hwnd);
  (ImmSetCompositionStringProc) (himc, SCS_SETSTR, str, strlen (str), NULL, 0);
  (ImmReleaseContextProc) (hwnd, himc);
}

void
mw32_set_ime_status (HWND hwnd, int openp)
{
  HIMC himc;

  himc = (ImmGetContextProc) (hwnd);
  (ImmSetOpenStatusProc) (himc, openp);
  (ImmReleaseContextProc) (hwnd, himc);

  return;
}

int
mw32_get_ime_status (HWND hwnd)
{
  HIMC himc;
  int ret;

  himc = (ImmGetContextProc) (hwnd);
  ret = (ImmGetOpenStatusProc) (himc);
  (ImmReleaseContextProc) (hwnd, himc);

  return ret;
}

int
mw32_set_ime_mode (HWND hwnd, int mode, int mask)
{
  HIMC himc;
  DWORD cmode, smode;

  himc = (ImmGetContextProc) (hwnd);
  if (!(ImmGetConversionStatusProc) (himc, &cmode, &smode)) return 0;

  cmode = (cmode & (~mask)) | (mode & mask);

  (ImmSetConversionStatusProc) (himc, cmode, smode);
  (ImmReleaseContextProc) (hwnd, himc);

  return 1;
}

int
mw32_get_ime_undetermined_string_length (HWND hwnd)
{
  long len;
  HIMC himc;

  himc = (ImmGetContextProc) (hwnd);
  if (!himc) return 0;
  len = (ImmGetCompositionStringProc) (himc, GCS_COMPSTR, NULL, 0);
  (ImmReleaseContextProc) (hwnd, himc);
  return len;
}

BOOL
mw32_get_ime_result_string (HWND hwnd)
{
  HIMC hIMC;
  int size;
  HANDLE himestr;
  LPSTR lpstr;
  struct frame *f;
  struct mw32_display_info *dpyinfo = GET_MW32_DISPLAY_INFO (hwnd);

  if (waiting_on_main_thread)
    choke_keystroke = TRUE;

  hIMC = (ImmGetContextProc) (hwnd);
  if (!hIMC) return FALSE;

  size = (ImmGetCompositionStringProc) (hIMC, GCS_RESULTSTR, NULL, 0);
  size += sizeof (TCHAR);

  input_method_function_string = himestr = GlobalAlloc (GHND, size);
  if (!himestr) abort ();
  lpstr = GlobalLock (himestr);
  if (!lpstr) abort ();
  (ImmGetCompositionStringProc) (hIMC, GCS_RESULTSTR, lpstr, size);
  (ImmReleaseContextProc) (hwnd, hIMC);
  lpstr [size-1] = 0;
  GlobalUnlock (himestr);

  if (waiting_on_main_thread)
    SetEvent (input_method_function_event);
  else
    {
      f = mw32_window_to_frame (dpyinfo, hwnd);
      PostMessage (NULL, WM_MULE_IME_REPORT,
		   (WPARAM) himestr, (LPARAM) f);
    }
  return TRUE;
}

BOOL
mw32_ime_get_composition_string (HWND hwnd)
{
  HIMC hIMC;
  int size;
  HANDLE himestr;
  LPSTR lpstr;
  struct frame *f;
  struct mw32_display_info *dpyinfo = GET_MW32_DISPLAY_INFO (hwnd);

  hIMC = (ImmGetContextProc) (hwnd);
  if (!hIMC) return FALSE;

  size = (ImmGetCompositionStringProc) (hIMC, GCS_COMPSTR, NULL, 0);
  size += sizeof (TCHAR);

  input_method_function_string = himestr = GlobalAlloc (GHND, size);
  if (!himestr) abort ();
  lpstr = GlobalLock (himestr);
  if (!lpstr) abort ();
  (ImmGetCompositionStringProc) (hIMC, GCS_COMPSTR, lpstr, size);
  (ImmReleaseContextProc) (hwnd, hIMC);
  lpstr [size-1] = 0;
  GlobalUnlock (himestr);

  if (waiting_on_main_thread)
    SetEvent (input_method_function_event);
  else
    {
      f = mw32_window_to_frame (dpyinfo, hwnd);
      PostMessage (NULL, WM_MULE_IME_REPORT,
		   (WPARAM) himestr, (LPARAM) f);
    }
  return TRUE;
}

#ifdef IME_RECONVERSION
/* TODO: This is dangerous because manipulating buffer contnet in
   message-thread without synchronizing. This must be fixed. */
int
mw32_get_ime_reconversion_string (HWND hwnd, WPARAM wParam,
				  RECONVERTSTRING *reconv)
{
  HIMC hIMC;
  int len, result;
  char *lpstr;
  Lisp_Object str, start, end;
  Lisp_Object Qread_only = intern ("read-only");
  struct frame *f = SELECTED_FRAME ();

  if (!NILP (current_buffer->read_only))
    return 0;  /* Cannot signal here */

  if (!NILP (current_buffer->mark_active))
    {
      if (marker_position (current_buffer->mark) < PT)
	{
	  start = Fmarker_position (current_buffer->mark);
	  end   = Fpoint ();
	}
      else
	{
	  start = Fpoint ();
	  end   = Fmarker_position (current_buffer->mark);
	}
    }
  else
    {
      int pt = PT;
      int pt_byte = PT_BYTE;

      if (NILP (Fbolp ()))
	Fforward_word (make_number (-1));
      start = Fpoint ();
      Fforward_word (make_number (1));
      end = Fpoint ();

      SET_PT_BOTH (pt, pt_byte);
    }


  /* Examine if reconvered string can be inserted here. */
  if (XINT (Fpoint_min ()) <  XINT (start))
    {
      Lisp_Object before = Fsub1 (start);

      if (!NILP (Fget_text_property (before, Qread_only, Qnil))
	  && NILP (Fget_text_property (before,
				       intern ("rear-nonsticky"), Qnil)))
	return 0;  /* Cannot signal here. */
    }
  if (XINT (Fpoint_max ()) >  XINT (end)
      && !NILP (Fget_text_property (end, Qread_only, Qnil))
      && !NILP (Fget_text_property (end, intern ("front-sticky"), Qnil)))
    return 0;  /* Cannot signal here. */

  /* Examine read-only property of reconversion target. */
  str = Fbuffer_substring (start, end);
  if (!NILP (Ftext_property_any (make_number (0), Flength (str),
				 Qread_only, Qt,
				 str)))
    return 0;  /* Cannot signal here */

  str = Fsubstring_no_properties (str, Qnil, Qnil);
  MW32_ENCODE_TEXT (str, Vlocale_coding_system, &lpstr, &len);
  result = sizeof (RECONVERTSTRING) + len + 1;

  /* If lParam of IME_RECONVERTSTRING is NULL, return required size. */
  if (reconv == NULL)
    return result;

  hIMC = (ImmGetContextProc) (hwnd);
  if (!hIMC)
    return 0;

  /* Memories are reserved in advance. */
  strcpy ((LPSTR) reconv + sizeof (RECONVERTSTRING), lpstr);
  reconv->dwStrLen = len;
  reconv->dwStrOffset = sizeof (RECONVERTSTRING);
  reconv->dwTargetStrLen = len;
  reconv->dwTargetStrOffset = 0;

  /* Reconverted area is all of selected string. */
  reconv->dwCompStrOffset = 0;
  reconv->dwCompStrLen = len;
  reconv->dwTargetStrOffset = 0;

#if 0 /* Why not need for automatic adjustment? */
  /* Automatically adjust RECONVERTSTRING if not selected. */
  if (NILP (current_buffer->mark_active))
    (ImmSetCompositionStringProc) (hIMC,
				   SCS_QUERYRECONVERTSTRING,
				   (LPCVOID) reconv,
				   reconv->dwSize,
				   NULL, 0 );
#endif

  if ((ImmSetCompositionStringProc) (hIMC,
				     SCS_SETRECONVERTSTRING,
				     (LPCVOID) reconv,
				     reconv->dwSize,
				     NULL, 0))
    {
      /* Set the position of candidate list dialog. */
      mw32_set_ime_conv_window (hwnd, XWINDOW (f->selected_window));

      /* Delete the selected area. */
      Fdelete_region (start, end);
    }
  else
    result = 0;

  (ImmReleaseContextProc) (hwnd, hIMC);
  return result;
}
#endif /* IME_RECONVERSION */

void
mw32_ime_control_init (void)
{
  HMODULE hImm32;
  HMODULE hUser32;
  hImm32 = GetModuleHandle ("IMM32.DLL");
  if (!hImm32) hImm32 = LoadLibrary ("IMM32.DLL");

  fIME = FALSE;
  Vime_control = Qnil;
  IME_event_off_count = 0;

  if (hImm32)
    {
      ImmGetOpenStatusProc =
	(IMMGETOPENSTATUSPROC)
	GetProcAddress (hImm32,
			"ImmGetOpenStatus");
      ImmSetOpenStatusProc =
	(IMMSETOPENSTATUSPROC)
	GetProcAddress (hImm32,
			"ImmSetOpenStatus");
      ImmGetDefaultIMEWndProc =
	(IMMGETDEFAULTIMEWNDPROC)
	GetProcAddress (hImm32,
			"ImmGetDefaultIMEWnd");
      ImmGetCompositionStringProc =
	(IMMGETCOMPOSITIONSTRINGPROC)
	GetProcAddress (hImm32, "ImmGetCompositionStringA");
      ImmSetCompositionStringProc =
	(IMMSETCOMPOSITIONSTRINGPROC)
	GetProcAddress (hImm32, "ImmSetCompositionStringA");
      ImmSetCompositionFontProc =
	(IMMSETCOMPOSITIONFONTPROC)
	GetProcAddress (hImm32, "ImmSetCompositionFontA");
      ImmGetContextProc =
	(IMMGETCONTEXTPROC)
	GetProcAddress (hImm32,
			"ImmGetContext");
      ImmGetConversionStatusProc =
	(IMMGETCONVERSIONSTATUSPROC)
	GetProcAddress (hImm32,
			"ImmGetConversionStatus");
      ImmSetConversionStatusProc =
	(IMMSETCONVERSIONSTATUSPROC)
	GetProcAddress (hImm32,
			"ImmSetConversionStatus");
      ImmGetConversionListProc =
	(IMMGETCONVERSIONLISTPROC)
	GetProcAddress (hImm32,
			"ImmGetConversionListA");
      ImmConfigureIMEProc =
	(IMMCONFIGUREIMEPROC)
	GetProcAddress (hImm32,
			"ImmConfigureIMEA");
      ImmNotifyIMEProc =
	(IMMNOTIFYIMEPROC)
	GetProcAddress (hImm32,
			"ImmNotifyIME");
      ImmReleaseContextProc =
	(IMMRELEASECONTEXTPROC)
	GetProcAddress (hImm32,
			"ImmReleaseContext");
      ImmCreateContextProc =
	(IMMCREATECONTEXTPROC)
	GetProcAddress (hImm32,
			"ImmCreateContext");
      ImmDestroyContextProc =
	(IMMDESTROYCONTEXTPROC)
	GetProcAddress (hImm32,
			"ImmDestroyContext");
      ImmAssociateContextProc =
	(IMMASSOCIATECONTEXTPROC)
	GetProcAddress (hImm32,
			"ImmAssociateContext");
      ImmGetCandidateListProc =
	(IMMGETCANDIDATELISTPROC)
	GetProcAddress (hImm32,
			"ImmGetCandidateListA");
      ImmGetCandidateListCountProc =
	(IMMGETCANDIDATELISTCOUNTPROC)
	GetProcAddress (hImm32,
			"ImmGetCandidateListCountA");
      ImmGetHotKeyProc =
	(IMMGETHOTKEYPROC)
	GetProcAddress (hImm32,
			"ImmGetHotKey");
      ImmIsIMEProc =
	(IMMISIMEPROC)
	GetProcAddress (hImm32,
			"ImmIsIME");
      ImmGetVirtualKeyProc =
	(IMMGETVIRTUALKEYPROC)
	GetProcAddress (hImm32,
			"ImmGetVirtualKey");
      if (ImmGetOpenStatusProc &&
	  ImmSetOpenStatusProc &&
	  ImmGetDefaultIMEWndProc &&
	  ImmGetCompositionStringProc &&
	  ImmSetCompositionStringProc &&
	  ImmSetCompositionFontProc &&
	  ImmGetContextProc &&
	  ImmGetConversionStatusProc &&
	  ImmSetConversionStatusProc &&
	  ImmGetConversionListProc &&
	  ImmConfigureIMEProc &&
	  ImmNotifyIMEProc &&
	  ImmReleaseContextProc &&
	  ImmCreateContextProc &&
	  ImmDestroyContextProc &&
	  ImmAssociateContextProc &&
	  ImmGetCandidateListProc &&
	  ImmGetCandidateListCountProc &&
	  ImmGetHotKeyProc &&
	  ImmIsIMEProc &&
	  ImmGetVirtualKeyProc)
	{
	  fIME = TRUE;
	  Vime_control = Qt;
	  input_method_function_event = CreateEvent (NULL, FALSE, FALSE, NULL);
	  fep_switch_event = CreateEvent (NULL, FALSE, FALSE, NULL);
	}
    }
}

#ifdef HAVE_NTGUI
void
mw32_set_ime_font (HWND hwnd, LPLOGFONT psetlf)
{
  HIMC himc;

  if (fIME && psetlf && !NILP (Vime_control))
    {
      himc = (ImmGetContextProc) (hwnd);
      if (!himc) return;
      (ImmSetCompositionFontProc) (himc, psetlf);
      (ImmReleaseContextProc) (hwnd, himc);
    }
}
#endif  /* HAVE_NTGUI */

/* From here, communication programs to make IME a conversion machine. */

void
check_immcontext (Lisp_Object context)
{
  if (NUMBERP (context))
    {
      if (!((XFASTINT (context) >= 0) &&
	    (XFASTINT (context) < MAX_CONVAGENT)))
	error ("Wrong number of agent");
    }
  else
    {
      CHECK_LIST (context);
      CHECK_NUMBER (XCONS (context)->car);
      CHECK_NUMBER (XCONS (context)->u.cdr);
    }
}

HIMC
immcontext (Lisp_Object context)
{
  if (NUMBERP (context))
    return agent[XFASTINT (context)].himc;
  else
    return ((HIMC)((((unsigned long) (XUINT (XCONS (context)->car))) << 16) |
		   (((unsigned long) (XUINT (XCONS (context)->u.cdr))) & 0xffff)));
}

LRESULT CALLBACK
conversion_agent_wndproc (HWND hwnd, UINT message,
			  WPARAM wparam, LPARAM lparam)
{
  HIMC himc, holdimc;

  switch (message)
    {
    case WM_CREATE:
      himc = (ImmCreateContextProc) ();
      holdimc = (ImmAssociateContextProc) (hwnd, himc);
      SetWindowLong (hwnd, 0, (LONG)himc);
      SetWindowLong (hwnd, 4, (LONG)holdimc);
      break;

    case WM_DESTROY:
      holdimc = (HIMC)GetWindowLong (hwnd, 4);
      himc = (ImmAssociateContextProc) (hwnd, holdimc);
      (ImmDestroyContextProc) (himc);
      break;

    case WM_MULE_IMM_SET_STATUS:
      mw32_set_ime_status (hwnd, (int) wparam);
      break;

    case WM_MULE_IMM_GET_STATUS:
      return mw32_get_ime_status (hwnd);

    case WM_MULE_IMM_GET_UNDETERMINED_STRING_LENGTH:
      return mw32_get_ime_undetermined_string_length (hwnd);

    case WM_MULE_IMM_SET_MODE:
      return mw32_set_ime_mode (hwnd, (int) wparam, (int) lparam);

    case WM_MULE_IMM_SET_COMPOSITION_STRING:
#if 0
      return mw32_set_ime_composition_string (hwnd,
					      (int) wparam, (int) lparam);
#endif

    case WM_MULE_IMM_GET_RESULT_STRING:
      return mw32_get_ime_result_string (hwnd);

    case WM_MULE_IMM_NOTIFY:
#if 0
      return mw32_ime_notify (hwnd, (int) wparam, (int) lparam);
#endif

    default:
      return DefWindowProc (hwnd, message, wparam, lparam);
    }
  return 0;
}

int
initialize_conversion_agent ()
{
  int i;
  WNDCLASS wc;

  for (i = 0; i < MAX_CONVAGENT; i++)
    {
      agent[i].hwnd = 0;
      agent[i].himc = 0;
    }

  wc.style = 0;
  wc.lpfnWndProc = conversion_agent_wndproc;
  wc.cbClsExtra = 0;
  wc.cbWndExtra = sizeof (long) * 2;
  wc.hInstance = hinst;
  wc.hIcon = NULL;
  wc.hCursor = NULL;
  wc.hbrBackground = NULL;
  wc.lpszMenuName = NULL;
  wc.lpszClassName = CONVAGENT_CLASS;

  if (!RegisterClass (&wc))
    return 0;

  return 1;
}

#if 0
void
generate_ime_hot_key (HWND hwnd)
{
  HKL imehkl;
  UINT modifier;
  UINT vkey;
  (ImmGetHotKeyProc) (IME_JHOTKEY_CLOSE_OPEN,
		      &modifier, &vkey, &imehkl);
#endif

Lisp_Object
get_style_lisp_object (DWORD dwStyle)
{
  switch (dwStyle)
    {
    case IME_CAND_READ:
      return intern ("read");
    case IME_CAND_CODE:
      return intern ("code");
    case IME_CAND_MEANING:
      return intern ("meaning");
    case IME_CAND_RADICAL:
      return intern ("radical");
    case IME_CAND_STROKE:
      return intern ("stroke");
    case IME_CAND_UNKNOWN:
      return intern ("unknown");
    default:
      break;
    }

  return Qnil;
}

Lisp_Object
get_attribute_lisp_object (BYTE attr)
{
  switch (attr)
    {
    case ATTR_INPUT:
      return intern ("input");
    case ATTR_TARGET_CONVERTED:
      return intern ("target-converted");
    case ATTR_CONVERTED:
      return intern ("converted");
    case ATTR_TARGET_NOTCONVERTED:
      return intern ("target-not-converted");
    case ATTR_INPUT_ERROR:
      return intern ("input-error");
    default:
      break;
    }
  return Qnil;
}

BYTE lisp_object_to_attribute_data (Lisp_Object attr)
{
  if (EQ (attr, intern ("input")))
    return ATTR_INPUT;
  else if (EQ (attr, intern ("target-converted")))
    return ATTR_TARGET_CONVERTED;
  else if (EQ (attr, intern ("converted")))
    return ATTR_CONVERTED;
  else if (EQ (attr, intern ("target-not-converted")))
    return ATTR_TARGET_NOTCONVERTED;
  else if (EQ (attr, intern ("input-error")))
    return ATTR_INPUT_ERROR;
  else
    error ("Wrong attribute");

  return 0;
}


/* Keystroke choking functions.
   Snatch keystrokes on IME switching. This cannot be perfectly done
   by other than filter driver of keyboard. Here is the second best
   way that occasionally fails to protect the order of keystrokes. */
int
mw32_ime_choke_keystroke (int keycode, int n)
{
  if (! choke_keystroke
      || keycode == 0
      || choked_key_index >= CHOKEDKEYBUFLEN)
    return FALSE;

  choked_key_buf[choked_key_index++] = keycode;
  choked_key_buf[choked_key_index] = 0;

  return TRUE;
}

int
mw32_ime_stop_choke_keystrokes ()
{
  choke_keystroke = 0;
  choked_key_index = 0;
  choked_key_buf[0] = 0;
  
  return 0;
}

/* Key strokes cannot be directly posted into IME process. */
static void
mw32_change_shift_state (int *ctrlstate, int ctrl, int *shiftstate, int shift)
{
  if (*ctrlstate && ! ctrl)
    mw32_send_key_input (VK_CONTROL, 0, KEYEVENTF_KEYUP);
  else if (! *ctrlstate && ctrl)
    mw32_send_key_input (VK_CONTROL, 0, 0);
  if (*shiftstate && ! shift)
    mw32_send_key_input (VK_SHIFT, 0, KEYEVENTF_KEYUP);
  else if (! *shiftstate && shift)
    mw32_send_key_input (VK_SHIFT, 0, 0);

  *ctrlstate = ctrl;
  *shiftstate = shift;
}


static void
mw32_ime_inject_keystrokes (char *str)
{
  char *p;
  int orgctrlstate, ctrlstate;
  int orgshiftstate, shiftstate;

  /* Cancel control key if pressed */
  orgctrlstate = ctrlstate = (GetKeyState (VK_CONTROL) & 0x80) != 0;
  orgshiftstate = shiftstate = (GetKeyState (VK_SHIFT) & 0x80) != 0;

  /* str assumed not to contain non-ascii characters. */
  for (p = str ; *p ; p++)
    {
      SHORT ret;
      int vk;
      UINT sc;
      int ctrl, shift;
      HKL kl;

      kl = GetKeyboardLayout (0);
      ret = VkKeyScanEx (*p, kl);
      if (ret == (SHORT)0xffff)
	continue;
      ctrl = ((ret & 0x200) != 0);
      shift = ((ret & 0x100) != 0);
      vk = ret & 0xff;
      sc = MapVirtualKeyEx (vk, 0, kl);

      mw32_change_shift_state (&ctrlstate, ctrl, &shiftstate, shift);

      if (GetKeyState (vk) & 0x8000)
	mw32_send_key_input (vk, sc, KEYEVENTF_KEYUP);
      mw32_send_key_input (vk, sc, 0);
    }

  mw32_change_shift_state (&ctrlstate, orgctrlstate,
			   &shiftstate, orgshiftstate);
}

/* Keystrokes can be posted directly into message thread while IME is
   deactivated. */
void
mw32_ime_inject_keymessage (char *str)
{
  char *p;

  for (p = str ; *p ; p++)
    {
      PostMessage (FRAME_MW32_WINDOW (XFRAME (selected_frame)),
		   WM_EMACS_KEYSTROKE,
		   toupper (*p),
		   1);
    }

  if (*p)
    PostMessage (FRAME_MW32_WINDOW (XFRAME (selected_frame)),
		 WM_EMACS_FLUSH_MESSAGE, 0, 0);
}

void
mw32_ime_cancel_input_function (void)
 {
   waiting_on_main_thread = FALSE;

  if (input_method_function_string != INVALID_HANDLE_VALUE)
      GlobalFree (input_method_function_string);

  input_method_function_string = INVALID_HANDLE_VALUE;

  SetEvent (input_method_function_event);
}

Lisp_Object
mw32_ime_input_method_function_unwind (arg)
     Lisp_Object arg;
{
  mw32_ime_cancel_input_function ();

  if (NILP (arg))
    Ffep_force_off (Qnil);

  if (choked_key_index > 0)
    mw32_ime_inject_keymessage (choked_key_buf);
  mw32_ime_stop_choke_keystrokes ();
  return Qnil;
}


/*
  Emacs Lisp function entries
*/


DEFUN ("mw32-ime-input-method-function",
       Fmw32_ime_input_method_function, Smw32_ime_input_method_function,
       0, 1, 0, doc: /* Low level input method function for IME.
STR is initial composition string.
Returns (RESULTSTR COMPSTR LASTCHAR) where RESULTSTR is the conversion result
string, COMPSTR is the composition string left behind in conversion, and
LASTCHAR is the keycode of the last input during conversion. */)
  (str)
{
  HWND hwnd;
  int count = SPECPDL_INDEX ();
  Lisp_Object resultstr, compstr, result;
  struct frame *f;
  struct mw32_display_info *dpyinfo;
  HANDLE hstr;

  hwnd = FRAME_MW32_WINDOW (XFRAME (selected_frame));
  dpyinfo = GET_MW32_DISPLAY_INFO (hwnd);
  f = mw32_window_to_frame (dpyinfo, hwnd);

  record_unwind_protect (mw32_ime_input_method_function_unwind,
			 Ffep_get_mode ());
  waiting_on_main_thread = TRUE;
  ResetEvent (input_method_function_event);

  choke_keystroke = TRUE;
  Ffep_force_on (Qnil);

  result = Qnil;

  /* Flush received but not emacs-queued message. */
  PostMessage (FRAME_MW32_WINDOW (XFRAME (selected_frame)),
	       WM_EMACS_FLUSH_MESSAGE, 0, 0);

  /* If new ascii (expected..) key event comes after this function
     invoked, get it and add to composition string. */
  while (kbd_buffer_events_waiting (0))
    {
      Lisp_Object kseq, args[2];

      kseq = call5 (intern ("read-key-sequence"), Qnil, Qnil, Qnil, Qnil, Qnil);
      if (STRINGP (kseq))
	{
	  args[0] = str;
	  args[1] = kseq;
	  str = Fconcat (2, args);
	}
    }
  /* redisplay won't done when waiting keyevents exist. */
  redisplay_preserve_echo_area (30);
  mw32_set_ime_conv_window (hwnd,
			    cursor_in_echo_area ?
			    XWINDOW (f->minibuffer_window):
			    XWINDOW (f->selected_window));

  if (choked_key_index > 0)
    {
      Lisp_Object args[2];
      args[0] = str;
      args[1] = make_string (choked_key_buf, choked_key_index);
      str = Fconcat (2, args);
    }
  mw32_ime_stop_choke_keystrokes ();
  if (STRINGP (str))
    mw32_ime_inject_keystrokes (SDATA (str));

  /* Rescue from dead block. This may not happen. */
  while (WaitForSingleObject (input_method_function_event, 500L)
	 == WAIT_TIMEOUT)
    QUIT;

  choke_keystroke = TRUE;
  hstr = input_method_function_string;
  input_method_function_string = INVALID_HANDLE_VALUE;
  resultstr = compstr = result = Qnil;

  if (hstr != INVALID_HANDLE_VALUE)
    {
      char* str = GlobalLock (hstr);
      if (str)
	{
	  resultstr = make_string (str, strlen (str));
	  resultstr = DECODE_SYSTEM (resultstr);
	}

      GlobalUnlock (hstr);
      GlobalFree (hstr);
    }

  waiting_on_main_thread = TRUE;
  ResetEvent (input_method_function_event);

  SendMessage (hwnd, WM_MULE_IMM_GET_COMPOSITION_STRING, 0, 0);
  while (WaitForSingleObject (input_method_function_event, 500L)
	 == WAIT_TIMEOUT)
    QUIT;

  if (hstr != INVALID_HANDLE_VALUE)
    {
      char* str = GlobalLock (hstr);
      if (str)
	{
	  compstr = make_string (str, strlen (str));
	  compstr = DECODE_SYSTEM (compstr);
	}

      GlobalUnlock (hstr);
      GlobalFree (hstr);
    }

  result = Fcons (resultstr,
		  Fcons (compstr,
			 Fcons (make_number (last_ime_vkeycode),
				Qnil)));
  return unbind_to (count, result);
}

DEFUN ("mw32-ime-available", Fmw32_ime_available, Smw32_ime_available,
       0, 0, 0,
       doc: /* Non nil if input locale has IME. */)
  ()
{
  return (fIME
	  && ImmIsIMEProc (GetKeyboardLayout (0)) ? Qt : Qnil);
}

DEFUN ("mw32-input-language-code",
       Fmw32_input_language_code, Smw32_input_language_code,
       0, 0, 0,
       doc: /* Return input language code.
This is lower 16 bit value of GetKeyboardLayout (0). */)
  ()
{
  return (make_number (((int)GetKeyboardLayout (0)) & 0xffff));
}

DEFUN ("fep-force-on", Ffep_force_on, Sfep_force_on, 0, 1, 0,
       doc: /* Force status of IME open.
If EVENPT is not nil, this function also creates [kanji] key event.
`mw32-fep-switch-by-key-event' controlls how activate IME.  */)
  (eventp)
     Lisp_Object eventp;
{
  if (fIME && !NILP (Vime_control))
    {
      HWND hwnd;

      if (!NILP (Ffep_get_mode ())) return Qnil;
#ifdef HAVE_NTGUI
      if (NILP (eventp))
	IME_event_off_count++;
      hwnd = FRAME_MW32_WINDOW (SELECTED_FRAME ());
#else
      hwnd = hwndConsole;
#endif
#ifdef HAVE_NTGUI
      if (mw32_fep_switch_by_key_event)
	{
	  ResetEvent (fep_switch_event);
	  mw32_send_key_input (VK_KANJI, 0, 0);
	  WaitForSingleObject (fep_switch_event, 500L);
	}
      else
#endif
	SendMessage (hwnd, WM_MULE_IMM_SET_STATUS, 1, 0);
    }
  return Qnil;
}


DEFUN ("fep-force-off", Ffep_force_off, Sfep_force_off, 0, 1, 0,
       doc: /* Force status of IME close.
If EVENPT is not nil, this function also creates [kanji] key event.
`mw32-fep-switch-by-key-event' controlls how deactivate IME.  */)
  (eventp)
     Lisp_Object eventp;
{
  if (fIME && !NILP (Vime_control))
    {
      HWND hwnd;

      if (NILP (Ffep_get_mode ())) return Qnil;
#ifdef HAVE_NTGUI
      if (NILP (eventp))
	IME_event_off_count++;
      hwnd = FRAME_MW32_WINDOW (SELECTED_FRAME ());
#else
      hwnd = hwndConsole;
#endif
#ifdef HAVE_NTGUI
      if (mw32_fep_switch_by_key_event)
	{
	  ResetEvent (fep_switch_event);
	  mw32_send_key_input (VK_KANJI, 0, 0);
	  WaitForSingleObject (fep_switch_event, 500L);
	}
      else
#endif
	SendMessage (hwnd, WM_MULE_IMM_SET_STATUS, 0, 0);
    }
  return Qnil;
}


DEFUN ("fep-get-mode", Ffep_get_mode, Sfep_get_mode, 0, 0, "",
       doc: /* Get IME status.
t means status of IME is open.  nil means it is close.  */)
  ()
{
  if (fIME && !NILP (Vime_control))
    {
      HWND hwnd;
      int result;

#ifdef HAVE_NTGUI
      hwnd = FRAME_MW32_WINDOW (SELECTED_FRAME ());
#else
      hwnd = hwndConsole;
#endif
      result = SendMessage (hwnd, WM_MULE_IMM_GET_STATUS, 0, 0);

      return result ? Qt : Qnil;
    }
  else
    return Qnil;
}

DEFUN ("w32-ime-undetermined-string-length",
       Fw32_ime_undetermined_string_length,
       Sw32_ime_undetermined_string_length, 0, 0, "",
       doc: /* Return length in byte of undetermined strings the current IME have.  */)
     ()
{

  if (fIME && !NILP (Vime_control))
    {
      HWND hwnd;
      long len;

#ifdef HAVE_NTGUI
      hwnd = FRAME_MW32_WINDOW (SELECTED_FRAME ());
#else
      hwnd = hwndConsole;
#endif
      len = SendMessage (hwnd, WM_MULE_IMM_GET_UNDETERMINED_STRING_LENGTH,
			 0, 0);

      return make_number (len);
    }
  else
    return Qnil;
}

  static struct {
    DWORD mask;
    char* posi_symbol;
    char* nega_symbol;
  } cmode_list[] = {
    {IME_CMODE_SYMBOL,		"symbol",	"nosymbol"},
    {IME_CMODE_SOFTKBD,		"softkbd",	"nosoftkbd"},
    {IME_CMODE_ROMAN,		"roman",	"noroman"},
    {IME_CMODE_NOCONVERSION,	"noconversion",	"conversion"},
    {IME_CMODE_NATIVE	,	"native",	"nonative"},
    {IME_CMODE_KATAKANA,	"katakana",	"nokatakana"},
    {IME_CMODE_HANJACONVERT,	"hanja",	"nohanja"},
    {IME_CMODE_FULLSHAPE,	"fullshape",	"halfshape"},
    {IME_CMODE_FIXED,		"fixed",	"nofixed"},
    {IME_CMODE_EUDC,		"eudc",		"noeudc"},
    {IME_CMODE_CHARCODE,	"charcode",	"nocharcode"},
    /* compatibility */
    {IME_CMODE_CHARCODE,	"code",		"nocode"},
    {IME_CMODE_HANJACONVERT,	"kanji",	"nokanji"},
    {IME_CMODE_KATAKANA,	"katakana",	"hiragana"},
    {0,				NULL}
  };


DEFUN ("imm-get-conversion-status",
	Fimm_get_conversion_status, Simm_get_conversion_status,
       0, 0, 0, doc: /* not documented */)
  ()
{
  DWORD dwConversion, dwSentence;
  HIMC himc;
  HWND hwnd;
  Lisp_Object ret;
  int i, used;

  if (! fIME ||  NILP (Vime_control))
    return Qnil;

  hwnd = FRAME_MW32_WINDOW (SELECTED_FRAME ());
  himc = (ImmGetContextProc) (hwnd);
  ret = Qnil;
  used = 0;

  if (ImmGetConversionStatusProc
      && ImmGetConversionStatusProc (himc, &dwConversion, &dwSentence))
    {
      for (i = 0 ; cmode_list[i].mask ; i++)
	{
	  if (! (used & cmode_list[i].mask))
	    {
	      used |= cmode_list[i].mask;

	      if (dwConversion & cmode_list[i].mask)
		ret = Fcons (intern (cmode_list[i].posi_symbol), ret);
	      else
		ret = Fcons (intern (cmode_list[i].nega_symbol), ret);
	    }
	}
    }

  (ImmReleaseContextProc) (hwnd, himc);
  return ret;
}


DEFUN ("w32-set-ime-mode",
       Fw32_set_ime_mode,
       Sw32_set_ime_mode, 1, 2, 0,
       doc: /* Set IME mode to MODE. If FRAME is omitted, the selected frame is used.  */)
     (mode, frame)
     Lisp_Object mode, frame;
{
  FRAME_PTR f;

  if (NILP (frame))
    {
      f = SELECTED_FRAME ();
    }
  else
    {
      CHECK_FRAME (frame);
      f = XFRAME (frame);
    }
  if (fIME && !NILP (Vime_control))
    {
      HWND hwnd;
      int i, done, ret;
      int newmode, mask;

      newmode = 0;
      mask = 0;
      done = 0;

      for (i = 0 ; cmode_list[i].mask ; i++)
	{
	  if (EQ (mode, intern (cmode_list[i].posi_symbol)))
	    {
	      newmode |= cmode_list[i].mask;
	      mask |= cmode_list[i].mask;
	      done = 1;
	    }
	  else if (EQ (mode, intern (cmode_list[i].nega_symbol)))
	    {
	      newmode &= ~cmode_list[i].mask;
	      mask |= cmode_list[i].mask;
	      done = 1;
	    }
	}

      if (! done)
	error ("unknown mode!!");

      hwnd = FRAME_MW32_WINDOW (f);
      ret = SendMessage (hwnd, WM_MULE_IMM_SET_MODE,
			 (WPARAM) newmode, (LPARAM) mask);

      if (!ret) return Qnil;
      return Qt;
    }
  return Qnil;
}

DEFUN ("w32-ime-register-word-dialog",
       Fw32_ime_register_word_dialog,
       Sw32_ime_register_word_dialog, 2, 2, 0,
       doc: /* Open IME regist word dialog.  */)
     (reading, word)
     Lisp_Object reading, word;
{
  HKL hkl;
  int len;
  REGISTERWORD regword;
  char *creading, *cword;

  CHECK_STRING (reading);
  CHECK_STRING (word);

  if (fIME && !NILP (Vime_control) && ImmConfigureIMEProc)
    {
      hkl = GetKeyboardLayout (0);
      MW32_ENCODE_TEXT (reading, Vlocale_coding_system, &creading, &len);
      regword.lpReading = creading;
      MW32_ENCODE_TEXT (word, Vlocale_coding_system, &cword, &len);
      regword.lpWord = cword;

      ShowCursor (TRUE);
      (ImmConfigureIMEProc) (hkl, FRAME_MW32_WINDOW (SELECTED_FRAME ()),
			     IME_CONFIG_REGISTERWORD, &regword);
      ShowCursor (FALSE);
    }
  return Qnil;
}

#if 0   /* incomplete */

DEFUN ("w32-ime-get-conversion-list",
       Fw32_ime_get_conversion_list,
       Sw32_ime_get_conversion_list, 2, 2, 0,
       doc: /* Get IME conversion list.
OBJECT is converted by IME.  Return candidates.
OPTION is as follows currently.
1....'forward
      convert normally.
2....'backward
      convert backward.

OPTION will be revised in the future.

Result have the form as follow.
(STYLE CANDIDATE1 CANDIDATE2 ...)
  */)
     (object, option)
     Lisp_Object object, option;
{
  Lisp_Object result, style;
  struct gcpro gcpro1, gcpro2;
  HKL hkl;
  HIMC himc;
  UINT flag;
  int i, len, nbytes;
  LPCANDIDATELIST lpcd;
  MEADOW_ENCODE_ALLOC_PREDEFINE;

  CHECK_STRING (object);
  if (EQ (option, intern ("forward")))
    flag = GCL_CONVERSION;
  else if (EQ (option, intern ("backward")))
    flag = GCL_REVERSECONVERSION;
  else
    error ("Unknown option %s", option);

  hkl = GetKeyboardLayout (0);
  himc = (ImmCreateContextProc) ();

  MEADOW_ENCODE_ALLOC (LISPY_STRING_BYTES (object));
  MEADOW_ENCODE (SDATA (object), LISPY_STRING_BYTES (object));
  len = MEADOW_ENCODE_PRODUCED;
  MEADOW_ENCODE_BUF[len] = '\0';

  nbytes = (ImmGetConversionListProc) (hkl, himc, MEADOW_ENCODE_BUF,
				       NULL, 0, flag);

  lpcd = (LPCANDIDATELIST) alloca (nbytes);
  if (!lpcd) return Qnil;

  (ImmGetConversionListProc) (hkl, himc, MEADOW_ENCODE_BUF,
			      lpcd, nbytes, flag);

  (ImmDestroyContextProc) (himc);

  result = Qnil;

  style = get_style_lisp_object (lpcd->dwStyle);

  GCPRO2 (style, result);

  for (i = lpcd->dwCount - 1;i >= 0;i--)
    {
      result = Fcons (build_string (((unsigned char *) lpcd) +
				    lpcd->dwOffset[i]),
		      result);
    }

  UNGCPRO;

  return result;
}

#endif

#ifdef ENABLE_IMM_CONTEXT

DEFUN ("w32-ime-create-context",
       Fw32_ime_create_context,
       Sw32_ime_create_context, 0, 0, 0,
       doc: /* Create IME context.  */)
     ()
{
  HIMC himc;

  CHECK_IME_FACILITY;

  himc = (ImmCreateContextProc) ();

  return Fcons (IMMCONTEXTCAR (himc), IMMCONTEXTCDR (himc));
}

DEFUN ("w32-ime-destroy-context",
       Fw32_ime_destroy_context,
       Sw32_ime_destroy_context, 1, 1, 0,
       doc: /* Destroy IME context.  */)
     (context)
     Lisp_Object context;
{
  HIMC himc;

  CHECK_IME_FACILITY;

  check_immcontext (context);
  himc = immcontext (context);

  (ImmDestroyContextProc) (himc);
  return Qnil;
}

DEFUN ("w32-ime-associate-context",
       Fw32_ime_associate_context,
       Sw32_ime_associate_context, 1, 2, 0,
       doc: /* Associate IME CONTEXT to FRAME.
Return an old context handle.  */)
     (context, frame)
     Lisp_Object context, frame;
{
  HWND hwnd;
  HIMC himc;

  CHECK_IME_FACILITY;

  check_immcontext (context);

  if (NILP (frame))
    hwnd = FRAME_MW32_WINDOW (selected_frame);
  else
    {
      CHECK_FRAME (frame);
      hwnd = FRAME_MW32_WINDOW (XFRAME (frame));
    }

  himc = immcontext (context);
  himc = (ImmAssociateContextProc) (hwnd, himc);

  return Fcons (IMMCONTEXTCAR (himc), IMMCONTEXTCDR (himc));
}

#endif /* ENABLE_IMM_CONTEXT */

DEFUN ("w32-ime-create-conversion-agent",
       Fw32_ime_create_conversion_agent,
       Sw32_ime_create_conversion_agent, 0, 0, 0,
       doc: /* Create conversion agent.  */)
     ()
{
  int i;
  MSG msg;
  HWND hwnd;
  HIMC himc;

  CHECK_IME_FACILITY;

  if (conversion_agent_num == -1)
    {
      if (!initialize_conversion_agent ())
	return Qnil;
      conversion_agent_num = 0;
    }
  else if (conversion_agent_num == MAX_CONVAGENT)
    return Qnil;

  conversion_agent_num++;

  for (i = 0;i < MAX_CONVAGENT;i++)
    if (!agent[i].himc) break;

  SEND_MSGTHREAD_INFORM_MESSAGE (WM_MULE_IME_CREATE_AGENT,
				 (WPARAM) 0, (LPARAM) 0);
  WAIT_REPLY_MESSAGE (&msg, WM_MULE_IME_CREATE_AGENT_REPLY);
  hwnd = (HWND) msg.wParam;
  agent[i].hwnd = hwnd;
  agent[i].himc = (HIMC)GetWindowLong (hwnd, 0);

  /*  ShowWindow (hwnd, SW_SHOW); */

  SendMessage (hwnd, WM_MULE_IMM_SET_STATUS, 1, 0);

  return make_number (i);
}

DEFUN ("w32-ime-destroy-conversion-agent",
       Fw32_ime_destroy_conversion_agent,
       Sw32_ime_destroy_conversion_agent, 1, 1, 0,
       doc: /* Destroy conversion agent.  */)
     (convagent)
     Lisp_Object convagent;
{
  int num;
  MSG msg;
  HWND hwnd;

  CHECK_IME_FACILITY;
  CHECK_NUMBER (convagent);
  num = XINT (convagent);

  if ((conversion_agent_num == 0) ||
      (num < 0) ||
      (num > MAX_CONVAGENT) ||
      (agent[num].himc == 0))
    error ("Fail to destroy agent");

  conversion_agent_num--;

  (ImmSetOpenStatusProc) (agent[num].himc, FALSE);

  SEND_INFORM_MESSAGE (agent[num].hwnd, WM_MULE_IME_DESTROY_AGENT,
		       (WPARAM) 0, (LPARAM) 0);
  WAIT_REPLY_MESSAGE (&msg, WM_MULE_IME_DESTROY_AGENT_REPLY);
  agent[num].hwnd = 0;
  agent[num].himc = 0;

  return Qnil;
}

DEFUN ("w32-ime-set-composition-string",
       Fw32_ime_set_composition_string,
       Sw32_ime_set_composition_string, 3, 4, 0,
       doc: /* Set IME composition string.
CONTEXT must be a valid context handle.
COMPOSITION must be an alist consists of clause information.
FIELD specifies the field of composition strings which must be
one of the follows.
  'clause
  'string
  'attribute
When READINGP is non-nil, reading strings would be set.  */)
       (context, composition, field, readingp)
     Lisp_Object context, composition, field, readingp;
{
  HIMC himc;
  Lisp_Object curclause, curelem, str, attr;
  unsigned char *context_string, *cs;
  BYTE *attr_array, *aa;
  DWORD *clause_array, *ca;
  int clause_num, size, strsize, str_total_size;
  /*  int start_idx, end_idx; */
  struct coding_system coding;

  clause_num = size = 0;

  CHECK_IME_FACILITY;

  check_immcontext (context);
  CHECK_LIST (composition);
  CHECK_SYMBOL (field);
  himc = immcontext (context);

  for (curclause = composition; CONSP (curclause);
       curclause = XCONS (curclause)->u.cdr)
    {
      CHECK_LIST (curclause);
      curelem = XCONS (curclause)->car;
      CHECK_LIST (curelem);
      str = XCONS (curelem)->car;
      attr = XCONS (curelem)->u.cdr;
      CHECK_STRING (str);
      CHECK_SYMBOL (attr);
      size += LISPY_STRING_BYTES (str);
      clause_num++;
    }

  if (size == 0) return Qnil;

  setup_coding_system (Fcheck_coding_system (Vlocale_coding_system),
		       &coding);
  size = encoding_buffer_size (&coding, size);

  attr_array = (BYTE *) alloca (size * sizeof (BYTE));
  context_string = (unsigned char *) alloca (size * sizeof (char));
  clause_array = (DWORD *) alloca ((clause_num + 1) * sizeof (DWORD));
  if ((!attr_array) || (!context_string) || (!clause_array))
    error ("Can't allocate memory!");

  aa = attr_array;
  cs = context_string;
  ca = clause_array;
  *ca = 0;

  str_total_size = 0;
  for (curclause = composition; CONSP (curclause);
       curclause = XCONS (curclause)->u.cdr)
    {
      curelem = XCONS (curclause)->car;
      str = XCONS (curelem)->car;
      attr = XCONS (curelem)->u.cdr;
      encode_coding (&coding, SDATA (str), cs,
		     LISPY_STRING_BYTES (str), size);
      strsize = coding.produced;
      str_total_size += strsize;
      size -= strsize;
      cs += strsize;
      memset (aa,
	      lisp_object_to_attribute_data (attr),
	      strsize);
      aa += strsize;
      *(ca + 1) = *ca + strsize;
      ca++;
    }

  if (!NILP (readingp))
    {
      if (EQ (field, intern ("string")))
	(ImmSetCompositionStringProc) (himc, SCS_SETSTR, NULL, 0,
				       context_string, str_total_size);
      else if (EQ (field, intern ("clause")))
	(ImmSetCompositionStringProc) (himc, SCS_CHANGECLAUSE, NULL, 0,
				       clause_array,
				      (clause_num + 1) * sizeof (DWORD));
      else if (EQ (field, intern ("attribute")))
	(ImmSetCompositionStringProc) (himc, SCS_CHANGEATTR, NULL, 0,
				       attr_array, str_total_size);
      else
	error ("Unknown field:%s", SDATA (SYMBOL_NAME (field)));
    }
  else
    {
      if (EQ (field, intern ("string")))
	(ImmSetCompositionStringProc) (himc, SCS_SETSTR,
				       context_string, str_total_size,
				       NULL, 0);
      else if (EQ (field, intern ("clause")))
	(ImmSetCompositionStringProc) (himc, SCS_CHANGECLAUSE,
				       clause_array,
				       (clause_num + 1) * sizeof (DWORD),
				       NULL, 0);
      else if (EQ (field, intern ("attribute")))
	(ImmSetCompositionStringProc) (himc, SCS_CHANGEATTR,
				       attr_array, str_total_size,
				       NULL, 0);
      else
	error ("Unknown field:%s", SDATA (SYMBOL_NAME (field)));
    }

  return Qnil;

}

DEFUN ("w32-ime-get-composition-string",
       Fw32_ime_get_composition_string,
       Sw32_ime_get_composition_string, 2, 2, 0,
       doc: /* Get IME composition strings.
INFO means as follows.

    'comp.........current composition information.
    'compread.....current reading composition information.
    'result.......resultant composition information.
    'resultread...resultant reading composition information.  */)

     (context, info)
     Lisp_Object context, info;
{
  HIMC himc;
  DWORD clause_index, attr_index, str_index;

  DWORD *clause_array;
  BYTE *attr_array, *aa;
  unsigned char *compstr, *cs;
  long clause_size, attr_size, compstr_size;

  int start_idx, end_idx;

  int i, clause_num, size;

  Lisp_Object str;

  Lisp_Object result = Qnil;

  CHECK_IME_FACILITY;

  check_immcontext (context);
  himc = immcontext (context);

  if (EQ (info, intern ("comp")))
    {
      clause_index = GCS_COMPCLAUSE;
      attr_index = GCS_COMPATTR;
      str_index = GCS_COMPSTR;
     }
  else if (EQ (info, intern ("compread")))
    {
      clause_index = GCS_COMPREADCLAUSE;
      attr_index = GCS_COMPREADATTR;
      str_index = GCS_COMPREADSTR;
    }
  else if (EQ (info, intern ("result")))
    {
      clause_index = GCS_RESULTCLAUSE;
      attr_index = GCS_COMPATTR;
      str_index = GCS_RESULTSTR;
    }
  else if (EQ (info, intern ("resultread")))
    {
      clause_index = GCS_RESULTREADCLAUSE;
      attr_index = GCS_COMPREADATTR;
      str_index = GCS_RESULTREADSTR;
    }
  else
    error ("Invalid option!");

  clause_size = (ImmGetCompositionStringProc) (himc, clause_index, NULL, 0);
  attr_size = (ImmGetCompositionStringProc) (himc, attr_index, NULL, 0);
  compstr_size = (ImmGetCompositionStringProc) (himc, str_index, NULL, 0);

  if ((clause_size < 0) ||
      (attr_size < 0) ||
      (compstr_size < 0))
    error ("IME internal error!");

  clause_array = (DWORD *) alloca (clause_size);
  attr_array = (BYTE *) alloca (attr_size);
  compstr = (unsigned char *) alloca (compstr_size);

  if ((!attr_array) || (!compstr) || (!clause_array))
    error ("Can't allocate memory!");

  if (((ImmGetCompositionStringProc)
       (himc, clause_index, clause_array, clause_size) < 0) ||
      ((ImmGetCompositionStringProc)
       (himc, attr_index, attr_array, attr_size) < 0) ||
      ((ImmGetCompositionStringProc)
       (himc, str_index, compstr, compstr_size) < 0))
    error ("IME internal error!");

  clause_num = clause_size / sizeof (DWORD) - 2;
  end_idx = 0;
  for (i = clause_num; i >= 0; i--)
    {
      cs = compstr + clause_array[i];
      aa = attr_array + clause_array[i];
      size = clause_array[i + 1] - clause_array[i];

      start_idx = end_idx;
      str = Fdecode_coding_string (make_string (cs, size),
				   Vlocale_coding_system,
				   Qt);
      end_idx = start_idx + LISPY_STRING_BYTES (str);
      result = concat2 (result, str);

      Fput_text_property (make_number (start_idx), make_number (end_idx),
			  Qim_info, get_attribute_lisp_object (*aa),
			  result);
    }

  return result;
}

DEFUN ("w32-ime-get-candidate-list",
       Fw32_ime_get_candidate_list,
       Sw32_ime_get_candidate_list, 2, 2, 0,
       doc: /* Get IME candidate list.  */)
     (context, index)
     Lisp_Object context, index;
{
  HIMC himc;
  DWORD counts;
  LPCANDIDATELIST lpcd;
  int size, i;
  Lisp_Object style, result = Qnil;

  CHECK_IME_FACILITY;
  check_immcontext (context);
  CHECK_NUMBER (index);
  himc = immcontext (context);

  size = (ImmGetCandidateListCountProc) (himc, &counts);
  lpcd = (LPCANDIDATELIST) alloca (size);

  if (!lpcd)
    error ("Can't allocate memory!");

  if (!(ImmGetCandidateListProc) (himc, XFASTINT (index), lpcd, size))
    error ("Can't retrieve any candidate lists.");

  for (i = lpcd->dwCount - 1; i >= 0; i--)
    {
      result =
	Fcons (Fdecode_coding_string (build_string (((unsigned char *) lpcd) +
						    lpcd->dwOffset[i]),
				      Vlocale_coding_system, Qt),
	       result);
    }

  style = get_style_lisp_object (lpcd->dwStyle);

  result = Fcons (style, result);

  return result;
}

DEFUN ("w32-ime-select-candidate",
       Fw32_ime_select_candidate,
       Sw32_ime_select_candidate, 3, 3, 0,
       doc: /* Select a candidate.
CONTEXT must be a valid context handle.
CLAUSE must be a clause index where you want
to change the current candidate.
CANDIDATE must be a candidate index.  */)
     (context, clause, candidate)
     Lisp_Object context, clause, candidate;
{
  HIMC himc;

  CHECK_IME_FACILITY;
  check_immcontext (context);
  CHECK_NUMBER (clause);
  CHECK_NUMBER (candidate);

  himc = immcontext (context);
  if (!(ImmNotifyIMEProc) (himc, NI_SELECTCANDIDATESTR,
			   XFASTINT (clause), XFASTINT (candidate)))
    error ("Fail to select candidate!");

  return Qnil;
}

DEFUN ("w32-ime-change-candidate",
       Fw32_ime_change_candidate,
       Sw32_ime_change_candidate, 2, 2, 0,
       doc: /* change a candidate list.  */)
     (context, clause)
     Lisp_Object context, clause;
{
  HIMC himc;

  CHECK_IME_FACILITY;
  check_immcontext (context);
  CHECK_NUMBER (clause);

  himc = immcontext (context);

  if (!(ImmNotifyIMEProc) (himc, NI_CHANGECANDIDATELIST,
			   XFASTINT (clause), 0));
    error ("Fail to change a candidate list!");

  return Qnil;
}

DEFUN ("w32-ime-open-candidate",
       Fw32_ime_open_candidate,
       Sw32_ime_open_candidate, 2, 2, 0,
       doc: /* Open a candidate list.  */)
     (context, clause)
     Lisp_Object context, clause;
{
  HIMC himc;

  CHECK_IME_FACILITY;
  check_immcontext (context);
  CHECK_NUMBER (clause);

  himc = immcontext (context);

  if (!(ImmNotifyIMEProc) (himc, NI_OPENCANDIDATE,
			   XFASTINT (clause), 0));
    error ("Fail to open a candidate list!");

  return Qnil;
}

DEFUN ("w32-ime-close-candidate",
       Fw32_ime_close_candidate,
       Sw32_ime_close_candidate, 2, 2, 0,
       doc: /* Open a candidate list.  */)
     (context, clause)
     Lisp_Object context, clause;
{
  HIMC himc;

  CHECK_IME_FACILITY;
  check_immcontext (context);
  CHECK_NUMBER (clause);

  himc = immcontext (context);

  if (!(ImmNotifyIMEProc) (himc, NI_CLOSECANDIDATE,
			   XFASTINT (clause), 0));
    error ("Fail to close a candidate list!");

  return Qnil;
}

DEFUN ("w32-ime-deal-with-context",
       Fw32_ime_deal_with_context,
       Sw32_ime_deal_with_context, 2, 2, 0,
       doc: /* Notify IME to deal with a context.
You can command IME to change a context.
OPERATION must be one of the followings.

  'cancel .... clear the composition strings.
  'complete .. make the composition strings result strings.
  'convert ... convert the composition strings.
  'revert .... revert the composition strings.  */)
     (context, operation)
     Lisp_Object context, operation;
{
  HIMC himc;
  DWORD op;

  CHECK_IME_FACILITY;

  check_immcontext (context);

  CHECK_SYMBOL (operation);

  himc = immcontext (context);

  if (EQ (operation, intern ("cancel")))
    op = CPS_CANCEL;
  else if (EQ (operation, intern ("complete")))
    op = CPS_COMPLETE;
  else if (EQ (operation, intern ("convert")))
    op = CPS_CONVERT;
  else if (EQ (operation, intern ("revert")))
    op = CPS_REVERT;
  else
    error ("Unknown operation:%s", SDATA (SYMBOL_NAME (operation)));

  if (!(ImmNotifyIMEProc) (himc, NI_COMPOSITIONSTR,
			   op, 0))
    error ("Fail to deal with the context.");

  return Qnil;
}

#endif /* IME_CONTROL */

syms_of_mw32ime ()
{
#ifdef IME_CONTROL

  Qim_info = intern ("im_info");
  staticpro (&Qim_info);

  DEFVAR_LISP ("ime-control", &Vime_control, doc: /* IME control flag  */);
  DEFVAR_BOOL ("mw32-fep-switch-by-key-event", &mw32_fep_switch_by_key_event,
	       doc: /* If non-nil, use windows key event instead of IMM API in `fep-force-on'
and `fep-force-off'. */);
  mw32_fep_switch_by_key_event = FALSE;
  DEFVAR_LISP ("mw32-ime-composition-window", &Vmw32_ime_composition_window,
	       doc: /* If this is a window of current frame, IME composition window appears on the
window instead of current window." */);
  Vmw32_ime_composition_window = Qnil;

  defsubr (&Simm_get_conversion_status);
  defsubr (&Smw32_ime_input_method_function);
  defsubr (&Smw32_ime_available);
  defsubr (&Smw32_input_language_code);
  defsubr (&Sfep_force_on);
  defsubr (&Sfep_force_off);
  defsubr (&Sfep_get_mode);
  defsubr (&Sw32_ime_undetermined_string_length);

  defsubr (&Sw32_set_ime_mode);

  defsubr (&Sw32_ime_register_word_dialog);

#ifdef ENABLE_IMM_CONTEXT
  defsubr (&Sw32_ime_create_context);
  defsubr (&Sw32_ime_destroy_context);
  defsubr (&Sw32_ime_associate_context);
#endif

  defsubr (&Sw32_ime_create_conversion_agent);
  defsubr (&Sw32_ime_destroy_conversion_agent);
  defsubr (&Sw32_ime_set_composition_string);
  defsubr (&Sw32_ime_get_composition_string);
  defsubr (&Sw32_ime_get_candidate_list);
  defsubr (&Sw32_ime_select_candidate);
  defsubr (&Sw32_ime_change_candidate);
  defsubr (&Sw32_ime_open_candidate);
  defsubr (&Sw32_ime_close_candidate);
  defsubr (&Sw32_ime_deal_with_context);
  /*  defsubr (&Sw32_ime_get_conversion_list); */

#endif /* IME_CONTROL */
}
