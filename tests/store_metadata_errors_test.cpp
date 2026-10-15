// SPDX-License-Identifier: GPL-3.0-or-later
#include "store/metadata_detail.hpp"
#include "temporary_directory.hpp"
#include <toml++/toml.hpp>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void legacy_error(auto action, const std::filesystem::path& path, std::string_view diagnostic) {
    try { action(); }
    catch (const std::runtime_error& e) {
        const std::string message(e.what());
        require(message.find(path.string()) != message.npos, "missing store path in legacy diagnostic");
        if (message.find(diagnostic) == message.npos)
            throw std::runtime_error("missing diagnostic '" + std::string(diagnostic) + "' in: " + message);
        return;
    }
    catch (...) { throw std::runtime_error("legacy exception no longer derives from runtime_error"); }
    throw std::runtime_error("invalid metadata accepted");
}
// Announce a different file length, then serve the real bytes. Exercises the
// production reader deterministically; no racing writer or special file needed.
class ChangedBuffer : public std::stringbuf {
    std::streamoff announced_;
    bool at_announced_end_ = false;
public:
    ChangedBuffer(std::string text, std::streamoff announced)
        : std::stringbuf(std::move(text), std::ios::in), announced_(announced) {}
    pos_type seekoff(off_type off, std::ios::seekdir dir, std::ios::openmode mode) override {
        if (dir == std::ios::end && off == 0) {
            at_announced_end_ = true;
            return pos_type(announced_);
        }
        if (dir == std::ios::cur && off == 0 && at_announced_end_) return pos_type(announced_);
        at_announced_end_ = false;
        return std::stringbuf::seekoff(off, dir, mode);
    }
    pos_type seekpos(pos_type position, std::ios::openmode mode) override {
        at_announced_end_ = false;
        return std::stringbuf::seekpos(position, mode);
    }
};
void tests() {
    using namespace athena::store;
    athena::test::TemporaryDirectory fixture("athena-metadata-errors");
    const auto root = fixture.path();
    legacy_error([&] { read_metadata(root); }, root, "Cannot open TOML document");
    std::filesystem::create_directories(root / ".athena");
    auto write = [&](std::string_view bytes) { std::ofstream(root / ".athena/metadata.toml", std::ios::binary) << bytes; };
    write("name = [");
    legacy_error([&] { read_metadata(root); }, root, "Error");
    std::string original_diagnostic;
    try { toml::parse_file((root / ".athena/metadata.toml").string()); }
    catch (const toml::parse_error& e) {
        original_diagnostic = "Impossibile leggere i metadati del pacchetto '" + root.string() + "': " + e.what();
    }
    require(!original_diagnostic.empty(), "original parser did not reject malformed fixture");
    try { read_metadata(root); throw std::logic_error("malformed metadata accepted"); }
    catch (const std::runtime_error& e) {
        require(e.what() == original_diagnostic, "legacy malformed diagnostic differs from original parser wrapper");
    }
    write("name='pkg'\ndependencies=[]\nfuture_extension={value=1}\n");
    require(read_metadata(root).name == "pkg", "legacy generic API lost permissive extensions");
    legacy_error([&] { read_metadata(root, 1024); }, root, "Unknown TOML field");
    legacy_error([&] { read_metadata(root, 1); }, root, "file bound");
    write("name=[0]\n");
    legacy_error([&] { read_metadata(root, 1024); }, root, "Unexpected TOML value type");
    ChangedBuffer short_buffer("name='pkg'\n", 100);
    std::istream short_stream(&short_buffer);
    legacy_error([&] { metadata_detail::read_metadata_stream(short_stream, root, 1024); }, root, "Incomplete TOML document read");
    ChangedBuffer grown_buffer("name='pkg'\n", 1);
    std::istream grown_stream(&grown_buffer);
    legacy_error([&] { metadata_detail::read_metadata_stream(grown_stream, root, 1024); }, root, "changed beyond authorized bytes");
}
}
int main() { try { tests(); return 0; } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; } }
