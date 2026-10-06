#include <archive.h>
#include <openssl/evp.h>

extern "C" int packaging_fixture_core() {
    // Keep both real external dependencies in DT_NEEDED, including when they
    // are discovered through non-system symlinks in the regression test.
    return EVP_sha256() != nullptr && archive_version_number() > 0;
}
