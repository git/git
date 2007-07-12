;;; vc-git.el --- VC backend for the git version control system

;; Copyright (C) 2006 Alexandre Julliard

;; This program is free software; you can redistribute it and/or
;; modify it under the terms of the GNU General Public License as
;; published by the Free Software Foundation; either version 2 of
;; the License, or (at your option) any later version.
;;
;; This program is distributed in the hope that it will be
;; useful, but WITHOUT ANY WARRANTY; without even the implied
;; warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
;; PURPOSE.  See the GNU General Public License for more details.
;;
;; You should have received a copy of the GNU General Public
;; License along with this program; if not, write to the Free
;; Software Foundation, Inc., 59 Temple Place, Suite 330, Boston,
;; MA 02111-1307 USA

;;; Commentary:

;; This file contains a VC backend for the git version control
;; system.
;;
;; To install: put this file on the load-path and add GIT to the list
;; of supported backends in `vc-handled-backends'; the following line,
;; placed in your ~/.emacs, will accomplish this:
;;
;;     (add-to-list 'vc-handled-backends 'GIT)
;;
;; TODO
;;  - changelog generation
;;  - working with revisions other than HEAD
;;

(eval-when-compile (require 'cl))

(defvar git-commits-coding-system 'utf-8
  "Default coding system for git commits.")

(defun vc-git--run-command-string (file &rest args)
  "Run a git command on FILE and return its output as string."
  (let* ((ok t)
         (str (with-output-to-string
                (with-current-buffer standard-output
                  (unless (eq 0 (apply #'call-process "git" nil '(t nil) nil
                                       (append args (list (file-relative-name file)))))
                    (setq ok nil))))))
    (and ok str)))

(defun vc-git--run-command (file &rest args)
  "Run a git command on FILE, discarding any output."
  (let ((name (file-relative-name file)))
    (eq 0 (apply #'call-process "git" nil (get-buffer "*Messages") nil (append args (list name))))))

(defun vc-git-registered (file)
  "Check whether FILE is registered with git."
  (with-temp-buffer
    (let* ((dir (file-name-directory file))
           (name (file-relative-name file dir)))
      (and (ignore-errors
             (when dir (cd dir))
             (eq 0 (call-process "git" nil '(t nil) nil "ls-files" "-c" "-z" "--" name)))
           (let ((str (buffer-string)))
             (and (> (length str) (length name))
                  (string= (substring str 0 (1+ (length name))) (concat name "\0"))))))))

(defun vc-git-state (file)
  "git-specific version of `vc-state'."
  (let ((diff (vc-git--run-command-string file "diff-index" "-z" "HEAD" "--")))
    (if (and diff (string-match ":[0-7]\\{6\\} [0-7]\\{6\\} [0-9a-f]\\{40\\} [0-9a-f]\\{40\\} [ADMU]\0[^\0]+\0" diff))
        'edited
      'up-to-date)))

(defun vc-git-workfile-version (file)
  "git-specific version of `vc-workfile-version'."
  (let ((str (with-output-to-string
               (with-current-buffer standard-output
                 (call-process "git" nil '(t nil) nil "symbolic-ref" "HEAD")))))
    (if (string-match "^\\(refs/heads/\\)?\\(.+\\)$" str)
        (match-string 2 str)
      str)))

(defun vc-git-symbolic-commit (commit)
  "Translate COMMIT string into symbolic form.
Returns nil if not possible."
  (and commit
       (with-temp-buffer
	 (and
	  (zerop
	   (call-process "git" nil '(t nil) nil "name-rev"
			 "--name-only" "--tags"
			 commit))
	  (goto-char (point-min))
	  (= (forward-line 2) 1)
	  (bolp)
	  (buffer-substring-no-properties (point-min) (1- (point-max)))))))

(defun vc-git-previous-version (file rev)
  "git-specific version of `vc-previous-version'."
  (let ((default-directory (file-name-directory (expand-file-name file)))
	(file (file-name-nondirectory file)))
    (vc-git-symbolic-commit
     (with-temp-buffer
       (and
	(zerop
	 (call-process "git" nil '(t nil) nil "rev-list"
		       "-2" rev "--" file))
	(goto-char (point-max))
	(bolp)
	(zerop (forward-line -1))
	(not (bobp))
	(buffer-substring-no-properties
	   (point)
	   (1- (point-max))))))))

(defun vc-git-next-version (file rev)
  "git-specific version of `vc-next-version'."
  (let* ((default-directory (file-name-directory
			     (expand-file-name file)))
	(file (file-name-nondirectory file))
	(current-rev
	 (with-temp-buffer
	   (and
	    (zerop
	     (call-process "git" nil '(t nil) nil "rev-list"
			   "-1" rev "--" file))
	    (goto-char (point-max))
	    (bolp)
	    (zerop (forward-line -1))
	    (bobp)
	    (buffer-substring-no-properties
	     (point)
	     (1- (point-max)))))))
    (and current-rev
	 (vc-git-symbolic-commit
	  (with-temp-buffer
	    (and
	     (zerop
	      (call-process "git" nil '(t nil) nil "rev-list"
			    "HEAD" "--" file))
	     (goto-char (point-min))
	     (search-forward current-rev nil t)
	     (zerop (forward-line -1))
	     (buffer-substring-no-properties
	      (point)
	      (progn (forward-line 1) (1- (point))))))))))

(defun vc-git-revert (file &optional contents-done)
  "Revert FILE to the version stored in the git repository."
  (if contents-done
      (vc-git--run-command file "update-index" "--")
    (vc-git--run-command file "checkout" "HEAD")))

(defun vc-git-checkout-model (file)
  'implicit)

(defun vc-git-workfile-unchanged-p (file)
  (let ((sha1 (vc-git--run-command-string file "hash-object" "--"))
        (head (vc-git--run-command-string file "ls-tree" "-z" "HEAD" "--")))
    (and head
         (string-match "[0-7]\\{6\\} blob \\([0-9a-f]\\{40\\}\\)\t[^\0]+\0" head)
         (string= (car (split-string sha1 "\n")) (match-string 1 head)))))

(defun vc-git-register (file &optional rev comment)
  "Register FILE into the git version-control system."
  (vc-git--run-command file "update-index" "--add" "--"))

(defun vc-git-print-log (file &optional buffer)
  (let ((name (file-relative-name file))
        (coding-system-for-read git-commits-coding-system))
    (vc-do-command buffer 'async "git" name "rev-list" "--pretty" "HEAD" "--")))

(defun vc-git-diff (file &optional rev1 rev2 buffer)
  (let ((name (file-relative-name file))
        (buf (or buffer "*vc-diff*")))
    (if (and rev1 rev2)
        (vc-do-command buf 0 "git" name "diff-tree" "-p" rev1 rev2 "--")
      (vc-do-command buf 0 "git" name "diff-index" "-p" (or rev1 "HEAD") "--"))
    ; git-diff-index doesn't set exit status like diff does
    (if (vc-git-workfile-unchanged-p file) 0 1)))

(defun vc-git-checkin (file rev comment)
  (let ((coding-system-for-write git-commits-coding-system))
    (vc-git--run-command file "commit" "-m" comment "--only" "--")))

(defun vc-git-checkout (file &optional editable rev destfile)
  (if destfile
      (let ((fullname (substring
                       (vc-git--run-command-string file "ls-files" "-z" "--full-name" "--")
                       0 -1))
            (coding-system-for-read 'no-conversion)
            (coding-system-for-write 'no-conversion))
        (with-temp-file destfile
          (eq 0 (call-process "git" nil t nil "cat-file" "blob"
                              (concat (or rev "HEAD") ":" fullname)))))
    (vc-git--run-command file "checkout" (or rev "HEAD"))))

(defun vc-git-annotate-command (file buf &optional rev)
  ; FIXME: rev is ignored
  (let ((name (file-relative-name file)))
    (call-process "git" nil buf nil "blame" name)))

(defun vc-git-annotate-time ()
  (and (re-search-forward "[0-9a-f]+ (.* \\([0-9]+\\)-\\([0-9]+\\)-\\([0-9]+\\) \\([0-9]+\\):\\([0-9]+\\):\\([0-9]+\\) \\([-+0-9]+\\) +[0-9]+)" nil t)
       (vc-annotate-convert-time
        (apply #'encode-time (mapcar (lambda (match) (string-to-number (match-string match))) '(6 5 4 3 2 1 7))))))

;; Not really useful since we can't do anything with the revision yet
;;(defun vc-annotate-extract-revision-at-line ()
;;  (save-excursion
;;    (move-beginning-of-line 1)
;;    (and (looking-at "[0-9a-f]+")
;;         (buffer-substring (match-beginning 0) (match-end 0)))))

(provide 'vc-git)
