---
name: Git and commit policy
---

# Git and commit policy

BEX Robotics' git rules, adapted to Towel.

## Commit granularity

- Each commit is one consistent, reviewable change. A reviewer should be able to state what the commit does in one sentence and verify the diff matches.
- That rule is for code. A commit with no significant code change — docs, policy, reviewer prompts, comment fixes — does not have to stand alone: such changes may share one commit, and an unpushed one is usually the place for the next. Use judgement.
- Never mix logic changes with reformatting, renames, or drive-by cleanups. If you reformatted while editing, commit the reformat separately (usually first).
- A work session is almost never one commit. Before committing, review `git diff --cached`; if it contains more than one logical change, split it — stage per change (`git add <paths>` or `git add -p`) and commit each separately.
- Avoid committing untested code. If unavoidable, mark it clearly in the message.

## Fixing an unpushed commit: amend, never stack

- A fix to a commit that has **not left this machine** belongs **in that commit** — `git commit --amend`, or `git commit --fixup=<sha>` followed by `git rebase -i --autosquash <sha>~1`. Do not add a follow-up commit.
- This covers everything: a typo, a style violation caught by the hook, a missed rename, a build fix, a reworded message. If the original commit is unpushed, the history should end up looking as though the mistake never happened.
- A separate fix commit is correct **only** when the broken commit is already pushed — then history is shared and rewriting it is off the table.
- Check before assuming: `git log @{u}..HEAD` lists what is still local and therefore still amendable.
- Rewriting history moves every SHA from the amended commit onward. Before rebasing, check whether the range contains commits **someone else authored** (`git log --format='%h %an' <base>..HEAD`) — if it does, stop and ask rather than silently rewriting their commits. Never reuse a SHA remembered from earlier in a conversation; re-derive it.
- **SHA churn is not news.** Several people and sessions work this repo at once, commits get moved, reworded and rebased constantly, and that is what rebase is for. Re-derive SHAs from git every time — always — and then say nothing about it. Do not report that they changed, do not caveat a report with a warning that they may change, do not narrate a moved base as though something went wrong. The only time upstream movement is worth a sentence is when it **changed the work**: a commit of yours landed already, or something incoming overlaps files you touched. "Your SHAs are different now" is noise the reader cannot act on.
- **Do not fight over commit order.** When another session has reordered the local commits, its order stands: do not reorder them again, not even to get your own commit pushed sooner. If a push you were told to do now needs a different order, tell the user and let them decide.

## Unpushed work that is still moving: no back and forth inside one push

A feature gets tried, shown, and changed several times before it is pushed: a lamp becomes an icon, a flag changes its meaning, a message is dropped. Everything pushed together is read together, by the reviewer and by whoever bisects later, so what goes out is **the final design, told once**. A push in which one commit adds a thing and a later one renames or removes it is noise: two reviews of code that no longer exists, and a history that records the author's afternoon instead of the change.

- **Each unpushed commit is true by itself at every moment.** When the design moves, the change goes into the commit that introduced the thing, not into the newest one: the protocol's new flag into the protocol commit, the reworked screen into the screen commit. The test, before a push and before any report that lists the commits: read `git log @{u}..HEAD` as a reviewer would. A subject that describes something the tree no longer has, or a pair of commits where one undoes part of the other, gets folded.
- **Fold at every iteration, not at the end.** Commit the first working version, then fold each round of changes into it the moment that round builds: `git commit --fixup=<sha>`, then `git rebase -i --autosquash <sha>~1`, in the same step (an agent has no editor to confirm the list in: `GIT_SEQUENCE_EDITOR=true git rebase -i --autosquash <sha>~1`). Left for later, the folds pile up across commits and files until splitting them is an hour of surgery, and for that hour the commits describe a design nobody has any more.
- **"Uncommitted until approved" is not a state.** A commit is local and amendable; approval is for the push. Work held in the tree while the commits under it describe the previous design is the same back and forth, hidden: the person asking "why is this not committed" is right. Something that does not work yet stays out of the commits; something that works and is waiting to be looked at goes in.
- **A `fixup!` commit lives for one step.** It is squashed by the command after the one that made it, and never left standing across a report, a question to the user or a pause. A `fixup!` somebody else made is theirs: leave it, and keep it after its target when reordering.
- **The message moves with the content.** A fold that changes what a commit does rewords it in the same rebase, subject included (`exec git commit --amend -F <file>` after the squash). A subject that still names the old design is the most visible half of the back and forth.
- **Restructure when the units changed, not for looks.** Split a commit that has grown a second, independent change. Merge two that have become one change. Move a change down to the commit a later one turned out to depend on. Reorder so that what is ready sits below what is still moving, and a part can be pushed. Do none of it to tidy history that is already true: every rewrite moves the SHAs of the commits above it, some of them other sessions'.
- **Review what settles.** The post-commit review (below) runs on a commit after a fold, not on each intermediate version: rounds are paid for, and a finding against a design that changed ten minutes later is wasted. When a commit keeps moving, review it when the user has seen the change and it has stopped, and always before it is pushed.

## No merge commits, on any branch

- History is a line on every branch, `main` and feature branches alike. To pick up what landed on `main`, rebase onto it (`git rebase origin/main`, then `git push --force-with-lease` on a branch); never `git merge`, and never `git pull` without `--rebase`.

## Commit messages

**A commit message is communication: the author showing the other developers what they did.** So it says what the work means, in words someone on a different team understands: what is different now for whoever uses the thing, before how it was done. For a #fix both halves are required, in that order: what the user saw go wrong, then the low-level cause and the fix.

**A commit message is for outsiders.** The people who wrote the change, and anyone who works on that code, read the diff. The message is for everyone else: a colleague on another team, a bisect six months later, a reviewer who has never opened these files. Written by an author who has just spent hours inside the work, it comes out reading perfectly to the author and to nobody else. Every rule below serves that one reader.

- Prefix the subject with the part of Towel that behaves differently, as a user would name it: `graph: ...`, `diff view: ...`, `commit panel: ...`, `rebase editor: ...`, `zoom: ...`; `build:`, `ci:`, `README:` for the rest. Narrow with a comma when it helps: `diff view, hunk staging: ...`.
  - **The prefix answers "what do I click to see the change?"** Name the screen or feature, not the source file: `diff view: ...`, not `diffview.cpp: ...`.
- A commit that fixes a broken build says so at the START of the message, right after the component prefix: `ci macOS build fix: ...`. Anyone scanning history or a CI timeline must be able to spot the unbreak commit instantly, without opening it.
- A refactoring commit — no functional change — is clearly labeled as such: `graph refactoring: ...`. The label is a promise to the reviewer that behavior is identical, so they can review structure instead of hunting for semantic changes; never put a functional change under a refactoring label.
- A commit that changes only tests says so in the prefix: `graph tests: ...`. Like `refactoring`, the label is a promise: nothing the user runs changes.
- A commit that fixes the style of a recent commit says so and names it: `diff view refactoring: fix style of new commit fd26400 — blank lines before comment blocks`. The author learns what slipped through, and anyone reading either commit finds the other.
- Match message weight to change weight — longer messages for significant changes, shorter for less significant ones. A one-line typo or brace fix gets a one-line subject and no body; a substantive change gets a subject plus a body explaining the why; a design-level change gets a real explanation of the approach and its trade-offs. Never pad a trivial commit's message, and never leave a heavy commit unexplained. Tone tracks magnitude the same way: a milestone may say so, a brace fix says nothing about itself.
- **Write for a colleague on another team.** The reader is a competent engineer who does not work on this subsystem, has not read its design doc, and is looking at this commit because it turned up in a bisect or a review. So a body says **what the thing is** before it says what changed about it, and it says **why it exists at all** before it says why it was done this way. Name the mechanism in plain words the first time it appears — a port, a file, a program, a protocol — rather than assuming the reader already has the vocabulary.
  - A pointer to a design doc is not an explanation. `docs/Tasks/ota.md section 17` tells a reader where to go and nothing about what they are looking at; it belongs at the end, after the message has already stood on its own.
  - The same goes for anything the reader cannot expand: an internal component name, a wire format, an acronym. Either spell it out or do not lean on it.
  - **Meaning, not identifiers.** Say what a quantity, flag or function *is*, not what the code calls it: "force = mass × acceleration", not `F=ma`; "thrust per unit of motor command = the upward push the accelerometer measured ÷ the average motor command", not `G1_T = mean(-fz) / sum(u_mean)`. The identifier is in the diff for anyone who wants it; the message is the one place the meaning is written down. When the reader will need to find the name, give it once, after the meaning: "the upward push the accelerometer measures (`Fz` in the CIT log message)".
  - The test: hand the message to somebody on another team. If their first question is "what is that?", the message has not started yet.
  - Before committing, read it cold: list every noun a stranger could not expand from general engineering knowledge (a script name, "the release", "the marker", "the trial", "the copy"), and either explain each in plain words where it first appears or remove it. The first paragraph describes the feature to someone who has never heard of it; only then does the message say what changed.
  - Don't cite an unpushed SHA. It changes on the next rebase and ends up pointing at nothing. Say "the previous commit" or name it by subject.

## Reporting

- Every report of a commit or push states the **git height** — the repo's total
  commit count from the root: `git rev-list --count HEAD`. Format: "pushed,
  height 4213". It gives an at-a-glance sense of repo motion, makes two reports
  comparable ("height 4213" → "height 4215" = two commits landed), and flags
  trouble instantly — a height that went *down* means history was rewritten,
  which better have been agreed on.

- **Show the git commands you ran.** A report that says "pushed, height 4213"
  teaches nobody; the same report with `git push origin <sha>:main` above it
  teaches whoever reads it over your shoulder. Put the commands that changed
  something — commit, push, rebase, reset, cherry-pick, the interactive
  incantations — in a code block in the reply, verbatim, with the real shas
  rather than placeholders. Queries that only read (`git log`, `git status`,
  `git diff`) are noise; leave them out unless the output is the point. This is
  for the people reading along, so favour the command that shows the idea: the
  `--fixup` plus `--autosquash` pair over a bare amend, the explicit refspec
  over a bare push.

- **Show the commit message, in green, in every commit report.** Like a code
  diff: the message is new text being added to history, so it is shown as an
  addition — a `diff` fence, every line prefixed `+`, the full message verbatim
  (subject, body, trailers). Every commit, amends and rewords included; a
  reworded message is shown whole again.

  The fence opens with a label line, `@@ commit message @@`, so the reader
  knows what the green text is before reading it: green alone also means a
  code addition. A diff viewer colours an `@@` line as a hunk header, apart
  from the message. The label says what happened to the message when it was
  not simply written: `@@ commit message, amended @@`, `@@ commit message,
  reworded @@`. Several commits in one report: one fence each, each with its
  label.

  ```diff
  @@ commit message @@
  + wall: commit log shows time on older commits; log pane wider
  +
  + Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
  ```

- **A command you ran and a command you are asking for look different.** Any
  command, git or not. The two must never be confused: one is a record, the
  other is a request, and a reader who pastes the record re-runs a push. So:
  - **Ran it yourself:** a `text` fence (no syntax colouring, so it reads
    grey), every line prefixed `# ran: `, **one plain command per line**:
    never joined with `;`, `&&`, `||`, `|` or a lone `&`, no redirection
    (`>`, `>>`, `<`), no `$(...)`, `<(...)`, `>(...)`, `=(...)` or backticks. The `#`
    makes the line a comment in bash. In stock zsh it does not: `#` is a
    failed command there, and the rest of the line is still parsed — a `;`
    or `&` carries on to the next command, a `>` still truncates its file,
    a `$(...)` or `<(...)` still runs. A line
    that holds one plain command does nothing in either. A command that had
    one of those parts is written in words: `# ran: make  (output to
    build.log)`.
  - **Asking the reader to run it:** a `sh` fence, bare, nothing prefixed, so
    it pastes as-is. Introduce it in words ("run as root:"). Never prefixed
    with `!` (Claude Code's run-it-here prefix) either, and never suggested
    with it: a command handed over is usually handed over because it needs
    `sudo`, and `!` cannot answer a password prompt.

  ```text
  # ran: git commit --fixup=35c0d268
  # ran: git rebase -i --autosquash 35c0d268~1
  # ran: git push origin HEAD:main
  ```

  ```sh
  sudo bash tools/wall/deploy/update.sh
  ```

## Staying current: fetch often, rebase early

**For an agent this is a gate: no git operation on a branch that is behind its remote.** Fetch
before every commit, amend, rebase and push. If the remote has commits the branch lacks, rebase
first (`git rebase --autostash origin/main`, which carries a dirty tree across), then do the
operation. Unpushed commits are fine and can wait for the user's word; unpulled
ones are not.

**When the rebase stops on a conflict, stop with it.** If the conflict is in your own commit,
resolve it. If it is in somebody else's, abort (`git rebase --abort` puts the checkout and its
uncommitted edits back as they were), tell the user and ask. Never go around it: no committing
on top of the diverged branch, and no carrying your commit onto `origin/main` in a side worktree to
push it alone (`git push origin <sha>:main`). That gets one commit out and leaves the checkout
holding a stale copy of it, further behind, for the next person to untangle.

For people it stays a recommendation, written down because it is the cheaper workflow.

A working copy starts drifting the moment somebody else pushes, and the cost of finding out is not
linear: two commits of overlap is a merge, forty is a project.

- **Fetch more often than feels necessary.** Session start, before a commit, before a push, after
  anything that took minutes. It changes nothing and costs a second, which is what makes it the one
  habit worth having unconditionally.
- **What matters is overlap, not distance.** "20 behind" stops being read by the second week;
  "three of the six files you have modified moved this morning, and moses wrote them" does not. When
  nothing came in, say nothing — silence is what keeps the report worth reading.
- **Rebase while it is still boring**, which is most of the time: no local commits means a
  fast-forward, local commits that are yours and apply cleanly means a rebase.
- **An agent keeps its own checkout current without being asked**, its own dirty tree included — it
  knows what it dirtied. It does not rewrite a colleague's local commits, resolve a conflict
  unattended, move a tree under a running build, or touch uncommitted work in somebody else's
  checkout. It snapshots before anything moves, and it says what it did.
- **Being in a hurry is a fine reason to put off a rebase** and a poor one to put off knowing: the
  fetch and the overlap report cost nothing, and may be the thing that changes your mind.

After a rebase, re-read what moved: conclusions drawn from files that changed underneath are stale,
local SHAs are dead, and nothing warns you about either. **More than one session commits in this
checkout.** Never `git stash` or `git reset` here: either can sweep up another session's
uncommitted work. Stage your own paths (`git add <paths>`), not `git add -A`.

## Post-commit review

- Right after every commit that changes code, review it before it leaves the machine: a fresh
  reviewer that has not seen the work (the `code-review` skill, or a subagent given the diff and
  these rules files, not your conclusions). It checks correctness, then these conventions.
- Run `python3 ~/dev/bex/scripts/check-cpp-style.py --changed <files>` over the changed C++ too,
  when that BEX checkout is present.
- Findings are triaged (confirm/refute) and fixed in the reviewed commit, per amend-never-stack:
  `--amend` while it is still `HEAD`, `--fixup=<sha>` plus `--autosquash` once another commit sits
  on it. Then review the amended commit again: an amend is a new diff.
- **Two rounds per commit.** The reviewer is a model: each run surfaces a different subset, so
  "run until it says nothing" does not terminate. Round one finds the real defects; round two
  confirms the fixes. Anything left after that is reported, not chased.
- A pure-formatting or docs commit needs only the style script.

## Permissions

- **Pushing to `main` of github.com/BEX-Robotics/towel is authorized** once a commit has had its
  review. Fetch and rebase first, per "Staying current".
- **A release tag (`vX.Y.Z`) needs the owner's word.** CI publishes it to everyone who downloads.
- Never rewrite pushed history (no force push to `main`); the repo is public.
- Never create or switch branches without the owner's word. Commit on `main`.
- Commits use the noreply identity configured in this checkout; never a personal email address.
