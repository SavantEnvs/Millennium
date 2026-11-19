// The `json` module Lua plugins use (src/lua_host/api/json.c, a modified lua-cjson): plugins
// json.decode() HTTP responses and other data they do not control, then json.encode() results.
#include "lua_fuzz_util.h"
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    lua_State* L = new_plugin_state();
    if (!L) return 0;
    push_module(L, luaopen_cjson);
    const int json = lua_gettop(L);

    const char* arg = reinterpret_cast<const char*>(data);
    if (pcall_field(L, json, "decode", &arg, &size, 1, 1) == 0) {
        // re-encode whatever was decoded (tables, strings, numbers, json.null)
        const int decoded = lua_gettop(L);
        lua_getfield(L, json, "encode");
        lua_pushvalue(L, decoded);
        if (lua_pcall(L, 1, 1, 0) == 0 && lua_type(L, -1) == LUA_TSTRING) {
            size_t n = 0;
            const char* enc = lua_tolstring(L, -1, &n);
            (void)pcall_field(L, json, "decode", &enc, &n, 1, 0);
        }
    }
    lua_close(L);
    return 0;
}
