; Koskari syntax highlighting for Helix

(comment) @comment

(string) @string

(number) @constant.numeric

(keyword) @constant.other.keyword

; Layout block heads and special forms
((_layout_header
  (symbol) @keyword.control))

; List head: treat known specials and layout heads as keyword
; Source: design/SPECIALS.md, design/LAYOUTS.md
((list (symbol) @keyword.control)
 (#any-of? @keyword.control
   ; Control flow
   "define" "if" "cond" "when" "unless" "and" "or"
   "begin" "let" "lambda" "try" "raise"
   "while" "for-each" "break" "continue"
   ; Variables and bindings
   "import" "import-from"
   "ref" "attr" "setq"
   "set!" "bind!" "use-parameter" "parameterq" "None?"
   "bindq" "delq" "refq"
   "class" "define-syntax" "define-coroutine" "when-main"
   ; Context and continuations
   "with" "dyn-with" "dyn-wind" "parameterize"
   "call/cc" "let/cc" "capture/cc" "reset" "shift"
   ; compile/ namespace
   "compile/bytecode" "compile/begin" "compile/call" "compile/lambda"
   "compile/let" "compile/cond" "compile/if" "compile/when" "compile/unless"
   "compile/and" "compile/or" "compile/try" "compile/raise"
   "compile/while" "compile/for-each" "compile/break" "compile/continue"
   "compile/parameterize" "compile/dyn-wind" "compile/setq" "compile/inplace"
   "compile/ref" "compile/refq" "compile/bindq" "compile/define"
   "compile/set!" "compile/bind!" "compile/define-parameter" "compile/parameterq"
   "compile/None?"
   "compile/define-syntax" "compile/class" "compile/delq"
   "compile/attr" "compile/del-attr" "compile/set-attr" "compile/ref-attr"
   "compile/item" "compile/ref-item" "compile/deref" "compile/del-item"
   "compile/set-item" "compile/quote" "compile/quasiquote"
   "compile/quote-block" "compile/quasiquote-block" "compile/apply-layout"
   "compile/unquote" "compile/unquote-splicing"
   "compile/car" "compile/cdr" "compile/cons"
   "compile/call/cc" "compile/let/cc" "compile/capture/cc"
   "compile/reset" "compile/shift" "compile/with" "compile/dyn-with"
   "compile/import" "compile/import-from"))

; General symbols
(symbol) @variable.other

; Punctuation and operators
["(" ")" "[" "]" "{" "}"] @punctuation.bracket

["." "," "'" "`"] @keyword.operator

(unquote_splicing
  ["," "@"] @keyword.operator)

(unquote
  [","] @keyword.operator)

; Collection markers
["#(" "#[" "#{"] @punctuation.definition.collection
