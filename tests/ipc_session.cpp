#include "ipc_session.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <thread>
#include <tvterm/vtermemu.h>
#define Uses_THardwareInfo
#include <tvision/tv.h>
static std::atomic<bool> countCopies{false};
static std::atomic<int> largeCopies{0};
void *operator new(size_t n) {
    if (countCopies && n > 32768)
        ++largeCopies;
    if (auto p = std::malloc(n))
        return p;
    throw std::bad_alloc();
}
void operator delete(void *p) noexcept { std::free(p); }
void *operator new[](size_t n) { return ::operator new(n); }
void operator delete[](void *p) noexcept { std::free(p); }

using namespace agentvision;
static void check(bool v, const char *s) {
    if (!v)
        throw std::runtime_error(s);
}
static ConnectionEvent event(std::shared_ptr<CoreConnection> c, ConnectionEvent::Kind kind) {
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    ConnectionEvent e;
    while (std::chrono::steady_clock::now() < end) {
        if (c->pollEvent(e)) {
            check(e.kind != ConnectionEvent::Kind::Lost, "contact loss");
            if (e.kind == kind)
                return e;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("event timeout");
}
static std::shared_ptr<CoreConnection> start(const char *mode) {
    setenv("AV_CONNECTION_CASE", mode, 1);
    auto r = CoreConnection::start(FAKE_CORE);
    check(bool(r.connection), "start");
    event(r.connection, ConnectionEvent::Kind::Ready);
    return r.connection;
}
int main() {
    THardwareInfo hardware;
    try {
        auto c = start("binding");
        c->createSession(24, 80);
        auto first = event(c, ConnectionEvent::Kind::Created);
        auto a = c->endpoint(first.session);
        IpcSessionTransport old(c, a);
        auto bytes = old.readChunk();
        check(bytes.bytes == std::vector<char>({'A', '\0', char(255)}),
              "owned decoder payload survives subsequent frame storage");
        old.consumed(bytes.bytes.size());
        char oversized[32769]{};
        countCopies = true;
        auto large = old.enqueueInput({oversized, sizeof oversized}, tvterm::InputOrigin::User);
        countCopies = false;
        check(large == tvterm::EnqueueResult::Overflow && largeCopies == 0,
              "oversize input rejects before payload allocation");
        check(old.requestClose() != 0, "bound Close admitted");
        event(c, ConnectionEvent::Kind::Closed);
        auto req = c->createSession(24, 80);
        auto replacement = event(c, ConnectionEvent::Kind::Created);
        check(replacement.session == first.session, "synthetic opaque ID reused after retirement");
        check(bytes.bytes == std::vector<char>({'A', '\0', char(255)}),
              "owned payload unchanged after decoder storage reused for Close and replacement "
              "Create");
        IpcSessionTransport current(c, c->endpoint(replacement.session));
        check(old.enqueueInput({"x", 1}, tvterm::InputOrigin::User) ==
                  tvterm::EnqueueResult::Closed,
              "stale input rejected");
        old.resize({80, 24});
        check(old.requestClose() == 0, "stale close rejected");
        auto payload = current.readChunk();
        check(payload.bytes == std::vector<char>({'A', '\0', char(255)}),
              "replacement owns unchanged output");
        current.consumed(payload.bytes.size());
        check(current.enqueueInput({"x", 1}, tvterm::InputOrigin::User) ==
                  tvterm::EnqueueResult::Queued,
              "current input admitted");
        current.resize({80, 24});
        auto response = current.readChunk();
        check(response.bytes == std::vector<char>({'o', 'k'}),
              "peer confirms no stale request/resize/ticket allocation and ordered current input "
              "resize");
        current.consumed(response.bytes.size());
        auto ownclose = current.requestClose();
        check(ownclose > req, "current Close valid");
        event(c, ConnectionEvent::Kind::Closed);
        auto other = start("binding");
        IpcSessionTransport mismatch(other, a);
        check(mismatch.enqueueInput({"x", 1}, tvterm::InputOrigin::User) ==
                      tvterm::EnqueueResult::Closed &&
                  mismatch.requestClose() == 0,
              "mismatched connection rejected");
        check(mismatch.readChunk().kind == tvterm::TransportChunk::Kind::Lost,
              "mismatch has no reader authority");
        c->shutdown();
        check(c->join().graceful, "binding connection graceful");
        other->shutdown();
        check(other->join().graceful, "other connection graceful");
        auto f = start("flush-close");
        f->createSession(24, 80);
        IpcSessionTransport final(f, f->endpoint(event(f, ConnectionEvent::Kind::Created).session));
        auto data = final.readChunk();
        final.consumed(data.bytes.size());
        auto end = final.readChunk();
        check(end.kind == tvterm::TransportChunk::Kind::End, "end distinct");
        check(final.metadata().state != SessionState::Exited, "status deferred");
        check(final.enqueueInput({"x", 1}, tvterm::InputOrigin::User) ==
                  tvterm::EnqueueResult::Closed,
              "sealed endpoint suppresses input before visible exit");
        final.resize({80, 24});
        final.flushed(end.sequence);
        check(final.metadata().state == SessionState::Exited, "actual flush publishes exit");
        check(final.requestClose() != 0, "retained Exited session can Close");
        event(f, ConnectionEvent::Kind::Closed);
        f->shutdown();
        check(f->join().graceful, "final graceful");
        auto presented = start("flush-close");
        presented->createSession(24, 80);
        auto retained =
            presented->endpoint(event(presented, ConnectionEvent::Kind::Created).session);
        auto presentationTransport =
            std::unique_ptr<IpcSessionTransport>(new IpcSessionTransport(presented, retained));
        auto *presentationBinding = presentationTransport.get();
        tvterm::VTermEmulatorFactory factory;
        auto *controller = tvterm::TerminalController::createWithTransport(
            {80, 24}, factory, std::move(presentationTransport));
        struct Owner {
            tvterm::TerminalController *value;
            ~Owner() {
                if (value)
                    value->shutDown();
            }
        } presentation{controller};
        check(controller != nullptr, "real controller construction");
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!controller->clientIsDisconnected() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        check(controller->clientIsDisconnected() &&
                  presentationBinding->metadata().state == SessionState::Exited,
              "real controller End flushed before presentation finish");
        check(presentationBinding->metadata().status.kind == ExitKind::Exit &&
                  presentationBinding->metadata().status.value == 7,
              "retained status is actual authoritative exact exit 7");
        controller->finishPresentation();
        check(presentationBinding->requestClose() != 0,
              "retained Exited own binding Close remains admitted after real presentation finish");
        event(presented, ConnectionEvent::Kind::Closed);
        check(presentationBinding->requestClose() == 0,
              "finished retired binding cannot close twice");
        controller->shutDown();
        presentation.value = nullptr;
        presented->shutdown();
        check(presented->join().graceful, "finished retained presentation cleanup");
        auto lost = start("flush-close");
        lost->createSession(24, 80);
        IpcSessionTransport lostExited(
            lost, lost->endpoint(event(lost, ConnectionEvent::Kind::Created).session));
        auto lostData = lostExited.readChunk();
        lostExited.consumed(lostData.bytes.size());
        auto lostEnd = lostExited.readChunk();
        check(lostEnd.kind == tvterm::TransportChunk::Kind::End, "lost fixture final End");
        lostExited.flushed(lostEnd.sequence);
        check(lostExited.metadata().state == SessionState::Exited,
              "lost fixture authoritative exit");
        lost->cancelLocal();
        check(lostExited.metadata().state == SessionState::Exited && lostExited.requestClose() == 0,
              "contact loss rejects own Close even when retained exact Exited metadata remains");
        check(!lost->join().graceful, "contact loss never claims graceful cleanup");
        auto cancelled = start("binding");
        cancelled->createSession(24, 80);
        auto made = event(cancelled, ConnectionEvent::Kind::Created);
        auto endpoint = cancelled->endpoint(made.session);
        IpcSessionTransport detached(cancelled, endpoint);
        auto released = detached.readChunk();
        detached.consumed(released.bytes.size());
        detached.cancelLocal();
        check(detached.enqueueInput({"x", 1}, tvterm::InputOrigin::User) ==
                      tvterm::EnqueueResult::Closed &&
                  detached.requestClose() == 0,
              "cancelled running binding removes all adapter admission authority");
        detached.resize({80, 24});
        check(detached.readChunk().kind == tvterm::TransportChunk::Kind::Lost,
              "local cancellation wakes without exact exit");
        check(cancelled->closeSession(made.session) != 0,
              "authoritative Close remains independent of local detach");
        event(cancelled, ConnectionEvent::Kind::Closed);
        cancelled->shutdown();
        check(cancelled->join().graceful, "cancelled binding cleanup");
        std::cout << "PASS: identity binding, stale operations, mismatch, owned output, final "
                     "flush and retained Close\n";
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
