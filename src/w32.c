/* Utility and Unix shadow routines for GNU Emacs on the Microsoft W32 API.
   Copyright (C) 1994, 1995, 2000, 2001, 2002, 2003, 2004,
                 2005, 2006, 2007, 2008 Free Software Foundation, Inc.

This file is part of GNU Emacs.

GNU Emacs is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or (at your option)
any later version.

GNU Emacs is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU Emacs; see the file COPYING.  If not, write to
the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.

   Geoff Voelker (voelker@cs.washington.edu)                         7-29-94
*/
#include <stddef.h> /* for offsetof */
#include <stdlib.h>
#include <stdio.h>
#include <io.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/time.h>
#include <sys/utime.h>
#include <mbstring.h>	/* for _mbspbrk */

/* must include CRT headers *before* config.h */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#undef access
#undef chdir
#undef chmod
#undef creat
#undef ctime
#undef fopen
#undef link
#undef mkdir
#undef mktemp

#ifdef MEADOW
#undef stat
#endif

#undef open
#undef rename
#undef rmdir
#undef unlink

#undef close
#undef dup
#undef dup2
#undef pipe
#undef read
#undef write

#undef strerror

#include "lisp.h"

#include <pwd.h>
#include <grp.h>

#ifdef __GNUC__
#define _ANONYMOUS_UNION
#define _ANONYMOUS_STRUCT
#endif
#include <windows.h>
#include <shlobj.h>

#ifdef MEADOW
#include <tchar.h>
#include <winioctl.h>
#include <shellapi.h>
#include "charset.h"
#include "coding.h"
#include "mw32reg.h"
#include "mw32sync.h"

extern Lisp_Object Vfile_name_coding_system;
Lisp_Object Vmw32_symlink_program;
Lisp_Object Vmw32_symlink_hook;
char * mw32_file_truename (const char *path);

#ifndef IO_REPARSE_TAG_SYMLINK
#define IO_REPARSE_TAG_SYMLINK 0xA000000CL
#endif
#ifndef FSCTL_GET_REPARSE_POINT
#define FSCTL_GET_REPARSE_POINT     CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 42, \
        METHOD_BUFFERED, FILE_ANY_ACCESS) // , REPARSE_DATA_BUFFER
#endif

#if !defined (REPARSE_DATA_BUFFER_HEADER_SIZE)
typedef struct _REPARSE_DATA_BUFFER {
  ULONG  ReparseTag;
  USHORT  ReparseDataLength;
  USHORT  Reserved;
  union {
    struct {
      USHORT  SubstituteNameOffset;
      USHORT  SubstituteNameLength;
      USHORT  PrintNameOffset;
      USHORT  PrintNameLength;
      ULONG  Flags;
      WCHAR  PathBuffer[1];
      } SymbolicLinkReparseBuffer;
    struct {
      USHORT  SubstituteNameOffset;
      USHORT  SubstituteNameLength;
      USHORT  PrintNameOffset;
      USHORT  PrintNameLength;
      WCHAR  PathBuffer[1];
      } MountPointReparseBuffer;
    struct {
      UCHAR  DataBuffer[1];
    } GenericReparseBuffer;
  };
} REPARSE_DATA_BUFFER, *PREPARSE_DATA_BUFFER;

#define REPARSE_DATA_BUFFER_HEADER_SIZE  \
  FIELD_OFFSET(REPARSE_DATA_BUFFER, GenericReparseBuffer)

#define MAXIMUM_REPARSE_DATA_BUFFER_SIZE  ( 16 * 1024 )
#endif	/*  !defined (REPARSE_DATA_BUFFER_HEADER_SIZE) */
#endif  /* ifdef MEADOW */

#ifdef HAVE_SOCKETS	/* TCP connection support, if kernel can do it */
#include <sys/socket.h>
#undef socket
#undef bind
#undef connect
#undef htons
#undef ntohs
#undef inet_addr
#undef gethostname
#undef gethostbyname
#undef getservbyname
#undef getpeername
#undef shutdown
#undef setsockopt
#undef getsockopt
#undef listen
#undef getsockname
#undef accept
#undef recvfrom
#undef sendto
#endif

#include "w32.h"
#include "ndir.h"
#include "w32heap.h"
#include "systime.h"

#ifndef CSTR_EQUAL
#define CSTR_EQUAL 2
#endif

#ifdef MEADOW
typedef struct {
  unsigned int fd_count;
  SOCKET fd_array[FD_SETSIZE];
} pfn_fd_set;
#endif

typedef HRESULT (WINAPI * ShGetFolderPath_fn)
  (IN HWND, IN int, IN HANDLE, IN DWORD, OUT char *);

void globals_of_w32 ();

extern Lisp_Object Vw32_downcase_file_names;
extern Lisp_Object Vw32_generate_fake_inodes;
extern Lisp_Object Vw32_get_true_file_attributes;
#ifdef MEADOW
extern Lisp_Object Vw32_get_true_file_link_count;
#endif
/* Defined in process.c for its own purpose.  */
extern Lisp_Object Qlocal;

extern int w32_num_mouse_buttons;


/*
  Initialization states
 */
static BOOL g_b_init_is_windows_9x;
static BOOL g_b_init_open_process_token;
static BOOL g_b_init_get_token_information;
static BOOL g_b_init_lookup_account_sid;
static BOOL g_b_init_get_sid_identifier_authority;

/*
  BEGIN: Wrapper functions around OpenProcessToken
  and other functions in advapi32.dll that are only
  supported in Windows NT / 2k / XP
*/
  /* ** Function pointer typedefs ** */
typedef BOOL (WINAPI * OpenProcessToken_Proc) (
    HANDLE ProcessHandle,
    DWORD DesiredAccess,
    PHANDLE TokenHandle);
typedef BOOL (WINAPI * GetTokenInformation_Proc) (
    HANDLE TokenHandle,
    TOKEN_INFORMATION_CLASS TokenInformationClass,
    LPVOID TokenInformation,
    DWORD TokenInformationLength,
    PDWORD ReturnLength);
#ifdef _UNICODE
const char * const LookupAccountSid_Name = "LookupAccountSidW";
#else
const char * const LookupAccountSid_Name = "LookupAccountSidA";
#endif
typedef BOOL (WINAPI * LookupAccountSid_Proc) (
    LPCTSTR lpSystemName,
    PSID Sid,
    LPTSTR Name,
    LPDWORD cbName,
    LPTSTR DomainName,
    LPDWORD cbDomainName,
    PSID_NAME_USE peUse);
typedef PSID_IDENTIFIER_AUTHORITY (WINAPI * GetSidIdentifierAuthority_Proc) (
    PSID pSid);

  /* ** A utility function ** */
static BOOL
is_windows_9x ()
{
  static BOOL s_b_ret=0;
  OSVERSIONINFO os_ver;
  if (g_b_init_is_windows_9x == 0)
    {
      g_b_init_is_windows_9x = 1;
      ZeroMemory(&os_ver, sizeof(OSVERSIONINFO));
      os_ver.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
      if (GetVersionEx (&os_ver))
        {
          s_b_ret = (os_ver.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS);
        }
    }
  return s_b_ret;
}

  /* ** The wrapper functions ** */

BOOL WINAPI open_process_token (
    HANDLE ProcessHandle,
    DWORD DesiredAccess,
    PHANDLE TokenHandle)
{
  static OpenProcessToken_Proc s_pfn_Open_Process_Token = NULL;
  HMODULE hm_advapi32 = NULL;
  if (is_windows_9x () == TRUE)
    {
      return FALSE;
    }
  if (g_b_init_open_process_token == 0)
    {
      g_b_init_open_process_token = 1;
      hm_advapi32 = LoadLibrary ("Advapi32.dll");
      s_pfn_Open_Process_Token =
        (OpenProcessToken_Proc) GetProcAddress (hm_advapi32, "OpenProcessToken");
    }
  if (s_pfn_Open_Process_Token == NULL)
    {
      return FALSE;
    }
  return (
      s_pfn_Open_Process_Token (
          ProcessHandle,
          DesiredAccess,
          TokenHandle)
      );
}

BOOL WINAPI get_token_information (
    HANDLE TokenHandle,
    TOKEN_INFORMATION_CLASS TokenInformationClass,
    LPVOID TokenInformation,
    DWORD TokenInformationLength,
    PDWORD ReturnLength)
{
  static GetTokenInformation_Proc s_pfn_Get_Token_Information = NULL;
  HMODULE hm_advapi32 = NULL;
  if (is_windows_9x () == TRUE)
    {
      return FALSE;
    }
  if (g_b_init_get_token_information == 0)
    {
      g_b_init_get_token_information = 1;
      hm_advapi32 = LoadLibrary ("Advapi32.dll");
      s_pfn_Get_Token_Information =
        (GetTokenInformation_Proc) GetProcAddress (hm_advapi32, "GetTokenInformation");
    }
  if (s_pfn_Get_Token_Information == NULL)
    {
      return FALSE;
    }
  return (
      s_pfn_Get_Token_Information (
          TokenHandle,
          TokenInformationClass,
          TokenInformation,
          TokenInformationLength,
          ReturnLength)
      );
}

BOOL WINAPI lookup_account_sid (
    LPCTSTR lpSystemName,
    PSID Sid,
    LPTSTR Name,
    LPDWORD cbName,
    LPTSTR DomainName,
    LPDWORD cbDomainName,
    PSID_NAME_USE peUse)
{
  static LookupAccountSid_Proc s_pfn_Lookup_Account_Sid = NULL;
  HMODULE hm_advapi32 = NULL;
  if (is_windows_9x () == TRUE)
    {
      return FALSE;
    }
  if (g_b_init_lookup_account_sid == 0)
    {
      g_b_init_lookup_account_sid = 1;
      hm_advapi32 = LoadLibrary ("Advapi32.dll");
      s_pfn_Lookup_Account_Sid =
        (LookupAccountSid_Proc) GetProcAddress (hm_advapi32, LookupAccountSid_Name);
    }
  if (s_pfn_Lookup_Account_Sid == NULL)
    {
      return FALSE;
    }
  return (
      s_pfn_Lookup_Account_Sid (
          lpSystemName,
          Sid,
          Name,
          cbName,
          DomainName,
          cbDomainName,
          peUse)
      );
}

PSID_IDENTIFIER_AUTHORITY WINAPI get_sid_identifier_authority (
    PSID pSid)
{
  static GetSidIdentifierAuthority_Proc s_pfn_Get_Sid_Identifier_Authority = NULL;
  HMODULE hm_advapi32 = NULL;
  if (is_windows_9x () == TRUE)
    {
      return NULL;
    }
  if (g_b_init_get_sid_identifier_authority == 0)
    {
      g_b_init_get_sid_identifier_authority = 1;
      hm_advapi32 = LoadLibrary ("Advapi32.dll");
      s_pfn_Get_Sid_Identifier_Authority =
        (GetSidIdentifierAuthority_Proc) GetProcAddress (
            hm_advapi32, "GetSidIdentifierAuthority");
    }
  if (s_pfn_Get_Sid_Identifier_Authority == NULL)
    {
      return NULL;
    }
  return (s_pfn_Get_Sid_Identifier_Authority (pSid));
}

/* Array of W32 filename special characters */
static const int
w32_filename_illegal_characters[] = {'*', '?', '<', '>', '\"', 0};
static const int
w32_filename_special_characters[] = {'*', '?', '<', '>', '\"', '\\', '/', 0};

/*
  END: Wrapper functions around OpenProcessToken
  and other functions in advapi32.dll that are only
  supported in Windows NT / 2k / XP
*/


/* Equivalent of strerror for W32 error codes.  */
char *
w32_strerror (int error_no)
{
  static char buf[500];

  if (error_no == 0)
    error_no = GetLastError ();

  buf[0] = '\0';
  if (!FormatMessage (FORMAT_MESSAGE_FROM_SYSTEM, NULL,
		      error_no,
		      0, /* choose most suitable language */
		      buf, sizeof (buf), NULL))
    sprintf (buf, "w32 error %u", error_no);
  return buf;
}

/* Return 1 if P is a valid pointer to an object of size SIZE.  Return
   0 if P is NOT a valid pointer.  Return -1 if we cannot validate P.

   This is called from alloc.c:valid_pointer_p.  */
int
w32_valid_pointer_p (void *p, int size)
{
  SIZE_T done;
  HANDLE h = OpenProcess (PROCESS_VM_READ, FALSE, GetCurrentProcessId ());

  if (h)
    {
      unsigned char *buf = alloca (size);
      int retval = ReadProcessMemory (h, p, buf, size, &done);

      CloseHandle (h);
      return retval;
    }
  else
    return -1;
}

static char startup_dir[MAXPATHLEN];

/* Get the current working directory.  */
char *
getwd (char *dir)
{
  void dostounix_filename (char *);
#if 0
  if (GetCurrentDirectory (MAXPATHLEN, dir) > 0)
    return dir;
  return NULL;
#else
  /* Emacs doesn't actually change directory itself, and we want to
     force our real wd to be where emacs.exe is to avoid unnecessary
     conflicts when trying to rename or delete directories.  */
  strcpy (dir, startup_dir);
  dostounix_filename (dir);
  return dir;
#endif
}

#ifndef HAVE_SOCKETS
/* Emulate gethostname.  */
int
gethostname (char *buffer, int size)
{
  /* NT only allows small host names, so the buffer is
     certainly large enough.  */
  return !GetComputerName (buffer, &size);
}
#endif /* HAVE_SOCKETS */

/* Emulate getloadavg.  */
int
getloadavg (double loadavg[], int nelem)
{
  int i;

  /* A faithful emulation is going to have to be saved for a rainy day.  */
  for (i = 0; i < nelem; i++)
    {
      loadavg[i] = 0.0;
    }
  return i;
}

/* Emulate getpwuid, getpwnam and others.  */

#define PASSWD_FIELD_SIZE 256

static char the_passwd_name[PASSWD_FIELD_SIZE];
static char the_passwd_passwd[PASSWD_FIELD_SIZE];
static char the_passwd_gecos[PASSWD_FIELD_SIZE];
static char the_passwd_dir[PASSWD_FIELD_SIZE];
static char the_passwd_shell[PASSWD_FIELD_SIZE];

static struct passwd the_passwd =
{
  the_passwd_name,
  the_passwd_passwd,
  0,
  0,
  0,
  the_passwd_gecos,
  the_passwd_dir,
  the_passwd_shell,
};

static struct group the_group =
{
  /* There are no groups on NT, so we just return "root" as the
     group name.  */
  "root",
};

int
getuid ()
{
  return the_passwd.pw_uid;
}

int
geteuid ()
{
  /* I could imagine arguing for checking to see whether the user is
     in the Administrators group and returning a UID of 0 for that
     case, but I don't know how wise that would be in the long run.  */
  return getuid ();
}

int
getgid ()
{
  return the_passwd.pw_gid;
}

int
getegid ()
{
  return getgid ();
}

struct passwd *
getpwuid (int uid)
{
  if (uid == the_passwd.pw_uid)
    return &the_passwd;
  return NULL;
}

struct group *
getgrgid (gid_t gid)
{
  return &the_group;
}

struct passwd *
getpwnam (char *name)
{
  struct passwd *pw;

  pw = getpwuid (getuid ());
  if (!pw)
    return pw;

  if (stricmp (name, pw->pw_name))
    return NULL;

  return pw;
}

void
init_user_info ()
{
  /* Find the user's real name by opening the process token and
     looking up the name associated with the user-sid in that token.

     Use the relative portion of the identifier authority value from
     the user-sid as the user id value (same for group id using the
     primary group sid from the process token). */

  char         user_sid[256], name[256], domain[256];
  DWORD        length = sizeof (name), dlength = sizeof (domain), trash;
  HANDLE       token = NULL;
  SID_NAME_USE user_type;

  if (open_process_token (GetCurrentProcess (), TOKEN_QUERY, &token)
      && get_token_information (token, TokenUser,
				(PVOID) user_sid, sizeof (user_sid), &trash)
      && lookup_account_sid (NULL, *((PSID *) user_sid), name, &length,
			     domain, &dlength, &user_type))
    {
      strcpy (the_passwd.pw_name, name);
      /* Determine a reasonable uid value. */
      if (stricmp ("administrator", name) == 0)
	{
	  the_passwd.pw_uid = 0;
	  the_passwd.pw_gid = 0;
	}
      else
	{
	  SID_IDENTIFIER_AUTHORITY * pSIA;

	  pSIA = get_sid_identifier_authority (*((PSID *) user_sid));
	  /* I believe the relative portion is the last 4 bytes (of 6)
	     with msb first. */
	  the_passwd.pw_uid = ((pSIA->Value[2] << 24) +
			       (pSIA->Value[3] << 16) +
			       (pSIA->Value[4] << 8)  +
			       (pSIA->Value[5] << 0));
	  /* restrict to conventional uid range for normal users */
	  the_passwd.pw_uid = the_passwd.pw_uid % 60001;

	  /* Get group id */
	  if (get_token_information (token, TokenPrimaryGroup,
				     (PVOID) user_sid, sizeof (user_sid), &trash))
	    {
	      SID_IDENTIFIER_AUTHORITY * pSIA;

	      pSIA = get_sid_identifier_authority (*((PSID *) user_sid));
	      the_passwd.pw_gid = ((pSIA->Value[2] << 24) +
				   (pSIA->Value[3] << 16) +
				   (pSIA->Value[4] << 8)  +
				   (pSIA->Value[5] << 0));
	      /* I don't know if this is necessary, but for safety... */
	      the_passwd.pw_gid = the_passwd.pw_gid % 60001;
	    }
	  else
	    the_passwd.pw_gid = the_passwd.pw_uid;
	}
    }
  /* If security calls are not supported (presumably because we
     are running under Windows 95), fallback to this. */
  else if (GetUserName (name, &length))
    {
      strcpy (the_passwd.pw_name, name);
      if (stricmp ("administrator", name) == 0)
	the_passwd.pw_uid = 0;
      else
	the_passwd.pw_uid = 123;
      the_passwd.pw_gid = the_passwd.pw_uid;
    }
  else
    {
      strcpy (the_passwd.pw_name, "unknown");
      the_passwd.pw_uid = 123;
      the_passwd.pw_gid = 123;
    }

  /* Ensure HOME and SHELL are defined. */
  if (getenv ("HOME") == NULL)
    abort ();
  if (getenv ("SHELL") == NULL)
    abort ();

  /* Set dir and shell from environment variables. */
  strcpy (the_passwd.pw_dir, getenv ("HOME"));
  strcpy (the_passwd.pw_shell, getenv ("SHELL"));

  if (token)
    CloseHandle (token);
}

int
random ()
{
  /* rand () on NT gives us 15 random bits...hack together 30 bits.  */
  return ((rand () << 15) | rand ());
}

void
srandom (int seed)
{
  srand (seed);
}

/* Check if str contains chars.
   chars must be an array of characters (not string)
   that is terminated by 0. */
LPTSTR is_string_contains_chars (LPCTSTR str, const int *chars)
{
  LPTSTR sp = (LPTSTR) str;
  int *cp;

  for (;*sp;sp = CharNext (sp))
    {
      for (cp = (int *) chars;*cp;cp++)
	{
	  if (*sp == *cp)
	    return sp;
	}
    }

  return NULL;
}

/* Normalize filename by converting all path separators to
   the specified separator.  Also conditionally convert upper
   case path name components to lower case.  */

static void
normalize_filename (LPTSTR fp, TCHAR path_sep)
{
  LPTSTR n_fp;
  TCHAR next_char;

  if (lstrlen (fp) > MAX_PATH)
    return;

  /* Always lower-case drive letters a-z, even if the filesystem
     preserves case in filenames.
     This is so filenames can be compared by string comparison
     functions that are case-sensitive.  Even case-preserving filesystems
     do not distinguish case in drive letters.  */
  if (*(CharNext (fp)) == ':' && *fp >= 'A' && *fp <= 'Z')
    {
      next_char = tolower (*fp);
      *fp = next_char;
      fp = CharNext (fp);
      fp = CharNext (fp);
    }

  if (NILP (Vw32_downcase_file_names))
    {
      for (; *fp; fp = n_fp)
	{
	  n_fp = CharNext (fp);
	  if (IS_DIRECTORY_SEP (*fp))
	    *fp = path_sep;
	}
    }
  else
    {
      for (; *fp; fp = n_fp)
	{
	  n_fp = CharNext (fp);
	  if (IS_DIRECTORY_SEP (*fp))
	    *fp = path_sep;
	  else if (_istupper(*fp))
	    *fp = _totlower(*fp);
	}
    }

  return;
}

void
unixtodos_argument (p, ep, h2sp, qp, s2isp)
     register char *p;
     int ep, h2sp, qp, s2isp;
{
  int qf;
  char *o;

  qf = 0;
  o = p;
  while (-1)
    {
      if (*p == '\\' &&
	  ep)
	{
	  p++;
	  *o = *p;
	  if (!*p) break;
	  o++;
	  p++;
	  continue;
	}
      if (qf)
	{
	  if (*p == qf)
	    {
	      qf = 0;
	      p++;
	      continue;
	    }
	  else *o = *p;
	}
      else
	{
	  switch (*p)
	    {
	    case '/':
	      if (!s2isp)
		*o = *p;
	      else
		*o = '\\';
	      break;
	    case '-':
	      if (!h2sp)
		*o = *p;
	      else
		*o = '/';
	      break;

	    case '\'':
	    case '"':
	      if (!qp)
		*o = *p;
	      else
		{
		  qf = *p;
		  p++;
		  continue;
		}
	      break;
	    default :
	      *o = *p;
	      break;
	    }
	}
      if (!*p)
	{
	  *o = *p;
	  break;
	}
      o++;
      p++;
    }
}

static void
encode_filename (p)
     char *p;
{
  Lisp_Object fname = build_string (p);

  fname = ENCODE_FILE (fname);
  memcpy (p, SDATA (fname), SBYTES (fname) + 1);
}

static void
decode_filename (p)
     char *p;
{
  Lisp_Object fname = build_string (p);

  fname = DECODE_FILE (fname);
  memcpy (p, SDATA (fname), SBYTES (fname) + 1);
}

/* Destructively turn backslashes into slashes.  */
void
dostounix_filename (p)
     register char *p;
{
  normalize_filename (p, '/');
}

/* Destructively turn slashes into backslashes.  */
void
unixtodos_filename (p)
     register char *p;
{
  normalize_filename (p, '\\');
}

/* Remove all CR's that are followed by a LF.
   (From msdos.c...probably should figure out a way to share it,
   although this code isn't going to ever change.)  */
int
crlf_to_lf (n, buf)
     register int n;
     register unsigned char *buf;
{
  unsigned char *np = buf;
  unsigned char *startp = buf;
  unsigned char *endp = buf + n;

  if (n == 0)
    return n;
  while (buf < endp - 1)
    {
      if (*buf == 0x0d)
	{
	  if (*(++buf) != 0x0a)
	    *np++ = 0x0d;
	}
      else
	*np++ = *buf++;
    }
  if (buf < endp)
    *np++ = *buf++;
  return np - startp;
}

/* Parse the root part of file name, if present.  Return length and
    optionally store pointer to char after root.  */
static int
parse_root (LPCTSTR name, LPCTSTR *pPath)
{
  TCHAR firstchar;
  TCHAR secondchar;
  LPTSTR fp;

  LPCTSTR start = name;

  if (name == NULL)
    return 0;

  firstchar = *name;
  fp = CharNext (name);
  secondchar = *fp;
  fp = CharNext (fp);

  /* find the root name of the volume if given */
  if (IsCharAlpha (firstchar) && secondchar == ':')
    {
      if (IS_DIRECTORY_SEP (*fp))
	fp = CharNext (fp);

      name = fp;
    }
  else if (IS_DIRECTORY_SEP (firstchar) && IS_DIRECTORY_SEP (secondchar))
    {
      int slashes = 2;

      for (;*fp;fp = CharNext (fp))
        {
	  if (IS_DIRECTORY_SEP (*fp) && (slashes == 0))
	    break;
	  slashes--;
	}
      if (IS_DIRECTORY_SEP (*fp))
	fp = CharNext (fp);

      name = fp;
    }

  if (pPath)
    *pPath = name;

  return (name - start) / sizeof (TCHAR);
}

/* Get long base name for name; name is assumed to be absolute.  */
static int
get_long_basename (LPTSTR name, LPTSTR buf, int size)
{
  WIN32_FIND_DATA find_data;
  HANDLE dir_handle;
  int len = 0;

  /* must be valid filename, no wild cards or other invalid characters */
  if (is_string_contains_chars (name, w32_filename_illegal_characters))
    return 0;

  dir_handle = FindFirstFile (name, &find_data);
  if (dir_handle != INVALID_HANDLE_VALUE)
    {
      if ((len = lstrlen (find_data.cFileName)) < size)
	memcpy (buf, find_data.cFileName, sizeof (TCHAR) * (len + 1));
      else
	len = 0;
      FindClose (dir_handle);
    }
  return len;
}

/* Get long name for file, if possible (assumed to be absolute).  */
BOOL
w32_get_long_filename (LPTSTR name, LPTSTR buf, int size)
{
  char * o = buf;
  TCHAR *p;
  char *q;
  TCHAR full[MAX_PATH];
  int len;

  len = lstrlen (name);
  if (len >= MAX_PATH)
    return FALSE;

  /* Use local copy for destructive modification.  */
  memcpy (full, name, sizeof (TCHAR) * (len + 1));
  unixtodos_filename (full);

  /* Copy root part verbatim.  */
  len = parse_root (full, &p);
  memcpy (o, full, sizeof (TCHAR) * len);
  o += len;
  *o = '\0';
  size -= len;

  while (p != NULL && *p)
    {
      q = p;

      for(;; p = CharNext (p))
	{
	  if (!(*p) || IS_DIRECTORY_SEP (*p))
	    break;
	}

      if (*p) *p = '\0';
      else p = NULL;

      len = get_long_basename (full, o, size);
      if (len > 0)
	{
	  o += len;
	  size -= len;
	  if (p != NULL)
	    {
	      *p = '\\';
	      p = CharNext (p);

	      if (size < 2)
		return FALSE;

	      *o = '\\';
	      o = CharNext (o);

	      size--;
	      *o = '\0';
	    }
	}
      else
	return FALSE;
    }

  return TRUE;
}

int
is_unc_volume (LPCTSTR filename)
{
  LPCTSTR ptr = filename;
  TCHAR firstchar;
  TCHAR secondchar;

  firstchar = *ptr;
  ptr = CharNext (ptr);
  secondchar = *ptr;
  ptr = CharNext (ptr);

  if (!IS_DIRECTORY_SEP (firstchar) || !IS_DIRECTORY_SEP (secondchar) || !(*ptr))
    return 0;

  if (is_string_contains_chars (ptr, w32_filename_special_characters))
    return 0;

  return 1;
}

/* Routines that are no-ops on NT but are defined to get Emacs to compile.  */

int
sigsetmask (int signal_mask)
{
  return 0;
}

int
sigmask (int sig)
{
  return 0;
}

int
sigblock (int sig)
{
  return 0;
}

int
sigunblock (int sig)
{
  return 0;
}

int
setpgrp (int pid, int gid)
{
  return 0;
}

int
alarm (int seconds)
{
  return 0;
}

void
unrequest_sigio (void)
{
  return;
}

void
request_sigio (void)
{
  return;
}

#ifndef MEADOW

#define REG_ROOT "SOFTWARE\\GNU\\Emacs"

LPBYTE
w32_get_resource (key, lpdwtype)
    char *key;
    LPDWORD lpdwtype;
{
  LPBYTE lpvalue;
  HKEY hrootkey = NULL;
  DWORD cbData;

  /* Check both the current user and the local machine to see if
     we have any resources.  */

  if (RegOpenKeyEx (HKEY_CURRENT_USER, REG_ROOT, 0, KEY_READ, &hrootkey) == ERROR_SUCCESS)
    {
      lpvalue = NULL;

      if (RegQueryValueEx (hrootkey, key, NULL, NULL, NULL, &cbData) == ERROR_SUCCESS
	  && (lpvalue = (LPBYTE) xmalloc (cbData)) != NULL
	  && RegQueryValueEx (hrootkey, key, NULL, lpdwtype, lpvalue, &cbData) == ERROR_SUCCESS)
	{
          RegCloseKey (hrootkey);
	  return (lpvalue);
	}

      if (lpvalue) xfree (lpvalue);

      RegCloseKey (hrootkey);
    }

  if (RegOpenKeyEx (HKEY_LOCAL_MACHINE, REG_ROOT, 0, KEY_READ, &hrootkey) == ERROR_SUCCESS)
    {
      lpvalue = NULL;

      if (RegQueryValueEx (hrootkey, key, NULL, NULL, NULL, &cbData) == ERROR_SUCCESS
	  && (lpvalue = (LPBYTE) xmalloc (cbData)) != NULL
	  && RegQueryValueEx (hrootkey, key, NULL, lpdwtype, lpvalue, &cbData) == ERROR_SUCCESS)
	{
          RegCloseKey (hrootkey);
	  return (lpvalue);
	}

      if (lpvalue) xfree (lpvalue);

      RegCloseKey (hrootkey);
    }

  return (NULL);
}
#endif /* not MEADOW */

char *get_emacs_configuration (void);
extern Lisp_Object Vsystem_configuration;

void
init_environment (char ** argv)
{
  static const char * const tempdirs[] = {
    "$TMPDIR", "$TEMP", "$TMP", "c:/"
  };

  int i;

  const int imax = sizeof (tempdirs) / sizeof (tempdirs[0]);

  /* Make sure they have a usable $TMPDIR.  Many Emacs functions use
     temporary files and assume "/tmp" if $TMPDIR is unset, which
     will break on DOS/Windows.  Refuse to work if we cannot find
     a directory, not even "c:/", usable for that purpose.  */
  for (i = 0; i < imax ; i++)
    {
      const char *tmp = tempdirs[i];

      if (*tmp == '$')
	tmp = getenv (tmp + 1);
      /* Note that `access' can lie to us if the directory resides on a
	 read-only filesystem, like CD-ROM or a write-protected floppy.
	 The only way to be really sure is to actually create a file and
	 see if it succeeds.  But I think that's too much to ask.  */
#if _MSC_VER >= 1400
#define _access sys_access
#endif
      if (tmp && _access (tmp, D_OK) == 0)
	{
	  char * var = alloca (strlen (tmp) + 8);
	  sprintf (var, "TMPDIR=%s", tmp);
	  _putenv (strdup (var));
	  break;
	}
#if _MSC_VER >= 1400
#undef _access
#endif
    }
  if (i >= imax)
    cmd_error_internal
      (Fcons (Qerror,
	      Fcons (build_string ("no usable temporary directories found!!"),
		     Qnil)),
       "While setting TMPDIR: ");

  /* Check for environment variables and use registry settings if they
     don't exist.  Fallback on default values where applicable.  */
  {
    int i;
    LPBYTE lpval;
    DWORD dwType;
    char locale_name[32];
    struct stat ignored;
    char default_home[MAX_PATH];

    static const struct env_entry
    {
      char * name;
      char * def_value;
    } dflt_envvars[] =
    {
      {"HOME", "C:/"},
      {"PRELOAD_WINSOCK", NULL},
      {"emacs_dir", "C:/emacs"},
      {"EMACSLOADPATH", "%emacs_dir%/site-lisp;%emacs_dir%/../site-lisp;%emacs_dir%/lisp;%emacs_dir%/leim"},
      {"SHELL", "%emacs_dir%/bin/cmdproxy.exe"},
      {"EMACSDATA", "%emacs_dir%/etc"},
      {"EMACSPATH", "%emacs_dir%/bin"},
      /* We no longer set INFOPATH because Info-default-directory-list
	 is then ignored.  */
      /*  {"INFOPATH", "%emacs_dir%/info"},  */
#ifdef MEADOW
      {"INFOPATH", "%emacs_dir%/info"},
      {"BITMAPPATH", "%emacs_dir%/bitmap"},
#endif
      {"EMACSDOC", "%emacs_dir%/etc"},
      {"TERM", "cmd"},
      {"LANG", NULL},
    };

#define N_ENV_VARS sizeof(dflt_envvars)/sizeof(dflt_envvars[0])

    /* We need to copy dflt_envvars[] and work on the copy because we
       don't want the dumped Emacs to inherit the values of
       environment variables we saw during dumping (which could be on
       a different system).  The defaults above must be left intact.  */
    struct env_entry env_vars[N_ENV_VARS];

    for (i = 0; i < N_ENV_VARS; i++)
      env_vars[i] = dflt_envvars[i];

    /* For backwards compatibility, check if a .emacs file exists in C:/
       If not, then we can try to default to the appdata directory under the
       user's profile, which is more likely to be writable.   */
    if (stat ("C:/.emacs", &ignored) < 0)
    {
      HRESULT profile_result;
      /* Dynamically load ShGetFolderPath, as it won't exist on versions
	 of Windows 95 and NT4 that have not been updated to include
	 MSIE 5.  Also we don't link with shell32.dll by default.  */
      HMODULE shell32_dll;
      ShGetFolderPath_fn get_folder_path;
      shell32_dll = GetModuleHandle ("shell32.dll");
      get_folder_path = (ShGetFolderPath_fn)
	GetProcAddress (shell32_dll, "SHGetFolderPathA");

      if (get_folder_path != NULL)
	{
	  profile_result = get_folder_path (NULL, CSIDL_APPDATA, NULL,
					    0, default_home);

	  /* If we can't get the appdata dir, revert to old behaviour.  */
	  if (profile_result == S_OK)
	    env_vars[0].def_value = default_home;
	}

      /* Unload shell32.dll, it is not needed anymore.  */
      FreeLibrary (shell32_dll);
    }

  /* Get default locale info and use it for LANG.  */
  if (GetLocaleInfo (LOCALE_USER_DEFAULT,
                     LOCALE_SABBREVLANGNAME | LOCALE_USE_CP_ACP,
                     locale_name, sizeof (locale_name)))
    {
      for (i = 0; i < N_ENV_VARS; i++)
        {
          if (strcmp (env_vars[i].name, "LANG") == 0)
            {
              env_vars[i].def_value = locale_name;
              break;
            }
        }
    }

#define SET_ENV_BUF_SIZE (4 * MAX_PATH)	/* to cover EMACSLOADPATH */

    /* Treat emacs_dir specially: set it unconditionally based on our
       location, if it appears that we are running from the bin subdir
       of a standard installation.  */
    {
      char *p;
      char modname[MAX_PATH];

      if (!GetModuleFileName (NULL, modname, MAX_PATH))
	abort ();
      if ((p = strrchr (modname, '\\')) == NULL)
	abort ();
      *p = 0;

      if ((p = strrchr (modname, '\\')) && stricmp (p, "\\bin") == 0)
	{
	  char buf[SET_ENV_BUF_SIZE];

	  *p = 0;
	  for (p = modname; *p; p++)
	    if (*p == '\\') *p = '/';

	  _snprintf (buf, sizeof(buf)-1, "emacs_dir=%s", modname);
	  _putenv (strdup (buf));
	}
      /* Handle running emacs from the build directory: src/oo-spd/i386/  */

      /* FIXME: should use substring of get_emacs_configuration ().
	 But I don't think the Windows build supports alpha, mips etc
         anymore, so have taken the easy option for now.  */
      else if (p && stricmp (p, "\\i386") == 0)
	{
	  *p = 0;
	  p = strrchr (modname, '\\');
	  if (p != NULL)
	    {
	      *p = 0;
	      p = strrchr (modname, '\\');
	      if (p && stricmp (p, "\\src") == 0)
		{
		  char buf[SET_ENV_BUF_SIZE];

		  *p = 0;
		  for (p = modname; *p; p++)
		    if (*p == '\\') *p = '/';

		  _snprintf (buf, sizeof(buf)-1, "emacs_dir=%s", modname);
		  _putenv (strdup (buf));
		}
	    }
	}
    }

    for (i = 0; i < N_ENV_VARS; i++)
      {
	if (!getenv (env_vars[i].name))
	  {
	    int dont_free = 0;

#ifdef MEADOW
	    if ((lpval = mw32_get_env_resource (env_vars[i].name, &dwType)) == NULL
#else
	    if ((lpval = w32_get_resource (env_vars[i].name, &dwType)) == NULL
#endif
		/* Also ignore empty environment variables.  */
		|| *lpval == 0)
	      {
		if (lpval) xfree (lpval);
		lpval = env_vars[i].def_value;
		dwType = REG_EXPAND_SZ;
		dont_free = 1;
	      }

	    if (lpval)
	      {
		char buf1[SET_ENV_BUF_SIZE], buf2[SET_ENV_BUF_SIZE];

		if (dwType == REG_EXPAND_SZ)
		  ExpandEnvironmentStrings ((LPSTR) lpval, buf1, sizeof(buf1));
		else if (dwType == REG_SZ)
		  strcpy (buf1, lpval);
		if (dwType == REG_EXPAND_SZ || dwType == REG_SZ)
		  {
		    _snprintf (buf2, sizeof(buf2)-1, "%s=%s", env_vars[i].name,
			       buf1);
		    _putenv (strdup (buf2));
		  }

		if (!dont_free)
		  xfree (lpval);
	      }
	  }
      }
  }

  /* Rebuild system configuration to reflect invoking system.  */
  Vsystem_configuration = build_string (EMACS_CONFIGURATION);

  /* Another special case: on NT, the PATH variable is actually named
     "Path" although cmd.exe (perhaps NT itself) arranges for
     environment variable lookup and setting to be case insensitive.
     However, Emacs assumes a fully case sensitive environment, so we
     need to change "Path" to "PATH" to match the expectations of
     various elisp packages.  We do this by the sneaky method of
     modifying the string in the C runtime environ entry.

     The same applies to COMSPEC.  */
  {
    char ** envp;

    for (envp = environ; *envp; envp++)
      if (_strnicmp (*envp, "PATH=", 5) == 0)
	memcpy (*envp, "PATH=", 5);
      else if (_strnicmp (*envp, "COMSPEC=", 8) == 0)
	memcpy (*envp, "COMSPEC=", 8);
  }

  /* Remember the initial working directory for getwd, then make the
     real wd be the location of emacs.exe to avoid conflicts when
     renaming or deleting directories.  (We also don't call chdir when
     running subprocesses for the same reason.)  */
  if (!GetCurrentDirectory (MAXPATHLEN, startup_dir))
    abort ();

  {
    char *p, *q;
    static TCHAR modname[MAX_PATH];
    TCHAR curdir[MAX_PATH];

    if (!GetModuleFileName (NULL, modname, MAX_PATH))
      abort ();

    /* Find out the last directory separator */
    for (p = modname;*p;p = CharNext (p))
      if (IS_DIRECTORY_SEP (*p)) q = p;
    if (!q) abort ();

    memcpy (curdir, modname, (q - modname) * sizeof(TCHAR));
    curdir[q - modname] = '\0';

    SetCurrentDirectory (curdir);

    /* Ensure argv[0] has the full path to Emacs.  */
    argv[0] = modname;
  }

#ifndef MEADOW
  /* Determine if there is a middle mouse button, to allow parse_button
     to decide whether right mouse events should be mouse-2 or
     mouse-3. */
  w32_num_mouse_buttons = GetSystemMetrics (SM_CMOUSEBUTTONS);
#endif

  init_user_info ();
}

char *
emacs_root_dir (void)
{
  static char root_dir[FILENAME_MAX];
  const char *p;

  p = getenv ("emacs_dir");
  if (p == NULL)
    abort ();
  strcpy (root_dir, p);
  root_dir[parse_root (root_dir, NULL)] = '\0';
  dostounix_filename (root_dir);
  return root_dir;
}

/* We don't have scripts to automatically determine the system configuration
   for Emacs before it's compiled, and we don't want to have to make the
   user enter it, so we define EMACS_CONFIGURATION to invoke this runtime
   routine.  */

char *
get_emacs_configuration (void)
{
  char *arch, *oem, *os;
  int build_num;
  static char configuration_buffer[32];
  OSVERSIONINFO osinfo_cache;

  osinfo_cache.dwOSVersionInfoSize = sizeof (OSVERSIONINFO);
  GetVersionEx (&osinfo_cache);

  /* Determine the processor type.  */
  switch (get_processor_type ())
    {

#ifdef PROCESSOR_INTEL_386
    case PROCESSOR_INTEL_386:
    case PROCESSOR_INTEL_486:
    case PROCESSOR_INTEL_PENTIUM:
      arch = "i386";
      break;
#endif

#ifdef PROCESSOR_INTEL_860
    case PROCESSOR_INTEL_860:
      arch = "i860";
      break;
#endif

#ifdef PROCESSOR_MIPS_R2000
    case PROCESSOR_MIPS_R2000:
    case PROCESSOR_MIPS_R3000:
    case PROCESSOR_MIPS_R4000:
      arch = "mips";
      break;
#endif

#ifdef PROCESSOR_ALPHA_21064
    case PROCESSOR_ALPHA_21064:
      arch = "alpha";
      break;
#endif

    default:
      arch = "unknown";
      break;
    }

  /* Use the OEM field to reflect the compiler/library combination.  */
#ifdef _MSC_VER
#define COMPILER_NAME	"msvc"
#else
#ifdef __GNUC__
#define COMPILER_NAME	"mingw"
#else
#define COMPILER_NAME	"unknown"
#endif
#endif
  oem = COMPILER_NAME;

  switch (osinfo_cache.dwPlatformId) {
  case VER_PLATFORM_WIN32_NT:
    os = "nt";
    build_num = osinfo_cache.dwBuildNumber;
    break;
  case VER_PLATFORM_WIN32_WINDOWS:
    if (osinfo_cache.dwMinorVersion == 0) {
      os = "windows95";
    } else {
      os = "windows98";
    }
    build_num = LOWORD (osinfo_cache.dwBuildNumber);
    break;
  case VER_PLATFORM_WIN32s:
    /* Not supported, should not happen. */
    os = "windows32s";
    build_num = LOWORD (osinfo_cache.dwBuildNumber);
    break;
  default:
    os = "unknown";
    build_num = 0;
    break;
  }

  if (osinfo_cache.dwPlatformId == VER_PLATFORM_WIN32_NT) {
    sprintf (configuration_buffer, "%s-%s-%s%d.%d.%d", arch, oem, os,
	     get_w32_major_version (), get_w32_minor_version (), build_num);
  } else {
    sprintf (configuration_buffer, "%s-%s-%s.%d", arch, oem, os, build_num);
  }

  return configuration_buffer;
}

char *
get_emacs_configuration_options (void)
{
  static char options_buffer[256];

/* Work out the effective configure options for this build.  */
#ifdef _MSC_VER
#define COMPILER_VERSION	"--with-msvc (%d.%02d)", _MSC_VER / 100, _MSC_VER % 100
#else
#ifdef __GNUC__
#define COMPILER_VERSION	"--with-gcc (%d.%d)", __GNUC__, __GNUC_MINOR__
#else
#define COMPILER_VERSION	""
#endif
#endif

  sprintf (options_buffer, COMPILER_VERSION);
#ifdef EMACSDEBUG
  strcat (options_buffer, " --no-opt");
#endif
#ifdef USER_CFLAGS
  strcat (options_buffer, " --cflags");
  strcat (options_buffer, USER_CFLAGS);
#endif
#ifdef USER_LDFLAGS
  strcat (options_buffer, " --ldflags");
  strcat (options_buffer, USER_LDFLAGS);
#endif
  return options_buffer;
}


#include <sys/timeb.h>

/* Emulate gettimeofday (Ulrich Leodolter, 1/11/95).  */
void
gettimeofday (struct timeval *tv, struct timezone *tz)
{
  struct _timeb tb;
  _ftime (&tb);

  tv->tv_sec = tb.time;
  tv->tv_usec = tb.millitm * 1000L;
  if (tz)
    {
      tz->tz_minuteswest = tb.timezone;	/* minutes west of Greenwich  */
      tz->tz_dsttime = tb.dstflag;	/* type of dst correction  */
    }
}

/* ------------------------------------------------------------------------- */
/* IO support and wrapper functions for W32 API. */
/* ------------------------------------------------------------------------- */

/* Place a wrapper around the MSVC version of ctime.  It returns NULL
   on network directories, so we handle that case here.
   (Ulrich Leodolter, 1/11/95).  */
char *
sys_ctime (const time_t *t)
{
  char *str = (char *) ctime (t);
  return (str ? str : "Sun Jan 01 00:00:00 1970");
}

/* Emulate sleep...we could have done this with a define, but that
   would necessitate including windows.h in the files that used it.
   This is much easier.  */
void
sys_sleep (int seconds)
{
  Sleep (seconds * 1000);
}

/* Internal MSVC functions for low-level descriptor munging */
extern int __cdecl _set_osfhnd (int fd, long h);
extern int __cdecl _free_osfhnd (int fd);

/* parallel array of private info on file handles */
filedesc fd_info [ MAXDESC ];

typedef struct volume_info_data {
  struct volume_info_data * next;

  /* time when info was obtained */
  DWORD     timestamp;

  /* actual volume info */
  char *    root_dir;
  DWORD     serialnum;
  DWORD     maxcomp;
  DWORD     flags;
  char *    name;
  char *    type;
} volume_info_data;

/* Global referenced by various functions.  */
static volume_info_data volume_info;

/* Vector to indicate which drives are local and fixed (for which cached
   data never expires).  */
static BOOL fixed_drives[26];

/* Consider cached volume information to be stale if older than 10s,
   at least for non-local drives.  Info for fixed drives is never stale.  */
#define DRIVE_INDEX( c ) ( (c) <= 'Z' ? (c) - 'A' : (c) - 'a' )
#define VOLINFO_STILL_VALID( root_dir, info )		\
  ( ( isalpha (root_dir[0]) &&				\
      fixed_drives[ DRIVE_INDEX (root_dir[0]) ] )	\
    || GetTickCount () - info->timestamp < 10000 )

/* Cache support functions.  */

/* Simple linked list with linear search is sufficient.  */
static volume_info_data *volume_cache = NULL;

static volume_info_data *
lookup_volume_info (char * root_dir)
{
  volume_info_data * info;

  for (info = volume_cache; info; info = info->next)
    if (stricmp (info->root_dir, root_dir) == 0)
      break;
  return info;
}

static void
add_volume_info (char * root_dir, volume_info_data * info)
{
  info->root_dir = xstrdup (root_dir);
  info->next = volume_cache;
  volume_cache = info;
}


/* Wrapper for GetVolumeInformation, which uses caching to avoid
   performance penalty (~2ms on 486 for local drives, 7.5ms for local
   cdrom drive, ~5-10ms or more for remote drives on LAN).  */
volume_info_data *
GetCachedVolumeInformation (TCHAR *root_dir)
{
  volume_info_data * info;
  char default_root[ MAX_PATH ];

  /* NULL for root_dir means use root from current directory.  */
  if (root_dir == NULL)
    {
      if (GetCurrentDirectory (MAX_PATH, default_root) == 0)
	return NULL;
      parse_root (default_root, &root_dir);
      *root_dir = 0;
      root_dir = default_root;
    }

  /* Local fixed drives can be cached permanently.  Removable drives
     cannot be cached permanently, since the volume name and serial
     number (if nothing else) can change.  Remote drives should be
     treated as if they are removable, since there is no sure way to
     tell whether they are or not.  Also, the UNC association of drive
     letters mapped to remote volumes can be changed at any time (even
     by other processes) without notice.

     As a compromise, so we can benefit from caching info for remote
     volumes, we use a simple expiry mechanism to invalidate cache
     entries that are more than ten seconds old.  */

#if 0
  /* No point doing this, because WNetGetConnection is even slower than
     GetVolumeInformation, consistently taking ~50ms on a 486 (FWIW,
     GetDriveType is about the only call of this type which does not
     involve network access, and so is extremely quick).  */

  /* Map drive letter to UNC if remote. */
  if ( isalpha( root_dir[0] ) && !fixed[ DRIVE_INDEX( root_dir[0] ) ] )
    {
      char remote_name[ 256 ];
      char drive[3] = { root_dir[0], ':' };

      if (WNetGetConnection (drive, remote_name, sizeof (remote_name))
	  == NO_ERROR)
	/* do something */ ;
    }
#endif

  info = lookup_volume_info (root_dir);

  if (info == NULL || ! VOLINFO_STILL_VALID (root_dir, info))
  {
    char  name[ 256 ];
    DWORD serialnum;
    DWORD maxcomp;
    DWORD flags;
    char  type[ 256 ];

    /* Info is not cached, or is stale. */
    if (!GetVolumeInformation (root_dir,
			       name, sizeof (name),
			       &serialnum,
			       &maxcomp,
			       &flags,
			       type, sizeof (type)))
      return NULL;

    /* Cache the volume information for future use, overwriting existing
       entry if present.  */
    if (info == NULL)
      {
	info = (volume_info_data *) xmalloc (sizeof (volume_info_data));
	add_volume_info (root_dir, info);
      }
    else
      {
	xfree (info->name);
	xfree (info->type);
      }

    info->name = xstrdup (name);
    info->serialnum = serialnum;
    info->maxcomp = maxcomp;
    info->flags = flags;
    info->type = xstrdup (type);
    info->timestamp = GetTickCount ();
  }

  return info;
}

/* Get information on the volume where name is held; set path pointer to
   start of pathname in name (past UNC header\volume header if present).  */
int
get_volume_info (LPCTSTR name, LPCTSTR *pPath)
{
  TCHAR temp[MAX_PATH];
  LPTSTR rootname = NULL;  /* default to current volume */
  LPTSTR fp;
  TCHAR firstchar;
  TCHAR secondchar;
  volume_info_data * info;

  if (name == NULL)
    return FALSE;

  firstchar = *name;
  fp = CharNext (name);
  secondchar = *fp;

  /* find the root name of the volume if given */
  if (IsCharAlpha (firstchar) && secondchar == ':')
    {
      rootname = temp;
      temp[0] = firstchar;
      temp[1] = secondchar;
      temp[2] = '\\';
      temp[3] = 0;
      name = CharNext (fp);
    }
  else if (IS_DIRECTORY_SEP (firstchar) && IS_DIRECTORY_SEP (secondchar))
    {
      LPTSTR str = temp;
      LPTSTR name_end = (LPTSTR) name;
      TCHAR currentchar;
      int len;
      int slashes = 4;
      rootname = temp;

      while(*name_end)
	{
	  if (IS_DIRECTORY_SEP (*name_end) && --slashes == 0)
	    break;
	  name_end = CharNext (name_end);
	}

      len = name_end - name;
      memcpy (str, name, len);
      str += len;

      *str++ = '\\';
      *str = '\0';
      name = name_end;
    }

  if (pPath)
    *pPath = name;

  info = GetCachedVolumeInformation (rootname);
  if (info != NULL)
    {
      /* Set global referenced by other functions.  */
      volume_info = *info;
      return TRUE;
    }
  return FALSE;
}

/* Determine if volume is FAT format (ie. only supports short 8.3
   names); also set path pointer to start of pathname in name.  */
int
is_fat_volume (const char * name, const char ** pPath)
{
  if (get_volume_info (name, pPath))
    return (volume_info.maxcomp == 12);
  return FALSE;
}

/* Map filename to a valid 8.3 name if necessary. */
char *
map_w32_filename (LPCTSTR name, LPCTSTR *pPath)
{
  static TCHAR shortname[MAX_PATH];
  LPTSTR str = shortname;
  TCHAR c;
  LPTSTR path, next_name;
  LPCTSTR save_name = name;

  if (lstrlen (name) >= MAX_PATH)
    {
      /* Return a filename which will cause callers to fail.  */
      strcpy (shortname, "?");
      return shortname;
    }

  if (is_fat_volume (name, (const char **)&path)) /* truncate to 8.3 */
    {
      register int left = 8;	/* maximum number of chars in part */
      register int extn = 0;	/* extension added? */
      register int dots = 2;	/* maximum number of dots allowed */

      while (name < path)
	*str++ = *name++;	/* skip past UNC header */

      while (c = *name)
        {
	  next_name = CharNext (name);
	  switch ( c )
	    {
	    case '\\':
	    case '/':
	      *str++ = '\\';
	      extn = 0;		/* reset extension flags */
	      dots = 2;		/* max 2 dots */
	      left = 8;		/* max length 8 for main part */
	      break;
	    case ':':
	      *str++ = ':';
	      extn = 0;		/* reset extension flags */
	      dots = 2;		/* max 2 dots */
	      left = 8;		/* max length 8 for main part */
	      break;
	    case '.':
	      if ( dots )
	        {
		  /* Convert path components of the form .xxx to _xxx,
		     but leave . and .. as they are.  This allows .emacs
		     to be read as _emacs, for example.  */

		  if (! *name ||
		      *name == '.' ||
		      IS_DIRECTORY_SEP (*name))
		    {
		      *str++ = '.';
		      dots--;
		    }
		  else
		    {
		      *str++ = '_';
		      left--;
		      dots = 0;
		    }
		}
	      else if ( !extn )
	        {
		  *str++ = '.';
		  extn = 1;		/* we've got an extension */
		  left = 3;		/* 3 chars in extension */
		}
	      else
	        {
		  /* any embedded dots after the first are converted to _ */
		  *str++ = '_';
		}
	      break;
	    case '~':
	    case '#':			/* don't lose these, they're important */
	      if ( ! left )
		str[-1] = c;		/* replace last character of part */
	      /* FALLTHRU */
	    default:
	      if (left >= next_name - name)
	        {
		  TCHAR ch;

		  while (name < next_name)
		    {
		      ch = *name++;
		      if (ch >= 'A' && ch <= 'Z')
			*str++ = tolower (ch);
		      else
			*str++ = ch;
		      left--;
		    }
		  dots = 0;		/* started a path component */
		}
	      break;
	    }
	  name = next_name;
	}
      *str = '\0';
    }
  else
    {
      lstrcpy (shortname, name);
      unixtodos_filename (shortname);
    }

  if (pPath)
    *pPath = shortname + (path - save_name);

  return shortname;
}

static int
is_exec (const char * name)
{
  char * p = strrchr (name, '.');
  return
    (p != NULL
     && (stricmp (p, ".exe") == 0 ||
	 stricmp (p, ".com") == 0 ||
	 stricmp (p, ".bat") == 0 ||
	 stricmp (p, ".cmd") == 0));
}

/* Emulate the Unix directory procedures opendir, closedir,
   and readdir.  We can't use the procedures supplied in sysdep.c,
   so we provide them here.  */

struct direct dir_static;       /* simulated directory contents */
static HANDLE dir_find_handle = INVALID_HANDLE_VALUE;
static int    dir_is_fat;
static TCHAR  dir_pathname[MAXPATHLEN+1];
static WIN32_FIND_DATA dir_find_data;

/* Support shares on a network resource as subdirectories of a read-only
   root directory. */
static HANDLE wnet_enum_handle = INVALID_HANDLE_VALUE;
HANDLE open_unc_volume (const char *);
char  *read_unc_volume (HANDLE, char *, int);
void   close_unc_volume (HANDLE);

DIR *
opendir (char *filename)
{
  DIR *dirp;

  /* Opening is done by FindFirstFile.  However, a read is inherent to
     this operation, so we defer the open until read time.  */

  if (dir_find_handle != INVALID_HANDLE_VALUE)
    return NULL;
  if (wnet_enum_handle != INVALID_HANDLE_VALUE)
    return NULL;

#ifdef MEADOW
  filename = mw32_file_truename (filename);
#endif

  if (is_unc_volume (filename))
    {
      wnet_enum_handle = open_unc_volume (filename);
      if (wnet_enum_handle == INVALID_HANDLE_VALUE)
	return NULL;
    }

  if (!(dirp = (DIR *) malloc (sizeof (DIR))))
    return NULL;

  dirp->dd_fd = 0;
  dirp->dd_loc = 0;
  dirp->dd_size = 0;

  lstrcpyn (dir_pathname, map_w32_filename (filename, NULL), MAXPATHLEN);
  dir_pathname[MAXPATHLEN] = '\0';
  dir_is_fat = is_fat_volume (filename, NULL);

  return dirp;
}

void
closedir (DIR *dirp)
{
  /* If we have a find-handle open, close it.  */
  if (dir_find_handle != INVALID_HANDLE_VALUE)
    {
      FindClose (dir_find_handle);
      dir_find_handle = INVALID_HANDLE_VALUE;
    }
  else if (wnet_enum_handle != INVALID_HANDLE_VALUE)
    {
      close_unc_volume (wnet_enum_handle);
      wnet_enum_handle = INVALID_HANDLE_VALUE;
    }
  xfree ((char *) dirp);
}

struct direct *
readdir (DIR *dirp)
{
  int downcase = !NILP (Vw32_downcase_file_names);

  if (wnet_enum_handle != INVALID_HANDLE_VALUE)
    {
      if (!read_unc_volume (wnet_enum_handle,
			      dir_find_data.cFileName,
			      MAX_PATH))
	return NULL;
    }
  /* If we aren't dir_finding, do a find-first, otherwise do a find-next. */
  else if (dir_find_handle == INVALID_HANDLE_VALUE)
    {
      TCHAR filename[MAXNAMLEN + 3];
      LPTSTR lastp;
      int ln;

      lstrcpy (filename, dir_pathname);
      ln = lstrlen (filename);
      lastp = CharPrev (filename, &filename[ln]);
      if (!IS_DIRECTORY_SEP (*lastp))
	lstrcat (filename, "\\");
      lstrcat (filename, "*");

      dir_find_handle = FindFirstFile (filename, &dir_find_data);

      if (dir_find_handle == INVALID_HANDLE_VALUE)
	return NULL;
    }
  else
    {
      if (!FindNextFile (dir_find_handle, &dir_find_data))
	return NULL;
    }

  /* Emacs never uses this value, so don't bother making it match
     value returned by stat().  */
  dir_static.d_ino = 1;

  dir_static.d_reclen = sizeof (struct direct) - MAXNAMLEN + 3 +
    dir_static.d_namlen - dir_static.d_namlen % 4;

  dir_static.d_namlen = strlen (dir_find_data.cFileName);
  lstrcpy (dir_static.d_name, dir_find_data.cFileName);
  /* If the file name in cFileName[] includes `?' characters, it means
     the original file name used characters that cannot be represented
     by the current ANSI codepage.  To avoid total lossage, retrieve
     the short 8+3 alias of the long file name.  */
  if (_mbspbrk (dir_find_data.cFileName, "?"))
    {
      lstrcpy (dir_static.d_name, dir_find_data.cAlternateFileName);
      /* 8+3 aliases are returned in all caps, which could break
 	 various alists that look at filenames' extensions.  */
      downcase = 1;
    }
  else
    lstrcpy (dir_static.d_name, dir_find_data.cFileName);
  dir_static.d_namlen = strlen (dir_static.d_name);
  if (dir_is_fat)
    _tcslwr (dir_static.d_name);
  else if (downcase)
    {
      register char *p;
      for (p = dir_static.d_name; *p; p++)
	if (*p >= 'a' && *p <= 'z')
	  break;
      if (!*p)
	_tcslwr (dir_static.d_name);
    }

  return &dir_static;
}

HANDLE
open_unc_volume (const char *path)
{
  NETRESOURCE nr;
  HANDLE henum;
  int result;

  nr.dwScope = RESOURCE_GLOBALNET;
  nr.dwType = RESOURCETYPE_DISK;
  nr.dwDisplayType = RESOURCEDISPLAYTYPE_SERVER;
  nr.dwUsage = RESOURCEUSAGE_CONTAINER;
  nr.lpLocalName = NULL;
  nr.lpRemoteName = (LPSTR)map_w32_filename (path, NULL);
  nr.lpComment = NULL;
  nr.lpProvider = NULL;

  result = WNetOpenEnum(RESOURCE_GLOBALNET, RESOURCETYPE_DISK,
			RESOURCEUSAGE_CONNECTABLE, &nr, &henum);

  if (result == NO_ERROR)
    return henum;
  else
    return INVALID_HANDLE_VALUE;
}

char *
read_unc_volume (HANDLE henum, char *readbuf, int size)
{
  DWORD count;
  int result;
  DWORD bufsize = 512;
  char *buffer;
  char *ptr;

  count = 1;
  buffer = alloca (bufsize);
  result = WNetEnumResource (wnet_enum_handle, &count, buffer, &bufsize);
  if (result != NO_ERROR)
    return NULL;

  /* WNetEnumResource returns \\resource\share...skip forward to "share". */
  ptr = ((LPNETRESOURCE) buffer)->lpRemoteName;
  ptr += 2;
  while (*ptr && !IS_DIRECTORY_SEP (*ptr)) ptr = CharNext (ptr);
  ptr = CharNext (ptr);

  lstrcpyn (readbuf, ptr, size);
  return readbuf;
}

void
close_unc_volume (HANDLE henum)
{
  if (henum != INVALID_HANDLE_VALUE)
    WNetCloseEnum (henum);
}

DWORD
unc_volume_file_attributes (const char *path)
{
  HANDLE henum;
  DWORD attrs;

  henum = open_unc_volume (path);
  if (henum == INVALID_HANDLE_VALUE)
    return -1;

  attrs = FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_DIRECTORY;

  close_unc_volume (henum);

  return attrs;
}


/* Shadow some MSVC runtime functions to map requests for long filenames
   to reasonable short names if necessary.  This was originally added to
   permit running Emacs on NT 3.1 on a FAT partition, which doesn't support
   long file names.  */

int
sys_access (const char * path, int mode)
{
  DWORD attributes;

  /* MSVC implementation doesn't recognize D_OK.  */
  path = map_w32_filename (path, NULL);
  if (is_unc_volume (path))
    {
      attributes = unc_volume_file_attributes (path);
      if (attributes == -1) {
	errno = EACCES;
	return -1;
      }
    }
  else if ((attributes = GetFileAttributes (path)) == -1)
    {
      /* Should try mapping GetLastError to errno; for now just indicate
	 that path doesn't exist.  */
      errno = EACCES;
      return -1;
    }
  if ((mode & X_OK) != 0 && !is_exec (path))
    {
      errno = EACCES;
      return -1;
    }
  if ((mode & W_OK) != 0 && (attributes & FILE_ATTRIBUTE_READONLY) != 0)
    {
      errno = EACCES;
      return -1;
    }
  if ((mode & D_OK) != 0 && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
    {
      errno = EACCES;
      return -1;
    }
  return 0;
}

int
sys_chdir (const char * path)
{
  return _chdir (map_w32_filename (path, NULL));
}

int
sys_chmod (const char * path, int mode)
{
  return _chmod (map_w32_filename (path, NULL), mode);
}

int
sys_chown (const char *path, uid_t owner, gid_t group)
{
  if (sys_chmod (path, _S_IREAD) == -1) /* check if file exists */
    return -1;
  return 0;
}

int
sys_creat (const char * path, int mode)
{
  return _creat (map_w32_filename (path, NULL), mode);
}

FILE *
sys_fopen(const char * path, const char * mode)
{
  int fd;
  int oflag;
  const char * mode_save = mode;

  /* Force all file handles to be non-inheritable.  This is necessary to
     ensure child processes don't unwittingly inherit handles that might
     prevent future file access. */

  if (mode[0] == 'r')
    oflag = O_RDONLY;
  else if (mode[0] == 'w' || mode[0] == 'a')
    oflag = O_WRONLY | O_CREAT | O_TRUNC;
  else
    return NULL;

  /* Only do simplistic option parsing. */
  while (*++mode)
    if (mode[0] == '+')
      {
	oflag &= ~(O_RDONLY | O_WRONLY);
	oflag |= O_RDWR;
      }
    else if (mode[0] == 'b')
      {
	oflag &= ~O_TEXT;
	oflag |= O_BINARY;
      }
    else if (mode[0] == 't')
      {
	oflag &= ~O_BINARY;
	oflag |= O_TEXT;
      }
    else break;

#ifdef MEADOW
  fd = _open (map_w32_filename (mw32_file_truename (path), NULL),
	      oflag | _O_NOINHERIT, 0644);
#else
  fd = _open (map_w32_filename (path, NULL), oflag | _O_NOINHERIT, 0644);
#endif
  if (fd < 0)
    return NULL;

  return _fdopen (fd, mode_save);
}

/* This only works on NTFS volumes, but is useful to have.  */
int
sys_link (const char * old, const char * new)
{
  HANDLE fileh;
  int   result = -1;
  TCHAR oldname[MAX_PATH], newname[MAX_PATH];

  if (old == NULL || new == NULL)
    {
      errno = ENOENT;
      return -1;
    }

  lstrcpy (oldname, map_w32_filename (old, NULL));
  lstrcpy (newname, map_w32_filename (new, NULL));

  fileh = CreateFile (oldname, 0, 0, NULL, OPEN_EXISTING,
		      FILE_FLAG_BACKUP_SEMANTICS, NULL);
  if (fileh != INVALID_HANDLE_VALUE)
    {
      int wlen;

      /* Confusingly, the "alternate" stream name field does not apply
         when restoring a hard link, and instead contains the actual
         stream data for the link (ie. the name of the link to create).
         The WIN32_STREAM_ID structure before the cStreamName field is
         the stream header, which is then immediately followed by the
         stream data.  */

      struct {
	WIN32_STREAM_ID wid;
	WCHAR wbuffer[MAX_PATH];	/* extra space for link name */
      } data;

      wlen = MultiByteToWideChar (CP_ACP, MB_PRECOMPOSED, newname, -1,
				  data.wid.cStreamName, MAX_PATH);
      if (wlen > 0)
	{
	  LPVOID context = NULL;
	  DWORD wbytes = 0;

	  data.wid.dwStreamId = BACKUP_LINK;
	  data.wid.dwStreamAttributes = 0;
	  data.wid.Size.LowPart = wlen * sizeof(WCHAR);
	  data.wid.Size.HighPart = 0;
	  data.wid.dwStreamNameSize = 0;

	  if (BackupWrite (fileh, (LPBYTE)&data,
			   offsetof (WIN32_STREAM_ID, cStreamName)
			   + data.wid.Size.LowPart,
			   &wbytes, FALSE, FALSE, &context)
	      && BackupWrite (fileh, NULL, 0, &wbytes, TRUE, FALSE, &context))
	    {
	      /* succeeded */
	      result = 0;
	    }
	  else
	    {
	      /* Should try mapping GetLastError to errno; for now just
		 indicate a general error (eg. links not supported).  */
	      errno = EINVAL;  // perhaps EMLINK?
	    }
	}

      CloseHandle (fileh);
    }
  else
    errno = ENOENT;

  return result;
}

int
sys_mkdir (const char * path)
{
  return _mkdir (map_w32_filename (path, NULL));
}

/* Because of long name mapping issues, we need to implement this
   ourselves.  Also, MSVC's _mktemp returns NULL when it can't generate
   a unique name, instead of setting the input template to an empty
   string.

   Standard algorithm seems to be use pid or tid with a letter on the
   front (in place of the 6 X's) and cycle through the letters to find a
   unique name.  We extend that to allow any reasonable character as the
   first of the 6 X's.  */
char *
sys_mktemp (char * template)
{
  char * p;
  int i;
  unsigned uid = GetCurrentThreadId ();
  static char first_char[] = "abcdefghijklmnopqrstuvwyz0123456789!%-_@#";

  if (template == NULL)
    return NULL;
  p = template + strlen (template);
  i = 5;
  /* replace up to the last 5 X's with uid in decimal */
  while (--p >= template && p[0] == 'X' && --i >= 0)
    {
      p[0] = '0' + uid % 10;
      uid /= 10;
    }

  if (i < 0 && p[0] == 'X')
    {
      i = 0;
      do
	{
	  int save_errno = errno;
	  p[0] = first_char[i];
	  if (sys_access (template, 0) < 0)
	    {
	      errno = save_errno;
	      return template;
	    }
	}
      while (++i < sizeof (first_char));
    }

  /* Template is badly formed or else we can't generate a unique name,
     so return empty string */
  template[0] = 0;
  return template;
}

int
sys_open (const char * path, int oflag, int mode)
{
#ifdef MEADOW
  const char* mpath = map_w32_filename (mw32_file_truename (path), NULL);
#else
  const char* mpath = map_w32_filename (path, NULL);
#endif
  /* Try to open file without _O_CREAT, to be able to write to hidden
     and system files. Force all file handles to be
     non-inheritable. */
  int res = _open (mpath, (oflag & ~_O_CREAT) | _O_NOINHERIT, mode);
  if (res >= 0)
    return res;
  return _open (mpath, oflag | _O_NOINHERIT, mode);
}

int
sys_rename (const char * oldname, const char * newname)
{
  BOOL result;
  TCHAR temp[MAX_PATH];

  /* MoveFile on Windows 95 doesn't correctly change the short file name
     alias in a number of circumstances (it is not easy to predict when
     just by looking at oldname and newname, unfortunately).  In these
     cases, renaming through a temporary name avoids the problem.

     A second problem on Windows 95 is that renaming through a temp name when
     newname is uppercase fails (the final long name ends up in
     lowercase, although the short alias might be uppercase) UNLESS the
     long temp name is not 8.3.

     So, on Windows 95 we always rename through a temp name, and we make sure
     the temp name has a long extension to ensure correct renaming.  */

  lstrcpy (temp, map_w32_filename (oldname, NULL));

  if (os_subtype == OS_WIN95)
    {
      char * o;
      char * p;
      int    i = 0;

      oldname = map_w32_filename (oldname, NULL);
      if (o = strrchr (oldname, '\\'))
	o++;
      else
	o = (char *) oldname;

      if (p = strrchr (temp, '\\'))
	p++;
      else
	p = temp;

      do
	{
	  /* Force temp name to require a manufactured 8.3 alias - this
	     seems to make the second rename work properly.  */
	  sprintf (p, "_.%s.%u", o, i);
	  i++;
	  result = rename (oldname, temp);
	}
      /* This loop must surely terminate!  */
      while (result < 0 && errno == EEXIST);
      if (result < 0)
	return -1;
    }

  /* Emulate Unix behaviour - newname is deleted if it already exists
     (at least if it is a file; don't do this for directories).

     Since we mustn't do this if we are just changing the case of the
     file name (we would end up deleting the file we are trying to
     rename!), we let rename detect if the destination file already
     exists - that way we avoid the possible pitfalls of trying to
     determine ourselves whether two names really refer to the same
     file, which is not always possible in the general case.  (Consider
     all the permutations of shared or subst'd drives, etc.)  */

  newname = map_w32_filename (newname, NULL);
  result = rename (temp, newname);

  if (result < 0
      && errno == EEXIST
      && _chmod (newname, 0666) == 0
      && _unlink (newname) == 0)
    result = rename (temp, newname);

  return result;
}

int
sys_rmdir (const char * path)
{
  return _rmdir (map_w32_filename (path, NULL));
}

int
sys_unlink (const char * path)
{
  path = map_w32_filename (path, NULL);

  /* On Unix, unlink works without write permission. */
  _chmod (path, 0666);
  return _unlink (path);
}

static FILETIME utc_base_ft;
static long double utc_base;
static int init = 0;

static time_t
convert_time (FILETIME ft)
{
  long double ret;

  if (!init)
    {
      /* Determine the delta between 1-Jan-1601 and 1-Jan-1970. */
      SYSTEMTIME st;

      st.wYear = 1970;
      st.wMonth = 1;
      st.wDay = 1;
      st.wHour = 0;
      st.wMinute = 0;
      st.wSecond = 0;
      st.wMilliseconds = 0;

      SystemTimeToFileTime (&st, &utc_base_ft);
      utc_base = (long double) utc_base_ft.dwHighDateTime
	* 4096.0L * 1024.0L * 1024.0L + utc_base_ft.dwLowDateTime;
      init = 1;
    }

  if (CompareFileTime (&ft, &utc_base_ft) < 0)
    return 0;

  ret = (long double) ft.dwHighDateTime
    * 4096.0L * 1024.0L * 1024.0L + ft.dwLowDateTime;
  ret -= utc_base;
  return (time_t) (ret * 1e-7L);
}

void
convert_from_time_t (time_t time, FILETIME * pft)
{
  long double tmp;

  if (!init)
    {
      /* Determine the delta between 1-Jan-1601 and 1-Jan-1970. */
      SYSTEMTIME st;

      st.wYear = 1970;
      st.wMonth = 1;
      st.wDay = 1;
      st.wHour = 0;
      st.wMinute = 0;
      st.wSecond = 0;
      st.wMilliseconds = 0;

      SystemTimeToFileTime (&st, &utc_base_ft);
      utc_base = (long double) utc_base_ft.dwHighDateTime
	* 4096 * 1024 * 1024 + utc_base_ft.dwLowDateTime;
      init = 1;
    }

  /* time in 100ns units since 1-Jan-1601 */
  tmp = (long double) time * 1e7 + utc_base;
  pft->dwHighDateTime = (DWORD) (tmp / (4096.0 * 1024 * 1024));
  pft->dwLowDateTime = (DWORD) (tmp - (4096.0 * 1024 * 1024) * pft->dwHighDateTime);
}

#if 0
/* No reason to keep this; faking inode values either by hashing or even
   using the file index from GetInformationByHandle, is not perfect and
   so by default Emacs doesn't use the inode values on Windows.
   Instead, we now determine file-truename correctly (except for
   possible drive aliasing etc).  */

/*  Modified version of "PJW" algorithm (see the "Dragon" compiler book). */
static unsigned
hashval (const unsigned char * str)
{
  unsigned h = 0;
  while (*str)
    {
      h = (h << 4) + *str++;
      h ^= (h >> 28);
    }
  return h;
}

/* Return the hash value of the canonical pathname, excluding the
   drive/UNC header, to get a hopefully unique inode number. */
static DWORD
generate_inode_val (LPCTSTR name)
{
  TCHAR fullname[MAX_PATH];
  LPTSTR p;
  unsigned hash;

  /* Get the truly canonical filename, if it exists.  (Note: this
     doesn't resolve aliasing due to subst commands, or recognise hard
     links.  */
  if (!w32_get_long_filename ((char *)name, fullname, MAX_PATH))
    abort ();

  parse_root (fullname, &p);
  /* Normal W32 filesystems are still case insensitive. */
  _tcslwr (p);
  return hashval (p);
}

#endif

/* MSVC stat function can't cope with UNC names and has other bugs, so
   replace it with our own.  This also allows us to calculate consistent
   inode values without hacks in the main Emacs code. */
int
stat (const char *path_0, struct stat * buf)
{
  LPCTSTR path = path_0;
  LPTSTR name, r, name1, name2, prevname;
  char drive_root[4];
  UINT devtype;
  WIN32_FIND_DATA wfd;
  HANDLE fh;
  DWORD fake_inode;
  int permission;
  int len;
  int rootdir = FALSE;

  if (path == NULL || buf == NULL)
    {
      errno = EFAULT;
      return -1;
    }

  name = (char *) map_w32_filename (path, &path);
  /* Must be valid filename, no wild cards or other invalid
     characters.  We use _mbspbrk to support multibyte strings that
     might look to strpbrk as if they included literal *, ?, and other
     characters mentioned below that are disallowed by Windows
     filesystems.  */
  if (is_string_contains_chars (name, w32_filename_illegal_characters))
    {
      errno = ENOENT;
      return -1;
    }

  name1 = CharNext (name);
  name2 = CharNext (name1);
  /* If name is "c:/.." or "/.." then stat "c:/" or "/".  */
  r = IS_DEVICE_SEP (*name1) ? name2 : name;
  if (IS_DIRECTORY_SEP (r[0]) && r[1] == '.' && r[2] == '.' && r[3] == '\0')
    {
      r[1] = r[2] = '\0';
    }

  /* Remove trailing directory separator, unless name is the root
     directory of a drive or UNC volume in which case ensure there
     is a trailing separator. */
  len = lstrlen (name);
  rootdir = (path >= name + len - 1
	     && (IS_DIRECTORY_SEP (*path) || *path == 0));
  name = memcpy (alloca ((len + 2) * sizeof(TCHAR)), name, (len + 1) * sizeof(TCHAR));

  prevname = CharPrev (name, &name[len]);

  if (is_unc_volume (name))
    {
      DWORD attrs = unc_volume_file_attributes (name);

      if (attrs == -1)
	return -1;

      memset (&wfd, 0, sizeof (wfd));
      wfd.dwFileAttributes = attrs;
      wfd.ftCreationTime = utc_base_ft;
      wfd.ftLastAccessTime = utc_base_ft;
      wfd.ftLastWriteTime = utc_base_ft;
      lstrcpy (wfd.cFileName, name);
    }
  else if (rootdir)
    {
      if (!IS_DIRECTORY_SEP (*prevname))
	{
	  name[len] = '\\';
	  name[len + 1] = '\0';
	}
      if (GetDriveType (name) < 2)
	{
	  errno = ENOENT;
	  return -1;
	}
      memset (&wfd, 0, sizeof (wfd));
      wfd.dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
      wfd.ftCreationTime = utc_base_ft;
      wfd.ftLastAccessTime = utc_base_ft;
      wfd.ftLastWriteTime = utc_base_ft;
      lstrcpy (wfd.cFileName, name);
    }
  else
    {
      if (IS_DIRECTORY_SEP (*prevname))
	name[len - 1] = '\0';

      /* (This is hacky, but helps when doing file completions on
	 network drives.)  Optimize by using information available from
	 active readdir if possible.  */
      len = strlen (dir_pathname);
      if (IS_DIRECTORY_SEP (dir_pathname[len-1]))
	len--;
      if (dir_find_handle != INVALID_HANDLE_VALUE
	  && CompareString(LOCALE_SYSTEM_DEFAULT, 0,
			   name, len, dir_pathname, len) == CSTR_EQUAL
	  && IS_DIRECTORY_SEP (name[len])
	  && lstrcmpi (name + len + 1, dir_static.d_name) == 0)
	{
	  /* This was the last entry returned by readdir.  */
	  wfd = dir_find_data;
	}
      else
	{
	  fh = FindFirstFile (name, &wfd);
	  if (fh == INVALID_HANDLE_VALUE)
	    {
	      errno = ENOENT;
	      return -1;
	    }
	  FindClose (fh);
	}
    }

  if (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    {
      buf->st_mode = _S_IFDIR;
      buf->st_nlink = 2;	/* doesn't really matter */
      fake_inode = 0;		/* this doesn't either I think */
    }
#ifndef MEADOW
  /* GetDriveType needs the root directory of NAME's drive.  */
  if (!(strlen (name) >= 2 && IS_DEVICE_SEP (name[1])))
    devtype = GetDriveType (NULL); /* use root of current diectory */
  else
    {
      strncpy (drive_root, name, 3);
      drive_root[3] = '\0';
      devtype = GetDriveType (drive_root);
    }

  if (!(NILP (Vw32_get_true_file_attributes)
	|| (EQ (Vw32_get_true_file_attributes, Qlocal) &&
	    devtype != DRIVE_FIXED && devtype != DRIVE_RAMDISK))
	   /* No access rights required to get info.  */
	   && (fh = CreateFile (name, 0, 0, NULL, OPEN_EXISTING, 0, NULL))
	      != INVALID_HANDLE_VALUE)
    {
      /* This is more accurate in terms of gettting the correct number
	 of links, but is quite slow (it is noticeable when Emacs is
	 making a list of file name completions). */
      BY_HANDLE_FILE_INFORMATION info;

      if (GetFileInformationByHandle (fh, &info))
	{
	  buf->st_nlink = info.nNumberOfLinks;
	  /* Might as well use file index to fake inode values, but this
	     is not guaranteed to be unique unless we keep a handle open
	     all the time (even then there are situations where it is
	     not unique).  Reputedly, there are at most 48 bits of info
	     (on NTFS, presumably less on FAT). */
	  fake_inode = info.nFileIndexLow ^ info.nFileIndexHigh;
	}
      else
	{
	  buf->st_nlink = 1;
	  fake_inode = 0;
	}

      switch (GetFileType (fh))
	{
	case FILE_TYPE_DISK:
	  buf->st_mode = _S_IFREG;
	  break;
	case FILE_TYPE_PIPE:
	  buf->st_mode = _S_IFIFO;
	  break;
	case FILE_TYPE_CHAR:
	case FILE_TYPE_UNKNOWN:
	default:
	  buf->st_mode = _S_IFCHR;
	}
      CloseHandle (fh);
    }
#endif /* not MEADOW */
  else
    {
      /* Don't bother to make this information more accurate.  */
      buf->st_mode = (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ?
	_S_IFDIR : _S_IFREG;
      buf->st_nlink = 1;
      fake_inode = 0;
    }

#if 0
  /* Not sure if there is any point in this.  */
  if (!NILP (Vw32_generate_fake_inodes))
    fake_inode = generate_inode_val (name);
  else if (fake_inode == 0)
    {
      /* For want of something better, try to make everything unique.  */
      static DWORD gen_num = 0;
      fake_inode = ++gen_num;
    }
#endif

  /* MSVC defines _ino_t to be short; other libc's might not.  */
  if (sizeof (buf->st_ino) == 2)
    buf->st_ino = fake_inode ^ (fake_inode >> 16);
  else
    buf->st_ino = fake_inode;

  /* consider files to belong to current user */
  buf->st_uid = the_passwd.pw_uid;
  buf->st_gid = the_passwd.pw_gid;

  /* volume_info is set indirectly by map_w32_filename */
  buf->st_dev = volume_info.serialnum;
  buf->st_rdev = volume_info.serialnum;


  buf->st_size = wfd.nFileSizeLow;

  /* Convert timestamps to Unix format. */
  buf->st_mtime = convert_time (wfd.ftLastWriteTime);
  buf->st_atime = convert_time (wfd.ftLastAccessTime);
  if (buf->st_atime == 0) buf->st_atime = buf->st_mtime;
  buf->st_ctime = convert_time (wfd.ftCreationTime);
  if (buf->st_ctime == 0) buf->st_ctime = buf->st_mtime;

  /* determine rwx permissions */
  if (wfd.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
    permission = _S_IREAD;
  else
    permission = _S_IREAD | _S_IWRITE;

  if (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    permission |= _S_IEXEC;
  else if (is_exec (name))
    permission |= _S_IEXEC;

  buf->st_mode |= permission | (permission >> 3) | (permission >> 6);

  return 0;
}

/* Provide fstat and utime as well as stat for consistent handling of
   file timestamps. */
int
fstat (int desc, struct stat * buf)
{
  HANDLE fh = (HANDLE) _get_osfhandle (desc);
  BY_HANDLE_FILE_INFORMATION info;
  DWORD fake_inode;
  int permission;

  switch (GetFileType (fh) & ~FILE_TYPE_REMOTE)
    {
    case FILE_TYPE_DISK:
      buf->st_mode = _S_IFREG;
      if (!GetFileInformationByHandle (fh, &info))
	{
	  errno = EACCES;
	  return -1;
	}
      break;
    case FILE_TYPE_PIPE:
      buf->st_mode = _S_IFIFO;
      goto non_disk;
    case FILE_TYPE_CHAR:
    case FILE_TYPE_UNKNOWN:
    default:
      buf->st_mode = _S_IFCHR;
    non_disk:
      memset (&info, 0, sizeof (info));
      info.dwFileAttributes = 0;
      info.ftCreationTime = utc_base_ft;
      info.ftLastAccessTime = utc_base_ft;
      info.ftLastWriteTime = utc_base_ft;
    }

  if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    {
      buf->st_mode = _S_IFDIR;
      buf->st_nlink = 2;	/* doesn't really matter */
      fake_inode = 0;		/* this doesn't either I think */
    }
  else
    {
      buf->st_nlink = info.nNumberOfLinks;
      /* Might as well use file index to fake inode values, but this
	 is not guaranteed to be unique unless we keep a handle open
	 all the time (even then there are situations where it is
	 not unique).  Reputedly, there are at most 48 bits of info
      (on NTFS, presumably less on FAT). */
      fake_inode = info.nFileIndexLow ^ info.nFileIndexHigh;
    }

  /* MSVC defines _ino_t to be short; other libc's might not.  */
  if (sizeof (buf->st_ino) == 2)
    buf->st_ino = fake_inode ^ (fake_inode >> 16);
  else
    buf->st_ino = fake_inode;

  /* consider files to belong to current user */
  buf->st_uid = 0;
  buf->st_gid = 0;

  buf->st_dev = info.dwVolumeSerialNumber;
  buf->st_rdev = info.dwVolumeSerialNumber;

  buf->st_size = info.nFileSizeLow;

  /* Convert timestamps to Unix format. */
  buf->st_mtime = convert_time (info.ftLastWriteTime);
  buf->st_atime = convert_time (info.ftLastAccessTime);
  if (buf->st_atime == 0) buf->st_atime = buf->st_mtime;
  buf->st_ctime = convert_time (info.ftCreationTime);
  if (buf->st_ctime == 0) buf->st_ctime = buf->st_mtime;

  /* determine rwx permissions */
  if (info.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
    permission = _S_IREAD;
  else
    permission = _S_IREAD | _S_IWRITE;

  if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    permission |= _S_IEXEC;
  else
    {
#if 0 /* no way of knowing the filename */
      char * p = strrchr (name, '.');
      if (p != NULL &&
	  (stricmp (p, ".exe") == 0 ||
	   stricmp (p, ".com") == 0 ||
	   stricmp (p, ".bat") == 0 ||
	   stricmp (p, ".cmd") == 0))
	permission |= _S_IEXEC;
#endif
    }

  buf->st_mode |= permission | (permission >> 3) | (permission >> 6);

  return 0;
}

#ifdef MEADOW
static unsigned
hashval (const unsigned char * str)
{
  unsigned h = 0;
  while (*str)
    {
      h = (h << 4) + *str++;
      h ^= (h >> 28);
    }
  return h;
}

ssize_t
execute_readlink_hook (const char *path, char *buf, size_t bufsize)
{
  Lisp_Object ents, ret, func, content;

  func = find_link_function (path);
  if (NILP (func))
    {
      errno = ENOENT;
      return -1;
    }

  if (CONSP (func))
    {
      content = XCDR (func);
      func = XCAR (func);

      if (! STRINGP (content))
	content = Qnil;
    }
  else
    content =
      Fmw32_file_content_string (build_string (path), Qnil);
  
  ret = call2 (func, build_string (path), content);

  if (STRINGP (ret))   /* Success */
    {
      strncpy (buf, SDATA (ret), bufsize);
      if (buf[bufsize - 1] == 0)
	return strlen (buf);
      return bufsize;
    }
  
  errno = 0;
  if (EQ (ret, intern ("enoent")))
    errno = ENOENT;
  else if (EQ (ret, intern ("einval")))
    errno = EINVAL;
  else if (EQ (ret, intern ("eio")))
    errno = EIO;

  return -1;
}

/* This function assumes that the path does not contain symlink except
   the last segment. */
ssize_t
readlink_notruename (const char *path, char *buf, size_t bufsize)
{
  HANDLE hf;
  WIN32_FIND_DATA find;
  char reparse_data[MAXIMUM_REPARSE_DATA_BUFFER_SIZE];
  REPARSE_DATA_BUFFER *rdb = (REPARSE_DATA_BUFFER *)reparse_data;
  DWORD retlen;
  ssize_t len;
  HANDLE fh;

  fh = FindFirstFile (path, &find);
  if (fh == INVALID_HANDLE_VALUE)
    {
      errno = ENOENT;
      return -1;
    }
  FindClose (fh);

  /* If the path is not native symlink, execute alternative link
     resolving algorithm set in mw32-symlink-handle-map. */
  if (! (find.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
      find.dwReserved0 != IO_REPARSE_TAG_SYMLINK)
    return execute_readlink_hook (path, buf, bufsize);

  if ((hf = CreateFile (path, 0,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL, OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS |
			FILE_FLAG_OPEN_REPARSE_POINT, NULL))
      == INVALID_HANDLE_VALUE)
    {
      errno = EINVAL;
      return -1;
    }

  if (DeviceIoControl (hf, FSCTL_GET_REPARSE_POINT,
		       NULL, 0,
		       reparse_data, sizeof (reparse_data),
		       &retlen, NULL) == 0)
    {
      errno = EIO;
      CloseHandle (hf);
      return -1;
    }

  CloseHandle (hf);
  len = WideCharToMultiByte (CP_ACP, 0,
			     rdb->SymbolicLinkReparseBuffer.PathBuffer
			     + rdb->SymbolicLinkReparseBuffer.PrintNameOffset
			     / sizeof (WCHAR),
			     rdb->SymbolicLinkReparseBuffer.PrintNameLength
			     / sizeof (WCHAR),
			     buf, bufsize, NULL, NULL);

  if (len == 0)	
    return -1;

  /* Make this buffer zero-terminated string, even if the buffer was
     filled with partial path string. Trimming of the path is not
     informed the caller. */
  buf[bufsize - 1] = 0;
  dostounix_filename (buf);
  
  return len;
}

/* Linux readlink emulation on Windows (Vista and the after)*/
ssize_t
readlink (const char *path, char *buf, size_t bufsize)
{
  char pbuf[MAXPATHLEN];

  if (mw32_file_truename0 (path, TRUE, pbuf, MAXPATHLEN) < 0)
    return readlink_notruename (path, buf, bufsize);
    
  return readlink_notruename (pbuf, buf, bufsize);

}


static int
count_links_and_generate_inode_number (const char *path, struct stat *buf)
{
  int ret = 0;
  HANDLE fh;
  
  if ((!NILP (Vw32_get_true_file_attributes))
      && (buf->st_ino == 0)
      && (((fh = CreateFile (path, 0,
			     (FILE_SHARE_READ | FILE_SHARE_WRITE),
			     NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL))
	   != INVALID_HANDLE_VALUE)
	  || ((fh = CreateFile (path, 0,
				(FILE_SHARE_READ | FILE_SHARE_WRITE),
				NULL, OPEN_EXISTING,
				FILE_FLAG_BACKUP_SEMANTICS, NULL))
	      != INVALID_HANDLE_VALUE)))
    {
      /* This is more accurate in terms of gettting the correct number
	 of links, but is quite slow (it is noticable when Emacs is
	 making a list of file name completions). */
      BY_HANDLE_FILE_INFORMATION info;

      if (GetFileInformationByHandle (fh, &info))
	{
	  int high_crc, low_crc, crc_numerator;
	  int fake_inode_bits = sizeof(buf->st_ino) * 8;

	  buf->st_nlink = info.nNumberOfLinks;
	  /* These expressions are for making 64bit
	     file index into fake_inode_bits order number.
	     A smaller order number is made by psude-CRC16....;-) */
	  crc_numerator = ((1 << (fake_inode_bits - 1)) +
			   (1 << (fake_inode_bits - 2)) +
			   (1 << 2) + 1);
	  high_crc = info.nFileIndexHigh % crc_numerator;
	  low_crc = info.nFileIndexLow % crc_numerator;
	  buf->st_ino = (high_crc + low_crc) % crc_numerator;
	}

      CloseHandle (fh);
    }
  else
    {
      int fake_inode;
      int fake_inode_bits = sizeof(buf->st_ino) * 8;
      int fake_inode_max = (1 << fake_inode_bits);
      fake_inode = hashval (path);
      buf->st_ino = fake_inode % fake_inode_max;
    }

  ret = 0;

  if ((!NILP (Vw32_get_true_file_link_count))
      && (buf->st_mode & _S_IFDIR))
    {
      WIN32_FIND_DATA w32fd;
      int len, max, num;
      LPTSTR prevname;
      TCHAR name[MAXPATHLEN];

      len = lstrlen ((LPTSTR) path);
      if (len > (MAXPATHLEN - 3))
	return ret;
      memcpy (name, path, sizeof (TCHAR) * (len + 1));

      unixtodos_filename (name);

      prevname = CharPrev(name, &name[len]);

      if (IS_DIRECTORY_SEP (*prevname))
	{
	  name[len] = '*';
	  name[len + 1] = '\0';
	}
      else
	{
	  name[len] = '\\';
	  name[len + 1] = '*';
	  name[len + 2] = '\0';
	}

      if (NUMBERP (Vw32_get_true_file_link_count))
	{
	  max = XINT (Vw32_get_true_file_link_count);
	  if (max <= 0)
	    return ret;
	}
      else
	max = -1;

      num = 0;
      fh = FindFirstFile (name, &w32fd);

      if (fh == INVALID_HANDLE_VALUE)
	return ret;

      do
	{
	  if (w32fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
	    num++;
	  if (max > 0)
	    max--;
	  else if (max == 0)
	    break;
	} while (FindNextFile (fh, &w32fd));

      FindClose (fh);

      buf->st_nlink = num;

    }

  return ret;
}
/* _stat of windows does not follow symbolic link. This is
   symlink-chase version of stat for windows. */
int
sys_stat (const char* path, struct stat *buf)
{
  char pbuf[MAXPATHLEN];
  int ret;
  
  if (mw32_file_truename0 (path, FALSE, pbuf, MAXPATHLEN) < 0)
    ret = stat (path, buf);
  else
    ret = stat (pbuf, buf);

  if (ret < 0)
    return -1;

  return count_links_and_generate_inode_number (path, buf);
}

/* _stat of windows does not tell wheather the directory is a symlink
   or not. */
int
lstat (const char *path, struct stat *buf)
{
  WIN32_FIND_DATA find;
  TCHAR pbuf[MAXPATHLEN];
  const char *lpath = pbuf;
  HANDLE fh;

  if (mw32_file_truename0 (path, TRUE, pbuf, MAXPATHLEN) < 0)
    lpath = path;
  
  if (stat (lpath, buf) < 0)
    return -1;

  /* Symbolic link */
  if ((fh = FindFirstFile (lpath, &find)) != INVALID_HANDLE_VALUE)
    {
      FindClose (fh);
      if ((find.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT &&
	   find.dwReserved0 == IO_REPARSE_TAG_SYMLINK) ||
	  ! NILP (find_link_function (lpath)))
	{	
	  /* Override file type for symbolic link */
	  buf->st_mode &= ~S_IFMT;
	  buf->st_mode |= S_IFLNK;
	}
    }

  return count_links_and_generate_inode_number (lpath, buf);
}

/* This is a list of the error messages of the auxiliary program
   symlink.exe used in symlink function following.  Lack of ability to
   redirect standard IOs of ShellExecute inhibits hand error messages
   from elevated subprocess. */
char *symlink_exe_messages[] = {
  "No Error",
  "Kernel32.dll not found.",
  "Symbolic link is unavailable on this system.",
  "Missing target name.",
  "Missing link name.",
  "Failed to convert source path to widechar.",
  "Failed to convert dest path to widechar.",
  "A required privilege is not held by the client.n",
  "Failed to create symbolic link"
};

/* It is privileged operation to create symbolic link on Windows
   Vista/7. So core process of symlink is in external program and this
   function simply it. */
int
symlink (const char *oldpath, const char *newpath)
{
  char *cmdbuf;
  int cmdbuflen;
  char *proc = "meadow_symlink.exe";
  struct stat st;
  DWORD ret;
  SHELLEXECUTEINFO si = {0};
  OSVERSIONINFO verinfo;

  if (! NILP (Ffboundp (Vmw32_symlink_hook)))
    {
      Lisp_Object ret;
      
      ret = call2 (Vmw32_symlink_hook,
		   build_string (oldpath), build_string (newpath));
      
      if (NILP (ret)) return 0;
      errno = 0;
      if (EQ (ret, intern ("enoent")))
	errno = ENOENT;
      else if (EQ (ret, intern ("einval")))
	errno = EINVAL;
      else if (EQ (ret, intern ("eio")))
	errno = EIO;

      return -1;
    }

  if (STRINGP (Vmw32_symlink_program))
    proc = SDATA (Vmw32_symlink_program);
  
  verinfo.dwOSVersionInfoSize = sizeof (verinfo);
  GetVersionEx (&verinfo);
  if (verinfo.dwMajorVersion < 6)  /* Vista is version 6 */
    error ("This system does not have symbolic link feature.");

  if (sys_stat (oldpath, &st) < 0)
    return -1;

  cmdbuflen = 3 +		/* -d / -f and SP   */
    strlen (oldpath) + 3 +	/* path and quotes and SP */
    strlen (newpath) + 3;	/* path and quotes and NULL*/
  cmdbuf = alloca (cmdbuflen);
  snprintf(cmdbuf, cmdbuflen, "%s \"%s\" \"%s\"",
	   ((st.st_mode & S_IFMT) == S_IFDIR) ? "-d" : "-f",
	   oldpath, newpath);

  /* CreatePrcess does not activate privilege elevation dialogue and
     returned error with GetLastError() = 740 (The requested operation
     requires elevation). ShellExecute is the only formal way to do
     elevation on startup process, maybe.. */

  si.cbSize = sizeof (si);
  si.lpVerb = "runas";			/* Do plivilege elevation */
  si.fMask = SEE_MASK_NOCLOSEPROCESS;	/* Allows get exit code   */
  si.lpFile = proc;
  si.lpParameters = cmdbuf;
  if (!ShellExecuteEx (&si) || (int)si.hInstApp < 33)
    {
      if ((int)si.hInstApp == 5)
	error ("Access denied");	/* Canceled in UAC dialog */
      error ("Failed to ShellExecute(%s):(hInstApp = %d)",
	     proc, si.hInstApp);
    }

  switch (WaitForSingleObject (si.hProcess, 10000)) /* 10 seconds */
    {
    case WAIT_TIMEOUT:
      CloseHandle (si.hProcess);
      error ("Make symbolic link timeout `%s\', `%s\'",
	     oldpath, newpath);
    case WAIT_ABANDONED:
      CloseHandle (si.hProcess);
      error ("WAIT_ABANDONED from WaitForSigleObject. abort.");
    }
  
  if (GetExitCodeProcess (si.hProcess, &ret) == 0)
    {
      CloseHandle (si.hProcess);
      error ("Failed to get exit code from %s : %d", proc, GetLastError());
    }
  CloseHandle (si.hProcess);

  if (ret > 0)
    {
      if (ret < sizeof (symlink_exe_messages) / sizeof (char *))
	error (symlink_exe_messages[ret]);
      error ("Unknown error return from privileged process.");
    }
  return 0;
}
#endif

#if _MSC_VER < 1400
int
utime (const char *name, struct utimbuf *times)
{
  struct utimbuf deftime;
  HANDLE fh;
  FILETIME mtime;
  FILETIME atime;

  if (times == NULL)
    {
      deftime.modtime = deftime.actime = time (NULL);
      times = &deftime;
    }

  /* Need write access to set times.  */
  fh = CreateFile (name, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
		   0, OPEN_EXISTING, 0, NULL);
  if (fh)
    {
      convert_from_time_t (times->actime, &atime);
      convert_from_time_t (times->modtime, &mtime);
      if (!SetFileTime (fh, NULL, &atime, &mtime))
	{
	  CloseHandle (fh);
	  errno = EACCES;
	  return -1;
	}
      CloseHandle (fh);
    }
  else
    {
      errno = EINVAL;
      return -1;
    }
  return 0;
}
#endif

#ifdef HAVE_SOCKETS

/* Wrappers for  winsock functions to map between our file descriptors
   and winsock's handles; also set h_errno for convenience.

   To allow Emacs to run on systems which don't have winsock support
   installed, we dynamically link to winsock on startup if present, and
   otherwise provide the minimum necessary functionality
   (eg. gethostname). */

/* function pointers for relevant socket functions */
int (PASCAL *pfn_WSAStartup) (WORD wVersionRequired, LPWSADATA lpWSAData);
void (PASCAL *pfn_WSASetLastError) (int iError);
int (PASCAL *pfn_WSAGetLastError) (void);
int (PASCAL *pfn_WSAEventSelect) (SOCKET s, HANDLE hEventObject, long lNetworkEvents);
HANDLE (PASCAL *pfn_WSACreateEvent) (void);
int (PASCAL *pfn_WSACloseEvent) (HANDLE hEvent);
int (PASCAL *pfn_socket) (int af, int type, int protocol);
int (PASCAL *pfn_bind) (SOCKET s, const struct sockaddr *addr, int namelen);
int (PASCAL *pfn_connect) (SOCKET s, const struct sockaddr *addr, int namelen);
#ifdef MEADOW
int (PASCAL *pfn_select) (int nfds, pfn_fd_set *readfds, pfn_fd_set *writefds, pfn_fd_set *excepfds, const struct timeval FAR *timeout);
#endif
int (PASCAL *pfn_ioctlsocket) (SOCKET s, long cmd, u_long *argp);
int (PASCAL *pfn_recv) (SOCKET s, char * buf, int len, int flags);
int (PASCAL *pfn_send) (SOCKET s, const char * buf, int len, int flags);
int (PASCAL *pfn_closesocket) (SOCKET s);
int (PASCAL *pfn_shutdown) (SOCKET s, int how);
int (PASCAL *pfn_WSACleanup) (void);

u_short (PASCAL *pfn_htons) (u_short hostshort);
u_short (PASCAL *pfn_ntohs) (u_short netshort);
unsigned long (PASCAL *pfn_inet_addr) (const char * cp);
int (PASCAL *pfn_gethostname) (char * name, int namelen);
struct hostent * (PASCAL *pfn_gethostbyname) (const char * name);
struct servent * (PASCAL *pfn_getservbyname) (const char * name, const char * proto);
int (PASCAL *pfn_getpeername) (SOCKET s, struct sockaddr *addr, int * namelen);
int (PASCAL *pfn_setsockopt) (SOCKET s, int level, int optname,
			      const char * optval, int optlen);
int (PASCAL *pfn_getsockopt) (SOCKET s, int level, int optname,
			      const char * optval, int *optlen);
int (PASCAL *pfn_listen) (SOCKET s, int backlog);
int (PASCAL *pfn_getsockname) (SOCKET s, struct sockaddr * name,
			       int * namelen);
SOCKET (PASCAL *pfn_accept) (SOCKET s, struct sockaddr * addr, int * addrlen);
int (PASCAL *pfn_recvfrom) (SOCKET s, char * buf, int len, int flags,
		       struct sockaddr * from, int * fromlen);
int (PASCAL *pfn_sendto) (SOCKET s, const char * buf, int len, int flags,
			  const struct sockaddr * to, int tolen);

/* SetHandleInformation is only needed to make sockets non-inheritable. */
BOOL (WINAPI *pfn_SetHandleInformation) (HANDLE object, DWORD mask, DWORD flags);
#ifndef HANDLE_FLAG_INHERIT
#define HANDLE_FLAG_INHERIT	1
#endif

HANDLE winsock_lib;
static int winsock_inuse;

BOOL
term_winsock (void)
{
  if (winsock_lib != NULL && winsock_inuse == 0)
    {
      /* Not sure what would cause WSAENETDOWN, or even if it can happen
	 after WSAStartup returns successfully, but it seems reasonable
	 to allow unloading winsock anyway in that case. */
      if (pfn_WSACleanup () == 0 ||
	  pfn_WSAGetLastError () == WSAENETDOWN)
	{
	  if (FreeLibrary (winsock_lib))
	  winsock_lib = NULL;
	  return TRUE;
	}
    }
  return FALSE;
}

BOOL
init_winsock (int load_now)
{
  WSADATA  winsockData;

  if (winsock_lib != NULL)
    return TRUE;

  pfn_SetHandleInformation = NULL;
  pfn_SetHandleInformation
    = (void *) GetProcAddress (GetModuleHandle ("kernel32.dll"),
			       "SetHandleInformation");

  winsock_lib = LoadLibrary ("Ws2_32.dll");

  if (winsock_lib != NULL)
    {
      /* dynamically link to socket functions */

#define LOAD_PROC(fn) \
      if ((pfn_##fn = (void *) GetProcAddress (winsock_lib, #fn)) == NULL) \
        goto fail;

      LOAD_PROC( WSAStartup );
      LOAD_PROC( WSASetLastError );
      LOAD_PROC( WSAGetLastError );
      LOAD_PROC( WSAEventSelect );
      LOAD_PROC( WSACreateEvent );
      LOAD_PROC( WSACloseEvent );
      LOAD_PROC( socket );
      LOAD_PROC( bind );
      LOAD_PROC( connect );
#ifdef MEADOW
      LOAD_PROC( select );
#endif
      LOAD_PROC( ioctlsocket );
      LOAD_PROC( recv );
      LOAD_PROC( send );
      LOAD_PROC( closesocket );
      LOAD_PROC( shutdown );
      LOAD_PROC( htons );
      LOAD_PROC( ntohs );
      LOAD_PROC( inet_addr );
      LOAD_PROC( gethostname );
      LOAD_PROC( gethostbyname );
      LOAD_PROC( getservbyname );
      LOAD_PROC( getpeername );
      LOAD_PROC( WSACleanup );
      LOAD_PROC( setsockopt );
      LOAD_PROC( getsockopt );
      LOAD_PROC( listen );
      LOAD_PROC( getsockname );
      LOAD_PROC( accept );
      LOAD_PROC( recvfrom );
      LOAD_PROC( sendto );
#undef LOAD_PROC

      /* specify version 1.1 of winsock */
      if (pfn_WSAStartup (0x101, &winsockData) == 0)
        {
	  if (winsockData.wVersion != 0x101)
	    goto fail;

	  if (!load_now)
	    {
	      /* Report that winsock exists and is usable, but leave
		 socket functions disabled.  I am assuming that calling
		 WSAStartup does not require any network interaction,
		 and in particular does not cause or require a dial-up
		 connection to be established. */

	      pfn_WSACleanup ();
	      FreeLibrary (winsock_lib);
	      winsock_lib = NULL;
	    }
	  winsock_inuse = 0;
	  return TRUE;
	}

    fail:
      FreeLibrary (winsock_lib);
      winsock_lib = NULL;
    }

  return FALSE;
}


int h_errno = 0;

/* function to set h_errno for compatability; map winsock error codes to
   normal system codes where they overlap (non-overlapping definitions
   are already in <sys/socket.h> */
static void
set_errno ()
{
  if (winsock_lib == NULL)
    h_errno = EINVAL;
  else
    h_errno = pfn_WSAGetLastError ();

  switch (h_errno)
    {
    case WSAEACCES:		h_errno = EACCES; break;
    case WSAEBADF: 		h_errno = EBADF; break;
    case WSAEFAULT:		h_errno = EFAULT; break;
    case WSAEINTR: 		h_errno = EINTR; break;
    case WSAEINVAL:		h_errno = EINVAL; break;
    case WSAEMFILE:		h_errno = EMFILE; break;
    case WSAENAMETOOLONG: 	h_errno = ENAMETOOLONG; break;
    case WSAENOTEMPTY:		h_errno = ENOTEMPTY; break;
#ifdef MEADOW
    case WSAEWOULDBLOCK:	h_errno = EINPROGRESS; break;
#endif
    }
  errno = h_errno;
}

static void
check_errno ()
{
  if (h_errno == 0 && winsock_lib != NULL)
    pfn_WSASetLastError (0);
}

/* Extend strerror to handle the winsock-specific error codes.  */
struct {
  int errnum;
  char * msg;
} _wsa_errlist[] = {
  WSAEINTR                , "Interrupted function call",
  WSAEBADF                , "Bad file descriptor",
  WSAEACCES               , "Permission denied",
  WSAEFAULT               , "Bad address",
  WSAEINVAL               , "Invalid argument",
  WSAEMFILE               , "Too many open files",

  WSAEWOULDBLOCK          , "Resource temporarily unavailable",
  WSAEINPROGRESS          , "Operation now in progress",
  WSAEALREADY             , "Operation already in progress",
  WSAENOTSOCK             , "Socket operation on non-socket",
  WSAEDESTADDRREQ         , "Destination address required",
  WSAEMSGSIZE             , "Message too long",
  WSAEPROTOTYPE           , "Protocol wrong type for socket",
  WSAENOPROTOOPT          , "Bad protocol option",
  WSAEPROTONOSUPPORT      , "Protocol not supported",
  WSAESOCKTNOSUPPORT      , "Socket type not supported",
  WSAEOPNOTSUPP           , "Operation not supported",
  WSAEPFNOSUPPORT         , "Protocol family not supported",
  WSAEAFNOSUPPORT         , "Address family not supported by protocol family",
  WSAEADDRINUSE           , "Address already in use",
  WSAEADDRNOTAVAIL        , "Cannot assign requested address",
  WSAENETDOWN             , "Network is down",
  WSAENETUNREACH          , "Network is unreachable",
  WSAENETRESET            , "Network dropped connection on reset",
  WSAECONNABORTED         , "Software caused connection abort",
  WSAECONNRESET           , "Connection reset by peer",
  WSAENOBUFS              , "No buffer space available",
  WSAEISCONN              , "Socket is already connected",
  WSAENOTCONN             , "Socket is not connected",
  WSAESHUTDOWN            , "Cannot send after socket shutdown",
  WSAETOOMANYREFS         , "Too many references",	    /* not sure */
  WSAETIMEDOUT            , "Connection timed out",
  WSAECONNREFUSED         , "Connection refused",
  WSAELOOP                , "Network loop",		    /* not sure */
  WSAENAMETOOLONG         , "Name is too long",
  WSAEHOSTDOWN            , "Host is down",
  WSAEHOSTUNREACH         , "No route to host",
  WSAENOTEMPTY            , "Buffer not empty",		    /* not sure */
  WSAEPROCLIM             , "Too many processes",
  WSAEUSERS               , "Too many users",		    /* not sure */
  WSAEDQUOT               , "Double quote in host name",    /* really not sure */
  WSAESTALE               , "Data is stale",		    /* not sure */
  WSAEREMOTE              , "Remote error",		    /* not sure */

  WSASYSNOTREADY          , "Network subsystem is unavailable",
  WSAVERNOTSUPPORTED      , "WINSOCK.DLL version out of range",
  WSANOTINITIALISED       , "Winsock not initialized successfully",
  WSAEDISCON              , "Graceful shutdown in progress",
#ifdef WSAENOMORE
  WSAENOMORE              , "No more operations allowed",   /* not sure */
  WSAECANCELLED           , "Operation cancelled",	    /* not sure */
  WSAEINVALIDPROCTABLE    , "Invalid procedure table from service provider",
  WSAEINVALIDPROVIDER     , "Invalid service provider version number",
  WSAEPROVIDERFAILEDINIT  , "Unable to initialize a service provider",
  WSASYSCALLFAILURE       , "System call failure",
  WSASERVICE_NOT_FOUND    , "Service not found",	    /* not sure */
  WSATYPE_NOT_FOUND       , "Class type not found",
  WSA_E_NO_MORE           , "No more resources available",  /* really not sure */
  WSA_E_CANCELLED         , "Operation already cancelled",  /* really not sure */
  WSAEREFUSED             , "Operation refused",	    /* not sure */
#endif

  WSAHOST_NOT_FOUND       , "Host not found",
  WSATRY_AGAIN            , "Authoritative host not found during name lookup",
  WSANO_RECOVERY          , "Non-recoverable error during name lookup",
  WSANO_DATA              , "Valid name, no data record of requested type",

  -1, NULL
};

char *
sys_strerror(int error_no)
{
  int i;
  static char unknown_msg[40];

  if (error_no >= 0 && error_no < sys_nerr)
    return sys_errlist[error_no];

  for (i = 0; _wsa_errlist[i].errnum >= 0; i++)
    if (_wsa_errlist[i].errnum == error_no)
      return _wsa_errlist[i].msg;

  sprintf(unknown_msg, "Unidentified error: %d", error_no);
  return unknown_msg;
}

/* [andrewi 3-May-96] I've had conflicting results using both methods,
   but I believe the method of keeping the socket handle separate (and
   insuring it is not inheritable) is the correct one. */

//#define SOCK_REPLACE_HANDLE

#ifdef SOCK_REPLACE_HANDLE
#define SOCK_HANDLE(fd) ((SOCKET) _get_osfhandle (fd))
#else
#define SOCK_HANDLE(fd) ((SOCKET) fd_info[fd].hnd)
#endif

int socket_to_fd (SOCKET s);

int
sys_socket(int af, int type, int protocol)
{
  SOCKET s;

  if (winsock_lib == NULL)
    {
      h_errno = ENETDOWN;
      return INVALID_SOCKET;
    }

  check_errno ();

  /* call the real socket function */
  s = pfn_socket (af, type, protocol);

  if (s != INVALID_SOCKET)
    return socket_to_fd (s);

  set_errno ();
  return -1;
}

/* Convert a SOCKET to a file descriptor.  */
int
socket_to_fd (SOCKET s)
{
  int fd;
  child_process * cp;

  /* Although under NT 3.5 _open_osfhandle will accept a socket
     handle, if opened with SO_OPENTYPE == SO_SYNCHRONOUS_NONALERT,
     that does not work under NT 3.1.  However, we can get the same
     effect by using a backdoor function to replace an existing
     descriptor handle with the one we want. */

  /* allocate a file descriptor (with appropriate flags) */
  fd = _open ("NUL:", _O_RDWR);
  if (fd >= 0)
    {
#ifdef SOCK_REPLACE_HANDLE
      /* now replace handle to NUL with our socket handle */
      CloseHandle ((HANDLE) _get_osfhandle (fd));
      _free_osfhnd (fd);
      _set_osfhnd (fd, s);
      /* setmode (fd, _O_BINARY); */
#else
      /* Make a non-inheritable copy of the socket handle.  Note
	 that it is possible that sockets aren't actually kernel
	 handles, which appears to be the case on Windows 9x when
	 the MS Proxy winsock client is installed.  */
      {
	/* Apparently there is a bug in NT 3.51 with some service
	   packs, which prevents using DuplicateHandle to make a
	   socket handle non-inheritable (causes WSACleanup to
	   hang).  The work-around is to use SetHandleInformation
	   instead if it is available and implemented. */
	if (pfn_SetHandleInformation)
	  {
	    pfn_SetHandleInformation ((HANDLE) s, HANDLE_FLAG_INHERIT, 0);
	  }
	else
	  {
	    HANDLE parent = GetCurrentProcess ();
	    HANDLE new_s = INVALID_HANDLE_VALUE;

	    if (DuplicateHandle (parent,
				 (HANDLE) s,
				 parent,
				 &new_s,
				 0,
				 FALSE,
				 DUPLICATE_SAME_ACCESS))
	      {
		/* It is possible that DuplicateHandle succeeds even
		   though the socket wasn't really a kernel handle,
		   because a real handle has the same value.  So
		   test whether the new handle really is a socket.  */
		long nonblocking = 0;
		if (pfn_ioctlsocket ((SOCKET) new_s, FIONBIO, &nonblocking) == 0)
		  {
		    pfn_closesocket (s);
		    s = (SOCKET) new_s;
		  }
		else
		  {
		    CloseHandle (new_s);
		  }
	      }
	  }
      }
      fd_info[fd].hnd = (HANDLE) s;
#endif

      /* set our own internal flags */
      fd_info[fd].flags = FILE_SOCKET | FILE_BINARY | FILE_READ | FILE_WRITE;

      cp = new_child ();
      if (cp)
	{
	  cp->fd = fd;
	  cp->status = STATUS_READ_ACKNOWLEDGED;

	  /* attach child_process to fd_info */
	  if (fd_info[ fd ].cp != NULL)
	    {
	      DebPrint (("sys_socket: fd_info[%d] apparently in use!\n", fd));
	      abort ();
	    }

	  fd_info[ fd ].cp = cp;

	  /* success! */
	  winsock_inuse++;	/* count open sockets */
	  return fd;
	}

      /* clean up */
      _close (fd);
    }
  pfn_closesocket (s);
  h_errno = EMFILE;
  return -1;
}


int
sys_bind (int s, const struct sockaddr * addr, int namelen)
{
  if (winsock_lib == NULL)
    {
      h_errno = ENOTSOCK;
      return SOCKET_ERROR;
    }

  check_errno ();
  if (fd_info[s].flags & FILE_SOCKET)
    {
      int rc = pfn_bind (SOCK_HANDLE (s), addr, namelen);
      if (rc == SOCKET_ERROR)
	set_errno ();
      return rc;
    }
  h_errno = ENOTSOCK;
  return SOCKET_ERROR;
}


int
sys_connect (int s, const struct sockaddr * name, int namelen)
{
  if (winsock_lib == NULL)
    {
      h_errno = ENOTSOCK;
      return SOCKET_ERROR;
    }

  check_errno ();
  if (fd_info[s].flags & FILE_SOCKET)
    {
      int rc = pfn_connect (SOCK_HANDLE (s), name, namelen);
      if (rc == SOCKET_ERROR)
	set_errno ();
      return rc;
    }
  h_errno = ENOTSOCK;
  return SOCKET_ERROR;
}

#ifdef MEADOW
int
sys_select_socket (int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout)
{
  int i, j, k, rc, cfds;
  pfn_fd_set *fds[3];
  fd_set *efds[3];

  if (winsock_lib == NULL)
    {
      h_errno = ENOTSOCK;
      return SOCKET_ERROR;
    }

  check_errno ();

  cfds = 0;
  efds[0] = readfds;
  efds[1] = writefds;
  efds[2] = exceptfds;

  for (i = 0 ; i < 3 ; i++)
    {
      fds[i] = NULL;
      if (efds[i])
	{
	  fds[i] = (pfn_fd_set*)alloca (sizeof (pfn_fd_set));
	  fds[i]->fd_count = 0;
	  for (j = 0 ; j < nfds ; j++)
	    if (FD_ISSET (j, efds[i]))
	      {
		if (! (fd_info[j].flags & FILE_SOCKET))
		  {
		    h_errno = ENOTSOCK;
		    return SOCKET_ERROR;
		  }
	      
		fds[i]->fd_array[fds[i]->fd_count++] = SOCK_HANDLE (j);
		cfds++;
	      }
	  FD_ZERO (efds[i]);
	}
    }

  if (cfds == 0)
    return 0;

  rc = pfn_select (nfds, fds[0], fds[1], fds[2], timeout);

  if (rc > 0)
    for (i = 0 ; i < 3 ; i++)
      if (fds[i])
	for (j = 0 ; j < fds[i]->fd_count ; j++)
	  {
	    for (k = 0 ;
		 k < nfds && fds[i]->fd_array [j] != SOCK_HANDLE (k) ;
		 k++);
	    if (k < nfds)
	      FD_SET (k, efds[i]);
	  }

  if (rc == SOCKET_ERROR)
    set_errno ();
  return rc;
}
#endif
u_short
sys_htons (u_short hostshort)
{
  return (winsock_lib != NULL) ?
    pfn_htons (hostshort) : hostshort;
}

u_short
sys_ntohs (u_short netshort)
{
  return (winsock_lib != NULL) ?
    pfn_ntohs (netshort) : netshort;
}

unsigned long
sys_inet_addr (const char * cp)
{
  return (winsock_lib != NULL) ?
    pfn_inet_addr (cp) : INADDR_NONE;
}

int
sys_gethostname (char * name, int namelen)
{
  if (winsock_lib != NULL)
    return pfn_gethostname (name, namelen);

  if (namelen > MAX_COMPUTERNAME_LENGTH)
    return !GetComputerName (name, (DWORD *)&namelen);

  h_errno = EFAULT;
  return SOCKET_ERROR;
}

struct hostent *
sys_gethostbyname(const char * name)
{
  struct hostent * host;

  if (winsock_lib == NULL)
    {
      h_errno = ENETDOWN;
      return NULL;
    }

  check_errno ();
  host = pfn_gethostbyname (name);
  if (!host)
    set_errno ();
  return host;
}

struct servent *
sys_getservbyname(const char * name, const char * proto)
{
  struct servent * serv;

  if (winsock_lib == NULL)
    {
      h_errno = ENETDOWN;
      return NULL;
    }

  check_errno ();
  serv = pfn_getservbyname (name, proto);
  if (!serv)
    set_errno ();
  return serv;
}

int
sys_getpeername (int s, struct sockaddr *addr, int * namelen)
{
  if (winsock_lib == NULL)
    {
      h_errno = ENETDOWN;
      return SOCKET_ERROR;
    }

  check_errno ();
  if (fd_info[s].flags & FILE_SOCKET)
    {
      int rc = pfn_getpeername (SOCK_HANDLE (s), addr, namelen);
      if (rc == SOCKET_ERROR)
	set_errno ();
      return rc;
    }
  h_errno = ENOTSOCK;
  return SOCKET_ERROR;
}


int
sys_shutdown (int s, int how)
{
  if (winsock_lib == NULL)
    {
      h_errno = ENETDOWN;
      return SOCKET_ERROR;
    }

  check_errno ();
  if (fd_info[s].flags & FILE_SOCKET)
    {
      int rc = pfn_shutdown (SOCK_HANDLE (s), how);
      if (rc == SOCKET_ERROR)
	set_errno ();
      return rc;
    }
  h_errno = ENOTSOCK;
  return SOCKET_ERROR;
}

int
sys_setsockopt (int s, int level, int optname, const void * optval, int optlen)
{
  if (winsock_lib == NULL)
    {
      h_errno = ENETDOWN;
      return SOCKET_ERROR;
    }

  check_errno ();
  if (fd_info[s].flags & FILE_SOCKET)
    {
      int rc = pfn_setsockopt (SOCK_HANDLE (s), level, optname,
			       (const char *)optval, optlen);
      if (rc == SOCKET_ERROR)
	set_errno ();
      return rc;
    }
  h_errno = ENOTSOCK;
  return SOCKET_ERROR;
}

#ifdef MEADOW
int
sys_getsockopt (int s, int level, int optname, char *optval, int *optlen)
{
  if (winsock_lib == NULL)
    {
      h_errno = ENETDOWN;
      return SOCKET_ERROR;
    }

  check_errno ();
  if (fd_info[s].flags & FILE_SOCKET)
    {
      int rc = pfn_getsockopt (SOCK_HANDLE (s),
			       level, optname, optval, optlen);
      if (rc == SOCKET_ERROR)
	set_errno ();
      return rc;
    }
  h_errno = ENOTSOCK;
  return SOCKET_ERROR;
}
#endif

int
sys_listen (int s, int backlog)
{
  if (winsock_lib == NULL)
    {
      h_errno = ENETDOWN;
      return SOCKET_ERROR;
    }

  check_errno ();
  if (fd_info[s].flags & FILE_SOCKET)
    {
      int rc = pfn_listen (SOCK_HANDLE (s), backlog);
      if (rc == SOCKET_ERROR)
	set_errno ();
      else
	fd_info[s].flags |= FILE_LISTEN;
      return rc;
    }
  h_errno = ENOTSOCK;
  return SOCKET_ERROR;
}

int
sys_getsockname (int s, struct sockaddr * name, int * namelen)
{
  if (winsock_lib == NULL)
    {
      h_errno = ENETDOWN;
      return SOCKET_ERROR;
    }

  check_errno ();
  if (fd_info[s].flags & FILE_SOCKET)
    {
      int rc = pfn_getsockname (SOCK_HANDLE (s), name, namelen);
      if (rc == SOCKET_ERROR)
	set_errno ();
      return rc;
    }
  h_errno = ENOTSOCK;
  return SOCKET_ERROR;
}

int
sys_accept (int s, struct sockaddr * addr, int * addrlen)
{
  if (winsock_lib == NULL)
    {
      h_errno = ENETDOWN;
      return -1;
    }

  check_errno ();
  if (fd_info[s].flags & FILE_LISTEN)
    {
      SOCKET t = pfn_accept (SOCK_HANDLE (s), addr, addrlen);
      int fd = -1;
      if (t == INVALID_SOCKET)
	set_errno ();
      else
	fd = socket_to_fd (t);

      fd_info[s].cp->status = STATUS_READ_ACKNOWLEDGED;
      ResetEvent (fd_info[s].cp->char_avail);
      return fd;
    }
  h_errno = ENOTSOCK;
  return -1;
}

int
sys_recvfrom (int s, char * buf, int len, int flags,
	  struct sockaddr * from, int * fromlen)
{
  if (winsock_lib == NULL)
    {
      h_errno = ENETDOWN;
      return SOCKET_ERROR;
    }

  check_errno ();
  if (fd_info[s].flags & FILE_SOCKET)
    {
      int rc = pfn_recvfrom (SOCK_HANDLE (s), buf, len, flags, from, fromlen);
      if (rc == SOCKET_ERROR)
	set_errno ();
      return rc;
    }
  h_errno = ENOTSOCK;
  return SOCKET_ERROR;
}

int
sys_sendto (int s, const char * buf, int len, int flags,
	    const struct sockaddr * to, int tolen)
{
  if (winsock_lib == NULL)
    {
      h_errno = ENETDOWN;
      return SOCKET_ERROR;
    }

  check_errno ();
  if (fd_info[s].flags & FILE_SOCKET)
    {
      int rc = pfn_sendto (SOCK_HANDLE (s), buf, len, flags, to, tolen);
      if (rc == SOCKET_ERROR)
	set_errno ();
      return rc;
    }
  h_errno = ENOTSOCK;
  return SOCKET_ERROR;
}

/* Windows does not have an fcntl function.  Provide an implementation
   solely for making sockets non-blocking.  */
int
fcntl (int s, int cmd, int options)
{
  if (winsock_lib == NULL)
    {
      h_errno = ENETDOWN;
      return -1;
    }

  check_errno ();
  if (fd_info[s].flags & FILE_SOCKET)
    {
      if (cmd == F_SETFL && options == O_NDELAY)
	{
	  unsigned long nblock = 1;
	  int rc = pfn_ioctlsocket (SOCK_HANDLE (s), FIONBIO, &nblock);
	  if (rc == SOCKET_ERROR)
	    set_errno();
	  /* Keep track of the fact that we set this to non-blocking.  */
	  fd_info[s].flags |= FILE_NDELAY;
	  return rc;
	}
      else
	{
	  h_errno = EINVAL;
	  return SOCKET_ERROR;
	}
    }
  h_errno = ENOTSOCK;
  return SOCKET_ERROR;
}

#endif /* HAVE_SOCKETS */


/* Shadow main io functions: we need to handle pipes and sockets more
   intelligently, and implement non-blocking mode as well. */

int
sys_close (int fd)
{
  int rc;

  if (fd < 0)
    {
      errno = EBADF;
      return -1;
    }

  if (fd < MAXDESC && fd_info[fd].cp)
    {
      child_process * cp = fd_info[fd].cp;

      fd_info[fd].cp = NULL;

      if (CHILD_ACTIVE (cp))
        {
	  /* if last descriptor to active child_process then cleanup */
	  int i;
	  for (i = 0; i < MAXDESC; i++)
	    {
	      if (i == fd)
		continue;
	      if (fd_info[i].cp == cp)
		break;
	    }
	  if (i == MAXDESC)
	    {
#ifdef HAVE_SOCKETS
	      if (fd_info[fd].flags & FILE_SOCKET)
		{
#ifndef SOCK_REPLACE_HANDLE
		  if (winsock_lib == NULL) abort ();

		  pfn_shutdown (SOCK_HANDLE (fd), 2);
		  rc = pfn_closesocket (SOCK_HANDLE (fd));
#endif
		  winsock_inuse--; /* count open sockets */
		}
#endif
	      delete_child (cp);
	    }
	}
    }

  /* Note that sockets do not need special treatment here (at least on
     NT and Windows 95 using the standard tcp/ip stacks) - it appears that
     closesocket is equivalent to CloseHandle, which is to be expected
     because socket handles are fully fledged kernel handles. */
  rc = _close (fd);

  if (rc == 0 && fd < MAXDESC)
    fd_info[fd].flags = 0;

  return rc;
}

int
sys_dup (int fd)
{
  int new_fd;

  new_fd = _dup (fd);
  if (new_fd >= 0 && new_fd < MAXDESC)
    {
      /* duplicate our internal info as well */
      fd_info[new_fd] = fd_info[fd];
    }
  return new_fd;
}


int
sys_dup2 (int src, int dst)
{
  int rc;

  if (dst < 0 || dst >= MAXDESC)
    {
      errno = EBADF;
      return -1;
    }

  /* make sure we close the destination first if it's a pipe or socket */
  if (src != dst && fd_info[dst].flags != 0)
    sys_close (dst);

  rc = _dup2 (src, dst);
  if (rc == 0)
    {
      /* duplicate our internal info as well */
      fd_info[dst] = fd_info[src];
    }
  return rc;
}

/* Unix pipe() has only one arg */
int
sys_pipe (int * phandles)
{
  int rc;
  unsigned flags;

  /* make pipe handles non-inheritable; when we spawn a child, we
     replace the relevant handle with an inheritable one.  Also put
     pipes into binary mode; we will do text mode translation ourselves
     if required.  */
  rc = _pipe (phandles, 0, _O_NOINHERIT | _O_BINARY);

  if (rc == 0)
    {
      /* Protect against overflow, since Windows can open more handles than
	 our fd_info array has room for.  */
      if (phandles[0] >= MAXDESC || phandles[1] >= MAXDESC)
	{
	  _close (phandles[0]);
	  _close (phandles[1]);
	  rc = -1;
	}
      else
	{
	  flags = FILE_PIPE | FILE_READ | FILE_BINARY;
	  fd_info[phandles[0]].flags = flags;

	  flags = FILE_PIPE | FILE_WRITE | FILE_BINARY;
	  fd_info[phandles[1]].flags = flags;
	}
    }

  return rc;
}

/* From ntproc.c */
extern int w32_pipe_read_delay;

/* Function to do blocking read of one byte, needed to implement
   select.  It is only allowed on sockets and pipes. */
int
_sys_read_ahead (int fd)
{
  child_process * cp;
  int rc;

  if (fd < 0 || fd >= MAXDESC)
    return STATUS_READ_ERROR;

  cp = fd_info[fd].cp;

  if (cp == NULL || cp->fd != fd || cp->status != STATUS_READ_READY)
    return STATUS_READ_ERROR;

  if ((fd_info[fd].flags & (FILE_PIPE | FILE_SOCKET)) == 0
      || (fd_info[fd].flags & FILE_READ) == 0)
    {
      DebPrint (("_sys_read_ahead: internal error: fd %d is not a pipe or socket!\n", fd));
      abort ();
    }

  cp->status = STATUS_READ_IN_PROGRESS;

  if (fd_info[fd].flags & FILE_PIPE)
    {
#ifdef MEADOW
      DWORD numbytes;
      rc = ReadFile((HANDLE)_get_osfhandle(fd),
		    &cp->chr, sizeof(char), &numbytes, NULL);

      if (rc && (numbytes == sizeof(char))) rc = sizeof(char);
      else rc = 0;
#else
      rc = _read (fd, &cp->chr, sizeof (char));
#endif

      /* Give subprocess time to buffer some more output for us before
	 reporting that input is available; we need this because Windows 95
	 connects DOS programs to pipes by making the pipe appear to be
	 the normal console stdout - as a result most DOS programs will
	 write to stdout without buffering, ie.  one character at a
	 time.  Even some W32 programs do this - "dir" in a command
	 shell on NT is very slow if we don't do this. */
      if (rc > 0)
	{
	  int wait = w32_pipe_read_delay;

	  if (wait > 0)
	    Sleep (wait);
	  else if (wait < 0)
	    while (++wait <= 0)
	      /* Yield remainder of our time slice, effectively giving a
		 temporary priority boost to the child process. */
	      Sleep (0);
	}
    }
#ifdef HAVE_SOCKETS
  else if (fd_info[fd].flags & FILE_SOCKET)
    {
      unsigned long nblock = 0;
      /* We always want this to block, so temporarily disable NDELAY.  */
      if (fd_info[fd].flags & FILE_NDELAY)
	pfn_ioctlsocket (SOCK_HANDLE (fd), FIONBIO, &nblock);

      rc = pfn_recv (SOCK_HANDLE (fd), &cp->chr, sizeof (char), 0);

      if (fd_info[fd].flags & FILE_NDELAY)
	{
	  nblock = 1;
	  pfn_ioctlsocket (SOCK_HANDLE (fd), FIONBIO, &nblock);
	}
    }
#endif

  if (rc == sizeof (char))
    cp->status = STATUS_READ_SUCCEEDED;
  else
    cp->status = STATUS_READ_FAILED;

  return cp->status;
}

int
_sys_wait_accept (int fd)
{
  HANDLE hEv;
  child_process * cp;
  int rc;

  if (fd < 0 || fd >= MAXDESC)
    return STATUS_READ_ERROR;

  cp = fd_info[fd].cp;

  if (cp == NULL || cp->fd != fd || cp->status != STATUS_READ_READY)
    return STATUS_READ_ERROR;

  cp->status = STATUS_READ_FAILED;

  hEv = pfn_WSACreateEvent ();
  rc = pfn_WSAEventSelect (SOCK_HANDLE (fd), hEv, FD_ACCEPT);
  if (rc != SOCKET_ERROR)
    {
      rc = WaitForSingleObject (hEv, INFINITE);
      pfn_WSAEventSelect (SOCK_HANDLE (fd), NULL, 0);
      if (rc == WAIT_OBJECT_0)
	cp->status = STATUS_READ_SUCCEEDED;
    }
  pfn_WSACloseEvent (hEv);

  return cp->status;
}

int
sys_read (int fd, char * buffer, unsigned int count)
{
  int nchars;
  int to_read;
  DWORD waiting;
  char * orig_buffer = buffer;

  if (fd < 0)
    {
      errno = EBADF;
      return -1;
    }

  if (fd < MAXDESC && fd_info[fd].flags & (FILE_PIPE | FILE_SOCKET))
    {
      child_process *cp = fd_info[fd].cp;

      if ((fd_info[fd].flags & FILE_READ) == 0)
        {
	  errno = EBADF;
	  return -1;
	}

      nchars = 0;

      /* re-read CR carried over from last read */
      if (fd_info[fd].flags & FILE_LAST_CR)
	{
	  if (fd_info[fd].flags & FILE_BINARY) abort ();
	  *buffer++ = 0x0d;
	  count--;
	  nchars++;
	  fd_info[fd].flags &= ~FILE_LAST_CR;
	}

      /* presence of a child_process structure means we are operating in
	 non-blocking mode - otherwise we just call _read directly.
	 Note that the child_process structure might be missing because
	 reap_subprocess has been called; in this case the pipe is
	 already broken, so calling _read on it is okay. */
      if (cp)
        {
	  int current_status = cp->status;

	  switch (current_status)
	    {
	    case STATUS_READ_FAILED:
	    case STATUS_READ_ERROR:
	      /* report normal EOF if nothing in buffer */
	      if (nchars <= 0)
		fd_info[fd].flags |= FILE_AT_EOF;
	      return nchars;

	    case STATUS_READ_READY:
	    case STATUS_READ_IN_PROGRESS:
	      DebPrint (("sys_read called when read is in progress\n"));
	      errno = EWOULDBLOCK;
	      return -1;

	    case STATUS_READ_SUCCEEDED:
	      /* consume read-ahead char */
	      *buffer++ = cp->chr;
	      count--;
	      nchars++;
	      cp->status = STATUS_READ_ACKNOWLEDGED;
	      ResetEvent (cp->char_avail);

	    case STATUS_READ_ACKNOWLEDGED:
	      break;

	    default:
	      DebPrint (("sys_read: bad status %d\n", current_status));
	      errno = EBADF;
	      return -1;
	    }

	  if (fd_info[fd].flags & FILE_PIPE)
	    {
	      PeekNamedPipe ((HANDLE) _get_osfhandle (fd), NULL, 0, NULL, &waiting, NULL);
	      to_read = min (waiting, (DWORD) count);

	      if (to_read > 0)
		nchars += _read (fd, buffer, to_read);
	    }
#ifdef HAVE_SOCKETS
	  else /* FILE_SOCKET */
	    {
	      if (winsock_lib == NULL) abort ();

	      /* do the equivalent of a non-blocking read */
	      pfn_ioctlsocket (SOCK_HANDLE (fd), FIONREAD, &waiting);
	      if (waiting == 0 && nchars == 0)
	        {
		  h_errno = errno = EWOULDBLOCK;
		  return -1;
		}

	      if (waiting)
	        {
		  /* always use binary mode for sockets */
		  int res = pfn_recv (SOCK_HANDLE (fd), buffer, count, 0);
		  if (res == SOCKET_ERROR)
		    {
		      DebPrint(("sys_read.recv failed with error %d on socket %ld\n",
				pfn_WSAGetLastError (), SOCK_HANDLE (fd)));
		      set_errno ();
		      return -1;
		    }
		  nchars += res;
		}
	    }
#endif
	}
      else
	{
	  int nread = _read (fd, buffer, count);
	  if (nread >= 0)
	    nchars += nread;
	  else if (nchars == 0)
	    nchars = nread;
	}

      if (nchars <= 0)
	fd_info[fd].flags |= FILE_AT_EOF;
#ifndef MEADOW
      /* Perform text mode translation if required.  */
      else if ((fd_info[fd].flags & FILE_BINARY) == 0)
	{
	  nchars = crlf_to_lf (nchars, orig_buffer);
	  /* If buffer contains only CR, return that.  To be absolutely
	     sure we should attempt to read the next char, but in
	     practice a CR to be followed by LF would not appear by
	     itself in the buffer.  */
	  if (nchars > 1 && orig_buffer[nchars - 1] == 0x0d)
	    {
	      fd_info[fd].flags |= FILE_LAST_CR;
	      nchars--;
	    }
	}
#endif
    }
  else
    nchars = _read (fd, buffer, count);

  return nchars;
}

/* For now, don't bother with a non-blocking mode */
int
sys_write (int fd, const void * buffer, unsigned int count)
{
  int nchars;

  if (fd < 0)
    {
      errno = EBADF;
      return -1;
    }

  if (fd < MAXDESC && fd_info[fd].flags & (FILE_PIPE | FILE_SOCKET))
    {
      if ((fd_info[fd].flags & FILE_WRITE) == 0)
	{
	  errno = EBADF;
	  return -1;
	}

      /* Perform text mode translation if required.  */
      if ((fd_info[fd].flags & FILE_BINARY) == 0)
	{
	  char * tmpbuf = alloca (count * 2);
	  unsigned char * src = (void *)buffer;
	  unsigned char * dst = tmpbuf;
	  int nbytes = count;

	  while (1)
	    {
	      unsigned char *next;
	      /* copy next line or remaining bytes */
	      next = _memccpy (dst, src, '\n', nbytes);
	      if (next)
		{
		  /* copied one line ending with '\n' */
		  int copied = next - dst;
		  nbytes -= copied;
		  src += copied;
		  /* insert '\r' before '\n' */
		  next[-1] = '\r';
		  next[0] = '\n';
		  dst = next + 1;
		  count++;
		}
	      else
		/* copied remaining partial line -> now finished */
		break;
	    }
	  buffer = tmpbuf;
	}
    }

#ifdef HAVE_SOCKETS
  if (fd < MAXDESC && fd_info[fd].flags & FILE_SOCKET)
    {
      unsigned long nblock = 0;
      if (winsock_lib == NULL) abort ();

      /* TODO: implement select() properly so non-blocking I/O works. */
      /* For now, make sure the write blocks.  */
      if (fd_info[fd].flags & FILE_NDELAY)
	pfn_ioctlsocket (SOCK_HANDLE (fd), FIONBIO, &nblock);

      nchars =  pfn_send (SOCK_HANDLE (fd), buffer, count, 0);

      /* Set the socket back to non-blocking if it was before,
	 for other operations that support it.  */
      if (fd_info[fd].flags & FILE_NDELAY)
	{
	  nblock = 1;
	  pfn_ioctlsocket (SOCK_HANDLE (fd), FIONBIO, &nblock);
	}

      if (nchars == SOCKET_ERROR)
        {
	  DebPrint(("sys_write.send failed with error %d on socket %ld\n",
		    pfn_WSAGetLastError (), SOCK_HANDLE (fd)));
	  set_errno ();
	}
    }
  else
#endif
    nchars = _write (fd, buffer, count);

  return nchars;
}

static void
check_windows_init_file ()
{
  extern int noninteractive, inhibit_window_system;

  /* A common indication that Emacs is not installed properly is when
     it cannot find the Windows installation file.  If this file does
     not exist in the expected place, tell the user.  */

  if (!noninteractive && !inhibit_window_system)
    {
      extern Lisp_Object Vwindow_system, Vload_path, Qfile_exists_p;
      Lisp_Object objs[2];
      Lisp_Object full_load_path;
      Lisp_Object init_file;
      int fd;

      objs[0] = Vload_path;
      objs[1] = decode_env_path (0, (getenv ("EMACSLOADPATH")));
      full_load_path = Fappend (2, objs);
      init_file = build_string ("term/w32-win");
      fd = openp (full_load_path, init_file, Fget_load_suffixes (), NULL, Qnil);
      if (fd < 0)
	{
	  Lisp_Object load_path_print = Fprin1_to_string (full_load_path, Qnil);
	  char *init_file_name = SDATA (init_file);
	  char *load_path = SDATA (load_path_print);
	  char *buffer = alloca (1024
				 + strlen (init_file_name)
				 + strlen (load_path));

	  sprintf (buffer,
		   "The Emacs Windows initialization file \"%s.el\" "
		   "could not be found in your Emacs installation.  "
		   "Emacs checked the following directories for this file:\n"
		   "\n%s\n\n"
		   "When Emacs cannot find this file, it usually means that it "
		   "was not installed properly, or its distribution file was "
		   "not unpacked properly.\nSee the README.W32 file in the "
		   "top-level Emacs directory for more information.",
		   init_file_name, load_path);
	  MessageBox (NULL,
		      buffer,
		      "Emacs Abort Dialog",
		      MB_OK | MB_ICONEXCLAMATION | MB_TASKMODAL);
      /* Use the low-level Emacs abort. */
#undef abort
	  abort ();
	}
      else
	{
	  _close (fd);
	}
    }
}

void
term_ntproc ()
{
#ifdef HAVE_SOCKETS
  /* shutdown the socket interface if necessary */
  term_winsock ();
#endif

  term_w32select ();
}

void
init_ntproc ()
{
#ifdef HAVE_SOCKETS
  /* Initialise the socket interface now if available and requested by
     the user by defining PRELOAD_WINSOCK; otherwise loading will be
     delayed until open-network-stream is called (w32-has-winsock can
     also be used to dynamically load or reload winsock).

     Conveniently, init_environment is called before us, so
     PRELOAD_WINSOCK can be set in the registry. */

  /* Always initialize this correctly. */
  winsock_lib = NULL;

  if (getenv ("PRELOAD_WINSOCK") != NULL)
    init_winsock (TRUE);
#endif

  /* Initial preparation for subprocess support: replace our standard
     handles with non-inheritable versions. */
  {
    HANDLE parent;
    HANDLE stdin_save =  INVALID_HANDLE_VALUE;
    HANDLE stdout_save = INVALID_HANDLE_VALUE;
    HANDLE stderr_save = INVALID_HANDLE_VALUE;

    parent = GetCurrentProcess ();

    /* ignore errors when duplicating and closing; typically the
       handles will be invalid when running as a gui program. */
    DuplicateHandle (parent,
		     GetStdHandle (STD_INPUT_HANDLE),
		     parent,
		     &stdin_save,
		     0,
		     FALSE,
		     DUPLICATE_SAME_ACCESS);

    DuplicateHandle (parent,
		     GetStdHandle (STD_OUTPUT_HANDLE),
		     parent,
		     &stdout_save,
		     0,
		     FALSE,
		     DUPLICATE_SAME_ACCESS);

    DuplicateHandle (parent,
		     GetStdHandle (STD_ERROR_HANDLE),
		     parent,
		     &stderr_save,
		     0,
		     FALSE,
		     DUPLICATE_SAME_ACCESS);

    fclose (stdin);
    fclose (stdout);
    fclose (stderr);

    if (stdin_save != INVALID_HANDLE_VALUE)
      _open_osfhandle ((long) stdin_save, O_TEXT);
    else
      _open ("nul", O_TEXT | O_NOINHERIT | O_RDONLY);
    _fdopen (0, "r");

    if (stdout_save != INVALID_HANDLE_VALUE)
      _open_osfhandle ((long) stdout_save, O_TEXT);
    else
      _open ("nul", O_TEXT | O_NOINHERIT | O_WRONLY);
    _fdopen (1, "w");

    if (stderr_save != INVALID_HANDLE_VALUE)
      _open_osfhandle ((long) stderr_save, O_TEXT);
    else
      _open ("nul", O_TEXT | O_NOINHERIT | O_WRONLY);
    _fdopen (2, "w");
  }

  /* unfortunately, atexit depends on implementation of malloc */
  /* atexit (term_ntproc); */
  signal (SIGABRT, term_ntproc);

  /* determine which drives are fixed, for GetCachedVolumeInformation */
  {
    /* GetDriveType must have trailing backslash. */
    char drive[] = "A:\\";

    /* Loop over all possible drive letters */
    while (*drive <= 'Z')
    {
      /* Record if this drive letter refers to a fixed drive. */
      fixed_drives[DRIVE_INDEX (*drive)] =
	(GetDriveType (drive) == DRIVE_FIXED);

      (*drive)++;
    }

    /* Reset the volume info cache.  */
    volume_cache = NULL;
  }

  /* Check to see if Emacs has been installed correctly.  */
  check_windows_init_file ();
}

/*
        shutdown_handler ensures that buffers' autosave files are
	up to date when the user logs off, or the system shuts down.
*/
BOOL WINAPI shutdown_handler(DWORD type)
{
  /* Ctrl-C and Ctrl-Break are already suppressed, so don't handle them.  */
  if (type == CTRL_CLOSE_EVENT        /* User closes console window.  */
      || type == CTRL_LOGOFF_EVENT    /* User logs off.  */
      || type == CTRL_SHUTDOWN_EVENT) /* User shutsdown.  */
    {
      /* Shut down cleanly, making sure autosave files are up to date.  */
      shut_down_emacs (0, 0, Qnil);
    }

  /* Allow other handlers to handle this signal.  */
  return FALSE;
}

Lisp_Object Vexec_suffix_list;

void setup_exec_suffix_list()
{
  static char exec_suffix[] = EXEC_SUFFIXES;
  Lisp_Object slot;
  char *s, *e;
  int bytes;
  int len = strlen(exec_suffix);

  Vexec_suffix_list = Qnil;
  s = exec_suffix;
  do {
    e = strchr (s, ':');
    if (!e) e = &exec_suffix[len];

    bytes = e - s;
    if (bytes > 0)
      {
	slot = make_string (s, bytes);
	Vexec_suffix_list = Fcons (slot, Vexec_suffix_list);
      }
    s = e + 1;
  }while (*e);

  Vexec_suffix_list = Fnreverse (Vexec_suffix_list);

  return;
}


DEFUN ("dos-to-unix-filename", Fdos_to_unix_filename, Sdos_to_unix_filename,
       1, 1, 0,
       doc: /* Return filename in unix form. To put it concretely, make '\\' in a
filename into '/'.(indestructively)  */)
     (filename)
     Lisp_Object filename;
{
  int size;
  char filetmp[FILENAME_MAX];

  CHECK_STRING (filename);
  size = SBYTES (filename);
  memcpy (filetmp, SDATA (filename), size + 1);

  encode_filename (filetmp);
  dostounix_filename (filetmp);
  decode_filename (filetmp);

  return make_string (filetmp, size);
}

DEFUN ("unix-to-dos-filename", Funix_to_dos_filename, Sunix_to_dos_filename,
       1, 1, 0,
       doc: /* Return filename in dos form. To put it concretely, make '/' in a
filename into '\\'.(indestructively).   */)
     (filename)
     Lisp_Object filename;
{
  int size;
  char filetmp[FILENAME_MAX];

  CHECK_STRING (filename);
  size = SBYTES (filename);
  memcpy (filetmp, SDATA (filename), size + 1);

  encode_filename (filetmp);
  unixtodos_filename (filetmp);
  decode_filename (filetmp);

  return make_string (filetmp, size);
}

DEFUN ("unix-to-dos-argument", Funix_to_dos_argument,
       Sunix_to_dos_argument,
       5, 5, 0,
       doc: /* Return argument in dos form.  */)
     (filename, ep, h2sp, qp, s2isp)
     Lisp_Object filename, ep, h2sp, qp, s2isp;
{
  char *filetmp;

  CHECK_STRING (filename);

  filetmp = (char *) alloca (SBYTES (filename) + 1);
  if (!filetmp)
    {
      error ("Cannot allocate memory!.");
    }
  memcpy (filetmp, SDATA (filename), SBYTES (filename) + 1);
  unixtodos_argument (filetmp, !NILP (ep), !NILP (h2sp),
		      !NILP (qp), !NILP (s2isp));

  return build_string (filetmp);

}

DEFUN ("w32-sfn-to-lfn", Fw32_sfn_to_lfn, Sw32_sfn_to_lfn, 1, 1, 0,
       doc: /* Convert short filename(8.3) to long filename.
If system fails to retrieve a long filename, return nil.  */)
     (sfn)
     Lisp_Object sfn;
{
  HANDLE hff;
  WIN32_FIND_DATA w32FindData;
  Lisp_Object directory, fullpath;
  struct gcpro gcpro1, gcpro2;

  CHECK_STRING (sfn);
  GCPRO2 (directory, fullpath);

  fullpath = Fencode_coding_string (Fexpand_file_name (sfn, Qnil),
				    Vfile_name_coding_system, Qt);
  directory = Ffile_name_directory (fullpath);
  hff = FindFirstFile (SDATA (fullpath), &w32FindData);
  if (hff == INVALID_HANDLE_VALUE)
    {
      return Qnil;
    }
  fullpath =
    Fexpand_file_name (Fdecode_coding_string
		       (build_string (w32FindData.cFileName),
			Vfile_name_coding_system, Qt),
		       directory);
  FindClose (hff);

  UNGCPRO;
  return fullpath;
}

DEFUN ("w32-lfn-to-sfn", Fw32_lfn_to_sfn, Sw32_lfn_to_sfn, 1, 1, 0,
       doc: /* Convert long filename to short filename(8.3).
If system fails to retrieve a short filename, return nil.  */)
     (lfn)
     Lisp_Object lfn;
{
#ifdef WIN32_VER4
  char buf[MAX_PATH+1];
  int  len;
  Lisp_Object fullpath;

  CHECK_STRING (lfn);
  fullpath = Fencode_coding_string (Fexpand_file_name (lfn, Qnil),
				    Vfile_name_coding_system, Qt);
  len = GetShortPathName (SDATA (fullpath), buf, MAX_PATH);
  if (!len)
    return Qnil;
  return Fdecode_coding_string (Fexpand_file_name (make_string (buf, len),
						   Qnil).
			       Vfile_name_coding_system, Qt);
#else
  return Qnil;
#endif
}

DEFUN ("w32-get-fqdn", Fw32_get_fqdn,
       Sw32_get_fqdn, 0, 0, 0,
       doc: /* Get the FQDN of this host.  */)
     ()
{
#define MAX_HOSTNAME 4096
#define RETRY_MAX 5
#define IS_HOSTNAME_FQDN(hostname) (strchr (hostname, '.'))

  char hostname[MAX_HOSTNAME];
  char **aliases;
  struct hostent *hp;
  int i;

  init_winsock (TRUE);
  sys_gethostname (hostname, MAX_HOSTNAME);
  if (!memchr (hostname, '\0', MAX_HOSTNAME))
    return Qnil;

  if (IS_HOSTNAME_FQDN (hostname))
    return build_string (hostname);

  for (i = 0;i < RETRY_MAX;i++)
    {
      hp = sys_gethostbyname (hostname);
      if (hp) break;
      if (TRY_AGAIN != pfn_WSAGetLastError())
	return Qnil;
    }

  if (IS_HOSTNAME_FQDN (hp->h_name))
    return build_string (hp->h_name);

  for (aliases = hp->h_aliases;*aliases;aliases++)
    {
      if (IS_HOSTNAME_FQDN (*aliases))
	return build_string (*aliases);
    }

  return Qnil;
}

DEFUN ("w32-find-executable", Fw32_find_executable,
       Sw32_find_executable, 1, 2, 0,
       doc: /* Find executable path that treat the specified FILE.
The optional argument DIRECTORY specify the default directory.
If the object executable is not found, return 'notfound.   */)
     (file, directory)
     Lisp_Object file, directory;
{
  unsigned char buf[MAX_PATH + 1];
  char *p;
  UINT ret;

  CHECK_STRING (file);
  if (NILP (directory)) p = NULL;
  else
    {
      CHECK_STRING (directory);
      directory = Fencode_coding_string (directory,
					 Vfile_name_coding_system,
					 Qt);
      p = SDATA (directory);
      unixtodos_filename (p);
    }

  file = Fencode_coding_string (file, Vfile_name_coding_system, Qt);

  ret = (UINT) FindExecutable (SDATA (file), p, buf);

  if (ret == ERROR_FILE_NOT_FOUND)
    error ("specified file:%s is not found", SDATA (file));
  else if (ret == ERROR_PATH_NOT_FOUND)
    error ("specified directory is not found", directory);
  else if (ret == ERROR_BAD_FORMAT)
    return intern ("badformat");
  else if (ret == 31)
    return intern ("notfound");
  else if (ret < 31)
    error ("unknown error");

  dostounix_filename (buf);

  return Fdecode_coding_string (build_string (buf),
				Vfile_name_coding_system, Qt);
}

void
syms_of_w32misc ()
{
  DEFVAR_LISP ("exec-suffix-list", &Vexec_suffix_list,
	       doc: /* *List of suffixes for executable.  */);
  setup_exec_suffix_list();

#ifdef MEADOW
  DEFVAR_LISP ("mw32-symlink-program", &Vmw32_symlink_program,
	       doc: /* Name of the external program for symlink.

Call the lisp function if this is a function, Call the external
program when string.  Lisp function is called with two arguments
OLDPATH and NEWPATH and should return nil if suceeded. On failure
sould return 'enoent, 'einval or 'eio. */);
  Vmw32_symlink_program = Qnil;

  DEFVAR_LISP ("mw32-symlink-hook", &Vmw32_symlink_hook,
	       doc: /* Hook function before symlink operation.
If this function returns nil, symlink() immediately returns successfully.
If returns t symlink() continues originally process. 
Also if returns 'enoent, 'einval, or 'eio symlink() immediately return
failure . Each symbol corresnponds to ENOENT, EINVAL, EIO of errno. */);

  Vmw32_symlink_hook = Qnil;
#endif

  defsubr (&Sdos_to_unix_filename);
  defsubr (&Sunix_to_dos_filename);
  defsubr (&Sunix_to_dos_argument);
  defsubr (&Sw32_sfn_to_lfn);
  defsubr (&Sw32_lfn_to_sfn);
  defsubr (&Sw32_get_fqdn);
  defsubr (&Sw32_find_executable);
}

/*
	globals_of_w32 is used to initialize those global variables that
	must always be initialized on startup even when the global variable
	initialized is non zero (see the function main in emacs.c).
*/
void
globals_of_w32 ()
{
  g_b_init_is_windows_9x = 0;
  g_b_init_open_process_token = 0;
  g_b_init_get_token_information = 0;
  g_b_init_lookup_account_sid = 0;
  g_b_init_get_sid_identifier_authority = 0;
  /* The following sets a handler for shutdown notifications for
     console apps. This actually applies to Emacs in both console and
     GUI modes, since we had to fool windows into thinking emacs is a
     console application to get console mode to work.  */
  SetConsoleCtrlHandler(shutdown_handler, TRUE);

#ifdef MEADOW
  InitializeCriticalSection (&critsec_message);
  InitializeCriticalSection (&critsec_access_event);
  interrupt_handle = CreateEvent(0, TRUE, FALSE, NULL);
#endif

#ifdef CANNOT_DUMP
  {
    extern char cd_lock_path[];
    extern char cd_load_path[];
    char *p;
    if (p = egetenv("EMACSLOCKDIR"))
      strcpy(cd_lock_path, p);
    else if (p = egetenv("TMP"))
      strcpy(cd_lock_path, p);
    else if (p = egetenv("TEMP"))
      strcpy(cd_lock_path, p);
    else
      GetWindowsDirectory(cd_lock_path, MAX_PATH);

    printf("path:%s\n", cd_load_path);
  }
#endif
}

/* end of w32.c */

/* arch-tag: 90442dd3-37be-482b-b272-ac752e3049f1
   (do not change this comment) */
