;;; mw32script.el
;;; Author: yamagus@kw.netlaputa.ne.jp (YAMAGUCHI, Shuhei)
;;; Modified by H.Miyashita.
;;; Version 1.2 (Feb 2, 1998)
;;;
;;; [USAGE]
;;; Add the following in your .emacs:
;;;  (require 'mw32script)
;;;  (mw32script-init)

(defconst mw32script-version "W32 Script version 1.2")
;; begin --- options
(defvar mw32script-argument-editing-alist
  '(("/sh$" . "sh.exe")
    ("/bash$" . "bash.exe")
    ("/perl$" . "perl.exe")
    ("/t?csh$" . "tcsh.exe")
    ("/ruby$" . "ruby.exe")
    ("/rubyw$" . "rubyw.exe")
    ("/env$" . "env.exe"))
  "Association list of script interpreter.")

(defvar mw32script-pathext '(".com" ".exe" ".bat" ".cmd")
  "Extention list of executables.")

(defvar mw32script-resolve-script t
  "If non-nil, mw32script-argument-editing-function
resolve the script association.")

(defvar mw32script-resolve-extention (fboundp 'Meadow-version)
  "If non-nil, mw32script-argument-editing-function
resolve the filename association.
This only works with Meadow version Alpha-3.00 or later.")

(defvar mw32script-recursive nil)
(defvar mw32script-original-file-executable-p nil)
(defvar mw32script-original-executable-find nil)
;; end --- options
(defvar mw32script-bufsiz 256)
(defvar mw32script-buffer-tmp " *mw32script*")
(defvar mw32script-pathext-regexp nil)

(defun mw32script-make-pathext-regexp ()
  (setq mw32script-pathext-regexp
	(concat "\\("
		(mapconcat
		 (lambda (x) (regexp-quote x))
		 mw32script-pathext "\\|")
		"\\)$")))

(defun mw32script-openp (command-name)
  "Locate the full path name of external-command COMMAND-NAME."
  (interactive "sExternal-command: ")
  (catch 'tag
    (let (path)
      (if (file-name-absolute-p command-name)
	  (if (and (file-executable-p command-name)
		   (null (file-directory-p command-name)))
	      (throw 'tag command-name)
	    (mapcar
	     (lambda (suf)
	       (setq path (expand-file-name (concat command-name suf)))
	       (and (file-executable-p path)
		    (null (file-directory-p path))
		    (throw 'tag path)))
	     (if (null mw32script-pathext)
		 '("")
	       mw32script-pathext)))
	(mapcar
	 (lambda (dir)
	   (mapcar
	    (lambda (suf)
	      (setq path (expand-file-name (concat command-name suf) dir))
	      (and (file-executable-p path)
		   (null (file-directory-p path))
		   (throw 'tag path)))
	    (if (null mw32script-pathext)
		'("")
	      (append (list "") mw32script-pathext))))
	 exec-path))) nil))


(defun mw32script-resolve-script (path &optional directory)
  "Find executable path that interprets the script specified PATH.
Return value is a list of arguments, and car of the list is argv[0].
The optional argument DIRECTORY specify the default directory.
If the object executable is not found, return nil."
  (interactive "fScript: ")
  (setq path
	(expand-file-name
	 (if directory
	     (concat (file-name-as-directory directory) path)
	   path)))
  (let ((buf (generate-new-buffer mw32script-buffer-tmp))
	limit args)
    (unwind-protect
	(save-excursion
	  (set-buffer buf)
	  (set-buffer-multibyte nil)
	  (condition-case nil
	      (progn
		(let ((coding-system-for-read 'raw-text))
		  (insert-file-contents path nil 0 mw32script-bufsiz))
		(goto-line 2)
		(setq limit (point))
		(goto-char 1)
		(if (re-search-forward
		     "\\`#![ \t]*\\([^ \t\n]+\\)[ \t]*" limit t)
		    (while
			(progn
			  (setq args
				(nconc args
				       (list
					(buffer-substring (match-beginning 1)
							  (match-end 1)))))
			  (re-search-forward "\\([^ \t\n]+\\)[ \t]*"
					     limit t))))
		args)
	    (file-error nil)))
      (kill-buffer buf))))


(defun mw32script-resolve-extention (path &optional directory)
  "Find executable path that associated with filename specified PATH.
Return value is a list of arguments, and car of the list is argv[0].
The optional argument DIRECTORY specify the default directory.
If the object executable is not found, return 'notfound."
  (interactive "fFile: ")
  (setq path
	(expand-file-name
	 (if directory
	     (concat (file-name-as-directory directory) path)
	   path)))
  (let (executable)
    (condition-case nil
	(progn
	  (setq executable (w32-find-executable path))
	  (if (eq executable 'notfound)
	      executable
	    (list executable)))
      (error nil))))


(defun mw32script-argument-editing-function (argument)
  "Resolv the script/filename association,
and do the argument editiong."
  (let ((argv0 (car argument)) sargs func ret)
    (if (string-match mw32script-pathext-regexp argv0)
	(funcall default-process-argument-editing-function argument)
      (and mw32script-resolve-extention
	   (setq sargs (mw32script-resolve-extention argv0)))
      (and mw32script-resolve-script
	   (or (not sargs) (eq sargs 'notfound))
	   (setq sargs (mw32script-resolve-script argv0)))
      (if (and sargs (not (eq sargs 'notfound)))
	  (progn
	    (setq argv0 (car sargs))
	    (catch 'tag
	      (mapcar
	       (lambda (pat)
		 (and (string-match (car pat) argv0)
		      (setq argv0 (mw32script-openp (cdr pat)))
		      (throw 'tag t)))
	       mw32script-argument-editing-alist))
	    (and (eq (setq func (find-process-argument-editing-function argv0))
		     (function mw32script-argument-editing-function))
		 (not mw32script-recursive)
		 (setq func default-process-argument-editing-function))
	    (if (consp (setq ret (funcall
				  func
				  (append (list argv0) (cdr sargs) argument))))
		ret
	      (cons argv0 ret)))
	(funcall default-process-argument-editing-function argument)))))

(defun mw32script-file-executable-p (filename)
  "Return t if filename can be executed by you.
For a directory, this means you can access files in that directory.

Add an analytical capability of the script file to this function by Meadow."
  (or (funcall mw32script-original-file-executable-p filename)
      (and (mw32script-resolve-script filename)
	   t)))

(defun mw32script-executable-find (command)
  "Search for command in `exec-path' and return the absolute file name.
Return nil if command is not found anywhere in `exec-path'.

Add an analytical capability of the script file to this function by Meadow."
  (catch 'detect
    (let ((paths exec-path)
	  (suffixes exec-suffixes)
	  cmds names path file)
      (while suffixes
	(setq cmds (cons (concat command (car suffixes)) cmds))
	(setq suffixes (cdr suffixes)))
      (setq cmds (nreverse cmds))
      (while (setq path (car paths))
	(setq paths (cdr paths))
	(setq names cmds)
	(while names
	  (setq file (locate-file-internal (car names) (list path)))
	  (when (and file
		     (not (file-directory-p file))
		     (mw32script-file-executable-p file))
	    (throw 'detect file))
	  (setq names (cdr names)))))))

(defun mw32script-init ()
  (interactive)
  (mw32script-make-pathext-regexp)
  (define-process-argument-editing
    ".*"
    (function mw32script-argument-editing-function) 'last)
  (add-to-list 'exec-suffix-list "")
  (unless mw32script-original-file-executable-p
    (setq mw32script-original-file-executable-p
	  (symbol-function 'file-executable-p))
    (fset 'file-executable-p 'mw32script-file-executable-p))
  (unless mw32script-original-executable-find
    (setq mw32script-original-executable-find
	  (symbol-function 'executable-find))
    (fset 'executable-find 'mw32script-executable-find)))

(provide 'mw32script)
