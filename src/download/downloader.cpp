// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "downloader.hpp"

#include <cstdlib>
#include <stdexcept>

namespace athena::download {

/*
 * Download a file from a URL using curl.
 *
 * curl is currently used as an external system utility rather than
 * as a C++ library. This keeps the initial implementation simple
 * and allows Athena to reuse the networking functionality already
 * available on the host system.
 */
std::filesystem::path download_file(
    const std::string& url,
    const std::filesystem::path& destination
)
{
    /*
     * -L              Follow HTTP redirects.
     * --fail          Return an error for HTTP failures.
     * --silent        Suppress normal progress output.
     * --show-error    Still display errors when --silent is enabled.
     * -o              Write the downloaded data to the destination.
     */
    const std::string command =
        "curl -L --fail --silent --show-error "
        "-o \"" + destination.string() + "\" "
        "\"" + url + "\"";

    /*
     * Execute curl and inspect its exit status.
     *
     * A non-zero status means that the download failed.
     */
    const int result = std::system(command.c_str());

    if (result != 0) {
        throw std::runtime_error(
            "Download fallito: " + url
        );
    }

    /*
     * Return the path of the downloaded file so that the next
     * stage of the installation pipeline can process it.
     */
    return destination;
}

}
