; Koskari syntax highlighting (canonical tree-sitter queries).
; Generated from highlights.scm.j2 — do not edit by hand.

(comment) @comment

(string) @string

(number) @constant.numeric

(keyword) @constant.other.keyword

; Layout block heads and special forms
((layout_header
  (symbol) @keyword.control))

; List head: treat known specials and layout heads as keyword
; Source: design/SPECIALS.md, design/LAYOUTS.md
((list (symbol) @keyword.control)
 (#any-of? @keyword.control

   "yield-from"
   "yield"
   "with"
   "while"
   "when-main"
   "when"
   "unquote-splicing"
   "unquote"
   "unless"
   "try"
   "syntax-rules"
   "syntax-match"
   "switch"
   "super"
   "subclass?"
   "special-rules"
   "special-match"
   "shift"
   "setq-values"
   "setq"
   "set-item"
   "set!"
   "return"
   "reset"
   "ref-item"
   "ref"
   "raise"
   "quote"
   "quasiquote"
   "parameterq"
   "parameterize"
   "or"
   "nil?"
   "macroexpand-1"
   "macro-rules"
   "macro-match"
   "locals"
   "local"
   "list-each"
   "letrec"
   "let/cc"
   "let-values"
   "let*"
   "let"
   "lambda"
   "item*"
   "item"
   "instance?"
   "incr!"
   "import-from"
   "import-all-from"
   "import"
   "if"
   "globals"
   "global"
   "gensym-thunk"
   "function-compose"
   "function"
   "for-each"
   "extend-syntax"
   "each"
   "dyn-with"
   "dyn-wind"
   "dict-each"
   "deref"
   "del-item"
   "del!"
   "define-values"
   "define-syntax"
   "define-parameter"
   "define-global"
   "define-demanding"
   "define-dataclass"
   "define-coroutine"
   "define-class"
   "define"
   "decorate"
   "declare-exports"
   "continue"
   "cons"
   "cond"
   "class"
   "cdr"
   "car"
   "capture/cc"
   "call/cc"
   "call-attr"
   "bytecode"
   "break"
   "bind!"
   "begin"
   "await"
   "attr*"
   "attr"
   "and"
   "None?"))

; General symbols
(symbol) @variable.other

; Punctuation and operators
["(" ")" "[" "]" "{" "}"] @punctuation.bracket

["'" "`" "," ",@" ":"] @keyword.operator

; Collection markers
["#(" "#[" "#{"] @punctuation.definition.collection

; The end.