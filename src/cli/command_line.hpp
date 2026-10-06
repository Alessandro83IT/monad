// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <vector>

namespace athena::cli {

/*
 * Represents the command-line input provided to Athena.
 *
 * The parser separates the first argument, which identifies the
 * command, from the remaining arguments belonging to that command.
 *
 * For example:
 *
 *     athena install hello
 *
 * becomes:
 *
 *     command   = "install"
 *     arguments = ["hello"]
 *
 * Keeping command-line parsing separate from command execution
 * allows the CLI implementation to evolve without coupling
 * argument parsing to package-management operations.
 */
struct CommandLine {

    // The command requested by the user.
    std::string command;

    // Arguments passed to that command.
    std::vector<std::string> arguments;
};

/*
 * Parse the arguments received by main().
 *
 * The function does not execute the requested command. It only
 * converts the raw argc/argv input into the CommandLine structure
 * used by the rest of the CLI layer.
 */
CommandLine parse(
    int argc,
    char* argv[]
);

}
