// Loopback IPC: the length-prefixed msgpack stream the CEF hook helper (a separate process
// that handles Steam's web content) sends back to Millennium over a pipe pair. The input is
// the raw byte stream the helper would write: u32-LE length + msgpack frame, repeated.
//
// register_loopback_conn takes ownership of both fds and serves the connection on a detached
// thread: it writes two patch-list frames, then reads frames until EOF and closes its write fd.
// The harness feeds the input, closes its end, and drains Millennium's write side until EOF, so
// every input is fully processed before the next one starts.
// Kept across inputs on purpose: patch_stream_recorder's bounded event ring (write-only here;
// nothing in this path reads it back) and the empty patch registry.
#include "instrumentation/loopback_ipc.h"
#include <csignal>
#include <cstdint>
#include <unistd.h>

// Millennium closes its read end when it stops reading; a write to it must fail with EPIPE,
// not kill the process.
extern "C" int LLVMFuzzerInitialize(int*, char***)
{
    signal(SIGPIPE, SIG_IGN);
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    int to_host[2], from_host[2];   // to_host: harness -> Millennium; from_host: Millennium -> harness
    if (pipe(to_host) != 0) return 0;
    if (pipe(from_host) != 0) { close(to_host[0]); close(to_host[1]); return 0; }

    register_loopback_conn(from_host[1], to_host[0]);

    size_t off = 0;
    while (off < size) {
        ssize_t w = write(to_host[1], data + off, size - off);
        if (w <= 0) break;              // reader gave up (EPIPE): the rest is never read
        off += static_cast<size_t>(w);
    }
    close(to_host[1]);

    char sink[4096];
    while (read(from_host[0], sink, sizeof sink) > 0) {}
    close(from_host[0]);
    return 0;
}
