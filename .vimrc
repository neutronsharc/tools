set nocompatible              " be iMproved, required
filetype off                  " required

" F2 to toggle paste mode: in paste mode you paste external code,
" keep external indent.
nnoremap <F2> :set invpaste paste?<CR>
set pastetoggle=<F2>
set showmode

set tabstop=2 expandtab
set nu
set hlsearch
set title

set shiftwidth=2
syntax on
set autoindent
set cindent

map <C-n> :tabnext<CR>
map <C-p> :tabprevious<CR>

if has("autocmd")
  " When editing a file, always jump to the last cursor position
  autocmd BufReadPost *
  \ if line("'\"") > 0 && line ("'\"") <= line("$") |
  \   exe "normal g'\"" |
  \ endif
endif

"set tw=80
"set wrap
"set term=color_xterm

highlight OverLength ctermbg=red ctermfg=white guibg=#592929
"match OverLength /\%81v.\+/
match OverLength /\%>80v.\+/

highlight ExtraWhitespace ctermbg=red guibg=red
match ExtraWhitespace /\s\+$/

let c_space_errors = 1


""" plugins

" set the runtime path to include Vundle and initialize
set rtp+=~/.vim/bundle/Vundle.vim
call vundle#begin()
" alternatively, pass a path where Vundle should install plugins
"call vundle#begin('~/some/path/here')

" let Vundle manage Vundle, required
Plugin 'VundleVim/Vundle.vim'

Plugin 'Valloric/YouCompleteMe'

call vundle#end()            " required
filetype plugin indent on    " required

let g:ycm_global_ycm_extra_conf = "~/.vim/.ycm_extra_conf.py"

