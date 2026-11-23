# Towel

*Always know where your HEAD is.*

A fast, good-looking git history viewer for Linux, built with Qt 6 Widgets (everything custom-painted), libgit2 and KSyntaxHighlighting. Lean: ~10 MB private memory plus window pixels; history costs ~48 bytes per commit (titles are read from git only for visible rows).

- **Commit graph**: colored lanes, round avatar nodes (GitHub / Gravatar), ref pills (local + remote + tags)
- **Diffs**: unified or split, hunks or full file, word-level change marks, syntax highlighting, image diffs
- **Sidebar** with local/remote branches (ahead/behind), tags, stashes; search by message, author, SHA or branch
- **Sync**: fetch, pull (always `--rebase --autostash`), push (`--force-with-lease` when needed), edit commit messages
- Reads history with libgit2 off the UI thread (66k commits in ~0.6 s); write operations shell out to `git`, so your SSH keys, hooks and config just work

## Download

Grab the latest build from [Releases](https://github.com/pavel-teramips/towel/releases/latest):

- **Linux:** `Towel-…-linux-x86_64.AppImage`. Make it executable (`chmod +x`) and run it.
- **macOS (Apple Silicon):** `Towel-…-macos-arm64.dmg`. The app is not signed yet, so the
  first time, right-click it → Open.
- **Windows:** `Towel-…-windows-x64.zip`. Unzip anywhere and run `towel.exe`. SmartScreen may
  warn because the app is not signed yet: More info → Run anyway.

Towel needs `git` on your PATH for pushing, pulling and committing.

## Build

```sh
sudo apt install cmake ninja-build qt6-base-dev \
    libkf6syntaxhighlighting-dev libgit2-dev
cmake -B build -G Ninja && cmake --build build
./build/towel /path/to/repo
```

Install a `git-towel` symlink on your `PATH` to get `git towel`.

## Keys

| Key | Action |
|---|---|
| ↑ ↓ / j k, PgUp PgDn, Home End | move through history |
| Ctrl+F, Enter / Shift+Enter | search, next / previous match |
| Esc | close diff |
| Alt+↑ / Alt+↓ | previous / next file in the diff |
| F5 / Ctrl+R | refresh (also automatic when the window regains focus) |
| Ctrl+O | open repository |

## License

GPL-3.0-or-later
