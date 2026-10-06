// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "verifier.hpp"

#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

/*
 * RAII wrapper for the FILE* returned by popen().
 *
 * pclose() must be called when the pipe is no longer needed.
 * Using a custom deleter allows std::unique_ptr to perform this
 * cleanup automatically.
 */
struct PipeCloser {
    void operator()(FILE* pipe) const
    {
        if (pipe != nullptr) {
            pclose(pipe);
        }
    }
};

}

namespace athena::verify {

/*
 * Calculate and verify the SHA-256 checksum of a file.
 *
 * The expected checksum comes from package.toml.
 * The calculated checksum is obtained from the system sha256sum
 * utility and compared with the expected value.
 */
bool verify_sha256(
    const std::filesystem::path& file,
    const std::string& expected_hash
)
{
    /*
     * Ask the operating system to calculate the SHA-256 checksum.
     *
     * sha256sum normally produces output in this form:
     *
     *   <hash>  <filename>
     *
     * We only need the first field.
     */
    const std::string command =
        "sha256sum \"" + file.string() + "\"";

    /*
     * Open a pipe to sha256sum.
     *
     * unique_ptr takes ownership of the FILE* and guarantees that
     * pclose() is called when the function exits.
     */
    std::unique_ptr<FILE, PipeCloser> pipe(
        popen(command.c_str(), "r")
    );

    if (!pipe) {
        throw std::runtime_error(
            "Impossibile calcolare SHA-256 per: " + file.string()
        );
    }

    /*
     * Read the output produced by sha256sum.
     */
    char buffer[256];

    if (!fgets(buffer, sizeof(buffer), pipe.get())) {
        throw std::runtime_error(
            "Impossibile leggere SHA-256 per: " + file.string()
        );
    }

    const std::string output(buffer);

    /*
     * Extract the checksum from the beginning of the sha256sum
     * output. The checksum ends at the first space character.
     */
    const std::string calculated_hash =
        output.substr(0, output.find(' '));

    /*
     * The file is valid only when the calculated checksum exactly
     * matches the checksum declared by the package.
     */
    return calculated_hash == expected_hash;
}

}
