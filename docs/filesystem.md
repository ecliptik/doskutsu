# Filesystem

DOS constraints must be handled explicitly — most desktop-era SDL games
assume at least one of these in a way that breaks on DOS.

## Audit for

- drive letters
- backslashes vs. forward slashes
- current working directory assumptions
- long filenames (many DOS setups are 8.3-only)
- case-sensitivity assumptions
- per-user config directories
- `$HOME`, `%APPDATA%`, XDG paths, registry, system-wide config dirs
- Unicode filenames
- temp directories
- file locking
- symlinks

## Recommended DOS policy

```
GAME\
    GAME.EXE
    CWSDPMI.EXE
    GAME.DAT
    <CONFIG>.CFG
    SAVE\
```

Keep configuration and saves local to the game directory (or a child
directory of it). Do not depend on `$HOME`, `%APPDATA%`, `XDG_CONFIG_HOME`,
the registry, or any system-wide config directory — none of these exist in
a meaningful form on DOS.

If the engine's file-format assumptions require long filenames, detect
support explicitly and fail with a clear message rather than silently
truncating. Prefer 8.3-safe filenames throughout the base engine port where
practical — it avoids an entire class of transfer/CF-card/real-hardware
friction later (see `docs/hardware-testing.md`).
