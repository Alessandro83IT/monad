// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace athena::process {

// Run an executable directly, without interpreting arguments through a shell.
// Returns the child's exit status; a signal termination maps to 128 + signal.
int run(
    const std::vector<std::string>& arguments,
    const std::filesystem::path& working_directory = {}
);

}
