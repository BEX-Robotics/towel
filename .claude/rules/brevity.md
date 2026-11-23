# Brevity — reply style

Chat replies only. Commits, docs, code comments keep their own rules. The TLDR rule
(CLAUDE.md) still applies on top.

Premise: reading is work. Pay per word, telegram-style. A reply is scanned, not read —
write for the scan.

## Telegram grammar

Drop what meaning survives without: articles, copulas, auxiliaries, "I", "the fact that".
Fragments are answers. Broken grammar that scans beats a full sentence that doesn't.

```
full   The build is passing now, and the three remaining warnings all come from vendored code.
terse  Build green. 3 warnings, all vendored.
```

## Terms, not descriptions

One established term replaces a clause: "race on drain()", "stale sysroot", "off-by-one
in the seq window" — not the paragraph defining them. Repo vocabulary; the reader is the team.

## Data stays raw

Numbers, paths, error strings, SHAs — verbatim. Never paraphrase data into prose:
"7 open, 5 blocked", not "most are waiting on something".

## Explain the one weird thing

Detail budget is asymmetric: the surprising item gets 2 lines, the nine normal ones get
zero. Nothing surprising → one-line reply.

## Under-explain, wait for the pull

Answer what was asked. Stop. Detail ships on request — a follow-up question costs the
reader less than a pre-emptive wall. No branches they didn't ask about.

```
Q: does the macOS build pass?
bad   I checked the macOS build and it completes successfully. There were a few
      warnings, but they appear to come from third-party code, so I don't think we
      need to worry about them.
good  Yes. 3 warnings, all vendored.
```

## Fixed words for fixed states

Same short word for the same recurring state, every time: done / blocked on X / pushed /
unpushed / unverified / dirty / flaky. Stable vocabulary scans; synonyms don't.

## Kill list

Process narrative (what you ran, read, ruled out) — cut it, or FYI it if worth keeping. Restating the question. "I've gone
ahead and", "it's worth noting", "as you can see", "successfully", "in summary",
"let me know if". Stacked hedges. Politeness padding.

## FYI: the skippable part, marked

Some text is worth having but not worth reading every time: what worked, why a request
(sudo, a reboot) is needed, how something was found, mechanism that changes nothing the
reader does. Mark it so the eye skips it and can still find it.

- **A line:** `(FYI: …)` inline, caps, in brackets.
- **More than a line:** one `text` fence at the bottom of the reply, each line `FYI: …`.
  The fence renders grey, like the `# ran:` commands (`.claude/rules/git.md`, Reporting),
  which share the same fence: both are record, not request.
- **Never FYI:** a decision, a failure, a risk warning, a command the reader must run,
  anything surprising. In doubt, it is not FYI: a warning hidden as FYI is the costly mistake.
- What the reader must act on stays above the fence, in the last screenful.

```text
FYI: AppImage + dialog fixes pushed, validated on Ubuntu 22.04.
FYI: the dmg is unsigned because there is no Apple Developer ID yet.
# ran: git push origin 6ee17f938:main
```

## Never cut

Whatever changes what the reader does next. Exact errors. Numbers. Names. Risk warnings.
A needed "why". Brevity compresses wording, not content — short, not shallow.

## Self-test

Delete a word — meaning intact? Delete it. Repeat.
