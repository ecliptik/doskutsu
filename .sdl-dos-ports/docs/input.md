# Input

## Common targets

```
keyboard
PS/2 mouse / DOS mouse driver
gameport joystick when the game actually uses one
```

`shared/patches/sdl3-dos/` already carries gameport joystick support
(calibration persistence, direct port axis read, per-side scaling,
diagnostics) inherited from doskutsu. Avoid building a new controller
abstraction layer per port — the SDL3 DOS backend's keyboard/mouse/joystick
surface is the target; wrap only as much as the game's existing input
abstraction requires to compile.

## Mouse-driven games (e.g. adventure engines)

- Mouse movement, left/right button, and keyboard shortcuts are usually
  sufficient.
- Disable or simplify any mouse-confinement/warping behavior the original
  engine implements for windowed desktop platforms — it doesn't apply here.
- Native resolution coordinates should map directly; avoid an extra
  scaling/remapping layer between SDL mouse events and game-space
  coordinates.

## What to avoid

Controller abstraction complexity (rumble, multiple simultaneous pads,
hot-plug) unless the specific game genuinely needs it for its core
gameplay loop. Most DOS-era-appropriate ports don't.
