/*
 * Symbolic link utility for Windows Vista or later   --  meadow_symlink.c  --
 *
 *        Copyright (C) 2009 Kyotaro Horiguchi <horiguti@meaodwy.org>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either versions 3, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with meadow_symlink, see the file COPYING.  If not, write to
 * the Free Software Foundation Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/*
 * HISTORY
 * -------
 *
 *    17 May, 2009 : Version 1.0.0 - first release.
 */

/*
 * COMPILATION
 * -------
 * gcc -mno-cygwin -o meadow_symlink.exe meadow_symlink.c
 *
 */

#include <windows.h>
#include <limits.h>
#include <stdio.h>
#include <sys/stat.h>

typedef BOOLEAN (WINAPI *CREATESYMBOLICLINKPROC)(LPSTR, LPSTR, DWORD);
CREATESYMBOLICLINKPROC CreateSymbolicLink;


static void
normalize_filename (LPTSTR fp, TCHAR path_sep)
{
  LPTSTR n_fp;
  TCHAR next_char;

  if (lstrlen (fp) > MAX_PATH)
    return;

  if (*(CharNext (fp)) == ':' && *fp >= 'A' && *fp <= 'Z')
    {
      next_char = tolower (*fp);
      *fp = next_char;
      fp = CharNext (fp);
      fp = CharNext (fp);
    }

  for (; *fp; fp = n_fp)
    {
      n_fp = CharNext (fp);
      if (*fp == '/')
	*fp = path_sep;
    }
  
  return;
}

int
setup_api (void)
{
  HMODULE hKnl32;

  hKnl32 = GetModuleHandle ("KERNEL32.DLL");
  if (!hKnl32)
    {
      fprintf (stderr, "Kernel32.dll not found.\n");
      return 1;
    }
  
  CreateSymbolicLink =
    (CREATESYMBOLICLINKPROC)
    GetProcAddress (hKnl32, "CreateSymbolicLinkW");
  if (CreateSymbolicLink == NULL)
    {
      fprintf (stderr, "Symbolic link is unavailable on this system.\n");
      return 2;
    }

  return 0;
}

void
usage (void)
{
  puts ("meadow_symlink -- symlink utility for meadow\n\n"
	"Usage: meadow_symlink -d|-f <link target> <link name>\n"
	"  -d : link target is a directory\n"
	"  -f : link target is a regular file\n");
  exit(0);
}

int
main (int argc, char *argv[])
{
  struct stat st;
  char *oldpath = NULL;
  char *newpath = NULL; 
  char oldpath2[MAX_PATH];
  char newpath2[MAX_PATH];
  WCHAR oldpath3[MAX_PATH];
  WCHAR newpath3[MAX_PATH];
  int i, len, ret;
  int is_directory = 0;
  int verbose = 0;
  
  ret = setup_api();
  if (ret != 0) return ret;

  for (i = 1 ; i < argc ; i++)
    {
      if (argv[i][0] == '-')
	{
	  if (argv[i][1] == 'd') is_directory = 1;
	  else if (argv[i][1] == 'v') verbose = 1;
	  else if (argv[i][1] == 'h') usage ();
	}
      else
	{
	  if (oldpath == NULL)
	    oldpath = argv[i];
	  else if (newpath == NULL)
	    newpath = argv[i];
	}
    }

  if (oldpath == NULL)
    {
      fprintf(stderr, "Missing target name.\n");
      return 3;
    }

  if (newpath == NULL)
    {
      fprintf (stderr, "Missing link name.\n");
      return 4;
    }
	  
  strncpy (oldpath2, oldpath, MAX_PATH - 1);
  strncpy (newpath2, newpath, MAX_PATH - 1);
  normalize_filename (oldpath2, '\\');
  normalize_filename (newpath2, '\\');

  if (verbose)
    fprintf (stderr, "%s => %s\n", oldpath2, newpath2);

  len = MultiByteToWideChar (CP_ACP, 0, oldpath2, -1, oldpath3, MAX_PATH - 1);
  if (len == 0)
    {
      fprintf (stderr, "Failed to convert source path to widechar.\n");
      return 5;
    }
  oldpath3[len] = 0;

  len = MultiByteToWideChar (CP_ACP, 0, newpath2, -1, newpath3, MAX_PATH - 1);
  if (len == 0)
    {
      fprintf (stderr, "Failed to convert dest path to widechar.\n");
      return 6;
    }
  newpath3[len] = 0;

  if (CreateSymbolicLink ((LPSTR)newpath3, (LPSTR)oldpath3, is_directory) == 0)
    {
      int e = GetLastError ();

      if (e == 1314)
	{
	  fprintf (stderr,
		   "A required privilege is not held by the client.\n");
	  return 7;
	}

      fprintf (stderr, "Failed to create symbolic link : win32 error = %d\n",
	       e);
      return 8;
    }

  return 0;
}
