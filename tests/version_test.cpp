#include "../src/version/version.hpp"

#include <cassert>
#include <iostream>
#include <stdexcept>

int main()
{
    assert(athena::version::less("1.9", "1.10"));
    assert(athena::version::greater("1.10", "1.9"));

    assert(athena::version::less("1.2", "1.2.1"));
    assert(athena::version::greater("1.2.1", "1.2"));

    assert(athena::version::greater("2.0", "1.99"));
    assert(athena::version::less("1.99", "2.0"));

    assert(!athena::version::less("1.2", "1.2"));
    assert(!athena::version::greater("1.2", "1.2"));

    assert(!athena::version::less("1.2", "1.2.0"));
    assert(!athena::version::greater("1.2", "1.2.0"));

    /*
     * Alphabetic components are valid because Athena versions must be
     * able to represent Arch package versions such as "1.2a" and
     * "2.44+r50+g1848099f063e-1".
     */
    assert(athena::version::less("1.2a", "1.3"));

    /*
     * Repeated separators without a version component are malformed.
     */
    bool invalid_version_rejected = false;

    try {
        athena::version::less("1..2", "1.3");
    }
    catch (const std::runtime_error&) {
        invalid_version_rejected = true;
    }

    assert(invalid_version_rejected);

    std::cout << "Version comparison tests passed.\n";

    return 0;
}
