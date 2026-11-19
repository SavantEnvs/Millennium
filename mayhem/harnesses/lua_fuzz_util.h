#pragma once
// A fresh LuaJIT state per input with the plugin API modules preloaded the way
// tests/lua/main.cc (upstream's Lua API test runner) registers them.
#include <cstddef>
#include <lua.hpp>

extern "C" int luaopen_cjson(lua_State* L);
extern "C" int luaopen_utils_lib(lua_State* L);

static inline lua_State* new_plugin_state()
{
    lua_State* L = luaL_newstate();
    if (!L) return nullptr;
    luaL_openlibs(L);
    return L;
}

// Leaves module `name` (opened by `open`) on the stack.
static inline void push_module(lua_State* L, lua_CFunction open)
{
    lua_pushcfunction(L, open);
    lua_call(L, 0, 1);
}

// Calls module[fn](args already pushed after the function); errors are Lua errors (pcall), not crashes.
static inline int pcall_field(lua_State* L, int module_idx, const char* fn, const char* const* sargs, const size_t* lens, int nargs, int nret)
{
    lua_getfield(L, module_idx, fn);
    for (int i = 0; i < nargs; ++i)
        lua_pushlstring(L, sargs[i], lens[i]);
    return lua_pcall(L, nargs, nret, 0);
}
