PANDOC ?= pandoc
PDF_ENGINE ?= xelatex
BOOK := pico-computer-3-book

CHAPTERS := $(sort $(wildcard [0-9][0-9]-*.md)) $(sort $(wildcard appendix-*.md))
COMMON := --metadata-file=metadata.yaml --toc --toc-depth=2 \
          --top-level-division=chapter --highlight-style=tango

all: html epub pdf

html: $(BOOK).html
epub: $(BOOK).epub
pdf:  $(BOOK).pdf
docx: $(BOOK).docx

$(BOOK).html: metadata.yaml $(CHAPTERS)
	$(PANDOC) $(COMMON) --standalone --embed-resources -o $@ $(CHAPTERS)

$(BOOK).epub: metadata.yaml $(CHAPTERS)
	$(PANDOC) $(COMMON) -o $@ $(CHAPTERS)

$(BOOK).pdf: metadata.yaml preamble.tex $(CHAPTERS)
	$(PANDOC) $(COMMON) --pdf-engine=$(PDF_ENGINE) -H preamble.tex -o $@ $(CHAPTERS)

$(BOOK).docx: metadata.yaml $(CHAPTERS)
	$(PANDOC) $(COMMON) -o $@ $(CHAPTERS)

clean:
	rm -f $(BOOK).html $(BOOK).epub $(BOOK).pdf $(BOOK).docx

.PHONY: all html epub pdf docx clean
