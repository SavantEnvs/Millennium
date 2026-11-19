// semver: version strings from remote update metadata and plugin/theme manifests.
// Input: "<v1>\0<v2>"; without a NUL both sides are the whole input.
#include "millennium/semver.h"
#include <cstdint>
#include <exception>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    std::string in(reinterpret_cast<const char*>(data), size);
    size_t nul = in.find('\0');
    std::string a = in.substr(0, nul), b = nul == std::string::npos ? a : in.substr(nul + 1);
    try { (void)semver::parse(a); } catch (const std::exception&) {}
    try { (void)semver::cmp(a, b); } catch (const std::exception&) {}
    try { (void)semver::cmp_pre_release(a, b); } catch (const std::exception&) {}
    return 0;
}
