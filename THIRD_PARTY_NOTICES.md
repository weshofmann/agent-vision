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
