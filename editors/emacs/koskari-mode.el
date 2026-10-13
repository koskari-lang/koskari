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
                 "\\(with\\|while\\|when-main\\|when\\|var\\|try\\|syntax-rules\\|syntax-match\\|"
                 "syntax\\|switch\\|special-rules\\|special-match\\|shift\\|reset\\|"
                 "quote-syntax\\|quote\\|quasiquote-syntax\\|quasiquote\\|match\\|macro-rules\\|"
                 "macro-match\\|import-from\\|if\\|global\\|for-each\\|finally\\|extend-syntax\\|"
                 "except\\|else\\|elif\\|dyn-with\\|dyn-wind\\|define-syntax\\|define\\|def\\|"
                 "class\\|call\\|build-class\\|begin\\)"

                 "\\b\\([^\n]*?\\)\\(:\\)")
         (list 2 font-lock-keyword-face)
         (list 4 font-lock-preprocessor-face))
   ;; Implicit list head (layout line: first atom on line, no leading paren)
   (list (concat "^\\([ \t]*\\)\\("
                 "zip\\|yield-from\\|yield\\|with\\|while\\|when-main\\|when\\|vars\\|var\\|"
                 "values\\?\\|values\\|use\\|unquote-splicing\\|unquote\\|unpack\\|unless\\|"
                 "unassigned\\?\\|type\\?\\|type\\|tuple\\?\\|tuple\\|try\\|truth\\|third\\|takeq\\|"
                 "take-item\\|take-attr\\|take!\\|syntax-rules\\|syntax-match\\|syntax\\|"
                 "symbol\\?\\|symbol\\|switch\\|super\\|sum\\|subclass\\?\\|str\\?\\|str\\|"
                 "staticmethod\\?\\|staticmethod\\|special-rules\\|special-match\\|sorted\\|"
                 "slice\\?\\|slice\\|sleep\\|shift\\|setq-values\\|setq\\|setattr\\|set\\?\\|"
                 "set-item\\|set-cdr\\|set-car\\|set-attr\\|set!\\|set\\|second\\|reversed\\|"
                 "return\\|reset\\|repr\\|refq\\|ref\\?\\|ref-item\\|ref-attr\\|ref\\|recursive\\?\\|"
                 "range\\?\\|range\\|raise\\|quote-syntax\\|quote\\|quasiquote-syntax\\|"
                 "quasiquote\\|py-repr\\|py-help\\|property\\?\\|property\\|proper\\?\\|print\\|"
                 "pragma\\|pass\\|partial\\?\\|partial\\|parameterq\\|parameterize\\|parameter\\|"
                 "pair\\?\\|pair\\|package-pragma\\|ord\\|or\\|open\\|on-demand\\?\\|object\\?\\|"
                 "object\\|not-in\\?\\|not-in\\|not-equal\\?\\|not\\|nil\\?\\|nil\\|next\\|min\\|max\\|"
                 "match\\|map\\|macroexpandq-1\\|macroexpandq\\|macroexpand-1\\|macroexpand\\|"
                 "macro-rules\\|macro-match\\|lt\\?\\|load-exception\\|list\\?\\|list-each\\|"
                 "list\\|letrec\\|let/cc\\|let-values\\|let-syntax\\|let\\*\\|let\\|len\\|le\\?\\|"
                 "lambda\\|labels\\|koskari-vars\\|keyword\\?\\|keyword-block\\|keyword\\|iter\\|"
                 "item-slice\\|item\\*\\|item\\|is\\?\\|is-not\\?\\|is-not\\|is\\|int\\?\\|int\\|"
                 "instance\\?\\|input\\|inplace-item\\|inplace-attr\\|inplace\\|inline-macro\\|"
                 "inline-block\\|incr!\\|in\\?\\|in\\|import-from\\|import-all-from\\|import\\|"
                 "if\\|id\\|help-pragma-key\\|help\\|hasitem\\*\\|hash\\|hasattr\\?\\|hasattr\\*\\|"
                 "has\\?\\|gt\\?\\|globals\\|global\\|getitem\\*\\|getattr\\*\\|getattr\\|"
                 "get-compiler\\|gensym-thunk\\|gensym\\|ge\\?\\|function-compose\\|function\\|"
                 "frozenset\\?\\|frozenset\\|fraction\\?\\|fraction\\|fourth\\|format\\|for-each\\|"
                 "float\\?\\|float\\|flet\\|first\\|filter\\|fifth\\|extend-syntax\\|evaluate\\|"
                 "equal\\?\\|enumerate\\|each\\|dyn-with\\|dyn-wind\\|divmod\\|dir\\|dict\\?\\|"
                 "dict-each\\|dict\\|deref\\|demand\\?\\|demand-call\\|demand-apply\\|demand\\|"
                 "delq\\|del-item\\|del-attr\\|del!\\|define-values\\|define-syntax\\|"
                 "define-parameter\\|define-global\\|define-fence\\|define-environ\\|define\\|"
                 "defer\\?\\|defer\\|default!\\|def\\|decr!\\|decorate-subject\\|decorate\\|"
                 "declare-exports\\|decimal\\?\\|decimal\\|datumq\\|datum\\?\\|datum\\|"
                 "create-task\\|coroutine-fn\\|continue\\|cons\\|cond\\|complex\\?\\|complex\\|"
                 "compileq\\|compile\\|clear-exception\\|classmethod\\?\\|classmethod\\|class\\|"
                 "chr\\|check-exception\\|cdr\\|cddr\\|cdddr\\|cddddr\\|car\\|capture/cc\\|"
                 "callable\\?\\|call/cc\\|call-attr\\|call\\|cadr\\|caddr\\|cadddr\\|caddddr\\|"
                 "bytes\\?\\|bytes\\|bytecode\\|build-syntax\\|build-special\\|build-slice\\|"
                 "build-proper\\|build-macro\\|build-class\\|break\\|bracket-splice\\|"
                 "bracket-proper\\|bracket-nil\\|bracket-cons\\|bool\\?\\|bool\\|block-begin\\|"
                 "block\\|bindq\\|bind!\\|begin\\|bare-syntax\\|bare-control-word\\|await\\|"
                 "attr\\*\\|attr\\|assert\\|apply-layouts\\|apply-layout\\|apply-fence\\|apply\\|"
                 "any\\?\\|and\\|all\\?\\|aiter\\|active-pragma\\|abs\\|None\\?\\|#tuple\\|#str\\|"
                 "#list-append\\|#list"

                 "\\)\\>")
         2 font-lock-keyword-face)
   ;; List-head special forms (first symbol in paren form)
   (list (concat "(\\s-*\\("
                 "zip\\|yield-from\\|yield\\|with\\|while\\|when-main\\|when\\|vars\\|var\\|"
                 "values\\?\\|values\\|use\\|unquote-splicing\\|unquote\\|unpack\\|unless\\|"
                 "unassigned\\?\\|type\\?\\|type\\|tuple\\?\\|tuple\\|try\\|truth\\|third\\|takeq\\|"
                 "take-item\\|take-attr\\|take!\\|syntax-rules\\|syntax-match\\|syntax\\|"
                 "symbol\\?\\|symbol\\|switch\\|super\\|sum\\|subclass\\?\\|str\\?\\|str\\|"
                 "staticmethod\\?\\|staticmethod\\|special-rules\\|special-match\\|sorted\\|"
                 "slice\\?\\|slice\\|sleep\\|shift\\|setq-values\\|setq\\|setattr\\|set\\?\\|"
                 "set-item\\|set-cdr\\|set-car\\|set-attr\\|set!\\|set\\|second\\|reversed\\|"
                 "return\\|reset\\|repr\\|refq\\|ref\\?\\|ref-item\\|ref-attr\\|ref\\|recursive\\?\\|"
                 "range\\?\\|range\\|raise\\|quote-syntax\\|quote\\|quasiquote-syntax\\|"
                 "quasiquote\\|py-repr\\|py-help\\|property\\?\\|property\\|proper\\?\\|print\\|"
                 "pragma\\|pass\\|partial\\?\\|partial\\|parameterq\\|parameterize\\|parameter\\|"
                 "pair\\?\\|pair\\|package-pragma\\|ord\\|or\\|open\\|on-demand\\?\\|object\\?\\|"
                 "object\\|not-in\\?\\|not-in\\|not-equal\\?\\|not\\|nil\\?\\|nil\\|next\\|min\\|max\\|"
                 "match\\|map\\|macroexpandq-1\\|macroexpandq\\|macroexpand-1\\|macroexpand\\|"
                 "macro-rules\\|macro-match\\|lt\\?\\|load-exception\\|list\\?\\|list-each\\|"
                 "list\\|letrec\\|let/cc\\|let-values\\|let-syntax\\|let\\*\\|let\\|len\\|le\\?\\|"
                 "lambda\\|labels\\|koskari-vars\\|keyword\\?\\|keyword-block\\|keyword\\|iter\\|"
                 "item-slice\\|item\\*\\|item\\|is\\?\\|is-not\\?\\|is-not\\|is\\|int\\?\\|int\\|"
                 "instance\\?\\|input\\|inplace-item\\|inplace-attr\\|inplace\\|inline-macro\\|"
                 "inline-block\\|incr!\\|in\\?\\|in\\|import-from\\|import-all-from\\|import\\|"
                 "if\\|id\\|help-pragma-key\\|help\\|hasitem\\*\\|hash\\|hasattr\\?\\|hasattr\\*\\|"
                 "has\\?\\|gt\\?\\|globals\\|global\\|getitem\\*\\|getattr\\*\\|getattr\\|"
                 "get-compiler\\|gensym-thunk\\|gensym\\|ge\\?\\|function-compose\\|function\\|"
                 "frozenset\\?\\|frozenset\\|fraction\\?\\|fraction\\|fourth\\|format\\|for-each\\|"
                 "float\\?\\|float\\|flet\\|first\\|filter\\|fifth\\|extend-syntax\\|evaluate\\|"
                 "equal\\?\\|enumerate\\|each\\|dyn-with\\|dyn-wind\\|divmod\\|dir\\|dict\\?\\|"
                 "dict-each\\|dict\\|deref\\|demand\\?\\|demand-call\\|demand-apply\\|demand\\|"
                 "delq\\|del-item\\|del-attr\\|del!\\|define-values\\|define-syntax\\|"
                 "define-parameter\\|define-global\\|define-fence\\|define-environ\\|define\\|"
                 "defer\\?\\|defer\\|default!\\|def\\|decr!\\|decorate-subject\\|decorate\\|"
                 "declare-exports\\|decimal\\?\\|decimal\\|datumq\\|datum\\?\\|datum\\|"
                 "create-task\\|coroutine-fn\\|continue\\|cons\\|cond\\|complex\\?\\|complex\\|"
                 "compileq\\|compile\\|clear-exception\\|classmethod\\?\\|classmethod\\|class\\|"
                 "chr\\|check-exception\\|cdr\\|cddr\\|cdddr\\|cddddr\\|car\\|capture/cc\\|"
                 "callable\\?\\|call/cc\\|call-attr\\|call\\|cadr\\|caddr\\|cadddr\\|caddddr\\|"
                 "bytes\\?\\|bytes\\|bytecode\\|build-syntax\\|build-special\\|build-slice\\|"
                 "build-proper\\|build-macro\\|build-class\\|break\\|bracket-splice\\|"
                 "bracket-proper\\|bracket-nil\\|bracket-cons\\|bool\\?\\|bool\\|block-begin\\|"
                 "block\\|bindq\\|bind!\\|begin\\|bare-syntax\\|bare-control-word\\|await\\|"
                 "attr\\*\\|attr\\|assert\\|apply-layouts\\|apply-layout\\|apply-fence\\|apply\\|"
                 "any\\?\\|and\\|all\\?\\|aiter\\|active-pragma\\|abs\\|None\\?\\|#tuple\\|#str\\|"
                 "#list-append\\|#list"

                 "\\)\\>")
         1 font-lock-keyword-face)
   ;; Leading colon keywords (:foo)
   (list ":\\([^ \t\n()\\[\\]{};\"'\x60]+\\)" 1 font-lock-builtin-face)
   ;; Trailing colon keywords (foo:)
   (list "\\([^ \t\n()\\[\\]{};\"'\x60]+\\):" 1 font-lock-builtin-face)
   ;; Unquote operators , and ,@
   (list ",@?" 0 font-lock-preprocessor-face t)
   ;; Syntax quote prefixes #' and #`
   (list "#['\x60]" 0 font-lock-preprocessor-face t)
   ;; Raw form prefix -'
   (list "-'" 0 font-lock-preprocessor-face t)
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