" Vim syntax file
" Language: Onda
" Filename: syntax/onda.vim

if exists("b:current_syntax")
  finish
endif

syntax case match

" --- Comments (# to end of line)
syntax match ondaComment /#.*/

" --- Strings
syntax region ondaString start=/"/ skip=/\\"/ end=/"/
syntax region ondaString start=/'/ skip=/\\'/ end=/'/

" --- Numbers
syntax match ondaNumber /\v[-+]?\d+(\.\d+)?/

" --- Labels: @label
syntax match ondaLabel /\v\@[A-Za-z_][A-Za-z0-9_]*/

" --- Function definitions: : name ... ;
syntax match ondaDefine /:/ nextgroup=ondaDefName skipwhite
syntax match ondaDefName /\v[A-Za-z_][A-Za-z0-9_]*/ contained

" --- Core language keywords / ops
syntax keyword ondaKeyword and drop dup if else then while do end not or over ret rot swap
syntax keyword ondaKeyword main
syntax match   ondaKeyword /\v\.(s)?\>/
syntax match   ondaKeyword /;/

" --- Standard library builtins
syntax keyword ondaBuiltin print print_u64 print_i64 print_char print_str
syntax keyword ondaBuiltin malloc calloc free realloc
syntax keyword ondaBuiltin memcpy memset memcmp
syntax keyword ondaBuiltin strlen strcmp strcpy strncpy strcat
syntax keyword ondaBuiltin fopen fclose fread fwrite
syntax keyword ondaBuiltin exit

" --- Identifiers
syntax match ondaIdentifier /\v<[A-Za-z_][A-Za-z0-9_]*>/

" --- TODO markers inside comments
syntax match ondaTodo /\v<(TODO|FIXME|NOTE|BUG)>/ containedin=ondaComment

" --- Highlight links
highlight default link ondaComment    Comment
highlight default link ondaString     String
highlight default link ondaNumber     Number
highlight default link ondaLabel      Function
highlight default link ondaDefine     Keyword
highlight default link ondaDefName    Function
highlight default link ondaKeyword    Keyword
highlight default link ondaBuiltin    Special
highlight default link ondaIdentifier Identifier
highlight default link ondaTodo       Todo

let b:current_syntax = "onda"
