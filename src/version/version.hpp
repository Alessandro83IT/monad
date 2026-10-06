// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

namespace athena::version {

bool less(
    const std::string& left,
    const std::string& right
);

bool greater(
    const std::string& left,
    const std::string& right
);

}
