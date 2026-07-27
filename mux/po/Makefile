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

# -c is --check: among other things it verifies that a c-format msgstr uses
# the same conversions as its msgid.  Without it msgfmt silently compiles a
# catalogue whose translated format takes an int where the caller passes a
# pointer, and mux_vsnprintf then walks that int as a UTF8 * -- measured as a
# SIGSEGV on `give <thing>=5` with "You give %d %s to %s." mistranslated with
# %s in place of %d (#1419 Phase 3).
#
# tests/nls/check_nls.py makes the same check over every .po in this tree, but
# only runs under `make test`.  This flag protects anyone who just builds a
# catalogue -- including translators working outside the repo, who are the
# people most likely to introduce the mismatch and least likely to run the
# suite.
mo: check-tools
	mkdir -p ../game/locale/xx/LC_MESSAGES
	msgfmt -c -o ../game/locale/xx/LC_MESSAGES/tinymux.mo xx.po
	@echo "wrote ../game/locale/xx/LC_MESSAGES/tinymux.mo"
