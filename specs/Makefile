.SUFFIXES: .pdf .ps .dvi .tex

.tex.pdf:
	pdflatex $<
	pdflatex $<

.tex.dvi:
	latex $<
	latex $<

.dvi.ps:
	dvips $<

all: muxman.pdf muxman.ps

muxman.pdf: muxman.tex

muxman.dvi: muxman.tex

muxman.ps: muxman.dvi

clean:
	-rm -rf muxman.pdf muxman.ps muxman.dvi muxman.ps muxman.aux muxman.log muxman.idx muxman.toc muxman.out
