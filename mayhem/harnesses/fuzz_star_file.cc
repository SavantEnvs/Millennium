// .star plugin package reader: the path Millennium takes when it loads a plugin package
// from disk (parse_star_file at discovery, star_read_javascript when injecting the
// frontend/webkit bundles, star_read_asset_index when serving plugin assets).
#include "fuzz_util.h"
#include "millennium/star_parser.h"
#include <exception>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    const std::string path = write_temp_input(data, size, ".star");
    if (path.empty()) return 0;
    // The public functions report bad input by returning empty/nullopt or throwing std::exception.
    try { (void)parse_star_file(path); } catch (const std::exception&) {}
    try { (void)star_read_javascript(path, star_js_section::frontend); } catch (const std::exception&) {}
    try { (void)star_read_javascript(path, star_js_section::webkit); } catch (const std::exception&) {}
    try { (void)star_read_asset_index(path); } catch (const std::exception&) {}
    unlink(path.c_str());
    return 0;
}
