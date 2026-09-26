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
for name in go.mod go.sum main.go probe_test.go ipc_probe.py; do
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
```

The Python single-thread harness uses `preexec_fn` only to install FD3. The actual
C++ inheritance probe uses `posix_spawn` file actions. Neither method in this
harness is a product frontend. Test-generated temporary HUP markers are
disposable; durable review evidence is this source listing and the sanitized
[observations](../../docs/go-core/probe-evidence.md), not a private terminal capture.

The `Cmd.Wait` tests evaluate standard-library behavior; their fallback kill
fixtures do not establish safe concurrent Darwin signalling. The separate
`TestSerializedReaper` validates the proposed single-owner alternative. The IPC
PTY fixture similarly establishes transport feasibility, not that production
cleanup, credit, framing validation, output sealing or PID safety is implemented.
