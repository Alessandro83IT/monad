// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "run.hpp"

#include <cerrno>
#include <csignal>
#include <stdexcept>
#include <system_error>
#include <sys/wait.h>
#include <unistd.h>

namespace athena::process {

int run(
    const std::vector<std::string>& arguments,
    const std::filesystem::path& working_directory
)
{
    if (arguments.empty() || arguments.front().empty()) {
        throw std::invalid_argument("process arguments must include an executable");
    }

    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1);
    for (const auto& argument : arguments) {
        argv.push_back(const_cast<char*>(argument.c_str()));
    }
    argv.push_back(nullptr);

    const pid_t child = ::fork();
    if (child < 0) {
        throw std::system_error(errno, std::generic_category(), "fork failed");
    }
    if (child == 0) {
        if (!working_directory.empty() && ::chdir(working_directory.c_str()) != 0) {
            _exit(126);
        }
        ::execvp(argv.front(), argv.data());
        _exit(errno == ENOENT ? 127 : 126);
    }

    int status = 0;
    while (::waitpid(child, &status, 0) < 0) {
        if (errno == EINTR) continue;
        throw std::system_error(errno, std::generic_category(), "waitpid failed");
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 125;
}

}
