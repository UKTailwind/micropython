"""Extract every complete example program from the book chapters into
book/examples/chNN/, one directory per chapter, ordered and named from
the prose where possible. REPL transcripts (blocks with >>>) are skipped.
"""
import ast
import re
import pathlib
import shutil

BOOK = pathlib.Path(__file__).resolve().parent
OUT = BOOK / "examples"

NAME_RE = re.compile(r"\b([A-Za-z0-9_]+\.py)\b")

# The reader's toolkit (chapter 36's list): modules later programs import.
# Copied into examples/lib/ under their real names so imports resolve.
LIBS = [("shapes.py", "ch11"), ("scorelib.py", "ch12"),
        ("handy.py", "ch13"), ("sfx.py", "ch20"),
        ("initials.py", "ch26"), ("gpad.py", "ch32"),
        ("bench.py", "ch35")]


def extract(md_path):
    lines = md_path.read_text(encoding="utf-8").splitlines()
    blocks = []  # (fence_start_line, fence_end_line, code_lines)
    i = 0
    while i < len(lines):
        if lines[i].strip() == "```python":
            indent = len(lines[i]) - len(lines[i].lstrip())
            j = i + 1
            while j < len(lines) and lines[j].lstrip() != "```":
                j += 1
            code = [l[indent:] if l[:indent].isspace() or not l[:indent] else l
                    for l in lines[i + 1:j]]
            blocks.append((i, j, code))
            i = j + 1
        else:
            i += 1
    return lines, blocks


def find_name(lines, start, end):
    """Nearest `something.py` mention: prose just before the block wins,
    then prose just after."""
    for k in range(start - 1, max(-1, start - 13), -1):
        if lines[k].strip().startswith("```"):
            break  # don't wander into the previous code block
        m = NAME_RE.findall(lines[k])
        if m:
            return m[-1]
    for k in range(end + 1, min(len(lines), end + 5)):
        if lines[k].strip().startswith("```"):
            break
        m = NAME_RE.findall(lines[k])
        if m:
            return m[0]
    return None


def chapter_title(lines):
    for l in lines:
        if l.startswith("# "):
            return l[2:].strip()
    return ""


def main():
    if OUT.exists():
        shutil.rmtree(OUT)
    OUT.mkdir()

    index = ["# Example programs, extracted from the book chapters",
             "",
             "One directory per chapter; files are numbered in the order they",
             "appear in the chapter, with the program's name from the text where",
             "it has one. REPL transcripts (`>>>` sessions) are not included --",
             "those are typed at the prompt, not run as programs. Files named",
             "`NN-fragment.py` are partial snippets shown mid-explanation and",
             "will not run on their own.",
             "",
             "Files here are generated -- edit the chapters, not these copies.",
             "Regenerate after editing chapters with:",
             "`python3 extract_examples.py` (run in the book directory).",
             ""]
    total = 0
    for md in sorted(BOOK.glob("[0-9][0-9]-*.md")):
        lines, blocks = extract(md)
        progs = [(s, e, code) for (s, e, code) in blocks
                 if not any(l.lstrip().startswith(">>>") for l in code)]
        if not progs:
            continue
        chdir = OUT / ("ch" + md.name[:2])
        chdir.mkdir()
        index.append("## ch%s -- %s" % (md.name[:2], chapter_title(lines)))
        index.append("")
        n = 0
        for (s, e, code) in progs:
            n += 1
            body = "\n".join(code).rstrip() + "\n"
            try:
                ast.parse(body)
                fragment = False
            except SyntaxError:
                fragment = True
            name = find_name(lines, s, e)
            if fragment:
                fname = "%02d-fragment.py" % n
            elif name:
                fname = "%02d-%s" % (n, name)
            else:
                fname = "%02d-listing.py" % n
            with open(chdir / fname, "w", encoding="utf-8", newline="\n") as f:
                f.write(body)
            note = " -- fragment shown mid-explanation, not standalone" \
                if fragment else ""
            index.append("- `%s` (%d lines)%s" % (fname, len(code), note))
            total += 1
        index.append("")
    index.append("Total: %d programs." % total)
    index.append("")

    # -- the importable toolkit, under its real names ------------------
    libdir = OUT / "lib"
    libdir.mkdir()
    index.append("## lib -- the reader's toolkit, under its real names")
    index.append("")
    index.append("Copy the contents of `lib/` into `/lib` on the flash drive")
    index.append("(or the directory you run from) so programs that import")
    index.append("`shapes`, `handy`, `bench` etc. find them. `handy.py` is the")
    index.append("chapter 13 module with chapter 18's `held()` added, as the")
    index.append("book instructs.")
    index.append("")
    for libname, ch in LIBS:
        src = sorted((OUT / ch).glob("*-" + libname))
        if not src:
            raise SystemExit("toolkit module %s not found in %s" % (libname, ch))
        body = src[0].read_text(encoding="utf-8")
        if libname == "handy.py":
            held = sorted((OUT / "ch18").glob("*-handy.py"))[0]
            body = (body.rstrip() + "\n\n\n"
                    + "# held() -- chapter 18's keyboard helper, given its\n"
                    + "# permanent home here as chapter 21 instructs.\n"
                    + held.read_text(encoding="utf-8"))
            note = " (chapter 13 + chapter 18's held())"
        else:
            note = " (from %s)" % ch
        with open(libdir / libname, "w", encoding="utf-8", newline="\n") as f:
            f.write(body)
        index.append("- `%s`%s" % (libname, note))
    index.append("")
    with open(OUT / "README.md", "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(index))
    print("wrote %d programs into %s" % (total, OUT))


if __name__ == "__main__":
    main()
