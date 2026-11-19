// Lua host RPC replies: the plugin host (src/lua_host) receives u32-length + msgpack frames from
// the Millennium parent process over its socket (rpc_client::call -> plugin_ipc::read_msg), and
// millennium.call_frontend_method turns the reply's returnJson into Lua values with
// push_json_to_lua (nesting-depth cap added in ba4d360a76); config.get/get_all decode replies too.
//
// The input is the byte stream the parent sends back. A fresh rpc_client over a fresh socketpair
// is used for every input (request ids restart at 1, no stashed responses or deferred
// notifications carry over) and the Lua state is closed after each input.
#include "lua_fuzz_util.h"
#include "rpc.h"
#include "lua_api.h"
#include "millennium/star_parser.h"

#include <csignal>
#include <cstdint>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>

// The host globals the Lua API reads (defined in src/lua_host/main.cc, which also runs the
// host's socket main loop and is therefore not linked).
rpc_client* g_rpc = nullptr;
std::string g_backend_dir;
std::string g_backend_file;
bool g_plugin_is_v2 = true;
std::unordered_map<std::string, AssetEntry> g_asset_index;

void log_runtime_error(const std::string&) {}
void log_lua_error(const char*, const char*) {}

extern "C" int LLVMFuzzerInitialize(int*, char***)
{
    signal(SIGPIPE, SIG_IGN); // the parent end may be closed while the writer still has bytes
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) return 0;
    const int host = sv[0], parent = sv[1];

    // the "parent": writes the input as its replies, then closes its write side
    std::thread writer([parent, data, size] {
        size_t off = 0;
        while (off < size) {
            ssize_t n = send(parent, data + off, size - off, MSG_NOSIGNAL);
            if (n <= 0) break;
            off += static_cast<size_t>(n);
        }
        shutdown(parent, SHUT_WR);
    });

    {
        rpc_client rpc(host);
        g_rpc = &rpc;
        lua_State* L = new_plugin_state();
        if (L) {
            push_module(L, luaopen_millennium_lib);
            const int mil = lua_gettop(L);
            const char* name = "fuzz";
            size_t len = 4;
            (void)pcall_field(L, mil, "call_frontend_method", &name, &len, 1, 2);
            lua_settop(L, mil);
            // only while the link is up: a call on a dead link sleeps 50 ms before throwing
            lua_getfield(L, mil, "config");
            const int cfg = lua_gettop(L);
            const char* key = "key";
            size_t klen = 3;
            if (rpc.connected()) (void)pcall_field(L, cfg, "get", &key, &klen, 1, 1);
            lua_settop(L, cfg);
            if (rpc.connected()) (void)pcall_field(L, cfg, "get_all", nullptr, nullptr, 0, 1);
            lua_close(L);
        }
        g_rpc = nullptr;
    }
    // unblock a writer still sending bytes nobody read, then reap it
    shutdown(host, SHUT_RDWR);
    writer.join();
    close(host);
    close(parent);
    return 0;
}
