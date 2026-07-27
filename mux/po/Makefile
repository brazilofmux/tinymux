# Server-message catalog tools (#1419 / #1473).
# Not part of the main autoconf recursive build — run from mux/po as needed.

.PHONY: pot mo check-tools help

help:
	@echo "Targets:"
	@echo "  pot   - regenerate tinymux.pot from M_()/N_() (xgettext)"
	@echo "  mo    - build game/locale/xx/LC_MESSAGES/tinymux.mo from xx.po"

check-tools:
	@command -v xgettext >/dev/null || { echo "need xgettext (gettext)"; exit 1; }
	@command -v msgfmt >/dev/null || { echo "need msgfmt (gettext)"; exit 1; }

pot: check-tools
	./update-pot.sh

mo: check-tools
	mkdir -p ../game/locale/xx/LC_MESSAGES
	msgfmt -o ../game/locale/xx/LC_MESSAGES/tinymux.mo xx.po
	@echo "wrote ../game/locale/xx/LC_MESSAGES/tinymux.mo"
