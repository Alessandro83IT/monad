#ifdef NDEBUG
#undef NDEBUG
#endif
#include <athena/core/api.hpp>
#include <athena/core/config.hpp>
#include <athena/core/locking.hpp>
#include <athena/core/path_layout.hpp>
#include <athena/core/schema.hpp>
#include "../src/cli/cli.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>
#include <cstdlib>
#include <sys/wait.h>
#include <unistd.h>

namespace {
struct TempRoot {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("athena-m0-" + std::to_string(std::filesystem::file_time_type::clock::now().time_since_epoch().count()));
    TempRoot() { std::filesystem::create_directories(path); }
    ~TempRoot() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};

void write_config(const std::filesystem::path& path, const std::filesystem::path& root)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    out << "schema_version = 1\n[paths]\nroot = \"" << root.string() << "\"\n";
}
}

int main()
{
    using namespace athena::core;
    TempRoot temp;

    const auto layout = PathLayout::from_root(temp.path / "layout");
    assert(layout.store == layout.root / "store");
    assert(layout.generation_state == layout.root / "generations");
    layout.validate();

    const PackageId package("zlib");
    const PackageVersion version("1.3.1");
    const RepositoryId repository("core");
    assert(package.value() == "zlib" && version.value() == "1.3.1");
    assert(repository.value() == "core");

    const ArtifactId artifact("sha256:output");
    const BuildInputId input("sha256:derivation");
    assert(artifact.value() != input.value());
    static_assert(!std::is_same_v<ArtifactId, BuildInputId>);
    const DigestSet digests{ContentDigest("sha256:content"), ManifestDigest("sha256:manifest")};
    assert(digests.output_digest.value() != digests.manifest_digest.value());
    const Provenance provenance{ArtifactOrigin::ImportedBinary, "arch", "snapshot-1", "pkg.tar.zst", "test-key"};
    assert(provenance.origin == ArtifactOrigin::ImportedBinary);

    validate_schema_version(SchemaKind::Configuration, 1);
    bool schema_rejected = false;
    try { validate_schema_version(SchemaKind::Journal, 99); }
    catch (const std::invalid_argument&) { schema_rejected = true; }
    assert(schema_rejected);
    assert(schema_name(SchemaKind::BuildIR) == "build_ir");

    const auto system_file = temp.path / "system.toml";
    const auto user_file = temp.path / "user.toml";
    const auto explicit_file = temp.path / "explicit.toml";
    write_config(system_file, temp.path / "system-root");
    write_config(user_file, temp.path / "user-root");
    write_config(explicit_file, temp.path / "explicit-root");

    ConfigLoadOptions options;
    options.system_config = system_file;
    options.user_config = user_file;
    options.explicit_config = explicit_file;
    options.overrides.root = temp.path / "cli-root";
    ConfigLoader loader;
    auto loaded = loader.load(options);
    assert(loaded);
    assert(loaded.config->paths.root == temp.path / "cli-root");
    assert(loaded.config->paths.generation_state == temp.path / "cli-root" / "generations");
    assert(loaded.config->provenance.at("root") == ConfigSource::CommandLine);

    ConfigLoadOptions no_explicit;
    no_explicit.system_config = system_file;
    no_explicit.user_config = user_file;
    loaded = loader.load(no_explicit);
    assert(loaded && loaded.config->paths.root == temp.path / "user-root");
    assert(loaded.config->provenance.at("root") == ConfigSource::User);

    ConfigLoadOptions invalid;
    invalid.system_config = temp.path / "missing-system.toml";
    invalid.user_config = temp.path / "missing-user.toml";
    invalid.explicit_config = temp.path / "missing-explicit.toml";
    assert(!loader.load(invalid));

    ConfigLoadOptions invalid_content;
    const auto malformed = temp.path / "bad.toml";
    std::ofstream(malformed) << "schema_version = 77\n[paths]\nroot = \"/tmp/bad\"\n";
    invalid_content.system_config = temp.path / "missing-system.toml";
    invalid_content.user_config = temp.path / "missing-user.toml";
    invalid_content.explicit_config = malformed;
    assert(!loader.load(invalid_content));

    const auto lock_path = temp.path / "locks" / "state.lock";
    const auto lock_layout = LockLayout::from_paths(layout);
    assert(lock_layout.state == layout.state / "locks" / "state.lock");
    assert(lock_layout.build_input(BuildInputId("../unsafe")).parent_path() == lock_layout.build_inputs);
    {
        FileLock first(lock_path, LockKind::State, LockMode::Exclusive, "test");
        assert(first.owns_lock());
        bool concurrent_rejected = false;
        try { FileLock second(lock_path, LockKind::State, LockMode::Exclusive, "contender", false); }
        catch (const std::runtime_error&) { concurrent_rejected = true; }
        assert(concurrent_rejected);

        int result_pipe[2];
        assert(::pipe(result_pipe) == 0);
        const pid_t child = ::fork();
        assert(child >= 0);
        if (child == 0) {
            ::close(result_pipe[0]);
            char result = '0';
            try { FileLock contender(lock_path, LockKind::State, LockMode::Exclusive, "child", false); }
            catch (const std::runtime_error&) { result = '1'; }
            (void)::write(result_pipe[1], &result, 1);
            ::_exit(0);
        }
        ::close(result_pipe[1]);
        char child_result = '0';
        assert(::read(result_pipe[0], &child_result, 1) == 1);
        ::close(result_pipe[0]);
        int child_status = 0;
        assert(::waitpid(child, &child_status, 0) == child);
        assert(WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0);
        assert(child_result == '1');
        bool order_rejected = false;
        try {
            LockSet set;
            set.acquire(temp.path / "b.lock", LockKind::StoreGc, LockMode::Exclusive, "test");
            set.acquire(temp.path / "a.lock", LockKind::State, LockMode::Exclusive, "test");
        } catch (const std::logic_error&) { order_rejected = true; }
        assert(order_rejected);
    }
    {
        FileLock after_release(lock_path, LockKind::State, LockMode::Exclusive, "after-release", false);
        assert(after_release.owns_lock());
    }

    // Config loading and path derivation are read-only until a caller explicitly creates directories.
    assert(!std::filesystem::exists(temp.path / "cli-root"));

    const auto isolated_home = temp.path / "home";
    std::filesystem::create_directories(isolated_home);
    ::setenv("HOME", isolated_home.c_str(), 1);
    ::setenv("XDG_CONFIG_HOME", (isolated_home / "xdg").c_str(), 1);
    char program[] = "athena";
    char help[] = "--help";
    char version_arg[] = "--version";
    char* help_argv[] = {program, help};
    char* version_argv[] = {program, version_arg};
    assert(athena::cli::run(2, help_argv) == 0);
    assert(athena::cli::run(2, version_argv) == 0);
    assert(!std::filesystem::exists(isolated_home / "xdg"));

    char config_cmd[] = "config";
    char check_cmd[] = "check";
    char root_opt[] = "--root";
    const auto check_root = temp.path / "check-root";
    std::string check_root_text = check_root.string();
    std::vector<char> check_root_arg(check_root_text.begin(), check_root_text.end());
    check_root_arg.push_back('\0');
    char* check_argv[] = {program, config_cmd, check_cmd, root_opt, check_root_arg.data()};
    assert(athena::cli::run(5, check_argv) == 0);
    assert(!std::filesystem::exists(check_root));
    std::cout << "core_foundation_test: passed\n";
}
