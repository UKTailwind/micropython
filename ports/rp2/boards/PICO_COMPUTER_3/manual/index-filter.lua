-- Auto-build the PDF's alphabetical index from inline code spans.
-- Every identifier-like `code` mention (`keydown(1)`, `pcaudio`,
-- `main.py`, ...) emits a \index{} entry, so the finished manual ends
-- with an index of each API name and the pages that mention it. The
-- markdown needs no markup of its own. Headings are skipped (\index is
-- unreliable inside \section) and Python keywords/builtins are dropped
-- as noise. LaTeX/PDF output only; other formats pass through.

local STOP = {}
for w in ([[True False None and or not in is if elif else for while def
class return import from as with try except finally break continue pass
global nonlocal lambda del print input self range len int str float bool
bytes list dict tuple set ord chr abs min max sum py md txt]]):gmatch('%S+') do
  STOP[w] = true
end

local function index_code(el)
  local name = el.text:match('^([%a_][%w_%.]*)%s*%(')
            or el.text:match('^([%a_][%w_%.]*)$')
  if not name or #name < 2 or STOP[name] then return nil end
  if name:match('^%a%d+$') then return nil end -- a1, x2: pin/param noise
  -- makeindex treats ! @ | " as special: escape with a leading ".
  -- Sort under the plain name; display in \texttt with _ escaped.
  local sort = name:gsub('[!@|"]', '"%0')
  local disp = sort:gsub('_', '\\_')
  return {el, pandoc.RawInline('latex',
      '\\index{' .. sort .. '@\\texttt{' .. disp .. '}}')}
end

function Pandoc(doc)
  if not FORMAT:match('latex') then return nil end
  for i, blk in ipairs(doc.blocks) do
    if blk.t ~= 'Header' then
      doc.blocks[i] = blk:walk({Code = index_code})
    end
  end
  return doc
end
