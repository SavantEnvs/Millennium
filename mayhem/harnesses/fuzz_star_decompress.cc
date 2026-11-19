// star_decompress: the public LZ4-style block decoder for .star sections (also exposed to
// Lua plugins as MILLENNIUM_DECOMPRESS). It throws std::runtime_error on malformed input.
#include "millennium/star_parser.h"
#include <exception>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    try { (void)star_decompress(data, size); } catch (const std::exception&) {}
    return 0;
}
