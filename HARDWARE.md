# Reference hardware

Every port should eventually be evaluated against the same hardware matrix,
so results are comparable across ports rather than each port inventing its
own baseline.

## Primary matrix

| ID | CPU | Role |
|---|---|---|
| HW-486-50 | 486DX2-50 | low-end torture test |
| HW-486-66 | 486DX2-66 | primary minimum target |
| HW-POD83 | Pentium OverDrive 83 | upgrade-path target |
| HW-5X86 | AMD Am5x86-133 | fast 486 platform |
| HW-P75 | Pentium 75 | recommended target |

For each machine, record when available: CPU, clock, L1/L2 cache, memory
size, chipset, video card, VESA BIOS/version, sound card, MIDI device, DOS
version, memory manager configuration. Cache configuration in particular
should never be omitted for a 486-class result — it materially changes
throughput.

## doskutsu's real-hardware baseline (for calibrating expectations)

| Hardware | Approximate render rate |
|---|---:|
| 486DX2-50 | ~19 FPS |
| 486DX2-66 | ~25 FPS |
| Pentium OverDrive 83 | ~33 FPS |
| Am5x86-133 | ~33 FPS |

Cave Story's game logic runs at a fixed simulation rate decoupled from
render rate — see `docs/timing.md`. A candidate does not need to hit
50/60 FPS to be playable if its simulation timing can be cleanly separated
from its render rate; use doskutsu's numbers as a rough complexity yardstick,
not a pass/fail bar.

## Real-hardware testing process

Real-hardware validation for all ports currently runs through
[vcctrl](https://github.com/ecliptik/vcctrl), a KVM/automation rig (PS/2
input injection, VGA/audio capture, power control, FTP file transfer) built
around one physical machine with hot-swappable CPUs matching the matrix
above. See [`docs/hardware-testing.md`](docs/hardware-testing.md) for the
integration design and current workflow, and
[`templates/vcctrl-profile.yaml.template`](templates/vcctrl-profile.yaml.template)
for how a new port defines its own vcctrl profile.

DOSBox-X (and, for a second correctness opinion, 86Box) automation is the
day-to-day regression gate; real hardware is the authoritative result for
performance and device-compatibility claims. Never report a performance
number measured only in an emulator as if it were a hardware result.
