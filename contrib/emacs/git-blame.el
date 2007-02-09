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
;; Compatibility: Emacs21


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
;; It requires GNU Emacs 21.  If you'are using Emacs 20, try
;; changing this:
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

(require 'cl)			      ; to use `push', `pop'

(defun color-scale (l)
  (let* ((colors ())
         r g b)
    (setq r l)
    (while r
      (setq g l)
      (while g
        (setq b l)
        (while b
          (push (concat "#" (car r) (car g) (car b)) colors)
          (pop b))
        (pop g))
      (pop r))
    colors))

(defvar git-blame-dark-colors
  (color-scale '("0c" "04" "24" "1c" "2c" "34" "14" "3c")))

(defvar git-blame-light-colors
  (color-scale '("c4" "d4" "cc" "dc" "f4" "e4" "fc" "ec")))

(defvar git-blame-ancient-color "dark green")

(defvar git-blame-proc nil
  "The running git-blame process")
(make-variable-buffer-local 'git-blame-proc)

(defvar git-blame-overlays nil
  "The git-blame overlays used in the current buffer.")
(make-variable-buffer-local 'git-blame-overlays)

(defvar git-blame-cache nil
  "A cache of git-blame information for the current buffer")
(make-variable-buffer-local 'git-blame-cache)

(defvar git-blame-mode nil)
(make-variable-buffer-local 'git-blame-mode)
(unless (assq 'git-blame-mode minor-mode-alist)
  (setq minor-mode-alist
	(cons (list 'git-blame-mode " blame")
	      minor-mode-alist)))

;;;###autoload
(defun git-blame-mode (&optional arg)
  (interactive "P")
  (if arg
      (setq git-blame-mode (eq arg 1))
    (setq git-blame-mode (not git-blame-mode)))
  (make-local-variable 'git-blame-colors)
  (git-blame-cleanup)
  (if git-blame-mode
      (progn
        (let ((bgmode (cdr (assoc 'background-mode (frame-parameters)))))
          (if (eq bgmode 'dark)
              (setq git-blame-colors git-blame-dark-colors)
            (setq git-blame-colors git-blame-light-colors)))
        (setq git-blame-cache (make-hash-table :test 'equal))
        (git-blame-run))))

;;;###autoload
(defun git-reblame ()
  "Recalculate all blame information in the current buffer"
  (unless git-blame-mode
    (error "git-blame is not active"))
  (interactive)
  (git-blame-cleanup)
  (git-blame-run))

(defun git-blame-run ()
  (if git-blame-proc
      ;; Should maybe queue up a new run here
      (message "Already running git blame")
    (let ((display-buf (current-buffer))
          (blame-buf (get-buffer-create
                      (concat " git blame for " (buffer-name)))))
      (setq git-blame-proc
            (start-process "git-blame" blame-buf
                           "git" "blame"
                           "--incremental" "--contents" "-"
                           (file-name-nondirectory buffer-file-name)))
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

(defun git-blame-cleanup ()
  "Remove all blame properties"
    (mapcar 'delete-overlay git-blame-overlays)
    (setq git-blame-overlays nil)
    (let ((modified (buffer-modified-p)))
      (remove-text-properties (point-min) (point-max) '(point-entered nil))
      (set-buffer-modified-p modified)))

(defun git-blame-sentinel (proc status)
  (with-current-buffer (process-buffer proc)
    (with-current-buffer git-blame-file
      (setq git-blame-proc nil)))
  ;;(kill-buffer (process-buffer proc))
  (message "git blame finished"))

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
          (inhibit-point-motion-hooks t))
      (when (not info)
        (let ((color (pop git-blame-colors)))
          (unless color
            (setq color git-blame-ancient-color))
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

(provide 'git-blame)

;;; git-blame.el ends here
