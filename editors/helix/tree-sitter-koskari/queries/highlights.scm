; Koskari syntax highlighting for Helix

(comment) @comment

(string) @string

(number) @constant.numeric

(keyword) @constant.other.keyword

; Layout block heads and special forms
((_layout_header
  (symbol) @keyword.control))

; List head: treat known specials as keyword
((list (symbol) @keyword.control)
 (#any-of? @keyword.control
   "define" "if" "cond" "when" "unless" "and" "or"
   "begin" "let" "lambda" "try" "raise"
   "import" "ref" "attr" "setq" "bindq" "delq"
   "refq" "class" "dyn-wind" "call/cc" "let/cc"
   "reset" "shift"))

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
