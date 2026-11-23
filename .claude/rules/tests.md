---
name: Tests
paths:
  - "src/**"
  - "tests/**"
---

# Tests

BEX's test policy, adapted. Towel has no test suite yet; the first one belongs in `tests/`, run by
CTest, against small fixture repositories that a test builds with `git` in a temporary directory.

## What earns a test

**Anything whose wrong answer is expensive and quiet.** That is the whole test. Loud failures — a crash, a refusal, a build error — announce themselves and a test adds little. The cases worth pinning are the ones that keep working, plausibly, while being wrong.

In practice that means:

- **Anything that writes to the user's repository.** Partial staging (`gitload::partialPatch`), the rebase plan (`Repo::rebase`), reword, undo. A wrong answer there quietly loses or corrupts someone's work.
- **Parsers.** Something outside this repo decides the input: git's output, diff text, commit messages with odd encodings.
- **The lane layout.** A graph that draws plausibly but wrong is exactly the quiet failure.
- **Anything a bug has already visited once.** A regression test is the cheapest test to justify and the easiest to write, because the failing input is already in your hand.

## What does not

**Painting is mostly not on this list.** A screenshot (`TOWEL_SHOT`) checked by eye is the instrument for how things look; a unit test that pins pixel positions pins today's layout in place.

Also not earning one: glue that would only be tested against a mock of itself, a getter, or a case the type system already refuses.

## Who decides that a test gets written

**Primitives: write the test.** A pure function with a fixed contract — hashing, encoding, a unit conversion, parsing a simple format — no state, no I/O, no device. The contract is the whole of it, a unit test is the only practical instrument, and it costs nothing to keep. No need to ask.

**Everything else: suggest it, and let the programmer decide.** Repo operations, anything with a sequence or a UI in it. There a unit test is one instrument among several, and often the weaker one: a scripted run on a scratch repo (`TOWEL_DEMO_SCRIPT`) may be what actually proves the thing. Which instrument fits is a judgement about the system, so it belongs to whoever owns it — say what a test would cover and wait.

The failure this prevents is a test arriving inside a fix commit as though it came with the fix. If a test is what proves a fix is real, write it to demonstrate, say so, and offer to drop it; do not assume it stays. "Testable" was never the same as "worth testing".

## The bar for keeping one

A test that has never failed and never could is a maintenance cost with no return. When a test survives a rewrite of the thing it tests without noticing, that is evidence about the test, not the rewrite.

**Do not add a test to raise a count.** There is no coverage target here and there should not be one — it converts the judgement above into an exercise in testing the easy things.
