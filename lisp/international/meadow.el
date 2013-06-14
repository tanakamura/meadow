;; -*- mode: Emacs-Lisp; coding: iso-2022-7bit-unix -*-
;;
;;   Author H.Miyashita
;;
;;;;;

(defgroup Meadow nil
  "Meadow"
  :group 'emacs)

(defvar mw32-last-selection nil
  "It is stored the last data from Emacs.")

;;;
;;; image function
;;;

(defun init-image-library (type image-library-alist)
  (and (boundp 'image-types) (not (null (memq type image-types)))))

(defun display-images-p (&optional display)
  "Return non-nil if DISPLAY can display images.

DISPLAY can be a display name, a frame, or nil (meaning the selected
frame's display).

This function is overridden by Meadow."
  (and (display-graphic-p display)
       (fboundp 'image-mask-p)
       (fboundp 'image-size)))

(add-hook
 'before-init-hook
 (lambda ()
   ;; BMP support
   (require 'image)
   (require 'image-file)

   (or (rassq 'bmp image-type-header-regexps)
       (setq image-type-header-regexps
	     (cons (cons "\\`BM" 'bmp) image-type-header-regexps)))
   (or (member "bmp" image-file-name-extensions)
       (setq image-file-name-extensions
	     (cons "bmp" image-file-name-extensions)))

   ;; append extra extensions
   (mapcar
    (lambda (type)
      (unless (or (member type '("txt" "shtml" "html" "htm"))
		  (member type image-file-name-extensions))
	(setq image-file-name-extensions
	      (cons type image-file-name-extensions))))
    (mw32-get-image-magick-extensions))))

;;;
;;; overwrite appearances
;;;

;;(set-face-background 'modeline "LightBlue")

;; Highlighting is only shown after moving the mouse, while keyboard
;; input turns off the highlight even when the mouse is over the
;; clickable text.
(setq mouse-highlight 1)

;;;
;;; overwrite splash handling
;;;

(defvar mw32-splash-masked-p nil
  "If non-nil, show a splash screen of Meadow with a heuristic mask.")

(defun use-fancy-splash-screens-p ()
  "Return t if fancy splash screens should be used."
  (when (image-type-available-p 'bmp)
    (let* ((img (create-image (or fancy-splash-image
				  "meadow.bmp") 'bmp))
	   (image-height (and img (cdr (image-size img))))
	   (window-height (1- (window-height (selected-window)))))
      (> window-height (+ image-height 15)))))

(defun fancy-splash-head ()
  "Insert the head part of the splash screen into the current buffer."
  (let* ((image-file (or fancy-splash-image
			 "meadow.bmp"))
	 (img (create-image image-file
			    'bmp
			    nil :heuristic-mask mw32-splash-masked-p))
	 (image-width (and img (car (image-size img))))
	 (window-width (window-width (selected-window))))
    (when img
      (when (> window-width image-width)
	;; Center the image in the window.
	(let ((pos (/ (- window-width image-width) 2)))
	  (insert (propertize " " 'display `(space :align-to ,pos))))

	;; Insert the image with a help-echo and a keymap.
	(let ((map (make-sparse-keymap))
	      (help-echo "mouse-2: browse http://www.meadowy.org/"))
	  (define-key map [mouse-2]
	    (lambda ()
	      (interactive)
	      (browse-url "http://www.meadowy.org/")
	      (throw 'exit nil)))
	  (define-key map [down-mouse-2] 'ignore)
	  (define-key map [up-mouse-2] 'ignore)
	  (insert-image img (propertize "xxx" 'help-echo help-echo
					'keymap map)))
	(insert "\n"))))
  (insert "Meadow is based on GNU Emacs.\n")
  (if (eq system-type 'gnu/linux)
      (fancy-splash-insert
       :face '(variable-pitch :foreground "red")
       "GNU Emacs is one component of a Linux-based GNU system.")
    (fancy-splash-insert
     :face '(variable-pitch :foreground "red")
     "GNU Emacs is one component of the GNU operating system."))
  (insert "\n"))

(defun fancy-splash-tail ()
  "Insert the tail part of the splash screen into the current buffer."
  (let ((fg (if (eq (frame-parameter nil 'background-mode) 'dark)
		"cyan" "darkblue")))
    (fancy-splash-insert :face `(variable-pitch :foreground ,fg)
			 "\nThis is "
			 (Meadow-version)
			 "\n based on "
			 (emacs-version)
			 "\n"
			 :face '(variable-pitch :height 0.5)
			 "Copyright (C) 2001 Free Software Foundation, Inc.\n"
			 "Copyright (C) 1995-2001 MIYASHITA Hisashi\n"
			 "Copyright (C) 2002, 2003, 2004, 2005, 2006, 2007 The Meadow Team")))

;;;
;;; Meadow MW32-IME API.
;;;

(defvar mw32-ime-on-hook nil
  "Functions to eval when IME is turned on at least.
Even if IME state is not changed, these functiona are maybe called.")
(defvar mw32-ime-off-hook nil
  "Functions to eval when IME is turned off at least.
Even if IME state is not changed, these functiona are maybe called.")
(defvar mw32-ime-buffer-switch-p t
  "If this variable is nil, IME control when buffer is switched is disabled.")
(defvar mw32-ime-show-mode-line nil
  "When t, mode line indicates IME state.")
(defvar mw32-ime-mode-line-state-indicator "[O]"
  "This is shown at the mode line. It is regarded as state of ime.")
(make-variable-buffer-local 'mw32-ime-mode-line-state-indicator)
(put 'mw32-ime-mode-line-state-indicator 'permanent-local t)
(defvar mw32-ime-mode-line-state-indicator-list '("-" "[|]" "[O]")
  "List of IME state indicator string.")
(defvar mw32-ime-mode-line-format-original nil
  "Original mode line format.")
(defvar mw32-ime-cont-on 'always
"If not nil, IME is always on during current-input-method is MW32-IME
on the ordinary buffer.")
(defvar mw32-input-method-noconv-regexp nil
  "Regexp represents printable-chars that does not activate IME.")

(setq search-highlight t)

;;;
;;; Emulation functions.
;;;

;;
;; Section: General definitions
;;

(defvar w32-fiber-program-name "fiber.exe")
(defvar w32-fiber-process-name "*fiber*")

(defun wildcard-to-regexp (pattern)
  (let ((i 0)
	(len (length pattern))
	(quotestr "")
	(result "")
	char
	result)
    (while (< i len)
      (setq char (aref pattern i)
	    i (1+ i))
      (cond ((= char ?*)
	     (setq result (concat result (regexp-quote quotestr) ".*")
		   quotestr ""))
	    ((= char ??)
	     (setq result (concat result (regexp-quote quotestr) ".")
		   quotestr ""))
	    (t
	     (setq quotestr (concat quotestr (char-to-string char))))))
    (concat "\\`" result (regexp-quote quotestr) "\\'")))

;;
;; Section: Font
;;

(defun w32-list-fonts (pattern &optional face frame max)
  (setq pattern (wildcard-to-regexp pattern))
  (if (null max) (setq max 2000))
  (let ((curfl (w32-font-list))
	curfs
	result)
  (while (and (> max 0)
	      (setq curfs (car curfl)))
      (if (string-match pattern curfs)
	  (setq result (cons curfs result)
		max (1- max)))
      (setq curfl (cdr curfl)))
  result))

(defalias 'x-list-fonts 'w32-list-fonts)

;;
;; Section: X file dialog
;;

(defalias 'x-file-dialog 'mw32-file-dialog)

;;; Section: focus frame

(defalias 'w32-focus-frame 'x-focus-frame)

;;
;; Section: Shell execute
;;

(defun w32-shell-execute (operation document &optional parameters show-flag)
  (if (and show-flag
	  (not (numberp show-flag)))
      (error "show-flag must be number or nil:%S" show-flag))
  (let ((coding-system-for-write locale-coding-system)
	(args (append
	       (list document)
	       (list "-b" operation)
	       (list "-d" default-directory)
	       (if parameters
		   (list "-p" parameters))
	       (if show-flag
		   (list "-n" (number-to-string show-flag))))))
    (apply 'call-process w32-fiber-program-name nil 0 nil
	   args)))

;;
;; Section: IME
;;

;; This is temporal solution.  In the future, we will prepare
;; dynamic configuration.
(defvar mw32-ime-coding-system-language-environment-alist
  '(("Japanese" . japanese-shift-jis)
    ("Chinese-GB" . chinese-iso-8bit)
    ("Chinese-BIG5" . chinese-big5)
    ("Korean" . korean-iso-8bit)))

;; This is temporal solution.
(defvar mw32-locale-ime-alist
  '((("japanese" . 1041) . "MW32-IME")))
;;    (("korean-hangul" . 1042) . "MW32-IME")))

(defun mw32-set-ime-if-available ()
  (let ((ime (assoc (cons default-input-method (mw32-input-language-code))
		    mw32-locale-ime-alist)))
    (when (and ime (mw32-ime-available))
	(setq default-input-method (cdr ime))
	(mw32-ime-initialize)
	(define-key global-map [kanji] 'toggle-input-method))))

;;
;; IME state indicator
;;
(global-set-key [kanji] 'ignore)
(global-set-key [compend] 'ignore)

(defun wrap-function-to-control-ime
  (function interactive-p interactive-arg &optional suffix)
  "Wrap FUNCTION, and IME control is enabled when FUNCTION is called.
An original function is saved to FUNCTION-SUFFIX when suffix is string.
If SUFFIX is nil, \"-original\" is added. "
  (let ((original-function
	 (intern (concat (symbol-name function)
			 (if suffix suffix "-original")))))
    (cond
     ((not (fboundp original-function))
      (fset original-function
	    (symbol-function function))
      (fset function
	    (list
	     'lambda '(&rest arguments)
	     (when interactive-p
	       (list 'interactive interactive-arg))
	     (`(cond
		((and (fep-get-mode)
		      (equal current-input-method "MW32-IME"))
		 (fep-force-off)
		 (run-hooks 'mw32-ime-off-hook)
		 (unwind-protect
		     (apply '(, original-function) arguments)
		   (when (and (not (fep-get-mode))
			      (equal current-input-method "MW32-IME"))
		     (fep-force-on)
		     (run-hooks 'mw32-ime-on-hook))))
		(t
		 (apply '(, original-function)
			arguments))))))))))

(defvar mw32-ime-toroku-region-yomigana nil
  "* if this variable is string, toroku-region regard this value as yomigana.")

(defun mw32-ime-toroku-region (begin end)
  (interactive "r")
  (let ((string (buffer-substring begin end))
	(mw32-ime-buffer-switch-p nil)
	(reading mw32-ime-toroku-region-yomigana))
    (unless (stringp reading)
      (w32-set-ime-mode 'hiragana)
      (setq reading
	    (read-multilingual-string
	     (format "Input reading of \"%s\":" string) nil "MW32-IME")))
    (w32-ime-register-word-dialog reading string)))

;; for IME management system.

(defun mw32-ime-sync-state (window)
  (when (and mw32-ime-buffer-switch-p
	     this-command
	     (not mw32-ime-composition-window))
    (with-current-buffer (window-buffer window)
      (if (window-minibuffer-p)
	  (fep-force-off)
	(let* ((frame (window-frame window)))
	  (if (string= current-input-method "MW32-IME")
	      (run-hooks 'mw32-ime-on-hook)
	    (run-hooks 'mw32-ime-off-hook))
	  (if mw32-ime-cont-on
	      (if (string= current-input-method "MW32-IME")
		  (if (or (eq mw32-ime-cont-on 'always)
			  (eq input-method-function 'mw32-input-method))
		      (fep-force-on))
		(fep-force-off))))))))

(defun mw32-ime-set-selected-window-buffer-hook (oldbuf newwin newbuf)
  (mw32-ime-sync-state newwin))

(defun mw32-ime-select-window-hook (old new)
  (mw32-ime-sync-state new))

(defun mw32-ime-mode-line-update ()
  (cond
   (mw32-ime-show-mode-line
    (unless (window-minibuffer-p)
      (setq mw32-ime-mode-line-state-indicator
	    (nth (if (fep-get-mode) 1 2)
		 mw32-ime-mode-line-state-indicator-list))))
   (t
    (setq mw32-ime-mode-line-state-indicator
	  (nth 0 mw32-ime-mode-line-state-indicator-list))))
  (force-mode-line-update))

(defun mw32-ime-init-mode-line-display ()
  (when (and mw32-ime-show-mode-line
	     (not (member 'mw32-ime-mode-line-state-indicator
			  mode-line-format)))
    (setq mw32-ime-mode-line-format-original
	  (default-value 'mode-line-format))
    (if (and (stringp (car mode-line-format))
	     (string= (car mode-line-format) "-"))
	(setq-default mode-line-format
		      (cons ""
			    (cons 'mw32-ime-mode-line-state-indicator
				  (cdr mode-line-format))))
      (setq-default mode-line-format
		    (cons ""
			  (cons 'mw32-ime-mode-line-state-indicator
				mode-line-format))))
    (force-mode-line-update t)))

;;; mw32-ime-toggle and mw32-ime-initialize are obsolete.
;;; These functions left here for backward compatibility.
(defun mw32-ime-toggle ()
  "This is obsoleted function."
  (interactive)
  (if (equal current-input-method "MW32-IME")
      (inactivate-input-method)
    (activate-input-method "MW32-IME")))

(defun mw32-ime-initialize ()
  "Initialize MW32-IME. It is unnecessary to call this function explicitly."
  (cond
   ((and (eq system-type 'windows-nt)
	 (eq window-system 'w32)
	 (featurep 'meadow))
    (let ((coding-system
	   (assoc-string current-language-environment
			 mw32-ime-coding-system-language-environment-alist
			 t)))
      (unless default-input-method
	(setq default-input-method "MW32-IME"))
      (mw32-ime-init-mode-line-display)
      (mw32-ime-mode-line-update)
      (add-hook 'select-window-functions 'mw32-ime-select-window-hook)
      (add-hook 'set-selected-window-buffer-functions
		'mw32-ime-set-selected-window-buffer-hook)

      (add-hook 'isearch-mode-hook 'mw32-isearch-mode-hook-function)
      (defadvice isearch-toggle-input-method (after mw32-fep-off activate)
	"Deactivate fep when mw32-ime-cont-on is not nil."
	(if (eq mw32-ime-cont-on t) (fep-force-off)))
      (defadvice isearch-done (after mw32-fep-on activate)
	"Deactivate fep when mw32-ime-cont-on is not nil."
	(if (and mw32-ime-cont-on
		 (string= current-input-method "MW32-IME"))
	    (fep-force-on)
	  (fep-force-off))
	(setq mw32-ime-composition-window nil))))))

(defun mw32-isearch-update ()
  (interactive)
  (isearch-update))

(defun mw32-isearch-mode-hook-function ()
  (cond
   ((eq mw32-ime-cont-on t)
    (fep-force-off))
   ((eq mw32-ime-cont-on 'always)
    (setq mw32-ime-composition-window (minibuffer-window))))
  (define-key isearch-mode-map [kanji] 'isearch-toggle-input-method)
  (define-key isearch-mode-map [compend] 'mw32-isearch-update))


(defun mw32-ime-uninitialize ()
  (cond ((and (eq system-type 'windows-nt)
	      (eq window-system 'w32)
	      (featurep 'meadow))
	 (setq-default mode-line-format
		       mw32-ime-mode-line-format-original)
	 (force-mode-line-update t)
	 (remove-hook 'select-window-functions
		      'mw32-ime-select-window-hook)
	 (remove-hook 'set-selected-window-buffer-functions
		      'mw32-ime-set-selected-window-buffer-hook)
	 (remove-hook 'isearch-mode-hook 'mw32-isearch-mode-hook-function)
	 (defadvice isearch-toggle-input-method (after mw32-fep-off disable))
	 (defadvice isearch-done (after mw32-fep-on disable)))))

(defun mw32-ime-exit-from-minibuffer ()
  (inactivate-input-method)
  (when (<= (minibuffer-depth) 1)
    (remove-hook 'minibuffer-exit-hook 'mw32-ime-exit-from-minibuffer)))

(defun mw32-ime-state-switch (&optional arg)
  (kill-local-variable 'input-method-function)
  (if (fep-get-mode)
      (fep-force-off))
  (if arg
      (progn
	(when (null (memq 'mw32-ime-select-window-hook
			  select-window-functions))
	  (mw32-ime-initialize))
	(setq inactivate-current-input-method-function
	      'mw32-ime-state-switch)
	(run-hooks 'input-method-activate-hook)
	(run-hooks 'mw32-ime-on-hook)
	(setq describe-current-input-method-function nil)
	(when (window-minibuffer-p)
	  (add-hook 'minibuffer-exit-hook 'mw32-ime-exit-from-minibuffer))
	(make-local-variable 'input-method-function)
	(if (not (eq mw32-ime-cont-on 'always))
	    (setq input-method-function 'mw32-input-method))
	(if mw32-ime-cont-on
	    (fep-force-on)))
    (setq current-input-method nil)
    (run-hooks 'input-method-inactivate-hook)
    (run-hooks 'mw32-ime-off-hook)
    (setq describe-current-input-method-function nil))
  (mw32-ime-mode-line-update))

(register-input-method "MW32-IME" "Japanese" 'mw32-ime-state-switch "Aあ"
		       "MW32 System IME")


;;;
;;; Inspect Device Capability/Intrinsic Facilities
;;;                Emulation layer for functions of xfuns.c

(defsubst mw32-emulate-x-display-argument (display)
  (cond ((stringp display) nil)
	((framep display) display)
	((null display) display)
	(t
	 (error "%S must be STRING or FRAME" display))))

(defun x-display-pixel-width (&optional display)
  (mw32-get-device-capability
   'width
   (mw32-emulate-x-display-argument display)))
(defun x-display-pixel-height (&optional display)
  (mw32-get-device-capability
   'height
   (mw32-emulate-x-display-argument display)))
(defun x-display-planes (&optional display)
  (mw32-get-device-capability
   ;; Notice that the meaning of "PLANES" in X are different
   ;; from that in Windows.
   'color-bits
   (mw32-emulate-x-display-argument display)))
(defun x-display-mm-height (&optional display)
  (mw32-get-device-capability
   'height-in-mm
   (mw32-emulate-x-display-argument display)))
(defun x-display-mm-width (&optional display)
  (mw32-get-device-capability
   'width-in-mm
   (mw32-emulate-x-display-argument display)))
(defun x-display-visual-class (&optional display)
  (let ((c (x-display-planes display))
	(n (mw32-get-device-capability
	    'colors)))
    (cond ((eq n 'full) 'true-color)
	  ((= n 1) 'static-gray)
	  ((> c (log c n)) 'pseudo-color)
	  (t 'static-color))))

(defun mw32-input-method (key)
  "Input method function for IME."
  (if (or
       (let ((case-fold-search nil))
	 (and key
	      mw32-input-method-noconv-regexp
	      (string-match mw32-input-method-noconv-regexp
			    (char-to-string key))))
       (fep-get-mode))
      (list key)
    (let ((redisplay-dont-pause t))
      (sit-for 0))

    (let* ((pos (point))
	   (modified-p (buffer-modified-p))
	   (ov (make-overlay (point) (1+ (point))))
	   (redisplay-dont-pause t)
	   ret result)
      (unwind-protect
	  (progn
	    (setq ret  (mw32-ime-input-method-function (char-to-string key)))
	    (setq result (append (car ret) nil))
	    (while (> (length (cadr ret)) 1)
	      (insert (car ret))
	      (move-overlay ov pos (point))
	      (if input-method-highlight-flag
		  (overlay-put ov 'face 'underline))
	      (sit-for 0)
	      (setq ret (mw32-ime-input-method-function))
	      (setq result (append result (append (car ret) nil)))))
	(if (and (cadr ret)
		 (eq (length (cadr ret)) 1))
	    (progn
	      (setq unread-input-method-events
		    (cons (car (cddr ret)) unread-input-method-events))))
	(delete-region pos (point))
	(delete-overlay ov)
	(set-buffer-modified-p modified-p))
      result)))

;; dummy vals.
(defun x-server-max-request-size (&optional display) 65535)
(defun x-server-vendor (&optional display) "MW32")
(defun x-server-version (&optional display) (list 11 0 1))
;; We should use multi-monitor APIs in the future.
(defun x-display-screens (&optional display) 1)
(defun x-display-backing-store (&optional display) 'not-useful)
(defun x-display-save-under (&optional display) nil)

(provide 'meadow)
