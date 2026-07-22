PANDOC ?= pandoc
PDF_ENGINE ?= xelatex
BOOK := pico-computer-3-book

CHAPTERS := $(sort $(wildcard [0-9][0-9]-*.md)) $(sort $(wildcard appendix-*.md))
COMMON := --metadata-file=metadata.yaml --toc --toc-depth=2 \
          --top-level-division=chapter --highlight-style=tango

# Figures. Each figure is authored as figs/NN-name.svg and rendered to a
# committed figs/NN-name.png that the chapters embed (![](figs/NN-name.png)).
# The PNGs are checked in, so the book builds with just pandoc + xelatex;
# rsvg-convert (librsvg2-bin) is needed only to (re)render after editing an
# SVG -- see the `figures` target below. (Same source-SVG/committed-PNG
# split as cover.svg / cover.png.)
FIGURES := $(wildcard figs/*.png)

all: html epub pdf

html: $(BOOK).html
epub: $(BOOK).epub
pdf:  $(BOOK).pdf
docx: $(BOOK).docx

$(BOOK).html: metadata.yaml cover.png cover-head.html cover-body.html $(FIGURES) $(CHAPTERS)
	$(PANDOC) $(COMMON) --standalone --embed-resources \
	    --include-in-header=cover-head.html \
	    --include-before-body=cover-body.html -o $@ $(CHAPTERS)

$(BOOK).epub: metadata.yaml cover.png $(FIGURES) $(CHAPTERS)
	$(PANDOC) $(COMMON) --epub-cover-image=cover.png -o $@ $(CHAPTERS)

$(BOOK).pdf: metadata.yaml preamble.tex cover.png $(FIGURES) $(CHAPTERS)
	$(PANDOC) $(COMMON) --pdf-engine=$(PDF_ENGINE) -H preamble.tex -o $@ $(CHAPTERS)

$(BOOK).docx: metadata.yaml $(CHAPTERS)
	$(PANDOC) $(COMMON) -o $@ $(CHAPTERS)

# (Re)render every figs/*.svg to a committed figs/*.png. Run after editing an
# SVG; needs rsvg-convert (`sudo apt install librsvg2-bin`). Kept out of the
# book targets on purpose, so a normal build never needs the tool.
FIG_WIDTH ?= 1560
figures:
	@for s in figs/*.svg; do \
	    rsvg-convert -w $(FIG_WIDTH) "$$s" -o "$${s%.svg}.png" && echo "rendered $$s"; \
	done

clean:
	rm -f $(BOOK).html $(BOOK).epub $(BOOK).pdf $(BOOK).docx

.PHONY: all html epub pdf docx figures clean
