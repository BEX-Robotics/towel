---
name: C++ conventions
paths:
  - "src/**"
---

# C++ conventions

BEX's C++ conventions (`.claude/rules/cpp.md` in the bex repo), adapted to a Qt application.
`.clang-format` does braces and spacing; `python3 ~/dev/bex/scripts/check-cpp-style.py <files>`
checks the blank-line and structure rules (`--fix` repairs the whitespace ones). It does not know
Qt, so its `zero-init` findings on Qt value types are noise (see Variables).

## Self-explanatory code

Prefer extracting into well-named functions over writing comments that describe *what*. Comments explain *why*. If a function is hard to name, it's not well-designed. Refactor now — it won't get easier later.

## Comments

- `//` preferred over `/* */`.
- Cover: why a decision was made, non-obvious constraints from hardware/APIs, tricky control flow, TODOs with ticket/link.
- No commented-out code. Use `#if 0` (nests better).
- **Preserve load-bearing comments** when refactoring (e.g. macro caveats like `CLI11_PARSE` early-exit).
- **Hex escapes in string literals need a naming comment.** `"\xe2\x80\x94"` is unreadable; every literal containing one gets a same-line (or immediately-above) comment decoding it — the encoding fact first, the typographic name in parentheses: `// \xe2\x80\x94 = UTF-8 for "—" (em dash)`. One comment may cover an adjacent cluster reusing the same character. Escapes (not raw UTF-8 in the literal) keep source ASCII-clean; the comment is the price.

## Naming

- `camelCase` for variables, functions, constants. No `k`, `c`, or other prefixes for constants.
- `PascalCase` for classes.
- Trailing acronyms in camelCase are fully capitalized: `getFD()`, `parseURL()`, `nodeID` — not `getFd()`, `parseUrl()`, `nodeId`.
  The acronyms this codebase writes every day, so there is no judgement call: USB, RC, IMU, FIFO, CS, PID, RPY, DMA, SPI, UART, I2C, CRC, ESC, LED, ADC, PWM, IRQ, DFU, ID, URL, FD, FCU, GCS, EKF, OTA, GPS, IP, TCP, UDP, DNS, HTTP, JSON. The list is the common cases, not the boundary: any other initialism at the end of a name is capitalized the same way. So `flushFIFO()`, `startRC()`, `printIMU()`, `otherCS`, `ratePID`, `RatePID`, `onUART()`, `rebootToDFU()` — never `flushFifo`, `startRc`, `printImu`, `otherCs`, `ratePid`.
- Acronyms mid-name or at start follow normal camelCase: `GpsFixType`, `gpsHandler`, `fifoDepth`, `imuRate_Hz` — not `GPSFixType`.
- Full, descriptive names — readable without diving into the implementation.
- Physical units as suffixes: `tableLength_m`, `exposureTime_us`, `targetBandwidth_b_s`, `gravityField_m_s2`.
  - The trigger is the quantity, whatever the name says: a timeout, hold, delay, interval or period is `_us`/`_ms`/`_s` (`irqTimeout_us`, `motorStartHold_ms`); a rate is `_Hz`, `_rad_s`, or whatever it counts per second (`blockRate_Hz`, `eulerRateMax_rad_s`, `uplinkRate_B_s`); an angle is `_rad` or `_deg` (`deltaAngle_rad`, `thrustAngle_rad`); a gyro bias `_rad_s`; an acceleration `_m_s2`; a voltage `_V`. A bare `rate`, `angle`, `bias` or `timeout` names a thing that has a unit, so it carries one.
  - SI prefixes: `u` micro, `m` milli, `k` kilo, `M` mega, `B` byte, `b` bit. Named-after-people units capitalized (`Pa`, `Hz`, `V`, `N`); others lowercase (`m`, `deg`, `bar`).
  - An underscore inside the suffix reads as "per", and repeats for each division: `rate_B_s` is bytes per second, `gravityField_m_s2` metres per second squared.
  - Prefer SI. Avoid imperial, RPM (use rad/s), percent (use 0..1 range).

## Ownership

- libgit2 objects live in the `GPtr` wrappers of `src/gitutil.h` (`GRepo`, `GCommit`, `GDiff`, ...);
  never a raw `git_*` pointer that someone must remember to free.
- Qt objects are owned by their parent (`new QLabel(parent)`); a parentless `QObject` is a leak
  unless something else visibly owns it.

## Variables

- Always initialize primitives at declaration. Zero is fine if no better value exists.
- Every structure declaration is `= {}` initialized — locals, members, arrays (`CommitRow row = {};`). Determinism: nothing is ever read indeterminate, even after the type grows a field. Exempt: constructor-managed class types (`std::`, Qt value types such as `QString`, `QList`, `QUrl`, the `GPtr` wrappers) and members always set in a mem-initializer list.
- Declare close to use.
- **Derive, don't store.** A member that can be computed from other members, or read from the object that owns the value, is not a member: no copy of a child's last result, no flag beside a timestamp that already says it, no "last X" kept only for a report. Every member is state a reader has to track, and a copy is state that can disagree with its original. When deriving is genuinely hard or costly, keep it, and let the name say it is a cache: `leanCached_rad`, `lastStepCache`.
- Avoid non-POD globals/statics — construction/destruction order is unpredictable. A function-local static (constructed on first use) is the exception Qt code needs, as in `theme::Zoom::instance()`.
- Pass large objects by `const&`.
- Use `0` (not `'\0'`) for null char.
- Use `0` (not `nullptr`) when initializing pointers — shorter and clearer. `nullptr` is justified when a pointer type is genuinely required (e.g. argument-based lookup, resolving overloads against pointer-typed parameters), or as a readability hint when the surrounding context doesn't make clear that a function argument is a pointer.

## Formatting

KNR braces:

```cpp
void f(char *s) {
    if (bad)
        return;

    doStuff();
}
```

- No braces for single-statement `if/else/for/while` bodies.
- `else` cuddles the closing brace: `} else if (...) {`, never `else` on its own line after `}`.
- No trailing whitespace.
- Blank lines between logical blocks in a function. Before a `return`, only when three or more statement lines precede it in the same block; a one- or two-statement body takes none, at any scope — a two-line `if` body, a `case`, a lambda, a tiny function (`return-blank` in the checker, both directions; `--fix` repairs it).
- After an `if` block — and after the LAST branch of an `if`/`else` chain, however short its body — blank line before the next statement. A statement glued right under an `else` body reads as part of the branch it isn't in.
- A `case` body of two or more lines is a block: blank line before the next `case`/`default` label. One-line bodies and stacked fall-through labels stay together (`case-blank` in the checker; `--fix` repairs it).
- Hard blank-line rules (mechanical, no judgment): no blank right after `{` or right before `}` — except between two adjacent multi-line blocks, where a header that wraps across lines is itself a block and takes a blank line before the body's first block; never two consecutive blanks; a `//` comment block that introduces the next declaration/operation is preceded by a blank line — at EVERY scope (function bodies, struct members, header declarations, file scope); consecutive multi-line declarations (struct + def object + `static_assert`, commented header decls) get blanks between them.
- Checked by `scripts/check-cpp-style.py` (hooked on every agent edit via `.claude/settings.json`; `--fix` repairs the whitespace rules).
- Space after keywords (`if (`, `while (`, `for (`), no space after function names (`func(`).
- Star next to variable name: `char *s`, not `char* s`.
- One declaration per line unless genuinely the same concept.
- No unrelated statements on one line.
- Zero tests are spelled `!`: `if (!strcmp(a, b))`, `if (!count)`, `if (!msg.id)` — never `== 0`. Applies to the `strcmp`/`memcmp` family and every other integer zero test.

## Include order (blank line between groups)

1. The file's own header first, in a `.cpp` (`#include "diffview.h"`).
2. Towel headers (`"theme.h"`, `"repo.h"`), alphabetical.
3. Third-party: Qt (`<QLabel>`), KDE (`<KSyntaxHighlighting/...>`), `<git2.h>`, alphabetical.
4. Standard library, alphabetical.

Use `<>` for third-party and the standard library, `""` for Towel's own.

## Structure

- Functions fit on one screen (~60 lines) — a working limit, not a suggestion. Never grow an existing function past it; add new logic as a call to a new named function instead.
- Any inline block over ~40 lines is extracted by default. When one genuinely must stay inline (e.g. an ordered early-return ladder whose priority order is the logic), its closing brace says what it closes: `} // if (active faulted) — escape the active source`.
- No macros for logic — prefer `constexpr` / `inline` functions.
- **A min or max is `std::min`/`std::max`, and a limit on both sides is `std::clamp` with limits that cannot cross (or `std::max(lo, std::min(x, hi))` when they can), never a `? :` comparison.** `std::max(x, lo)` reads at a glance; `x < lo ? lo : x` makes the reader check that the branches match the comparison, and that `<` was not meant as `<=`. Checked by the style script (`minmax-ternary`).
- Be cautious with nested includes.

## Tests

What earns a test is in `.claude/rules/tests.md`.

## Threading

- The UI thread paints and reacts; it never waits on git or the disk.
- Reads run on the global thread pool: `QtConcurrent::run([...] { ... }).then(this, [...] { ... })`.
  The worker opens its own libgit2 handle (libgit2 objects are not shared across threads) and
  returns plain data; the continuation runs on the UI thread and checks a generation counter so a
  stale result is dropped (`m_diffGen`, `m_statusGen`).
- Shared state between threads is a mutex-guarded member, named for what it guards, and rare.

## Blocking calls

- **Nothing on the UI thread blocks.** `git` runs through an asynchronous `QProcess` with
  `finished` connected; never `waitForFinished` on the UI thread. Network goes through
  `QNetworkAccessManager`.
- `gitSync()` (synchronous `QProcess`) on the UI thread is existing debt. Don't add a call; move
  one onto a worker when you touch its code.
- Index operations are serialized through `Repo`'s queue (`indexOp`), because git takes
  `index.lock`.

## Versioning

The version is `project(towel VERSION x.y.z)` in `CMakeLists.txt`; a release is a `vX.Y.Z` tag,
which CI turns into a GitHub release with the Linux, macOS and Windows packages.
