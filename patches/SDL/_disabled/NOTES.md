# Disabled SDL patches

## 0081-dv-path-banked-flush-audit-diag.patch
Disabled v1.0.4 (build-qa, per team-lead confirm-RC spec). DV_BANK_AUDIT was a
default-OFF diagnostic (sdl-engine, tasks #13/#15) to probe the g2k DV banked
present path during the backdrop-black VESA investigation. That investigation
concluded the bug is ENGINE-side (cadence-gated bg_subregion clip, fixed by
nxengine 0183+0186), not the SDL banked flush -- so DV_BANK_AUDIT served its
purpose and is now dead weight on the LFB path. Moved here so it's out of the
active build (verify-patches-applied excludes _disabled/). Restore by moving
back to patches/SDL/ + re-applying if a future banked-DV diag is needed.
