# Local preview desktop controls — G3 design checkpoint

This is the G3 design for the Local Workbench Preview assignment on PR #11.
It is stacked on Draft PR #12's user-created terminals. G2's review and
history-preserving PR #11 incorporation are pending; this document does not
claim a qualified combined build or authorize package handoff.

## User contract

- Each presented terminal keeps its stable local view identity and Go session
  identity. The user may set a presentation-only title of at most 48 UTF-8
  bytes. Empty input restores the default `Terminal <label>` title. Cancel
  leaves the title unchanged. Reject invalid UTF-8, C0/C1 controls and DEL;
  neither a shell title sequence nor process metadata can rename a window.
- The frame and Window List use one formatter for title and state. A known
  `exited <status>` or `closed` result stays known if contact is subsequently
  lost, with an additional `backend lost` marker. A `starting`, `live` or
  locally `closing` view instead shows `backend lost` on contact loss, never
  an inferred exit. Exited status becomes visible only through the existing
  endpoint final-output barrier. A bounded list shows at most the 16 retained
  views, including exited/lost views, and scrolls within the minimum
  supported outer-terminal size.
- New, Rename, Window List, Next, Previous, Move/Resize,
  Maximize/Restore, Select Text, Close and Quit are discoverable through the
  existing Ctrl-B menu. Its top level groups Terminal (New, Close, Rename),
  Windows (List, Next, Previous, Move/Resize, Maximize/Restore), Text
  (Select, Paste), Input (Grab, Release), Help, Suspend and Quit. Each
  submenu and the
  top level has at most seven rows plus borders, so every popup fits the
  40-column/14-row minimum outer terminal. A short Help action describes
  those controls and scrollback via wheel/visible scrollbar, including how
  to release Input Grab.
  Ordinary Tab and Shift-Tab continue into the shell; remove the current
  misleading `Tab`/`Shift-Tab` global-switch shortcut labels.
- Ctrl-Tab/Ctrl-Shift-Tab may become convenience shortcuts only if the actual
  input stack supplies distinguishable events in the supported outer terminal.
  The pinned Turbo Vision decoder can represent modified Tab, but a plain
  control-I byte is indistinguishable from Tab. The verified Ctrl-B menu path
  is the fallback; no Terminal.app preference changes or claimed support
  from source capability alone.

## Ownership and interaction

`TerminalWindow` owns a mutable display title separate from its immutable
local identity/Go endpoint. A small title validator handles UTF-8 and control
rejection before mutation. The existing `inputBoxRect` dialog supplies the
edit field, sized to fit a 40-column/14-row outer terminal; no IPC occurs
while it is open. The app resolves the currently selected terminal *after*
the dialog returns, since nested event service can retire a view meanwhile.

The Window List is a compact Turbo Vision dialog/listbox, not a new UI
framework. It captures stable local view IDs and renders bounded title/state
rows from current `TerminalWindow` metadata. It refreshes on relevant core or
terminal-update broadcasts while open. The pinned listbox resets row selection
when its collection is replaced, so the dialog keeps a separate ID-to-row
snapshot and restores selection by ID after each refresh when possible. It
copies the chosen ID before dialog destruction. On selection, the app resolves
that ID again among current desktop views, then selects the surviving window;
a retired target is reported or ignored without dereferencing a stale pointer.
The list never initiates session lifecycle operations. Rows reserve display
cells for the state and borders; title truncation uses Turbo Vision UTF-8
cell-width iteration and never cuts a multibyte sequence.

Next/Previous use the existing Turbo Vision desktop focus commands, with
menu navigation available when direct modified keys are unavailable.
Renaming and listing must not hold the connection, emulator or cleanup mutex
across modal UI operations. On backend loss the dialog remains local and its
rows show `backend lost`.

## Rejected alternatives and tradeoffs

A dynamic 16-item popup menu is smaller in code, but it can exceed the
minimum screen height and its state labels become stale while a modal menu
holds focus. A scrollable listbox adds a small local dialog but keeps all 16
views navigable and can refresh on core events. Parsing OSC shell titles
would mix user labels with process output and is outside the assignment.
Binding plain Tab steals shell input, and assuming Ctrl-Tab from a generic
`kbCtrlTab` constant would make an unverified promise about Terminal.app.

## Execution and verification gates

1. Add focused title-validation and shared frame/list formatting tests:
   invalid UTF-8, C0/C1/DEL, empty/cancel, wide Unicode and a long valid
   title at 40 columns; loss before exit, known exit then loss, and loss
   while closing. Then implement the local title field, rename dialog and
   common state formatting without hiding state or frame borders.
2. Add list/focus tests covering zero, one and 16 views, live and retained
   exited/lost views, target retirement during refresh, selected-target
   retirement, selected-ID preservation across collection replacement, and
   a surviving chosen target. Implement the list dialog using stable IDs.
3. Update the bounded grouped Ctrl-B menu, concise in-app help and README.
   At 40 columns/14 rows, open and complete every top-level/submenu action,
   including Next/Previous with zero, one and multiple views and New after
   closing the last view. Verify Tab and Shift-Tab still reach a shell, the
   menu fallback switches windows, and move/resize, selection,
   wheel/scrollbar, close and quit remain usable.
4. Run focused synthetic desktop/real-PTY checks and the relevant CTest
   subset on the exact final source. Request independent whole-PR review.
   Combined full-suite, package and mixed-use qualification belong to G4
   after PR #11 and PR #12 parent incorporation.

No Go core, AVCP wire, resource limit, shell policy, terminal emulator,
daemon, persistence or old-lifecycle change is part of G3. Keep this PR
Draft and unmerged.
