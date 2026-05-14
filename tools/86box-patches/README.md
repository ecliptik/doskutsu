# 86Box patches (build-qa task #11; wave-41)

Custom patches applied to a locally-built 86Box used by the
`make hwinv-86box-smoke` parity gate. We carry these against
upstream because the default v5.3 AppImage's IDE cache behavior
prevents host-side log capture in our headless smoke harness.

## 0001-write-through-ide-cache.patch

**Why**: 86Box's `hdd_image_write()` in `src/disk/hdd_image.c`
calls `fflush(hdd_images[id].file)` after every write but does NOT
call `fsync()`. The libc `fflush()` only flushes user-space stdio
buffers; the OS page cache holds the data and only writes to the
underlying disk file when the OS decides (or on `O_SYNC` open / on
`fsync()`). For our parity-smoke pattern (host-side `mcopy` reads
of the image file while/after the VM runs guest writes), the page
cache hold causes guest-side writes to be invisible from host until
86Box closes the file cleanly via Action -> Exit.

Under xvfb-or-headless harness invocation, no Action -> Exit is
reachable reliably (Qt menubar mnemonics don't activate via xdotool
key forwarding; menu mouse-clicks land on the emulated VGA canvas).
SIGTERM/SIGINT/SIGKILL all skip the clean-exit fflush+fclose path.

The patch adds `fsync(fileno(hdd_images[id].file));` immediately
after every `fflush()` in the two write paths
(`hdd_image_write` + `hdd_image_zero`). This forces the page cache
to write to disk synchronously, so host-side mcopy sees the writes
within milliseconds of guest-side completion.

Performance impact: NEGATIVE on heavy-write workloads (each sector
write becomes a synchronous disk-flush). Acceptable for our smoke
gate use case (HWINV.EXE writes ~5 KB total). Not appropriate for
gameplay-level workloads. For the wave-41-hwinv iter specifically
this trade is fine; future iters that exercise gameplay would
need a runtime toggle or env var to disable.

## Build instructions

```bash
git clone --depth=1 --branch v5.3 https://github.com/86Box/86Box.git
cd 86Box
git apply /path/to/tools/86box-patches/0001-write-through-ide-cache.patch
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DQT=ON -G "Unix Makefiles"
make -j$(nproc)
# Resulting binary: src/86Box
```

`tools/86box-run.sh` resolves the binary via the `BOX86_APPIMAGE`
env var or the default `~/emulators/86box/86Box-doskutsu` path.

## Versioning

Patch authored against 86Box v5.3 (build 8200; tag `v5.3` at
github.com/86Box/86Box). If 86Box main has restructured the IDE
write path since v5.3, re-derive against current master + update
the slot number.
