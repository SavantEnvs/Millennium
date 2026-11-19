// The `utils` module's string decoders and splitters (src/lua_host/api/utils.cc), which
// plugins apply to downloaded or user-supplied text. Side-effecting functions (exec, setenv,
// sleep, read_file/write_file) are not called. Input: <selector byte><a>\0<b>.
#include "lua_fuzz_util.h"
#include <cstdint>
#include <cstring>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 1) return -1;
    static const char* const one_arg[] = { "base64_decode", "url_decode", "from_hex", "trim", "ltrim", "rtrim", "hash", "base64_encode", "url_encode", "to_hex" };
    static const char* const two_arg[] = { "split", "startswith", "endswith" };
    const size_t n_one = sizeof one_arg / sizeof *one_arg, n_two = sizeof two_arg / sizeof *two_arg;
    const unsigned sel = data[0] % (n_one + n_two);

    const char* a = reinterpret_cast<const char*>(data + 1);
    const size_t rest = size - 1;
    const void* nul = memchr(a, '\0', rest);
    size_t alen = nul ? static_cast<size_t>(static_cast<const char*>(nul) - a) : rest;
    const char* b = nul ? a + alen + 1 : ",";
    size_t blen = nul ? rest - alen - 1 : 1;

    lua_State* L = new_plugin_state();
    if (!L) return 0;
    push_module(L, luaopen_utils_lib);
    const int utils = lua_gettop(L);
    if (sel < n_one) {
        (void)pcall_field(L, utils, one_arg[sel], &a, &rest, 1, 1);
    } else {
        const char* args[] = { a, b };
        const size_t lens[] = { alen, blen };
        (void)pcall_field(L, utils, two_arg[sel - n_one], args, lens, 2, 1);
    }
    lua_close(L);
    return 0;
}
