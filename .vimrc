"set tabstop=4
"set tabstop=4
set tabstop=2 expandtab
set nu
set hlsearch
set title

set shiftwidth=2
syntax on
set autoindent
set cindent

set tw=80
set wrap

highlight OverLength ctermbg=red ctermfg=white guibg=#592929
"match OverLength /\%81v.\+/
match OverLength /\%>80v.\+/

highlight ExtraWhitespace ctermbg=darkgreen guibg=darkgreen
match ExtraWhitespace /\s\+$/

let c_space_errors = 1

"""hit F2 ==  retab, then save
" map <F2> :retab <CR> :w! <CR>
""source cscope_maps.vim

" map tn :tabnew<CR>
" map wn <C-w><C-w>
map <C-n> :tabnext<CR>
map <C-p> :tabprevious<CR>


if has("autocmd")
  " When editing a file, always jump to the last cursor position
  autocmd BufReadPost *
  \ if line("'\"") > 0 && line ("'\"") <= line("$") |
  \   exe "normal g'\"" |
  \ endif
endif

"sourc ~/.vim/google.vim

"noremap <C-K> :ClangFormat<CR>
"inoremap <C-K> <C-O>:ClangFormat<CR>
