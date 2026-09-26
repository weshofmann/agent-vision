# Probe evidence — checkpoint pending final verification

The unmodified pinned bundle builds and the outer-PTY interaction demonstrates
core window/shell feasibility. Final exact-SHA verification and selected archived
screens will replace this checkpoint note before operator review.

The exploratory qualified run observed shell B still in zombie state after its
window closed (`82439 82425 Z`), while shell A remained alive (`82426 82425 Ss+`).
This blocks product lifecycle qualification. App quit returned 0 and emitted
alternate-screen entry/exit; cooked input was recovered. Immediate termios had
only the macOS PENDIN bit added (difference 536870912); checking it again after
input is pending. Do not claim byte-for-byte immediate termios restoration.

Probe tooling failures (pyte restore-decoding exception, ENOTTY after outer
session leader exited, duplicate DSR responses) were corrected only in the
throwaway driver. No upstream source modifications were made.
