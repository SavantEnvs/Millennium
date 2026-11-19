// Plugin, theme and update archives: Millennium downloads .zip files from the plugin/theme store
// and its updater and unpacks them with Util::GetLockedFiles + Util::ExtractZipArchive
// (src/util/zip.cc, minizip-ng + zlib-ng). The input is the archive file.
//
// Every input is extracted into a fresh directory under one per-process scratch dir, which is
// removed again before the next input, so no file state carries over.
#include "fuzz_util.h"
#include "millennium/zip.h"

#include <mz.h>
#include <mz_strm.h>
#include <mz_zip.h>
#include <mz_zip_rw.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>

static std::string g_scratch;

extern "C" int LLVMFuzzerInitialize(int*, char***)
{
    const char* dir = getenv("TMPDIR");
    std::string t = std::string(dir && *dir ? dir : "/tmp") + "/mayhem-zip-XXXXXX";
    if (mkdtemp(t.data())) g_scratch = t;
    return 0;
}

// ExtractZipArchive writes each entry to dest_dir + "/" + name and lets minizip-ng create symlink
// entries, so an archive can write outside dest_dir ("../x", or a symlink followed by a write
// through it). The fuzzer must not write outside its scratch dir (each input's result may depend
// only on that input), so archives that would do that are rejected, using minizip-ng's own reader
// over the same bytes.
static bool stays_inside(const uint8_t* data, size_t size)
{
    void* r = mz_zip_reader_create();
    if (!r) return false;
    bool ok = true;
    if (mz_zip_reader_open_buffer(r, const_cast<uint8_t*>(data), static_cast<int32_t>(size), 0) == MZ_OK) {
        for (int32_t err = mz_zip_reader_goto_first_entry(r); err == MZ_OK && ok; err = mz_zip_reader_goto_next_entry(r)) {
            mz_zip_file* fi = nullptr;
            if (mz_zip_reader_entry_get_info(r, &fi) != MZ_OK || !fi || !fi->filename) continue;
            void* zh = nullptr;
            if (mz_zip_reader_get_zip_handle(r, &zh) == MZ_OK && zh && mz_zip_entry_is_symlink(zh) == MZ_OK) ok = false;
            if (fi->linkname && *fi->linkname) ok = false;
            std::string name(fi->filename);
            for (char& c : name) if (c == '\\') c = '/';
            size_t pos = 0;
            while (ok && pos <= name.size()) {
                size_t end = name.find('/', pos);
                if (end == std::string::npos) end = name.size();
                if (name.compare(pos, end - pos, "..") == 0 && end - pos == 2) ok = false;
                pos = end + 1;
            }
        }
        mz_zip_reader_close(r);
    }
    mz_zip_reader_delete(&r);
    return ok;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (g_scratch.empty() || size > INT32_MAX) return 0;
    if (!stays_inside(data, size)) return -1;

    const std::string zip = write_temp_input(data, size, ".zip");
    if (zip.empty()) return 0;
    const std::string out = g_scratch + "/out";

    (void)Util::GetLockedFiles(zip, out);
    int last = 0;
    (void)Util::ExtractZipArchive(zip, out, [&last](int cur, int total, const char* file) {
        last = cur + total + (file ? static_cast<int>(file[0]) : 0);
    });

    std::error_code ec;
    std::filesystem::remove_all(out, ec);
    unlink(zip.c_str());
    return 0;
}
