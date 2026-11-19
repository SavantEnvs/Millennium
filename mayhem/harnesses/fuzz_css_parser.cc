// Theme CSS: parse_root_colors reads a theme stylesheet from disk, extracts its :root
// block and the colour properties/comments; the colour helpers convert each value.
#include "fuzz_util.h"
#include "head/css_parser.h"
#include <exception>

using namespace head;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    const std::string path = write_temp_input(data, size, ".css");
    if (path.empty()) return 0;
    try { (void)css_parser::parse_root_colors(path); } catch (const std::exception&) {}
    unlink(path.c_str());

    std::string value(reinterpret_cast<const char*>(data), size);
    value = value.substr(0, value.find('\n'));
    try {
        color_type t = css_parser::parse_color(value);
        (void)css_parser::convert_to_hex(value, t);
        (void)css_parser::convert_from_hex(value, t);
        (void)css_parser::convert_from_hex(value, color_type::RGBA);
    } catch (const std::exception&) {}
    return 0;
}
