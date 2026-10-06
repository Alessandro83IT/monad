// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "arch_repository.hpp"

#include "../dependency/parser.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace athena::repository {

namespace {

/*
 * Quote one argument for a POSIX shell command.
 *
 * The first Arch backend intentionally reuses the system tar utility,
 * just like Athena's existing archive module. Arguments are quoted
 * rather than interpolated raw so repository paths and package names
 * cannot alter the command structure.
 */
std::string shell_quote(const std::string& value)
{
    std::string result = "'";

    for (const char character : value) {

        if (character == '\'') {
            result += "'\\''";
        } else {
            result += character;
        }
    }

    result += "'";

    return result;
}

/*
 * Execute tar and capture stdout.
 *
 * Arch repository databases are tar archives. Modern tar
 * implementations understand the compression used by
 * .db.tar.zst, so no additional C++ compression dependency is needed
 * for this adapter.
 */
std::string tar_extract(
    const std::filesystem::path& archive,
    const std::string& member
)
{
    const std::string command =
        "tar -xOf " +
        shell_quote(archive.string()) +
        " " +
        shell_quote(member);

    FILE* pipe = popen(command.c_str(), "r");

    if (pipe == nullptr) {
        throw std::runtime_error(
            "Impossibile aprire il database Arch: " +
            archive.string()
        );
    }

    std::string output;
    char buffer[4096];

    while (true) {

        const std::size_t count =
            std::fread(
                buffer,
                1,
                sizeof(buffer),
                pipe
            );

        if (count > 0) {
            output.append(buffer, count);
        }

        if (count < sizeof(buffer)) {
            break;
        }
    }

    const int status = pclose(pipe);

    if (status != 0) {
        throw std::runtime_error(
            "Impossibile leggere '" +
            member +
            "' dal database Arch '" +
            archive.string() +
            "'"
        );
    }

    return output;
}

/*
 * Return all archive members whose basename is "desc".
 */
std::vector<std::string> list_desc_members(
    const std::filesystem::path& archive
)
{
    const std::string command =
        "tar -tf " +
        shell_quote(archive.string());

    FILE* pipe = popen(command.c_str(), "r");

    if (pipe == nullptr) {
        throw std::runtime_error(
            "Impossibile aprire il database Arch: " +
            archive.string()
        );
    }

    std::vector<std::string> members;
    char buffer[4096];

    while (std::fgets(
        buffer,
        sizeof(buffer),
        pipe
    ) != nullptr) {

        std::string member(buffer);

        while (!member.empty() &&
               (member.back() == '\n' ||
                member.back() == '\r')) {

            member.pop_back();
        }

        if (member.size() >= 5 &&
            member.compare(
                member.size() - 5,
                5,
                "/desc"
            ) == 0) {

            members.push_back(member);
        }
    }

    const int status = pclose(pipe);

    if (status != 0) {
        throw std::runtime_error(
            "Impossibile elencare il database Arch: " +
            archive.string()
        );
    }

    std::sort(
        members.begin(),
        members.end()
    );

    return members;
}

using Fields = std::map<
    std::string,
    std::vector<std::string>
>;

Fields parse_desc(const std::string& text)
{
    Fields fields;
    std::istringstream input(text);

    std::string line;
    std::string current_field;

    while (std::getline(input, line)) {

        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.empty()) {
            current_field.clear();
            continue;
        }

        if (line.front() == '%' &&
            line.back() == '%' &&
            line.size() >= 3) {

            current_field = line.substr(
                1,
                line.size() - 2
            );

            continue;
        }

        if (!current_field.empty()) {
            fields[current_field].push_back(line);
        }
    }

    return fields;
}

std::string first_field(
    const Fields& fields,
    const std::string& name
)
{
    const auto iterator = fields.find(name);

    if (iterator == fields.end() ||
        iterator->second.empty()) {

        return "";
    }

    return iterator->second.front();
}

std::string package_url(
    const std::string& base_url,
    const std::string& filename
)
{
    if (base_url.empty()) {
        return filename;
    }

    if (base_url.back() == '/') {
        return base_url + filename;
    }

    return base_url + "/" + filename;
}

/*
 * Arch optional dependencies have the form:
 *
 *     package: human readable reason
 *
 * Athena's dependency expression contains the package selector but
 * currently has no field for the explanatory text, so only the
 * dependency selector is imported.
 */
std::string optional_dependency_name(
    const std::string& value
)
{
    const auto separator = value.find(':');

    if (separator == std::string::npos) {
        return value;
    }

    return value.substr(0, separator);
}

/*
 * Arch provides entries may carry a version:
 *
 *     libfoo=1.2
 *
 * The current Athena PackageProvides model stores semantic names but
 * does not yet have versioned capability constraints. Preserve the
 * capability name and leave versioned provides for a later semantic
 * extension.
 */
std::string provided_capability_name(
    const std::string& value
)
{
    const auto separator =
        value.find_first_of("=<>");

    if (separator == std::string::npos) {
        return value;
    }

    return value.substr(0, separator);
}

void append_dependencies(
    athena::package::Package& package,
    const Fields& fields,
    const std::string& field_name,
    athena::dependency::DependencyKind kind,
    athena::dependency::DependencyContext context,
    bool optional_reason
)
{
    const auto iterator = fields.find(field_name);

    if (iterator == fields.end()) {
        return;
    }

    for (const auto& raw_value : iterator->second) {

        const std::string value =
            optional_reason
                ? optional_dependency_name(raw_value)
                : raw_value;

        if (value.empty()) {
            continue;
        }

        auto dependency =
            athena::dependency::parse_dependency(value);

        dependency.kind = kind;
        dependency.context = context;

        package.dependencies.push_back(
            std::move(dependency)
        );
    }
}

athena::package::Package package_from_desc(
    const Fields& fields,
    const std::string& base_url
)
{
    athena::package::Package package;

    package.name = first_field(fields, "NAME");
    package.version = first_field(fields, "VERSION");
    package.description = first_field(fields, "DESC");

    const std::string filename =
        first_field(fields, "FILENAME");

    package.source =
        package_url(base_url, filename);

    package.sha256 =
        first_field(fields, "SHA256SUM");

    // Preserve the complete repository record for the public imported-binary
    // adapter, including fields whose semantics Athena does not yet model.
    package.arch_metadata = fields;

    /*
     * Arch binary packages are already built. The build backend is
     * therefore intentionally empty at this stage.
     */
    package.build_system = "";

    append_dependencies(
        package,
        fields,
        "DEPENDS",
        athena::dependency::DependencyKind::Runtime,
        athena::dependency::DependencyContext::Target,
        false
    );

    append_dependencies(
        package,
        fields,
        "MAKEDEPENDS",
        athena::dependency::DependencyKind::Build,
        athena::dependency::DependencyContext::Build,
        false
    );

    append_dependencies(
        package,
        fields,
        "CHECKDEPENDS",
        athena::dependency::DependencyKind::Test,
        athena::dependency::DependencyContext::Build,
        false
    );

    append_dependencies(
        package,
        fields,
        "OPTDEPENDS",
        athena::dependency::DependencyKind::Optional,
        athena::dependency::DependencyContext::Target,
        true
    );

    const auto provides =
        fields.find("PROVIDES");

    if (provides != fields.end()) {

        for (const auto& value : provides->second) {

            const std::string capability =
                provided_capability_name(value);

            if (!capability.empty()) {
                package.provides.capabilities.push_back(
                    capability
                );
            }
        }
    }

    return package;
}

} // namespace

void ArchRepository::download(const std::string& url)
{
    if (url.empty()) {
        throw std::runtime_error(
            "Arch repository database URL vuoto"
        );
    }

    const auto parent = database_.parent_path();

    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    const std::filesystem::path temporary =
        database_.string() + ".download";

    /*
     * curl is used as the transport layer for now. The temporary file
     * prevents a failed or interrupted download from replacing a valid
     * cached repository database.
     */
    const std::string command =
        "curl --fail --silent --show-error "
        "--location --retry 3 "
        "--output " +
        shell_quote(temporary.string()) +
        " " +
        shell_quote(url);

    const int status =
        std::system(command.c_str());

    if (status != 0) {
        std::filesystem::remove(temporary);

        throw std::runtime_error(
            "Download del database Arch fallito: " +
            url
        );
    }

    std::error_code error;

    std::filesystem::rename(
        temporary,
        database_,
        error
    );

    if (error) {
        /*
         * Some filesystems do not replace an existing target during
         * rename. Remove the old cache only after the new download
         * completed successfully.
         */
        std::filesystem::remove(
            database_,
            error
        );

        error.clear();

        std::filesystem::rename(
            temporary,
            database_,
            error
        );
    }

    if (error) {
        std::filesystem::remove(temporary);

        throw std::runtime_error(
            "Impossibile installare il database Arch scaricato: " +
            error.message()
        );
    }
}

void ArchRepository::download_package(
    const athena::package::Package& package,
    const std::filesystem::path& destination
) const
{
    if (package.source.empty()) {
        throw std::runtime_error(
            "Il pacchetto Arch non contiene un URL sorgente"
        );
    }

    if (destination.empty()) {
        throw std::runtime_error(
            "Destinazione del pacchetto Arch vuota"
        );
    }

    const auto parent = destination.parent_path();

    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    const std::filesystem::path temporary =
        destination.string() + ".download";

    /*
     * Keep the transport layer deliberately simple for this first
     * integration. Later this can be replaced by a native HTTP client
     * and mirror fallback mechanism.
     */
    const std::string command =
        "curl --fail --silent --show-error "
        "--location --retry 3 "
        "--output " +
        shell_quote(temporary.string()) +
        " " +
        shell_quote(package.source);

    const int status =
        std::system(command.c_str());

    if (status != 0) {
        std::filesystem::remove(temporary);

        throw std::runtime_error(
            "Download del pacchetto Arch fallito: " +
            package.source
        );
    }

    std::error_code error;

    std::filesystem::rename(
        temporary,
        destination,
        error
    );

    if (error) {
        /*
         * Do not remove the existing destination until the new package
         * has been downloaded successfully.
         */
        std::filesystem::remove(
            destination,
            error
        );

        error.clear();

        std::filesystem::rename(
            temporary,
            destination,
            error
        );
    }

    if (error) {
        std::filesystem::remove(temporary);

        throw std::runtime_error(
            "Impossibile installare il pacchetto Arch scaricato: " +
            error.message()
        );
    }
}

void ArchRepository::verify_package_sha256(
    const athena::package::Package& package,
    const std::filesystem::path& package_path
) const
{
    if (package.sha256.empty()) {
        throw std::runtime_error(
            "Il pacchetto Arch non contiene SHA-256 nel repository"
        );
    }

    if (!std::filesystem::exists(package_path)) {
        throw std::runtime_error(
            "Pacchetto Arch non trovato: " +
            package_path.string()
        );
    }

    /*
     * The expected digest comes from %SHA256SUM% in core.db, while
     * sha256sum calculates the digest over the downloaded package.
     */
    const std::string command =
        "printf '%s  %s\\n' " +
        shell_quote(package.sha256) +
        " " +
        shell_quote(package_path.string()) +
        " | sha256sum --check --status";

    const int status =
        std::system(command.c_str());

    if (status != 0) {
        throw std::runtime_error(
            "Verifica SHA-256 del pacchetto Arch fallita: " +
            package_path.string()
        );
    }
}

void ArchRepository::verify_package_signature(
    const athena::package::Package& package,
    const std::filesystem::path& package_path,
    const std::filesystem::path& keyring
) const
{
    if (package.source.empty()) {
        throw std::runtime_error(
            "Il pacchetto Arch non contiene un URL sorgente"
        );
    }

    if (!std::filesystem::exists(package_path)) {
        throw std::runtime_error(
            "Pacchetto Arch non trovato: " +
            package_path.string()
        );
    }

    if (!std::filesystem::exists(keyring)) {
        throw std::runtime_error(
            "Keyring OpenPGP non trovato: " +
            keyring.string()
        );
    }

    const std::filesystem::path signature =
        package_path.string() + ".sig";

    const std::string command =
        "curl --fail --silent --show-error "
        "--location --retry 3 "
        "--output " +
        shell_quote(signature.string()) +
        " " +
        shell_quote(package.source + ".sig");

    const int download_status =
        std::system(command.c_str());

    if (download_status != 0) {
        std::filesystem::remove(signature);

        throw std::runtime_error(
            "Download della firma del pacchetto Arch fallito: " +
            package.source + ".sig"
        );
    }

    /*
     * gpgv performs detached-signature verification without modifying
     * the user's trust database.
     */
    const std::string verify_command =
        "gpgv --keyring " +
        shell_quote(keyring.string()) +
        " " +
        shell_quote(signature.string()) +
        " " +
        shell_quote(package_path.string());

    const int status =
        std::system(verify_command.c_str());

    std::filesystem::remove(signature);

    if (status != 0) {
        throw std::runtime_error(
            "Verifica della firma OpenPGP del pacchetto Arch fallita"
        );
    }
}

ArchRepository::ArchRepository(
    std::filesystem::path database,
    std::string base_url
)
    : database_(std::move(database)),
      base_url_(std::move(base_url))
{
}

void ArchRepository::load()
{
    if (!std::filesystem::exists(database_)) {
        throw std::runtime_error(
            "Database Arch inesistente: " +
            database_.string()
        );
    }

    if (!std::filesystem::is_regular_file(database_)) {
        throw std::runtime_error(
            "Il database Arch non è un file: " +
            database_.string()
        );
    }

    index_ = RepositoryIndex{};

    for (const auto& member :
         list_desc_members(database_)) {

        const auto fields =
            parse_desc(
                tar_extract(
                    database_,
                    member
                )
            );

        const auto package =
            package_from_desc(
                fields,
                base_url_
            );

        if (package.name.empty()) {
            throw ArchPackageMetadataError(
                "Voce Arch priva di %NAME% nel database '" +
                database_.string() +
                "'"
            );
        }

        if (package.version.empty()) {
            throw ArchPackageMetadataError(
                "Pacchetto Arch '" +
                package.name +
                "' privo di %VERSION%"
            );
        }

        index_.add(package);
    }
}

const RepositoryIndex& ArchRepository::index() const
{
    return index_;
}

std::vector<athena::package::Package>
ArchRepository::find(
    const std::string& name
) const
{
    return index_.find(name);
}

bool ArchRepository::contains(
    const std::string& name
) const
{
    return index_.contains(name);
}


void ArchRepository::extract_package(
    const std::filesystem::path& package_path,
    const std::filesystem::path& destination
) const
{
    if (!std::filesystem::exists(package_path)) {
        throw std::runtime_error(
            "Arch package does not exist: " +
            package_path.string()
        );
    }

    std::filesystem::create_directories(destination);

    /*
     * Validate every archive member before extraction.
     *
     * Arch packages are tar archives compressed with zstd.  We do not
     * allow absolute paths or parent traversal because extraction happens
     * into an Athena-controlled staging directory.
     */
    const auto archive_list =
        std::filesystem::path(
            destination.string() + ".members"
        );

    const auto list_command =
        "tar --zstd -tf " +
        shell_quote(package_path.string()) +
        " > " +
        shell_quote(archive_list.string());

    if (std::system(list_command.c_str()) != 0) {
        std::filesystem::remove(archive_list);
        throw std::runtime_error(
            "failed to inspect Arch package archive: " +
            package_path.string()
        );
    }

    {
        std::ifstream members(archive_list);
        if (!members) {
            std::filesystem::remove(archive_list);
            throw std::runtime_error(
                "failed to read Arch package member list"
            );
        }

        std::string member;

        while (std::getline(members, member)) {
            if (member.empty()) {
                continue;
            }

            std::filesystem::path member_path(member);

            if (member_path.is_absolute()) {
                std::filesystem::remove(archive_list);
                throw std::runtime_error(
                    "unsafe absolute path in Arch package: " +
                    member
                );
            }

            for (const auto& component : member_path) {
                if (component == "..") {
                    std::filesystem::remove(archive_list);
                    throw std::runtime_error(
                        "unsafe parent traversal in Arch package: " +
                        member
                    );
                }
            }
        }
    }

    std::filesystem::remove(archive_list);

    const auto extract_command =
        "tar --zstd -xf " +
        shell_quote(package_path.string()) +
        " -C " +
        shell_quote(destination.string());

    if (std::system(extract_command.c_str()) != 0) {
        throw std::runtime_error(
            "failed to extract Arch package: " +
            package_path.string()
        );
    }
}

athena::package::Package ArchRepository::load_package_metadata(
    const std::filesystem::path& extracted_package
) const
{
    const auto pkginfo =
        extracted_package / ".PKGINFO";

    if (!std::filesystem::exists(pkginfo)) {
        throw std::runtime_error(
            "Arch package does not contain .PKGINFO: " +
            extracted_package.string()
        );
    }

    std::ifstream input(pkginfo);

    if (!input) {
        throw std::runtime_error(
            "failed to open Arch .PKGINFO: " +
            pkginfo.string()
        );
    }

    athena::package::Package package;

    std::string line;

    /*
     * Arch .PKGINFO records are key/value pairs in the form:
     *
     *     pkgname = glibc
     *     pkgver = 2.44...
     *     depend = filesystem
     *
     * Keys and values are separated by '=' with optional whitespace.
     * Some keys occur multiple times, such as depend, optdepend and
     * makedepend, so every occurrence must be processed independently.
     */
    while (std::getline(input, line)) {
        if (line.empty() || line.front() == '#') {
            continue;
        }

        const auto separator = line.find('=');

        if (separator == std::string::npos) {
            continue;
        }

        std::string key =
            line.substr(0, separator);

        std::string value =
            line.substr(separator + 1);

        const auto trim = [](std::string& value) {
            const auto first =
                value.find_first_not_of(" \t\r");

            if (first == std::string::npos) {
                value.clear();
                return;
            }

            const auto last =
                value.find_last_not_of(" \t\r");

            value =
                value.substr(
                    first,
                    last - first + 1
                );
        };

        trim(key);
        trim(value);

        if (key == "pkgname") {
            package.name = value;
        }
        else if (key == "pkgver") {
            package.version = value;
        }
        else if (key == "pkgdesc") {
            package.description = value;
        }
        else if (key == "depend") {
            auto dependency =
                athena::dependency::parse_dependency(value);

            dependency.kind =
                athena::dependency::DependencyKind::Runtime;

            dependency.context =
                athena::dependency::DependencyContext::Target;

            package.dependencies.push_back(
                std::move(dependency)
            );
        }
        else if (key == "optdepend") {
            /*
             * Arch stores optional dependencies as:
             *
             *     package: reason
             *
             * Athena keeps the dependency expression separate from the
             * human-readable reason, so only the package expression is
             * imported here.
             */
            const auto dependency_name =
                optional_dependency_name(value);

            if (!dependency_name.empty()) {
                auto dependency =
                    athena::dependency::parse_dependency(
                        dependency_name
                    );

                dependency.kind =
                    athena::dependency::DependencyKind::Optional;

                dependency.context =
                    athena::dependency::DependencyContext::Target;

                package.dependencies.push_back(
                    std::move(dependency)
                );
            }
        }
        else if (key == "makedepend") {
            auto dependency =
                athena::dependency::parse_dependency(value);

            dependency.kind =
                athena::dependency::DependencyKind::Build;

            dependency.context =
                athena::dependency::DependencyContext::Build;

            package.dependencies.push_back(
                std::move(dependency)
            );
        }
        else if (key == "checkdepend") {
            auto dependency =
                athena::dependency::parse_dependency(value);

            dependency.kind =
                athena::dependency::DependencyKind::Test;

            dependency.context =
                athena::dependency::DependencyContext::Build;

            package.dependencies.push_back(
                std::move(dependency)
            );
        }
        else if (key == "provides") {
            const auto capability =
                provided_capability_name(value);

            if (!capability.empty()) {
                package.provides.capabilities.push_back(
                    capability
                );
            }
        }
    }

    if (package.name.empty()) {
        throw std::runtime_error(
            "Arch .PKGINFO does not contain pkgname"
        );
    }

    if (package.version.empty()) {
        throw std::runtime_error(
            "Arch .PKGINFO does not contain pkgver"
        );
    }

    return package;
}


} // namespace athena::repository
