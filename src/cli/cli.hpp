// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace athena::cli {

/*
 * Print the general help message.
 *
 * The help output describes the commands and options currently
 * exposed by the Athena command-line interface.
 */
void print_help();

/*
 * Execute the Athena command-line interface.
 *
 * This function is the main entry point between main() and the
 * rest of the application.
 *
 * It is responsible for:
 *
 *     1. initializing Athena's filesystem hierarchy;
 *     2. parsing the command line;
 *     3. handling global options such as --help and --version;
 *     4. dispatching commands to the appropriate subsystem.
 *
 * The return value follows the conventional command-line model:
 *
 *     0     successful execution
 *     nonzero error
 */
int run(
    int argc,
    char* argv[]
);

}
