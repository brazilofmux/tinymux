;;; stdlib.tf — TitanFugue standard library
;;; Loaded automatically at startup.
;;;
;;; Based on classic TinyFugue 5.0 kb-emacs.tf keybindings.
;;; All bindings can be overridden by the user's ~/.tfrc.

;;; ---- Emacs-style input editing ----
;;; (Most of these are already built into handle_key, but binding
;;; them explicitly ensures they work even if defaults change.)

/bind ^A = /dokey home
/bind ^B = /dokey left
/bind ^D = /dokey dch
/bind ^E = /dokey end
/bind ^F = /dokey right
/bind ^K = /dokey deol
/bind ^L = /dokey redraw
/bind ^N = /dokey recallf
/bind ^P = /dokey recallb

;;; ---- Scrollback / paging ----

/bind ^V = /dokey page

;;; ---- Multi-key sequences (Esc prefix = Meta) ----

;;; Word movement
/bind Esc-b = /dokey wleft
/bind Esc-f = /dokey wright

;;; Socket cycling (next/previous connection)
/bind Esc-n = /dokey socketf
/bind Esc-p = /dokey socketb

;;; Arrow-key socket cycling (classic TF Esc-Left / Esc-Right)
/bind Esc Left = /fg_prev
/bind Esc Right = /fg_next

;;; Scrollback
/bind Esc-v = /dokey pageback
/bind Esc-> = /dokey flush

;;; Shell
/bind Esc-! = /sh

;;; ---- Ctrl-X prefix sequences ----

/bind ^X^B = /listsockets
/bind ^Xk = /dc

;;; ---- Help ----
;;; Classic TF bound ^H^H and ^H? but ^H maps to Backspace in
;;; modern terminals, making rapid backspace trigger /help.
;;; Use F1 instead.

/bind F1 = /help
