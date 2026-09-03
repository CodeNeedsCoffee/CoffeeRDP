# Vendoring FreeRDP

CoffeeRDP builds its own patched fork of FreeRDP in-tree rather than linking
the system `freerdp-devel`/`libwinpr-devel` packages. This document explains
why, where the fork lives, and the procedure for pulling in a new upstream
FreeRDP release.

## What's vendored and why

`vendor/freerdp` is a git submodule pointing at a maintained fork of
FreeRDP, carrying a small number of patches on top of a stock upstream
release tag. It's built as part of the `coffeerdp` package itself (see the
`add_subdirectory(vendor/freerdp ...)` block in the top-level
`CMakeLists.txt`), not found via `find_package()` against a separately
installed system package.

This exists because real bugs were found in FreeRDP that matter to
CoffeeRDP users -- an autodetect/RTT probe that could kill a connection
outright, and hardware (VAAPI) H.264 decode sessions being opened eagerly
and held open uselessly -- and fixing them only helps if the fix actually
ships. Depending on the system FreeRDP package means CoffeeRDP inherits
whatever FreeRDP Fedora happens to package, patches included or not, on
Fedora's own timeline. Vendoring a patched fork means the `coffeerdp`
package is fully self-contained: one install, and every user gets the same
FreeRDP underneath it, with our fixes in place immediately rather than
waiting on an upstream release and then a Fedora rebuild.

This is a deliberate tradeoff, not a free lunch: CoffeeRDP now owns keeping
that fork current with upstream FreeRDP's own bugfix and security releases.
The mitigation is to keep the fork itself a *thin overlay* -- as few patch
commits as possible, each one small and easy to rebase -- so pulling in a
new upstream release stays a quick, low-risk operation rather than a merge
project. This is a deliberately occasional, manual process, not something
that tracks upstream `master` continuously.

## Where the fork lives

- Submodule path: `vendor/freerdp`
- Submodule remote (`.gitmodules`): `https://github.com/CodeNeedsCoffee/FreeRDP.git`
- Submodule branch: `coffeerdp-patches`

Inside the fork itself (`/home/evan/Code/FreeRDP` in this dev environment),
two remotes are configured:

- `origin` -- `https://github.com/CodeNeedsCoffee/FreeRDP.git`, the
  maintained fork. `coffeerdp-patches` lives here, and this is what the
  submodule points at.
- `upstream` -- `https://github.com/FreeRDP/FreeRDP.git`, the real FreeRDP
  project. Only fetched for specific release tags, not tracked
  continuously.

`coffeerdp-patches` is branched from a specific upstream release tag and
currently holds exactly two commits on top of it: the autodetect/RTT fix
(`libfreerdp/core/autodetect.c`) and the VAAPI eager-session fix
(`libfreerdp/core/connection.c` + `libfreerdp/gdi/gfx.c`).

## Updating to a new upstream FreeRDP release

This is a manual, occasional process -- do it when upstream ships a
bugfix/security release worth having, not on every upstream commit.

1. In the fork (`/home/evan/Code/FreeRDP`): `git fetch upstream --tags`.
2. Rebase the patches branch onto the new tag (rebase, not merge, to keep
   the overlay thin): `git checkout coffeerdp-patches && git rebase <new-tag>`.
   Expect few or no conflicts -- the patches are small and targeted. Resolve
   any that come up.
3. Build and test the fork standalone before touching CoffeeRDP at all
   (`cmake --build`, run its own test suite). Don't skip this -- a broken
   fork build is much easier to debug in isolation than after it's already
   wired into CoffeeRDP.
4. Push the rebased branch: `git push origin coffeerdp-patches --force`.
   **This is a force-push and rewrites history on a shared branch** -- only
   safe because `coffeerdp-patches` is meant to be an unstable, rebasing
   overlay that nothing else should be based on. Never force-push it if
   anyone else has branched off it for their own work.
5. Back in CoffeeRDP, bump the submodule pointer:
   ```
   cd vendor/freerdp
   git fetch
   git checkout <new-commit-or-tag>
   cd ../..
   git add vendor/freerdp
   ```
6. Build and test CoffeeRDP against the updated vendored FreeRDP
   (`cmake -B build -GNinja && cmake --build build`), and run through
   CoffeeRDP's own manual verification steps (a real session, quality
   presets, clipboard, etc.) before committing.
7. Commit the submodule bump with a message naming the new upstream
   FreeRDP version and confirming which of CoffeeRDP's patches still apply
   / were carried forward cleanly.

## See also

- `RELEASING.md` -- CoffeeRDP's own release checklist; a FreeRDP update is
  a separate, independent process from a CoffeeRDP version bump.
- `PACKAGING.md` -- how `vendor/freerdp` flows through the SRPM build
  (submodule content isn't included by a plain `git archive`, see
  `.copr/Makefile`'s comments).
