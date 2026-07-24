#!/bin/sh
# Build USER_MANUAL.pdf from USER_MANUAL.md, with page numbers
# (Page N of M footer via fancyhdr) and an auto-generated alphabetical
# index (manual/index-filter.lua + imakeidx). Output lands next to the
# md as USER_MANUAL.pdf; pass a path to put it elsewhere:
#     manual/build.sh [output.pdf]
# Needs pandoc + texlive-xetex etc. -- the same toolchain as the book
# (book/README.md). Pandoc's own PDF mode can't run makeindex (it
# compiles under -output-directory, where imakeidx's shell escape fails
# to find the .idx), so this generates .tex and drives xelatex +
# makeindex + xelatex twice more (TOC and LastPage need the reruns).
set -e
cd "$(dirname "$0")/.."
OUT=${1:-USER_MANUAL.pdf}
case "$OUT" in /*) ;; *) OUT="$(pwd)/$OUT" ;; esac
TMP=$(mktemp -d)
# MANUAL_KEEP_TMP=1 keeps the LaTeX build dir for inspection.
[ -n "$MANUAL_KEEP_TMP" ] && echo "build dir: $TMP" \
    || trap 'rm -rf "$TMP"' EXIT
pandoc USER_MANUAL.md -o "$TMP/manual.tex" --standalone \
    --lua-filter=manual/index-filter.lua \
    -H manual/preamble.tex --include-after-body=manual/after.tex \
    -V mainfont='DejaVu Sans' -V monofont='DejaVu Sans Mono' \
    -V geometry:margin=2cm -V fontsize=10pt --toc --toc-depth=2
cd "$TMP"
# TeX serialises \texttt into the .idx with context-dependent spacing;
# collapse it or makeindex splits one name into two index entries.
# (Pattern is backslash-free on purpose: GNU sed reads \t as TAB.)
fixidx() { sed -i 's/texttt  *{/texttt{/g' manual.idx; }
xelatex -interaction=batchmode manual.tex >/dev/null
fixidx; makeindex -q manual.idx
xelatex -interaction=batchmode manual.tex >/dev/null
fixidx; makeindex -q manual.idx
xelatex -interaction=batchmode manual.tex >/dev/null
cp manual.pdf "$OUT"
echo "wrote $OUT"
