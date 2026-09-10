# Licensing policy

Keep **engine code** separate from **game data**, always. See
`THIRD-PARTY.md` for how this applies to `shared/` itself.

## Never guess a license

The same "never guess" discipline that applies to an `upstream_url` in
`ports.yaml` applies to licensing: `upstream_license` stays `null` and
`upstream_license_verified: false` until someone has actually read that
project's own LICENSE/COPYING file (or equivalent) in the pinned revision
and recorded what it says, in `ports.yaml` and that port's own
`LICENSE-REVIEW.md`. A remembered or commonly-believed license is not
verification — this field governs what we are legally allowed to
distribute, modify, and how, so treat it with the same rigor as a legal
document, because it is one.

## Never contribute upstream

We do not send anything back to the projects we patch — no upstream PRs,
no issues, no bug reports, no mailing-list posts, about any vendored
source in `shared/` or in any port repo's own `vendor/`/`patches/`. All
patch and research work stays in this repository or the relevant port
repository. See `CLAUDE.md`'s "Never contribute upstream" section for the
full policy and rationale; it applies everywhere in this hub, in every
port repo, and in any harness/QA tooling (vcctrl included) touched while
working on a port.

## Every port needs a `LICENSE-REVIEW.md`

Before any release-packaging automation is built for a port, its repo
needs a `LICENSE-REVIEW.md` (template: `templates/LICENSE-REVIEW.md`)
tracking:

```
engine source license
DOS-specific patches license
SDL license
linked libraries
game asset license
music license
font license
redistribution rights
binary redistribution rights
source distribution obligations
```

## Never

- commit commercial game data
- commit copyrighted assets without explicit permission
- commit proprietary fonts
- commit music requiring separate permission

## Default model when licensing is uncertain

```
open engine
+
user-supplied original game data
```

This is the model doskutsu uses for Cave Story and is the safe default for
any new port — ship the engine, let the user supply their own legally
obtained copy of the game data.

## "Source available" is not "freely redistributable"

Several candidates in `ports.yaml` (VVVVVV in particular) have source
available under terms that are not automatically redistribution rights for
either source or compiled assets. Read the actual license before assuming
anything about what a finished port can ship.
