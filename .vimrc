
set nocompatible              " be iMproved, required
filetype off                  " required

" F2 to toggle paste mode: in paste mode you paste external code,
" keeping external indent.
nnoremap <F2> :set invpaste paste?<CR>
set pastetoggle=<F2>
set showmode

" set the runtime path to include Vundle and initialize
set rtp+=~/.vim/bundle/Vundle.vim
call vundle#begin()
" alternatively, pass a path where Vundle should install plugins
"call vundle#begin('~/some/path/here')

" let Vundle manage Vundle, required
Plugin 'VundleVim/Vundle.vim'

Plugin 'Valloric/YouCompleteMe'

" The following are examples of different formats supported.
" Keep Plugin commands between vundle#begin/end.
" plugin on GitHub repo
"Plugin 'tpope/vim-fugitive'
" plugin from http://vim-scripts.org/vim/scripts.html
"Plugin 'L9'
" Git plugin not hosted on GitHub
"Plugin 'git://git.wincent.com/command-t.git'
" git repos on your local machine (i.e. when working on your own plugin)
"Plugin 'file:///home/gmarik/path/to/plugin'
" The sparkup vim script is in a subdirectory of this repo called vim.
" Pass the path to set the runtimepath properly.
"Plugin 'rstacruz/sparkup', {'rtp': 'vim/'}
" Avoid a name conflict with L9
"Plugin 'user/L9', {'name': 'newL9'}

" All of your Plugins must be added before the following line
call vundle#end()            " required
filetype plugin indent on    " required

" To ignore plugin indent changes, instead use:
"filetype plugin on
"
" Brief help
" :PluginList       - lists configured plugins
" :PluginInstall    - installs plugins; append `!` to update or just :PluginUpdate
" :PluginSearch foo - searches for foo; append `!` to refresh local cache
" :PluginClean      - confirms removal of unused plugins; append `!` to auto-approve removal
"
" see :h vundle for more details or wiki for FAQ
" Put your non-Plugin stuff after this line

"set tabstop=4
set tabstop=2 expandtab
set nu
set hlsearch
set title

set shiftwidth=2
syntax on
set autoindent
set cindent

"set tw=80
"set wrap

"set term=color_xterm

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

"let g:ycm_global_ycm_extra_conf = "~/.vim/bundle/YouCompleteMe/third_party/ycmd/cpp/ycm/.ycm_extra_conf.py"
let g:ycm_global_ycm_extra_conf = "~/.vim/.ycm_extra_conf.py"

colorscheme torte
