# Towel

A fast, lean git history viewer and client: Qt 6 Widgets, everything custom-painted,
libgit2 for reads, the `git` CLI for every write. GPL-3.0-or-later, public at
github.com/pavel-teramips/towel. Tagline: "Always know where your HEAD is."

Working conventions are BEX Robotics' house style, adapted here: how to answer,
how to write C++, how to commit. The rules files under `.claude/rules/` carry the detail.


## Answering: any reply longer than 3 lines opens with a TLDR

Put it first, not last. Then the detail underneath for whoever wants it.

A TLDR is the **answer**, compressed — at most two short lines. Nothing else goes in it:

- Only what changes what the reader does next. If a sentence causes no action, cut it.
- No commentary on the process, no restating the question, no observing that something worked.
- Fewest characters that still answer. A fragment beats a sentence; a number beats a description.
- Open with what's NOT done and what the reader must decide or provide. What's achieved comes
  last, brief — or not at all. Never lead with success: done work needs nothing from the reader,
  so it earns the least prominent slot.

```
bad   Seven open, and the rebase-editor one is gone from the list
      because it's done — that's the tracker working as intended.
good  7 open. 2 need your call. Pick the shortcut for #3 and I start.

bad   Pulled and rebased cleanly — no conflicts, and the C++ still builds,
      so we're in good shape to continue.
good  Rebased clean, builds. 20 local commits.

bad   AppImage fix pushed and validated; dialog fixed too. Signing still open —
      need your go on the SignPath application.
good  Windows build unsigned — say go on SignPath and I apply.
      (AppImage + dialog fixes pushed.)
```

The test: delete the TLDR. If the reader loses nothing they needed, it was padding.

## The reader sees the last screenful

A guideline for judgement, not a rule. A terminal shows the last screenful. Whatever was said before
a run of tool calls, and the top of a long reply, may have scrolled away unread.

So the answer to the question, and whatever the reader has to decide or do, tend to belong within
that last screenful — not necessarily on its final line. Before closing a turn, picture what is
actually on screen and say again whatever the reader would otherwise miss — a question raised
mid-turn, a warning, a failure — rather than pointing up at it. The TLDR still opens the reply; on
one longer than a screen, the last screenful is its second home.

**Write for a person, not a log.** The reader is a busy human juggling other work: they remember
the gist of what was said a while ago, not its details, and digging back for them is work the
writer can spare them. So when something comes back (a decision, a step, a term), it comes with
enough to act on it right there: what it is in plain words, what each answer does, what it costs.
A bare "Yes or no?" that leans on an explanation from screens ago makes the reader do the
writer's job. The aim is working together with less friction, not a rule about where text sits.

## Lead with what matters, then stop

Length should track what the reader has to do with it. A finished change needs a line or two — the
diff and the commit message carry the detail, and that is where a reader who wants it will look.
Save the longer reply for the times something is blocked, something needs deciding, or you found
something the reader would want to know and wouldn't otherwise see.

Most of what pads a reply is the path you took to the answer: what you ran, what you read, what you
ruled out, what you verified along the way. That is your working, not theirs. Mechanism is the same
— worth writing when it changes what they do next, worth putting in the commit message otherwise.

Lean toward doing rather than asking. When the next step is clear and cheap to undo, take it and say
what changed. Keep asking for the things that are hard to walk back — pushing, deleting, a
release — and for real forks, where the answer changes the work rather than just confirming it.

## Fix the obstacle, not the route around it

When something in the path is broken — a config, a script that hangs, a missing dependency, a wrong
default — the answer is the fix at its source, and where it fits a smarter version of it: a timeout,
a check that names the cause, a default that works. A one-off override or a manual step leaves the
trap armed for the next person; offer one only as a labelled stopgap beside the fix, never instead
of it. This holds for the repo's scripts and CI and for a developer's machine alike.

Lean toward modern, quality tools. An outdated tool, runtime or dependency, or a weaker tool where a
better one exists, gets an upgrade recommended with its safe path: what it needs (`npm view <pkg>
engines`), what to rebuild, how CI proves it. "It works" is not a reason to stay stale. Risk decides
how and when to upgrade, one verified step at a time and not in the middle of an incident; it does
not decide whether. Pinned versions stay pinned, to current.

## What Towel is for, and what that rules out

- **Memory and startup are features.** The owner's standing priority: less memory is always
  faster. Measure before and after anything that could grow them (`/proc/<pid>/status` VmRSS,
  library count in `/proc/<pid>/maps`); a change that costs memory says how much in its commit.
  No Qt Quick/QML, no web engine, no platform-theme plugins (`main.cpp` hides the desktop from Qt
  for that reason).
- **The UI thread never waits on git.** Reads run on a worker (`QtConcurrent::run(...).then(this,
  ...)`) with their own libgit2 handle; writes run `git` through `QProcess`, asynchronously. The
  older synchronous `gitSync()` calls on the UI thread are debt: don't add new ones, and move an old
  one off when touching its code.
- **Writes go through the `git` CLI**, never libgit2: hooks, credentials, config and the user's
  expectations all live there. Every write that moves a ref goes through the undo journal
  (`Repo::journalBegin`/`journalEnd`).
- **Everything is painted, and scales.** Every pixel size goes through `theme::z()` and every font
  through `theme::uiFont`/`monoFont`, so Ctrl+scroll zoom reaches it.
- **Cross-platform.** Linux, macOS and Windows build in CI (`.github/workflows/build.yml`); nothing
  Linux-only without an `#ifdef` and a fallback.

## Layout

- `src/`: the app. `gitload.*` pure reads (worker-safe), `repo.*` the state and every git
  operation, `*view.*`/`panels.*`/`commitpanel.*` the painted UI, `theme.*` colors, fonts, zoom.
- `docs/`: design notes (`docs/design/`). A document a person reads goes here, not beside the code.
- `ci/`: CI helpers (`ci/deps.sh` builds libgit2 and KSyntaxHighlighting statically).
- Build outputs (`build/`, `deps/`, `_deps/`) stay out of git.

## Seeing a change work

Build with `cmake --build build`. The app has dev aids for driving it without hands, in
`MainWindow::runDemo()` and `main.cpp`:

- `TOWEL_SHOT=out.png TOWEL_SHOT_DELAY=ms` saves a window grab and quits. With it (or
  `TOWEL_DEMO_SCRIPT`) set, settings go to a throwaway folder: a dev run never touches the
  owner's window size, zoom, recent repositories or drafts.
- `TOWEL_DEMO_ROW=n`, `TOWEL_DEMO_FILE=n`, `TOWEL_DEMO_SEARCH=text` select things at start.
- `TOWEL_DEMO_SCRIPT="step;step"` runs steps 900 ms apart: `stage:path`, `unstage:path`,
  `stageall`, `unstageall`, `commit:msg`, `amend:msg`, `open:staged|unstaged:path`,
  `key:jjs` (`<`/`>` are Alt+Up/Down), `rows:a-b`, `undo`, `redo`, `checkout:kind:name`,
  `pick:sha`, `mixed:rev`, `rebase:sha`, `rebasego`, `zoom:n`.

Try a change on a scratch repo (`git init` in a temp dir), never on a repo someone works in.
Never stop or restart a Towel window you did not start yourself: the owner may have a repo open in it.

## Rules

- C++: `.claude/rules/cpp.md`.
- Git and commits: `.claude/rules/git.md`.
- Replies: `.claude/rules/brevity.md`.
- Tests: `.claude/rules/tests.md`.
- **Keep the structure in order.** When the right home for something doesn't exist yet, create it;
  never dump a file into the nearest pile because it is less work.
