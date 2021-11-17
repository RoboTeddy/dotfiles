;; TODO ————————————————————————————————————————————————————————————————————————
;; * try company-mode (w/ cider integration. careful of integration w/ evil)
;; * try smartparens and evil-smartparens
;; * look into auto-fill-mode
;; * look into magit
;; * remove neotree
;; * remove unused packages
;; * modify git-gutter to debounces buffer changes (like sublime's)
;; * consider removing window navigation via []
;; * consder using https://github.com/jwiegley/use-package
;; * make Ag use j/k to go prev/next rather than n/p
;; * consider:
;; (define-key evil-normal-state-map (kbd "j") 'evil-next-visual-line)
;; (define-key evil-normal-state-map (kbd "k") 'evil-previous-visual-line)
;; (define-key global-map (kbd "RET") 'newline-and-indent)


;; Simplify the window immediately on startup ——————————————————————————————————

(menu-bar-mode -1)
(tool-bar-mode -1)
(scroll-bar-mode -1)

(require 'cl)

;; Packages ————————————————————————————————————————————————————————————————————

(require 'package)
(add-to-list 'package-archives
	     '("melpa" . "http://melpa.milkbox.net/packages/") t)
(add-to-list 'package-archives
	     '("melpa-stable" . "http://melpa-stable.milkbox.net/packages/"))

;; Cider from MELPA is purportedly a bit too unstable
(add-to-list 'package-pinned-packages '(cider . "melpa-stable") t)

(setq package-enable-at-startup nil)
(package-initialize)

(defvar my-packages
  '(ag
    aggressive-indent
    align-cljlet
    cider
    clojure-mode
    evil
    evil-leader
    expand-region
    fill-column-indicator
    flx-ido
    ido-completing-read+
    ido-vertical-mode
    js2-mode
    json-mode
    json-reformat
    paredit
    projectile
    rainbow-delimiters
    smex))

(defun my-packages-installed-p ()
  (loop for p in my-packages
	when (not (package-installed-p p)) do (return nil)
        finally (return t)))

(unless (my-packages-installed-p)
  (package-refresh-contents)
  (dolist (p my-packages)
    (when (not (package-installed-p p))
      (package-install p))))

(dolist (p my-packages)
  (when (not (package-installed-p p))
    (package-install p)))

;; General —————————————————————————————————————————————————————————————————————

(setq inhibit-startup-message t)
(setq dired-use-ls-dired nil) ;; gets rid of a message during startup
(defun my-bell-function ()
  (unless (memq this-command
		'(isearch-abort abort-recursive-edit exit-minibuffer
				keyboard-quit mwheel-scroll down up next-line previous-line
				backward-char forward-char))
    (ding)))
(setq ring-bell-function 'my-bell-function)

(setq mouse-wheel-scroll-amount '(1 ((shift) . 1))) ;; one line at a time
(setq mouse-wheel-progressive-speed nil) ;; don't accelerate scrolling

;; Desktop —————————————————————————————————————————————————————————————————————

;;(setq desktop-path (list "~/emacs-blah"))
(desktop-save-mode 1)

;; File management —————————————————————————————————————————————————————————————

(global-auto-revert-mode 1)
(setq auto-save-visited-file-name t)
(add-hook 'focus-out-hook (lambda () (interactive) (save-some-buffers t)))
(setq require-final-newline t)
(add-hook 'before-save-hook 'delete-trailing-whitespace)
(add-to-list 'auto-mode-alist '("\\.log\\'" . auto-revert-tail-mode))

(setq create-lockfiles nil)
(make-directory "~/.emacs-saves" t)
(setq backup-directory-alist `(("." . "~/.emacs-backups"))
      auto-save-file-name-transforms '((".*" "~/.emacs-saves/\\1" t))
      backup-by-copying t
      delete-old-versions t
      kept-new-versions 6
      kept-old-versions 2
      version-control t)

;; Dired ———————————————————————————————————————————————————————————————————————

(add-hook 'dired-mode-hook 'auto-revert-mode)

;; reuse dired buffers
(put 'dired-find-alternate-file 'disabled nil)
(add-hook 'dired-mode-hook
	  (lambda ()
	    (define-key dired-mode-map (kbd "^")
	      (lambda () (interactive) (find-alternate-file "..")))
					; was dired-up-directory
	    ))

;; Evil ————————————————————————————————————————————————————————————————————————

(global-evil-leader-mode) ;; must be enabled prior to evil-mode
(evil-mode 1)
;; investigate: evil-want-visual-char-semi-exclusive
;;(setq evil-move-cursor-back nil ;; paredit structured navigation compatibility
;;      evil-highlight-closing-paren-at-point-states)

;; Make movement keys work like they should
(define-key evil-normal-state-map (kbd "<remap> <evil-next-line>") 'evil-next-visual-line)
(define-key evil-normal-state-map (kbd "<remap> <evil-previous-line>") 'evil-previous-visual-line)
(define-key evil-motion-state-map (kbd "<remap> <evil-next-line>") 'evil-next-visual-line)
(define-key evil-motion-state-map (kbd "<remap> <evil-previous-line>") 'evil-previous-visual-line)
;; Make horizontal movement cross lines
(setq-default evil-cross-lines t)



;; Projectile ——————————————————————————————————————————————————————————————————

(projectile-global-mode)

;; Ag ——————————————————————————————————————————————————————————————————————————

(setq ag-reuse-buffers 't)
(setq ag-highlight-search t)

;; Keyboard ————————————————————————————————————————————————————————————————————

(setq mac-command-key-is-meta t
      mac-option-modifier 'meta
      mac-command-modifier 'control)

;; Be quieter ——————————————————————————————————————————————————————————————————

(setq ring-bell-function
      (lambda ()
	(unless (memq this-command
		      '(isearch-abort abort-recursive-edit exit-minibuffer
				      keyboard-quit))
	  (ding))))

;; Rainbow delimiters ——————————————————————————————————————————————————————————

(add-hook 'prog-mode-hook 'rainbow-delimiters-mode)

;; Theme ———————————————————————————————————————————————————————————————————————

(add-to-list 'custom-theme-load-path "~/.emacs.d/themes")
(add-to-list 'load-path "~/.emacs.d/themes")
(load-theme 'ample-zen t)

;; Fill column

(setq-default fill-column 80)
;;(add-hook 'after-change-major-mode-hook 'fci-mode)
(add-hook 'prog-mode-hook 'fci-mode)
(add-hook 'text-mode-hook 'fci-mode)

;; Git gutter

;;(global-git-gutter-mode 1)
;;(custom-set-variables '(git-gutter:update-interval 3)) ;; notice git commits

;; Ido —————————————————————————————————————————————————————————————————————————

(ido-mode 1)
(ido-everywhere 1)
(flx-ido-mode 1)
;; disable ido faces to see flx highlights.
(setq ido-enable-flex-matching t)
(setq ido-use-faces nil)

(ido-vertical-mode 1)
(setq ido-vertical-define-keys 'C-n-and-C-p-only)

;; Smex ————————————————————————————————————————————————————————————————————————

(smex-initialize)
(global-set-key (kbd "M-x") 'smex)

;; Indentation —————————————————————————————————————————————————————————————————

(global-aggressive-indent-mode 1)

;; Selections / Regions ————————————————————————————————————————————————————————

(global-set-key (kbd "C-=") 'er/expand-region)
(delete-selection-mode 1)

;; Paredit —————————————————————————————————————————————————————————————————————
(autoload 'enable-paredit-mode "paredit"
	  "Turn on pseudo-structural editing of Lisp code." t)

;; make paredit work with delete-selection-mode
;; (put 'paredit-forward-delete 'delete-selection 'supersede)
;; (put 'paredit-backward-delete 'delete-selection 'supersede)
;; (put 'paredit-open-round 'delete-selection t)
;; (put 'paredit-open-square 'delete-selection t)
;; (put 'paredit-doublequote 'delete-selection t)
;; (put 'paredit-newline 'delete-selection t)

;; remove fwd/bkwd by sentence to make way
(define-key evil-motion-state-map "(" nil)
(define-key evil-motion-state-map ")" nil)

;; evil structured navigation
(evil-define-key 'normal paredit-mode-map (kbd "C-S-l") 'paredit-forward)
(evil-define-key 'normal paredit-mode-map (kbd "C-S-h") 'paredit-backward)
(evil-define-key 'normal paredit-mode-map (kbd "C-S-j") 'paredit-forward-down)
(evil-define-key 'normal paredit-mode-map (kbd "C-S-k") 'paredit-backward-up)

;; ideas: https://github.com/andrewmcveigh/emacs.d/blob/master/lisp/init-evil.el


;; Misc lisps ——————————————————————————————————————————————————————————————————

(add-hook 'emacs-lisp-mode-hook       #'enable-paredit-mode)
(add-hook 'eval-expression-minibuffer-setup-hook #'enable-paredit-mode)
(add-hook 'ielm-mode-hook             #'enable-paredit-mode)
(add-hook 'lisp-mode-hook             #'enable-paredit-mode)
(add-hook 'lisp-interaction-mode-hook #'enable-paredit-mode)
(add-hook 'scheme-mode-hook           #'enable-paredit-mode)

;; Clojure —————————————————————————————————————————————————————————————————————

(require 'clojure-mode)

(add-to-list 'auto-mode-alist '("\\.edn$" . clojure-mode))

(add-hook 'clojure-mode-hook 'enable-paredit-mode)
(add-hook 'clojure-mode-hook 'subword-mode)

(let ((modes '((and-let 1) (dofor 1) (debounced 1) (this-as 1))))
  (loop for p in modes
	do
	(put-clojure-indent (first p) (second p))))
;;(global-set-key "\C-c\ \C-v" 'align-cljlet)

;; Cider ———————————————————————————————————————————————————————————————————————

(add-hook 'cider-mode-hook 'cider-turn-on-eldoc-mode)
(add-hook 'cider-repl-mode-hook 'paredit-mode)
(add-hook 'cider-repl-mode-hook 'rainbow-delimiters-mode)

(setq cider-repl-pop-to-buffer-on-connect t
      cider-show-error-buffer t
      cider-auto-select-error-buffer t
      cider-repl-history-file "~/.emacs.d/cider-history"
      cider-repl-wrap-history t)

(evil-define-key 'normal cider-doc-mode-map "q" 'cider-popup-buffer-quit-function)
(evil-define-key 'normal cider-stacktrace-mode-map "q" 'cider-popup-buffer-quit-function)
(evil-define-key 'normal cider-popup-buffer-mode-map "q" 'cider-popup-buffer-quit-function)

;; Javascript ——————————————————————————————————————————————————————————————————

(setq js2-basic-offset 2)
(setq js2-mode-indent-ignore-first-tab t)
(setq js2-highlight-external-variables t)
(setq js2-highlight-level 3)
(setq js2-mirror-mode nil)
(setq js2-mode-show-parse-errors t)
(setq js2-mode-show-strict-warnings nil)
(setq js2-bounce-indent-p t)
(setq js2-pretty-multiline-declarations t)

;; Random stuff ————————————————————————————————————————————————————————————————

;; credit: aaiba
(defun insert-ascii-hr ()
  (interactive)
  (let ((dash "\u2014"))
    (move-end-of-line nil)
    (let ((c (char-to-string (char-before))))
      (when (not (or (string= c " ")
                     (string= c dash)))
        (insert " ")))
    (while (< (current-column) fill-column)
      (insert dash))))
(global-set-key (kbd "<f12>") 'insert-ascii-hr)

;; navigating buffers
(define-key evil-normal-state-map (kbd "]")
  (lambda () (interactive) (other-window 1)))
(define-key evil-normal-state-map (kbd "[")
  (lambda () (interactive) (other-window -1)))

(defun split-horizontally-evenly ()
  (interactive)
  (command-execute 'split-window-horizontally)
  (command-execute 'balance-windows))

(global-set-key (kbd "C-x k") 'kill-this-buffer)

(evil-leader/set-leader "<SPC>")
(evil-leader/set-key
  "x" 'smex
  "p" 'projectile-switch-project
  "f" 'projectile-find-file
  "g" 'projectile-ag
  "b" 'project-explorer-toggle
  "c" 'comment-or-uncomment-region
  "e" 'projectile-dired
  "l" 'cider-eval-last-sexp
  "b" 'cider-eval-buffer
  "n" 'cider-eval-ns-form
  "h" 'split-horizontally-evenly)
