.SUFFIXES: .pdf .ps .dvi .tex

.tex.pdf:
	pdflatex $<

.tex.dvi:
	latex $<

.dvi.ps:
	dvips $<

all: muxman.pdf muxman.ps

muxman.pdf: muxman.tex

muxman.dvi: muxman.tex

muxman.ps: muxman.dvi
