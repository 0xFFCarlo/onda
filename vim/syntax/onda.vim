" Vim syntax file
" Language: Onda
" Maintainer: you :)
" Filename: syntax/onda.vim

if exists("b:current_syntax")
  finish
endif

" --- Comments (# to end of line)
syntax match ondaComment /#.*/

" --- Strings (simple + with escapes)
" Double-quoted
syntax region ondaString start=/"/ skip=/\\"/ end=/"/
" Single-quoted (optional, remove if you don't want it)
syntax region ondaString start=/'/ skip=/\\'/ end=/'/

" --- Numbers
" Integers + floats, optional leading sign
syntax match ondaNumber /\v[-+]?\d+(\.\d+)?/

" --- Labels: @label (start with @, then identifier-ish)
syntax match ondaLabel /\v\@[A-Za-z_][A-Za-z0-9_]*/

" --- Keywords / ops (your list)
" Note: '.' is special in regex, so we escape it.
syntax keyword ondaKeyword and drop dup jmp jmp_if or over ret rot swap
syntax match   ondaKeyword /\v\.(s)?\>/
" If you want ONLY '.' and '.s' (not '.anything'), use:
" syntax match ondaKeyword /\v\.(s)?\ze(\s|$)/

" --- Identifiers (everything else word-like)
" Mark as Identifier so it's distinct from keywords/labels/numbers
syntax match ondaIdentifier /\v<[A-Za-z_][A-Za-z0-9_]*>/

" --- Optional: highlight TODO-ish inside comments
syntax match ondaTodo /\v<(TODO|FIXME|NOTE|BUG)>/ containedin=ondaComment

" --- Link to highlight groups (works with any colorscheme)
highlight default link ondaComment    Comment
highlight default link ondaString     String
highlight default link ondaNumber     Number
highlight default link ondaLabel      Function
highlight default link ondaKeyword    Keyword
highlight default link ondaIdentifier Identifier
highlight default link ondaTodo       Todo

let b:current_syntax = "onda"

