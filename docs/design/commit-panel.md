# Commit panel (design, reviewed with Fable 2026-11-19)

## Layout
Selecting the WIP row turns the right panel into the commit panel:
Unstaged (count, "Stage all") / Staged (count, "Unstage all") / message editor pinned at
the bottom (summary with soft 50/72 counter, body, Amend, button that says what it does:
"Commit 3 files to main"). File rows: click opens diff, hover +/− button or double-click
or Space toggles stage state; multi-select later.

## Diff modes
Opened from Unstaged → index→worktree, actions say "Stage". From Staged → HEAD→index,
actions "Unstage". Never the combined HEAD→worktree diff in the commit panel.

## Selection / keys (slices 2–3)
Hunk header gets "Stage hunk". Gutter click/drag selects changed lines (unified view only),
floating "Stage N lines" chip. J/K next/prev hunk, S stage, U unstage, Ctrl+Enter commit.
After staging keep the file open, scroll anchored on the next hunk; when the file has no
changes left in that mode, advance to the next file in the same list.

## Pitfalls
- No full graph refresh after stage ops: status-only reload + reload open diff; suppress
  the fingerprint poll during our own op and update the fingerprint after.
- Partially staged files appear in both lists (correct).
- Renames only visible once staged; unstage with both paths.
- Untracked dirs collapse to one row; whole-file staging only for untracked.
- Binary / submodule / mode-only: whole-file only.
- Build patches from raw bytes, preserve CR and "\ No newline at end of file".
- Serialize index ops (index.lock). Pass paths via --pathspec-from-file=- --pathspec-file-nul.
- Conflicted files: own section, no hunk staging.

## Implementation
- Whole file: `git add -A -- paths`; unstage `git restore --staged -- paths`
  (unborn HEAD: `git rm --cached`).
- Hunks/lines: synthesize single-file patch, `git apply --cached --whitespace=nowarn -`
  (`--reverse` to unstage). NOT --unidiff-zero. Staging subset: unselected `+` dropped,
  unselected `-` become context; inverted for unstage. Regenerate from a fresh 3-context
  diff even when viewing full file. Verify blob/index unchanged; on failure reload + show stderr.
- Commit: `git commit -F -` (+ `--amend`). Amend prefills HEAD message only if editor empty;
  warn if HEAD is pushed. Persist message draft per repo.

## Slices
1. Split lists, whole-file stage/unstage/all, mode-aware diff, editor, commit/amend,
   Ctrl+Enter, status-only refresh, draft persistence.
2. Hunk staging (J/K/S).
3. Line staging (unified).
Deferred: discard (needs undo), split-view line staging, partial staging of untracked,
conflict UI, templates, sign-off/GPG, message history.
