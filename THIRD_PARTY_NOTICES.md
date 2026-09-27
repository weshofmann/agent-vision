# Third-party baseline and notices

AgentVision uses these exact development revisions; no project license is chosen.
This inventory is not legal redistribution clearance.

| Component | Tested revision | Retained notice |
| --- | --- | --- |
| magiblot/tvterm | `210eb23564da06c358d2623388939bb02f7f3419` | `third_party/notices/tvterm.COPYRIGHT` (MIT) |
| magiblot/tvision | `640263136daa67b96a90c9bf6eb8816216ff76a3` | `third_party/notices/tvision.COPYRIGHT` (original Borland disclaimer, MIT modifications, embedded third-party notices) |
| magiblot/libvterm fork | `62b27d1db0c49eed55936a1bfa35102be91afe42` | `third_party/notices/libvterm.LICENSE` (MIT) |

CMake fetches tvterm and recursively checks out its selected submodules, verifies
all three HEADs, and applies only the [downstream patch](patches/README.md).
Generic system libvterm is not equivalent to this callback-extended fork.
Fetched sources retain their original notices; checked-in copies are byte-identical
to the pinned originals and copied to `build/licenses` beside the executable.
The platform ncurses, libutil and pthread libraries are linked through upstream's
existing build discovery. Consult their platform-distribution notices as well.

The opt-in independent Go core additionally uses:

| Component | Qualified revision | Retained notice |
| --- | --- | --- |
| creack/pty | `v1.1.24`, `edfbf75025b0ba4ee17c19f52d9b600fad80a787` (explicit local source plus Darwin patch) | `third_party/notices/creack-pty.LICENSE` (MIT, Keith Rarick) |
| Go runtime/standard library | `go1.27.0`, Darwin arm64 | `third_party/notices/Go.LICENSE`, `Go.PATENTS` (Go Authors) |

The Go standard-library dependency closure includes its vendored
`golang.org/x/net/dns/dnsmessage`; the qualified distribution's complete x/net
LICENSE and PATENTS are byte-identical to the retained Go notices. The core has
no additional module dependencies. CMake copies these complete notices only for
the opt-in build; it does not imply redistribution clearance for AgentVision.

The complete creack/pty module source is retained under
`core/third_party/creack-pty`, with the MIT notice unchanged. The pristine archive
identity and every upstream file digest are in `creack-pty.pristine.json`; the
reviewed changed-file digests and patch digest are in `creack-pty.downstream.json`.
The [upstream-relative patch](patches/creack-pty-darwin-master-boundary.patch)
adds only the Darwin borrowed-master boundary and its deterministic phase tests.
`cmake/verify_creack_source.py` checks the complete effective file set and both
reverse/forward patch identities before builds. The local module replacement is
not authenticated by `go.sum`; no build-time replacement download is used.
