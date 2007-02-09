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
;;  1) Load into emacs: M-x load-file RET git-blame.el RET
;;  2) Open a git-controlled file
;;  3) Blame: M-x git-blame-mode

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
  (color-scale '("00" "04" "08" "0c"
                 "10" "14" "18" "1c"
                 "20" "24" "28" "2c"
                 "30" "34" "38" "3c")))

(defvar git-blame-light-colors
  (color-scale '("c0" "c4" "c8" "cc"
                 "d0" "d4" "d8" "dc"
                 "e0" "e4" "e8" "ec"
                 "f0" "f4" "f8" "fc")))

(defvar git-blame-ancient-color "dark green")

(defvar git-blame-overlays nil)
(defvar git-blame-cache nil)

(defvar git-blame-mode nil)
(make-variable-buffer-local 'git-blame-mode)
(push (list 'git-blame-mode " blame") minor-mode-alist)

(defun git-blame-mode (&optional arg)
  (interactive "P")
  (if arg
      (setq git-blame-mode (eq arg 1))
    (setq git-blame-mode (not git-blame-mode)))
  (make-local-variable 'git-blame-overlays)
  (make-local-variable 'git-blame-colors)
  (make-local-variable 'git-blame-cache)
  (let ((bgmode (cdr (assoc 'background-mode (frame-parameters)))))
    (if (eq bgmode 'dark)
        (setq git-blame-colors git-blame-dark-colors)
      (setq git-blame-colors git-blame-light-colors)))
  (if git-blame-mode
      (git-blame-run)
    (git-blame-cleanup)))

(defun git-blame-run ()
  (let* ((display-buf (current-buffer))
         (blame-buf (get-buffer-create
                     (concat " git blame for " (buffer-name))))
         (proc (start-process "git-blame" blame-buf
                             "git" "blame" "--incremental"
                             (file-name-nondirectory buffer-file-name))))
    (mapcar 'delete-overlay git-blame-overlays)
    (setq git-blame-overlays nil)
    (setq git-blame-cache (make-hash-table :test 'equal))
    (with-current-buffer blame-buf
      (erase-buffer)
      (make-local-variable 'git-blame-file)
      (make-local-variable 'git-blame-current)
      (setq git-blame-file display-buf)
      (setq git-blame-current nil))
    (set-process-filter proc 'git-blame-filter)
    (set-process-sentinel proc 'git-blame-sentinel)))

(defun git-blame-cleanup ()
  "Remove all blame properties"
    (mapcar 'delete-overlay git-blame-overlays)
    (setq git-blame-overlays nil)
    (let ((modified (buffer-modified-p)))
      (remove-text-properties (point-min) (point-max) '(point-entered nil))
      (set-buffer-modified-p modified)))

(defun git-blame-sentinel (proc status)
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
                 (git-blame-new-commit
                  hash src-line res-line num-lines)))
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
                                         (cdr (assq 'color (cddddr info)))))
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

(defun git-blame-identify (&optional hash)
  (interactive)
  (shell-command
   (format "git log -1 --pretty=oneline %s" (or hash
                                                (git-blame-current-commit)))))

(provide 'git-blame)

;;; git-blame.el ends here
