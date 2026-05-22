;;; koskari-mode.el --- Major mode for Koskari (.ki) source files  -*- lexical-binding: t -*-

;; Copyright (C) 2024  Sibilant Contributors

;; This library is free software; you can redistribute it and/or modify
;; it under the terms of the GNU General Public License as published by
;; the Free Software Foundation; either version 3 of the License, or
;; (at your option) any later version.
;;
;; This library is distributed in the hope that it will be useful, but
;; WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;; General Public License for more details.
;;
;; You should have received a copy of the GNU General Public License
;; along with this library; if not, see <http://www.gnu.org/licenses/>.

;;; Commentary:

;; Major mode for editing Koskari source files (.ki).  Provides syntax
;; highlighting, indentation for layout blocks, and comment support.
;;
;; Koskari uses significant indentation for block bodies; after a block
;; header ending with `:', the body is indented one level.  Peer blocks
;; (elif, else, except, finally) align with their parent block header.

;;; Code:

(require 'font-lock)

(defgroup koskari nil
  "Major mode for Koskari (.ki) source files."
  :group 'languages)

(defcustom koskari-indent-offset 4
  "Number of spaces for each indentation level in Koskari."
  :type 'integer
  :safe 'integerp
  :group 'koskari)

(defvar koskari-mode-syntax-table
  (let ((table (make-syntax-table)))
    ;; Comment: ; to end of line
    (modify-syntax-entry ?\; "< b" table)
    (modify-syntax-entry ?\n "> b" table)
    ;; Paren-like delimiters
    (modify-syntax-entry ?\( "()" table)
    (modify-syntax-entry ?\) ")(" table)
    (modify-syntax-entry ?\[ "(]" table)
    (modify-syntax-entry ?\] ")[" table)
    (modify-syntax-entry ?\{ "(}" table)
    (modify-syntax-entry ?\} "){" table)
    ;; String delimiter
    (modify-syntax-entry ?\" "\"" table)
    ;; Symbol constituents: word chars plus Koskari extras
    (modify-syntax-entry ?_ "_" table)
    (modify-syntax-entry ?- "_" table)
    (modify-syntax-entry ?? "_" table)
    (modify-syntax-entry ?! "_" table)
    (modify-syntax-entry ?* "_" table)
    (modify-syntax-entry ?+ "_" table)
    (modify-syntax-entry ?< "_" table)
    (modify-syntax-entry ?= "_" table)
    (modify-syntax-entry ?> "_" table)
    ;; Quote and backtick as prefix (expression prefix)
    (modify-syntax-entry ?' "'" table)
    (modify-syntax-entry ?` "'" table)
    table)
  "Syntax table for Koskari mode.")

(defvar koskari-font-lock-keywords
  (list
   ;; Shebang
   (list "^#!.*$" 0 font-lock-comment-delimiter-face t)
   ;; Block headers with custom names (e.g. main []:); must come before keywords
   (list (concat "^\\([ \t]*\\)"
                 "\\([a-zA-Z_][a-zA-Z0-9_?!-]*\\)"
                 "\\b\\([^\n]*:\\)")
         (list 2 font-lock-function-name-face)
         (list 3 font-lock-preprocessor-face))
   ;; Block control keywords (overrides custom names for define, when, etc.)
   ;; Source: design/SPECIALS.md, design/LAYOUTS.md
   (list (concat "^\\([ \t]*\\)"
                 "\\(with\\|while\\|when-main\\|when\\|try\\|syntax-rules\\|syntax-match\\|"
                 "switch\\|special-rules\\|special-match\\|macro-rules\\|macro-match\\|if\\|"
                 "for-each\\|finally\\|extend-syntax\\|except\\|else\\|elif\\|dyn-with\\|"
                 "dyn-wind\\|define-syntax\\|define-dataclass\\|define-coroutine\\|"
                 "define-class\\|define\\|class\\|case\\|call\\|begin\\|before\\|after\\)"

                 "\\b\\([^\n]*?\\)\\(:\\)")
         (list 2 font-lock-keyword-face)
         (list 4 font-lock-preprocessor-face))
   ;; Implicit list head (layout line: first atom on line, no leading paren)
   (list (concat "^\\([ \t]*\\)\\("
                 "yield-from\\|yield\\|with\\|while\\|when-main\\|when\\|unquote-splicing\\|"
                 "unquote\\|unless\\|try\\|syntax-rules\\|syntax-match\\|switch\\|super\\|"
                 "subclass\\?\\|special-rules\\|special-match\\|shift\\|setq-values\\|setq\\|"
                 "set-item\\|set!\\|return\\|reset\\|ref-item\\|ref\\|raise\\|quote\\|quasiquote\\|"
                 "parameterq\\|parameterize\\|or\\|nil\\?\\|macroexpand-1\\|macro-rules\\|"
                 "macro-match\\|locals\\|local\\|list-each\\|letrec\\|let/cc\\|let-values\\|"
                 "let\\*\\|let\\|lambda\\|item\\*\\|item\\|instance\\?\\|incr!\\|import-from\\|"
                 "import\\|if\\|globals\\|global\\|gensym-thunk\\|function-compose\\|for-each\\|"
                 "extend-syntax\\|each\\|dyn-with\\|dyn-wind\\|dict-each\\|deref\\|del-item\\|"
                 "del!\\|define-values\\|define-syntax\\|define-parameter\\|define-global\\|"
                 "define-demanding\\|define-dataclass\\|define-coroutine\\|define-class\\|"
                 "define\\|decorate\\|declare-exports\\|continue\\|cons\\|cond\\|class\\|cdr\\|"
                 "car\\|capture/cc\\|call/cc\\|call-attr\\|bytecode\\|break\\|bind!\\|begin\\|"
                 "await\\|attr\\*\\|attr\\|and\\|None\\?"

                 "\\)\\>")
         2 font-lock-keyword-face)
   ;; List-head special forms (first symbol in paren form)
   (list (concat "(\\s-*\\("
                 "yield-from\\|yield\\|with\\|while\\|when-main\\|when\\|unquote-splicing\\|"
                 "unquote\\|unless\\|try\\|syntax-rules\\|syntax-match\\|switch\\|super\\|"
                 "subclass\\?\\|special-rules\\|special-match\\|shift\\|setq-values\\|setq\\|"
                 "set-item\\|set!\\|return\\|reset\\|ref-item\\|ref\\|raise\\|quote\\|quasiquote\\|"
                 "parameterq\\|parameterize\\|or\\|nil\\?\\|macroexpand-1\\|macro-rules\\|"
                 "macro-match\\|locals\\|local\\|list-each\\|letrec\\|let/cc\\|let-values\\|"
                 "let\\*\\|let\\|lambda\\|item\\*\\|item\\|instance\\?\\|incr!\\|import-from\\|"
                 "import\\|if\\|globals\\|global\\|gensym-thunk\\|function-compose\\|for-each\\|"
                 "extend-syntax\\|each\\|dyn-with\\|dyn-wind\\|dict-each\\|deref\\|del-item\\|"
                 "del!\\|define-values\\|define-syntax\\|define-parameter\\|define-global\\|"
                 "define-demanding\\|define-dataclass\\|define-coroutine\\|define-class\\|"
                 "define\\|decorate\\|declare-exports\\|continue\\|cons\\|cond\\|class\\|cdr\\|"
                 "car\\|capture/cc\\|call/cc\\|call-attr\\|bytecode\\|break\\|bind!\\|begin\\|"
                 "await\\|attr\\*\\|attr\\|and\\|None\\?"

                 "\\)\\>")
         1 font-lock-keyword-face)
   ;; Leading colon keywords (:foo)
   (list ":\\([^ \t\n()\\[\\]{};\"'\x60]+\\)" 1 font-lock-builtin-face)
   ;; Trailing colon keywords (foo:)
   (list "\\([^ \t\n()\\[\\]{};\"'\x60]+\\):" 1 font-lock-builtin-face)
   ;; Unquote operators , and ,@
   (list ",@?" 0 font-lock-preprocessor-face t)
   ;; Collection prefixes #( #[ #{
   (list "#\\([(\\[{])\\)" 1 font-lock-type-face)
   ;; Numbers
   (list "\\b0x[0-9a-fA-F]+\\b" 0 font-lock-constant-face t)
   (list "\\b0o[0-7]+\\b" 0 font-lock-constant-face t)
   (list "\\b0b[01]+\\b" 0 font-lock-constant-face t)
   (list "-?[0-9]+/[0-9]+\\b" 0 font-lock-constant-face t)
   (list "-?\\(?:[0-9]+\\.[0-9]*\\|\\.[0-9]+\\)\\(?:[eE]-?[0-9]+\\)?[fd]?\\b"
         0 font-lock-constant-face t)
   (list "-?[0-9]+[eE]-?[0-9]+[fd]?\\b" 0 font-lock-constant-face t)
   (list "-?[0-9]+\\(?:\\.[0-9]+\\)?[+-]\\(?:[0-9]*\\.?[0-9]*\\)[ij]\\b"
         0 font-lock-constant-face t)
   (list "-?[0-9]+\\(?:\\.[0-9]*\\|[0-9]*\\)\\.?d\\b" 0 font-lock-constant-face t)
   (list "-?[0-9]+\\b" 0 font-lock-constant-face t))
  "Font-lock keywords for Koskari mode.")

(defun koskari--previous-line-indent ()
  "Return indentation of previous non-empty line, or 0."
  (save-excursion
    (forward-line -1)
    (while (and (not (bobp)) (looking-at-p "[ \t]*$"))
      (forward-line -1))
    (if (bobp) 0 (current-indentation))))

(defun koskari--previous-line-ends-with-colon-p ()
  "Return non-nil if previous non-empty line ends with colon."
  (save-excursion
    (forward-line -1)
    (while (and (not (bobp)) (looking-at-p "[ \t]*$"))
      (forward-line -1))
    (and (not (bobp))
         (progn
           (end-of-line)
           (skip-chars-backward " \t")
           (and (not (bolp))
                (eq (char-before) ?:))))))

(defun koskari--line-is-peer-block-p ()
  "Return non-nil if current line starts a peer block."
  (looking-at-p
   (concat "^\\([ \t]*\\)"
           "\\(elif\\|else\\|except\\|finally\\)"
           "\\b")))

(defun koskari--find-parent-block-indent ()
  "Find indent of matching if/try for current peer block line.
Returns the column of the if/try line, or nil if not found."
  (save-excursion
    (let ((peer-indent (current-indentation)))
      (forward-line -1)
      (catch 'found
        (while (not (bobp))
          (skip-chars-backward "\n")
          (when (bobp)
            (throw 'found nil))
          (forward-line -1)
          (let ((indent (current-indentation)))
            (when (<= indent peer-indent)
              (when (or (looking-at-p "^\\([ \t]*\\)if\\b")
                        (looking-at-p "^\\([ \t]*\\)try\\b"))
                (throw 'found indent))
              (when (<= indent 0)
                (throw 'found nil)))))))))

(defun koskari-indent-line ()
  "Indent the current line according to Koskari block structure."
  (interactive "*")
  (let ((indent 0)
        (pos (point)))
    (save-excursion
      (beginning-of-line)
      (cond
       ;; Peer block: align with parent if/try
       ((koskari--line-is-peer-block-p)
        (let ((parent (koskari--find-parent-block-indent)))
          (if parent
              (setq indent parent)
            (setq indent (koskari--previous-line-indent)))))
       ;; Previous line ends with colon: indent body
       ((koskari--previous-line-ends-with-colon-p)
        (setq indent (+ (koskari--previous-line-indent) koskari-indent-offset)))
       ;; Inside parens: align with first element or add offset
       ((let ((ppss (syntax-ppss)))
          (when (nth 1 ppss)
            (goto-char (nth 1 ppss))
            (forward-char 1)
            (skip-chars-forward " \t")
            (setq indent (current-column))
            t)))
       ;; Default: use previous line indent
       (t
        (setq indent (koskari--previous-line-indent)))))
    (when (and (>= indent 0)
               (not (= indent (current-indentation))))
      (indent-line-to indent))
    (when (< (point) (line-beginning-position))
      (goto-char (line-beginning-position)))
    (when (and (>= pos (line-beginning-position))
               (< pos (line-end-position)))
      (goto-char (min pos (line-end-position))))))

;;;###autoload
(define-derived-mode koskari-mode prog-mode "Koskari"
  "Major mode for Koskari (.ki) source files.

Koskari is a Lisp-like language with significant indentation for
block bodies.  Block headers end with a colon; the body is indented
one level.  Peer blocks (elif, else, except, finally) align with
their parent block header.

\\{koskari-mode-map}"
  :syntax-table koskari-mode-syntax-table
  (setq-local comment-start ";")
  (setq-local comment-end "")
  (setq-local comment-start-skip ";+ *")
  (setq-local indent-line-function #'koskari-indent-line)
  (setq-local font-lock-defaults
              '(koskari-font-lock-keywords
                nil
                nil
                (("_" . "w") ("-" . "w") ("?" . "w") ("!" . "w")
                 ("*" . "w") ("+" . "w") ("<" . "w") ("=" . "w") (">" . "w"))
                nil
                (font-lock-syntactic-face-function
                 . font-lock-default-syntactic-face-function))))

;;;###autoload
(add-to-list 'auto-mode-alist '("\\.ki\\'" . koskari-mode))

;; Shebang / first-line matching for #!...koskari
(add-to-list 'interpreter-mode-alist '("koskari" . koskari-mode))

(provide 'koskari-mode)

;;; koskari-mode.el ends here