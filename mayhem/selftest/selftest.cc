// Known-answer tests for the parsers the fuzz targets exercise. Built with the project's
// normal flags by build.sh; run by test.sh, which reads the PASS/FAIL lines.
// Fixtures: mayhem/selftest/*.star (from mayhem/gen_star_seeds.py) and theme.css.
#include "head/css_parser.h"
#include "millennium/cmdline_parser.h"
#include "millennium/semver.h"
#include "millennium/star_parser.h"
#include <cstdio>
#include <functional>
#include <string>

static int g_fail = 0;
static void check(const char* name, const std::function<bool()>& fn)
{
    bool ok = false;
    try { ok = fn(); } catch (...) { ok = false; }
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++g_fail;
}

int main(int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : "mayhem/selftest";

    check("star_parse_plugin", [&] {
        auto p = parse_star_file(dir + "/plugin.star");
        return p && p->plugin_name == "demo-plugin" && p->plugin_json["common_name"] == "Demo Plugin" && p->plugin_json["version"] == "1.2.3" &&
               p->backend_entry == "main.lua" && p->has_frontend_js && p->has_webkit_js && !p->is_trusted;
    });
    check("star_frontend_bundle", [&] { return star_read_javascript(dir + "/plugin.star", star_js_section::frontend) == "console.log(\"millennium frontend\");"; });
    check("star_webkit_first_file", [&] { return star_read_javascript(dir + "/plugin.star", star_js_section::webkit) == "document.title = \"webkit\";"; });
    check("star_asset_index", [&] {
        auto idx = star_read_asset_index(dir + "/plugin.star");
        return idx.size() == 2 && idx.count("img/a.png") && idx.at("img/a.png").uncompressed_length == 16 && idx.at("img/a.png").compressed_length == 15 &&
               idx.count("dir/") && idx.at("dir/").compressed_length == 0;
    });
    check("star_shim_prefix", [&] {
        auto p = parse_star_file(dir + "/shim.star");
        return p && p->plugin_name == "demo-plugin" && star_read_javascript(dir + "/shim.star", star_js_section::frontend) == "console.log(\"millennium frontend\");";
    });
    check("star_bad_signature_untrusted", [&] { auto p = parse_star_file(dir + "/signed.star"); return p && !p->is_trusted && !p->has_frontend_js; });
    check("star_missing_file", [&] { return !parse_star_file(dir + "/does-not-exist.star"); });
    check("star_decompress_match", [] {
        const uint8_t in[] = { 6, 0, 0, 0, 0x22, 'a', 'b', 2, 0 };  // declares 6 bytes but decodes to 8: must throw
        const uint8_t in2[] = { 8, 0, 0, 0, 0x22, 'a', 'b', 2, 0 }; // literal "ab", then a 6-byte match at offset 2
        auto out = star_decompress(in2, sizeof in2);
        bool threw = false;
        try { star_decompress(in, sizeof in); } catch (const std::exception&) { threw = true; }
        return std::string(out.begin(), out.end()) == "abababab" && threw;
    });
    check("semver_cmp", [] {
        return semver::cmp("1.2.3", "1.2.4") < 0 && semver::cmp("2.0.0", "1.9.9") > 0 && semver::cmp("1.0.0", "1.0.0") == 0 &&
               semver::cmp("1.0.0-beta", "1.0.0") < 0 && semver::cmp_pre_release("alpha.1", "alpha.2") < 0;
    });
    check("semver_parse", [] { auto v = semver::parse("3.14.15-rc.1+build.7"); return v.major == 3 && v.minor == 14 && v.patch == 15 && v.prerelease == "rc.1" && v.build == "build.7"; });
    check("cmdline_roundtrip", [] {
        command c("\"/opt/steam dir/steam\" -silent --disable-gpu --remote-debugging-port=1");
        c.ensure_param("--remote-debugging-port", "8080");
        c.remove_param("--disable-gpu");
        return c.executable() == "steam" && c.params.size() == 2 && c.params[1] == "--remote-debugging-port=8080";
    });
    check("css_root_colors", [&] {
        auto j = head::css_parser::parse_root_colors(dir + "/theme.css");
        return j.is_array() && j.size() == 2 && j[0]["color"] == "--accent" && j[0]["name"].get<std::string>().rfind("Accent", 0) == 0 && j[0]["description"] == "Main accent colour" && j[0]["defaultColor"] == "#ff8800" && j[0]["type"] == 5 &&
               j[1]["color"] == "--panel" && j[1]["type"] == 4 && j[1]["defaultColor"] == "#0a141eff";
    });
    check("css_hex_conversion", [] {
        using head::color_type;
        return head::css_parser::parse_color("#ff8800") == color_type::Hex && head::css_parser::convert_from_hex("#ff8800", color_type::RGB) == "rgb(255, 136, 0)" &&
               head::css_parser::convert_to_hex("rgb(255, 136, 0)", color_type::RGB) == "#ff8800";
    });
    return g_fail ? 1 : 0;
}
