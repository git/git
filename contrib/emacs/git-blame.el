;;; git-blame.el --- Minor mode for incremental blame for Git  -*- coding: utf-8 -*-
;;
;; Copyright (C) 2007  David Kågedal
;;
;; Authors:    David Kågedal <davidk@lysator.liu.se>
;; Created:    31 Jan 2007
;; Message-ID: <87iren2vqx.fsf@morpheus.local>
;; License:    GPL
;; Keywords:   git, version control, release management
;;
;; Compatibility: Emacs21, Emacs22 and EmacsCVS
;;                Git 1.5 and up

;; This file is *NOT* part of GNU Emacs.
;; This file is distributed under the same terms as GNU Emacs.

;; This program is free software; you can redistribute it and/or
;; modify it under the terms of the GNU General Public License as
;; published by the Free Software Foundation; either version 2 of
;; the License, or (at your option) any later version.

;; This program is distributed in the hope that it will be
;; useful, but WITHOUT ANY WARRANTY; without even the implied
;; warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
;; PURPOSE.  See the GNU General Public License for more details.

;; You should have received a copy of the GNU General Public
;; License along with this program; if not, write to the Free
;; Software Foundation, Inc., 59 Temple Place, Suite 330, Boston,
;; MA 02111-1307 USA

;; http://www.fsf.org/copyleft/gpl.html


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;;; Commentary:
;;
;; Here is an Emacs implementation of incremental git-blame.  When you
;; turn it on while viewing a file, the editor buffer will be updated by
;; setting the background of individual lines to a color that reflects
;; which commit it comes from.  And when you move around the buffer, a
;; one-line summary will be shown in the echo area.

;;; Installation:
;;
;; To use this package, put it somewhere in `load-path' (or add
;; directory with git-blame.el to `load-path'), and add the following
;; line to your .emacs:
;;
;;    (require 'git-blame)
;;
;; If you do not want to load this package before it is necessary, you
;; can make use of the `autoload' feature, e.g. by adding to your .emacs
;; the following lines
;;
;;    (autoload 'git-blame-mode "git-blame"
;;              "Minor mode for incremental blame for Git." t)
;;
;; Then first use of `M-x git-blame-mode' would load the package.

;;; Compatibility:
;;
;; It requires GNU Emacs 21 or later and Git 1.5.0 and up
;;
;; If you'are using Emacs 20, try changing this:
;;
;;            (overlay-put ovl 'face (list :background
;;                                         (cdr (assq 'color (cddddr info)))))
;;
;; to
;;
;;            (overlay-put ovl 'face (cons 'background-color
;;                                         (cdr (assq 'color (cddddr info)))))


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;;; Code:

(eval-when-compile (require 'cl))			      ; to use `push', `pop'


(defun git-blame-color-scale (&rest elements)
  "Given a list, returns a list of triples formed with each
elements of the list.

a b => bbb bba bab baa abb aba aaa aab"
  (let (result)
    (dolist (a elements)
      (dolist (b elements)
        (dolist (c elements)
          (setq result (cons (format "#%s%s%s" a b c) result)))))
    result))

;; (git-blame-color-scale "0c" "04" "24" "1c" "2c" "34" "14" "3c") =>
;; ("#3c3c3c" "#3c3c14" "#3c3c34" "#3c3c2c" "#3c3c1c" "#3c3c24"
;; "#3c3c04" "#3c3c0c" "#3c143c" "#3c1414" "#3c1434" "#3c142c" ...)

(defmacro git-blame-random-pop (l)
  "Select a random element from L and returns it. Also remove
selected element from l."
  ;; only works on lists with unique elements
  `(let ((e (elt ,l (random (length ,l)))))
     (setq ,l (remove e ,l))
     e))

(defvar git-blame-dark-colors
  (git-blame-color-scale "0c" "04" "24" "1c" "2c" "34" "14" "3c")
  "*List of colors (format #RGB) to use in a dark environment.

To check out the list, evaluate (list-colors-display git-blame-dark-colors).")

(defvar git-blame-light-colors
  (git-blame-color-scale "c4" "d4" "cc" "dc" "f4" "e4" "fc" "ec")
  "*List of colors (format #RGB) to use in a light environment.

To check out the list, evaluate (list-colors-display git-blame-light-colors).")

(defvar git-blame-colors '()
  "Colors used by git-blame. The list is built once when activating git-blame
minor mode.")

(defvar git-blame-ancient-color "dark green"
  "*Color to be used for ancient commit.")

(defvar git-blame-autoupdate t
  "*Automatically update the blame display while editing")

(defvar git-blame-proc nil
  "The running git-blame process")
(make-variable-buffer-local 'git-blame-proc)

(defvar git-blame-overlays nil
  "The git-blame overlays used in the current buffer.")
(make-variable-buffer-local 'git-blame-overlays)

(defvar git-blame-cache nil
  "A cache of git-blame information for the current buffer")
(make-variable-buffer-local 'git-blame-cache)

(defvar git-blame-idle-timer nil
  "An idle timer that updates the blame")
(make-variable-buffer-local 'git-blame-cache)

(defvar git-blame-update-queue nil
  "A queue of update requests")
(make-variable-buffer-local 'git-blame-update-queue)

;; FIXME: docstrings
(defvar git-blame-file nil)
(defvar git-blame-current nil)

(defvar git-blame-mode nil)
(make-variable-buffer-local 'git-blame-mode)

(defvar git-blame-mode-line-string " blame"
  "String to display on the mode line when git-blame is active.")

(or (assq 'git-blame-mode minor-mode-alist)
    (setq minor-mode-alist
	  (cons '(git-blame-mode git-blame-mode-line-string) minor-mode-alist)))

;;;###autoload
(defun git-blame-mode (&optional arg)
  "Toggle minor mode for displaying Git blame

With prefix ARG, turn the mode on if ARG is positive."
  (interactive "P")
  (cond
   ((null arg)
    (if git-blame-mode (git-blame-mode-off) (git-blame-mode-on)))
   ((> (prefix-numeric-value arg) 0) (git-blame-mode-on))
   (t (git-blame-mode-off))))

(defun git-blame-mode-on ()
  "Turn on git-blame mode.

See also function `git-blame-mode'."
  (make-local-variable 'git-blame-colors)
  (if git-blame-autoupdate
      (add-hook 'after-change-functions 'git-blame-after-change nil t)
    (remove-hook 'after-change-functions 'git-blame-after-change t))
  (git-blame-cleanup)
  (let ((bgmode (cdr (assoc 'background-mode (frame-parameters)))))
    (if (eq bgmode 'dark)
	(setq git-blame-colors git-blame-dark-colors)
      (setq git-blame-colors git-blame-light-colors)))
  (setq git-blame-cache (make-hash-table :test 'equal))
  (setq git-blame-mode t)
  (git-blame-run))

(defun git-blame-mode-off ()
  "Turn off git-blame mode.

See also function `git-blame-mode'."
  (git-blame-cleanup)
  (if git-blame-idle-timer (cancel-timer git-blame-idle-timer))
  (setq git-blame-mode nil))

;;;###autoload
(defun git-reblame ()
  "Recalculate all blame information in the current buffer"
  (interactive)
  (unless git-blame-mode
    (error "Git-blame is not active"))

  (git-blame-cleanup)
  (git-blame-run))

(defun git-blame-run (&optional startline endline)
  (if git-blame-proc
      ;; Should maybe queue up a new run here
      (message "Already running git blame")
    (let ((display-buf (current-buffer))
          (blame-buf (get-buffer-create
                      (concat " git blame for " (buffer-name))))
          (args '("--incremental" "--contents" "-")))
      (if startline
          (setq args (append args
                             (list "-L" (format "%d,%d" startline endline)))))
      (setq args (append args
                         (list (file-name-nondirectory buffer-file-name))))
      (setq git-blame-proc
            (apply 'start-process
                   "git-blame" blame-buf
                   "git" "blame"
                   args))
      (with-current-buffer blame-buf
        (erase-buffer)
        (make-local-variable 'git-blame-file)
        (make-local-variable 'git-blame-current)
        (setq git-blame-file display-buf)
        (setq git-blame-current nil))
      (set-process-filter git-blame-proc 'git-blame-filter)
      (set-process-sentinel git-blame-proc 'git-blame-sentinel)
      (process-send-region git-blame-proc (point-min) (point-max))
      (process-send-eof git-blame-proc))))

(defun remove-git-blame-text-properties (start end)
  (let ((modified (buffer-modified-p))
        (inhibit-read-only t))
    (remove-text-properties start end '(point-entered nil))
    (set-buffer-modified-p modified)))

(defun git-blame-cleanup ()
  "Remove all blame properties"
    (mapcar 'delete-overlay git-blame-overlays)
    (setq git-blame-overlays nil)
    (remove-git-blame-text-properties (point-min) (point-max)))

(defun git-blame-update-region (start end)
  "Rerun blame to get updates between START and END"
  (let ((overlays (overlays-in start end)))
    (while overlays
      (let ((overlay (pop overlays)))
        (if (< (overlay-start overlay) start)
            (setq start (overlay-start overlay)))
        (if (> (overlay-end overlay) end)
            (setq end (overlay-end overlay)))
        (setq git-blame-overlays (delete overlay git-blame-overlays))
        (delete-overlay overlay))))
  (remove-git-blame-text-properties start end)
  ;; We can be sure that start and end are at line breaks
  (git-blame-run (1+ (count-lines (point-min) start))
                 (count-lines (point-min) end)))

(defun git-blame-sentinel (proc status)
  (with-current-buffer (process-buffer proc)
    (with-current-buffer git-blame-file
      (setq git-blame-proc nil)
      (if git-blame-update-queue
          (git-blame-delayed-update))))
  ;;(kill-buffer (process-buffer proc))
  ;;(message "git blame finished")
  )

(defvar in-blame-filter nil)

(defun git-blame-filter (proc str)
  (save-excursion
    (set-buffer (process-buffer proc))
    (goto-char (process-mark proc))
    (insert-before-markers str)
    (goto-char 0)
    (unless in-blame-filter
      (let ((more t)
            (in-blame-filter t))
        (while more
          (setq more (git-blame-parse)))))))

(defun git-blame-parse ()
  (cond ((looking-at "\\([0-9a-f]\\{40\\}\\) \\([0-9]+\\) \\([0-9]+\\) \\([0-9]+\\)\n")
         (let ((hash (match-string 1))
               (src-line (string-to-number (match-string 2)))
               (res-line (string-to-number (match-string 3)))
               (num-lines (string-to-number (match-string 4))))
           (setq git-blame-current
                 (if (string= hash "0000000000000000000000000000000000000000")
                     nil
                   (git-blame-new-commit
                    hash src-line res-line num-lines))))
         (delete-region (point) (match-end 0))
         t)
        ((looking-at "filename \\(.+\\)\n")
         (let ((filename (match-string 1)))
           (git-blame-add-info "filename" filename))
         (delete-region (point) (match-end 0))
         t)
        ((looking-at "\\([a-z-]+\\) \\(.+\\)\n")
         (let ((key (match-string 1))
               (value (match-string 2)))
           (git-blame-add-info key value))
         (delete-region (point) (match-end 0))
         t)
        ((looking-at "boundary\n")
         (setq git-blame-current nil)
         (delete-region (point) (match-end 0))
         t)
        (t
         nil)))

(defun git-blame-new-commit (hash src-line res-line num-lines)
  (save-excursion
    (set-buffer git-blame-file)
    (let ((info (gethash hash git-blame-cache))
          (inhibit-point-motion-hooks t)
          (inhibit-modification-hooks t))
      (when (not info)
	;; Assign a random color to each new commit info
	;; Take care not to select the same color multiple times
	(let ((color (if git-blame-colors
			 (git-blame-random-pop git-blame-colors)
		       git-blame-ancient-color)))
          (setq info (list hash src-line res-line num-lines
                           (git-describe-commit hash)
                           (cons 'color color))))
        (puthash hash info git-blame-cache))
      (goto-line res-line)
      (while (> num-lines 0)
        (if (get-text-property (point) 'git-blame)
            (forward-line)
          (let* ((start (point))
                 (end (progn (forward-line 1) (point)))
                 (ovl (make-overlay start end)))
            (push ovl git-blame-overlays)
            (overlay-put ovl 'git-blame info)
            (overlay-put ovl 'help-echo hash)
            (overlay-put ovl 'face (list :background
                                         (cdr (assq 'color (nthcdr 5 info)))))
            ;; the point-entered property doesn't seem to work in overlays
            ;;(overlay-put ovl 'point-entered
            ;;             `(lambda (x y) (git-blame-identify ,hash)))
            (let ((modified (buffer-modified-p)))
              (put-text-property (if (= start 1) start (1- start)) (1- end)
                                 'point-entered
                                 `(lambda (x y) (git-blame-identify ,hash)))
              (set-buffer-modified-p modified))))
        (setq num-lines (1- num-lines))))))

(defun git-blame-add-info (key value)
  (if git-blame-current
      (nconc git-blame-current (list (cons (intern key) value)))))

(defun git-blame-current-commit ()
  (let ((info (get-char-property (point) 'git-blame)))
    (if info
        (car info)
      (error "No commit info"))))

(defun git-describe-commit (hash)
  (with-temp-buffer
    (call-process "git" nil t nil
                  "log" "-1" "--pretty=oneline"
                  hash)
    (buffer-substring (point-min) (1- (point-max)))))

(defvar git-blame-last-identification nil)
(make-variable-buffer-local 'git-blame-last-identification)
(defun git-blame-identify (&optional hash)
  (interactive)
  (let ((info (gethash (or hash (git-blame-current-commit)) git-blame-cache)))
    (when (and info (not (eq info git-blame-last-identification)))
      (message "%s" (nth 4 info))
      (setq git-blame-last-identification info))))

;; (defun git-blame-after-save ()
;;   (when git-blame-mode
;;     (git-blame-cleanup)
;;     (git-blame-run)))
;; (add-hook 'after-save-hook 'git-blame-after-save)

(defun git-blame-after-change (start end length)
  (when git-blame-mode
    (git-blame-enq-update start end)))

(defvar git-blame-last-update nil)
(make-variable-buffer-local 'git-blame-last-update)
(defun git-blame-enq-update (start end)
  "Mark the region between START and END as needing blame update"
  ;; Try to be smart and avoid multiple callouts for sequential
  ;; editing
  (cond ((and git-blame-last-update
              (= start (cdr git-blame-last-update)))
         (setcdr git-blame-last-update end))
        ((and git-blame-last-update
              (= end (car git-blame-last-update)))
         (setcar git-blame-last-update start))
        (t
         (setq git-blame-last-update (cons start end))
         (setq git-blame-update-queue (nconc git-blame-update-queue
                                             (list git-blame-last-update)))))
  (unless (or git-blame-proc git-blame-idle-timer)
    (setq git-blame-idle-timer
          (run-with-idle-timer 0.5 nil 'git-blame-delayed-update))))

(defun git-blame-delayed-update ()
  (setq git-blame-idle-timer nil)
  (if git-blame-update-queue
      (let ((first (pop git-blame-update-queue))
            (inhibit-point-motion-hooks t))
        (git-blame-update-region (car first) (cdr first)))))

(provide 'git-blame)

;;; git-blame.el ends here
