// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../src/version/version.hpp"

#include <cassert>
#include <iostream>

int main()
{
    using athena::version::greater;
    using athena::version::less;

    /*
     * Basic numeric versions.
     */
    assert(!less("3.5.2", "3.5.2"));
    assert(less("3.5.2", "3.5.3"));
    assert(greater("3.5.3", "3.5.2"));

    /*
     * Arch pkgrel.
     */
    assert(less("3.5.2-1", "3.5.2-2"));
    assert(less("3.5.2-2", "3.5.3-1"));
    assert(!less("3.5.2-2", "3.5.2-1"));

    /*
     * Arch kernel-style versions.
     */
    assert(less("6.17.arch1-1", "6.17.arch1-2"));
    assert(less("6.17.arch1-2", "6.18.arch1-1"));
    assert(greater("6.18.arch1-1", "6.17.arch1-2"));

    /*
     * Epoch takes precedence over the complete pkgver/pkgrel.
     */
    assert(less("1:3.5.2-1", "2:1.0-1"));
    assert(greater("2:1.0-1", "1:99.99-99"));

    /*
     * Same epoch: compare pkgver, then pkgrel.
     */
    assert(less("1:3.5.2-1", "1:3.5.2-2"));
    assert(less("1:3.5.2-2", "1:3.5.3-1"));

    /*
     * Missing epoch is equivalent to epoch zero.
     */
    assert(!less("3.5.2-1", "0:3.5.2-1"));
    assert(!greater("3.5.2-1", "0:3.5.2-1"));

    /*
     * A missing pkgrel behaves as pkgrel zero.
     */
    assert(!less("3.5.2", "3.5.2-0"));
    assert(!greater("3.5.2", "3.5.2-0"));

    /*
     * Numeric components must be compared numerically rather than
     * lexicographically.
     */
    assert(less("3.5.9", "3.5.10"));
    assert(greater("3.5.10", "3.5.9"));

    /*
     * Leading zeroes must not change the numeric value.
     */
    assert(!less("3.05.002", "3.5.2"));
    assert(!greater("3.05.002", "3.5.2"));

    /*
     * Tilde represents a version preceding the corresponding release.
     */
    assert(less("3.5~rc1", "3.5"));
    assert(less("3.5~rc1", "3.5~rc2"));

    std::cout << "Arch version tests passed.\n";
    return 0;
}
