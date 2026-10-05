#include "artifact/scanner.hpp"
#include "artifact/encoding.hpp"
#include "artifact/identity.hpp"
#include "artifact_golden_vectors.hpp"
#include "artifact_test_support.hpp"
#include <fstream>
#include <iostream>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
using namespace athena::artifact;
namespace fs = std::filesystem;
namespace {
struct Tree {
    fs::path root;
    Tree() {
        std::string pattern = (fs::temp_directory_path() / "athena-identity-XXXXXX").native();
        require(::mkdtemp(pattern.data()) != nullptr, "mkdtemp"); root = pattern;
    }
    ~Tree() { std::error_code error; fs::remove_all(root, error); }
};
void write(const fs::path& path, const std::string& content, mode_t mode) {
    std::ofstream stream(path, std::ios::binary);
    stream.write(content.data(), static_cast<std::streamsize>(content.size()));
    stream.close(); require(!stream.fail(), "fixture write");
    require(::chmod(path.c_str(), mode) == 0, "fixture chmod");
}
}
int main()
{
    Tree a, b;
    fs::create_directory(a.root / "bin"); write(a.root / "bin/tool", "hi\n", 0755);
    fs::create_directory(a.root / "empty"); fs::create_symlink("../bin/tool", a.root / "l");
    fs::create_symlink("../bin/tool", b.root / "l"); fs::create_directory(b.root / "empty");
    fs::create_directory(b.root / "bin"); write(b.root / "bin/tool", "hi\n", 0700);
    require(::chmod((b.root / "bin").c_str(), 0700) == 0, "directory chmod");
    const timespec times[2]{{123, 0}, {456, 0}};
    require(::utimensat(AT_FDCWD, (b.root / "bin/tool").c_str(), times, 0) == 0, "fixture timestamp");
    try {
        const auto model_a = scan(a.root), model_b = scan(b.root);
        require(model_a == model_b, "irrelevant metadata changed model");
        require(canonical_bytes(model_a) == canonical_bytes(model_b), "irrelevant metadata changed bytes");
        const auto id = derive_artifact_id(model_a);
        require(id == derive_artifact_id(model_b), "irrelevant metadata changed identity");
        require(id.value() == golden_vectors[8].artifact_id, "scanned mixed tree golden");
        write(b.root / "bin/tool", "changed", 0700);
        require(derive_artifact_id(scan(b.root)) != id, "scanned content change omitted");
        write(b.root / "bin/tool", "hi\n", 0600);
        require(derive_artifact_id(scan(b.root)) != id, "scanned executable change omitted");
        write(b.root / "bin/tool", "hi\n", 0700);
        fs::rename(b.root / "bin/tool", b.root / "bin/other");
        require(derive_artifact_id(scan(b.root)) != id, "scanned path change omitted");
        fs::rename(b.root / "bin/other", b.root / "bin/tool");
        fs::remove(b.root / "empty");
        require(derive_artifact_id(scan(b.root)) != id, "scanned empty directory change omitted");
        fs::create_directory(b.root / "empty"); fs::remove(b.root / "l");
        fs::create_symlink("/bin/tool", b.root / "l");
        require(derive_artifact_id(scan(b.root)) != id, "scanned symlink change omitted");
        fs::remove(b.root / "l"); write(b.root / "l", "../bin/tool", 0644);
        require(derive_artifact_id(scan(b.root)) != id, "scanned type change omitted");
    } catch (const Error& error) {
        if (error.code() != ErrorCode::InspectionFailure ||
            std::string(error.what()).find("hidden trusted.*") == std::string::npos) throw;
        std::cout << "SKIP scanner identity integration: " << error.what() << '\n';
        return 77;
    }
}
