"enable per-project vimrc
set exrc

set t_Co=256

set smartindent

autocmd Filetype gitcommit setlocal spell textwidth=72

"for linux kernel git
filetype plugin indent on
syn on se title

"linux kernel use tabs for indent; each tab 8 columns
set tabstop=8
set shiftwidth=8
set softtabstop=8
set noexpandtab
