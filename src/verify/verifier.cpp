// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "verifier.hpp"

#include <openssl/evp.h>

#include <array>
#include <cerrno>
#include <fcntl.h>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace athena::verify {

bool verify_sha256(const std::filesystem::path& file, const std::string& expected_hash)
{
    const int fd = ::open(file.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) throw std::runtime_error("Impossibile aprire file per SHA-256: " + file.string());
    struct CloseFd { int value; ~CloseFd() { if (value >= 0) ::close(value); } } close_fd{fd};
    struct stat st{};
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode))
        throw std::runtime_error("Il file SHA-256 non è un file regolare: " + file.string());
    using Context = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
    Context context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!context || EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1)
        throw std::runtime_error("Impossibile inizializzare SHA-256");
    std::array<char, 65536> buffer{};
    for (;;) {
        const auto count = ::read(fd, buffer.data(), buffer.size());
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) throw std::runtime_error("Errore di lettura durante SHA-256: " + file.string());
        if (count == 0) break;
        if (EVP_DigestUpdate(context.get(), buffer.data(), static_cast<std::size_t>(count)) != 1)
            throw std::runtime_error("Errore durante SHA-256: " + file.string());
    }
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int length = 0;
    if (EVP_DigestFinal_ex(context.get(), digest.data(), &length) != 1)
        throw std::runtime_error("Impossibile completare SHA-256");
    std::ostringstream result;
    result << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < length; ++i) result << std::setw(2) << static_cast<unsigned int>(digest[i]);
    return result.str() == expected_hash;
}

}
