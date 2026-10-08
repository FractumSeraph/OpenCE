# Delta in the browser build

[Delta](https://halo.milenko.org/delta) is ChupathingyCE's network family:
what their builds say to each other and to their site beyond OpenCE's game
protocol, which stays OpenCE's byte for byte. Its specification is
`docs/delta.md` in [ChupathingyCE/chupathingyce](https://github.com/ChupathingyCE/chupathingyce)
(CC0, like OpenCE), and every part of it falls back silently to plain OpenCE
when the other side does not speak it. The browser build speaks the parts
below. All of it is browser-only (`port/web`, or inside `#ifdef HALO_WEB`):
the native builds stay exactly OpenCE's.

## What is here

| File | From | What it is |
| --- | --- | --- |
| `delta.h` | their `port/linux/include/delta.h` | Delta's numbers: the capability and platform registries, the compatibility table of OpenCE's network versions (`DELTA_LEGACY_VERSIONS`). Changed: `DELTA_WIRE` and the calls marked "web" at the end. |
| `delta_key.h` | their `port/linux/src/delta_key.h`, as it is | The Ed25519 public keys a signed legacy table must be signed with. |
| `../web_delta.c` | their `port/linux/src/delta.c`, adapted | The legacy number in use, and the signed legacy table: checked, cached in the save root, its kill switch. |

Taken from their commit `b78e6cfa` (October 8, 2026). The archive
(`S:\WebHalo\archive\git\ChupathingyCE-chupathingyce.git`) keeps their whole
history; to see what changed in a file since:
`git -C build\opence-web fetch S:\WebHalo\archive\git\ChupathingyCE-chupathingyce.git main:refs/remotes/chupa/main`,
then `git -C build\opence-web diff b78e6cfa chupa/main -- port/linux/include/delta.h`.

## The join range (the legacy number)

OpenCE's builds join only hosts of their exact network version. Delta's
builds join every version back to the newest *breaking* one of the
compatibility table (version 11 today), because the changes since were
additive: messages an older machine drops. The browser build does the same:
`network_client_manager.c`'s join check and `p2p_lobby.c`'s listings (both
under `HALO_WEB`) use `delta_legacy_minimum()` and `delta_legacy_maximum()`.
So the in-game Server Browser lists, and joins, OpenCE and ChupathingyCE
games of versions 11 to 24 (24 being this build's own).

**When OpenCE raises its network version**, this build's version has no row
in `DELTA_LEGACY_VERSIONS` until someone adds one, and until then the
browser joins its own version alone, exactly as OpenCE does. Add the row
(additive or breaking, as ChupathingyCE's `delta.h` has it once their
automation has classified the raise) to bring the range back.

## The signed legacy table

ChupathingyCE publishes a JSON table, signed with their Ed25519 key
(`delta_key.h`), at `https://halo.milenko.org/v1/delta/legacy` (and `.sig`),
and on GitHub (`delta-table` branch). Its rows are by *wire* (the revision of
the game protocol a build speaks); the browser build's wire, `opence-web`,
has no row, so the table never changes its numbers. What the browser takes
from it is its **kill switch** (`disabled_capabilities`: a Delta capability
found unsafe, which this build then never uses) and its serial.

The page (`online_client.js`, `startDeltaLegacyTable`) fetches it at start
and every four hours through this site's server
(`services/selfhost/server/delta-list.mjs`; the site sends no CORS headers),
or from GitHub, and hands it to the game (`web_delta_offer_table`), which
checks its size, then its signature, then reads it with a strict parser, and
takes it only when its serial is newer. The log says
`Delta: legacy table N from Delta List: ...`.

Test: `build\tools\smoke\delta.mjs http://localhost:8767/`.
