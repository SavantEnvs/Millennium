// command: tokenises and rebuilds the Steam / CEF helper command line Millennium rewrites.
#include "millennium/cmdline_parser.h"
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    std::string in(reinterpret_cast<const char*>(data), size); // c_str() NUL-terminates
    command cmd(in.c_str());
    (void)cmd.executable();
    cmd.ensure_param("--remote-debugging-port", "8080");
    cmd.ensure_param("-cef-enable-debugging");
    cmd.remove_param("--disable-gpu");
    std::string out = cmd.build();
    command again(out.c_str()); // the rebuilt line is parsed again by the next process
    (void)again.build();
    return 0;
}
