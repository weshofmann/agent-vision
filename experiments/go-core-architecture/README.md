# Disposable Go core architecture probes

Evidence only, **not an agentvision-core implementation**. The `.txt` fixtures
are sanitized source listings. Materialize executable probe code only inside
ignored `.probe/`; never add these fixtures to product build targets or promote
them into implementation. They intentionally exercise a reduced length-prefix
protocol, not the proposed product wire schema or complete concurrency design.

Qualified prerequisites: macOS 26.6.2 arm64, Go 1.27.0, Apple Clang 21.0.0, Python
3, network access for the pinned module. Use an explicit Go binary if the `go`
shim has no configured version; no global tool configuration is required.

From the assigned worktree, with Go 1.27.0 selected in PATH:

```sh
mkdir -p .probe/go-core
for name in go.mod go.sum main.go probe_test.go termios_policy.go ipc_probe.py; do
  cp "experiments/go-core-architecture/fixtures/$name.txt" ".probe/go-core/$name"
done
cp experiments/go-core-architecture/fixtures/parent.cpp.txt .probe/go-core/
cd .probe/go-core
export GOCACHE="$PWD/cache" GOMODCACHE="$PWD/modcache"
go mod download
go test -v -race -count=1 -timeout=30s ./...
go build -o child .
clang++ -x c++ -std=c++14 -Wall -Wextra parent.cpp.txt -o parent
./parent ./child
python3 ipc_probe.py
GODEBUG=execwait=2 GOGC=1 go test -v -race -run '^TestSerializedReaper$' -count=10 -timeout=30s ./...
cd ../..
cp experiments/go-core-architecture/fixtures/protocol-model.py.txt .probe/protocol-model.py
cp experiments/go-core-architecture/fixtures/startup-model.py.txt .probe/startup-model.py
python3 .probe/protocol-model.py
python3 .probe/startup-model.py
mkdir -p .probe/core-watchdog
cp experiments/go-core-architecture/fixtures/watchdog_probe.cpp.txt .probe/core-watchdog/watchdog_probe.cpp
clang++ -std=c++17 -Wall -Wextra -Werror -pthread .probe/core-watchdog/watchdog_probe.cpp -o .probe/core-watchdog/watchdog_probe
.probe/core-watchdog/watchdog_probe --self-test
```

The Python single-thread harness uses `preexec_fn` only to install FD3. The actual
C++ inheritance probe uses `posix_spawn` file actions. Neither method in this
harness is a product frontend. Test-generated temporary HUP markers are
disposable; durable review evidence is this source listing and the sanitized
[observations](../../docs/go-core/probe-evidence.md), not a private terminal capture.

The `Cmd.Wait` tests evaluate standard-library behavior; their fallback kill
fixtures do not establish safe concurrent Darwin signalling. The separate
`TestSerializedReaper` now uses pty.Open + os.StartProcess, explicit V0 termios,
single-owner Wait4, GC and FD/goroutine baseline checks. The prior exec.Cmd custom
reaper at checkpoint 3b9f576 fails execwait diagnostics and is superseded.
The IPC
PTY fixture similarly establishes transport feasibility, not that production
cleanup, credit, framing validation, output sealing or PID safety is implemented.

Supplementary root-cause comparisons (not the final production-path proposal):

```sh
mkdir -p .probe/termios-compare .probe/reaper-alternative
cp experiments/go-core-architecture/fixtures/termios_compare.go.txt .probe/termios-compare/main.go
cp experiments/go-core-architecture/fixtures/reaper-alternative.go.txt .probe/reaper-alternative/main.go
cd .probe/go-core
go run ../termios-compare/main.go
GODEBUG=execwait=2 GOGC=1 go run -race ../reaper-alternative/main.go
```

The protocol model tests bounded ledger/admission/seal rules, not byte codecs or
kernel/socket scheduling. The startup model tests threaded reservation/cancellation
barriers and conceptual resources, not actual PTY resources or interruptible spawn.
The native watchdog probe uses a real posix_spawn child and local waiting worker,
asserts deadline/stage/signal/reap state, and emits a synthetic restoration marker.
It does not call Turbo Vision terminal-restoration APIs or qualify descendant cleanup.
