// Runs upstream's Lua plugin-API suite (tests/lua/cases/*.lua) the way tests/lua/main.cc does,
// without Catch2: one fresh state per case file, PASS/FAIL per file. Built by build.sh with
// normal flags; run by test.sh.
#include <cstdio>
#include <filesystem>
#include <lua.hpp>
#include <set>
#include <string>

extern "C" int luaopen_regex_lib(lua_State* L);
extern "C" int luaopen_datetime_lib(lua_State* L);
extern "C" int luaopen_fs_lib(lua_State* L);
extern "C" int luaopen_utils_lib(lua_State* L);
extern "C" int luaopen_cjson(lua_State* L);

namespace fs = std::filesystem;

static void register_module(lua_State* L, const char* name, lua_CFunction loader)
{
    lua_pushcfunction(L, loader);
    lua_setfield(L, -2, name);
}

int main(int argc, char** argv)
{
    const fs::path cases = argc > 1 ? argv[1] : "tests/lua/cases";
    std::set<fs::path> files;
    for (const auto& e : fs::directory_iterator(cases))
        if (e.path().extension() == ".lua") files.insert(e.path());
    if (files.empty()) { std::printf("FAIL lua:no-cases\n"); return 1; }

    int failed = 0;
    for (const auto& path : files) {
        lua_State* L = luaL_newstate();
        luaL_openlibs(L);
        const auto backend = (fs::temp_directory_path() / "millennium-lua-test-backend").string();
        lua_pushstring(L, backend.c_str());
        lua_setglobal(L, "MILLENNIUM_PLUGIN_SECRET_BACKEND_ABSOLUTE");
        lua_getglobal(L, "package");
        lua_getfield(L, -1, "preload");
        register_module(L, "regex", luaopen_regex_lib);
        register_module(L, "datetime", luaopen_datetime_lib);
        register_module(L, "fs", luaopen_fs_lib);
        register_module(L, "utils", luaopen_utils_lib);
        register_module(L, "json", luaopen_cjson);
        lua_pop(L, 2);
        if (luaL_dofile(L, path.string().c_str()) != 0) {
            const char* err = lua_tostring(L, -1);
            std::printf("FAIL lua:%s %s\n", path.filename().string().c_str(), err ? err : "unknown Lua error");
            ++failed;
        } else {
            std::printf("PASS lua:%s\n", path.filename().string().c_str());
        }
        lua_close(L);
    }
    return failed ? 1 : 0;
}
