// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "cli/cli.hpp"

/*
 * Athena Package Manager entry point.
 *
 * main() intentionally contains no package-management logic.
 * Its only responsibility is to pass the command-line arguments
 * to the CLI layer.
 *
 * Keeping the entry point minimal separates the operating-system
 * process interface from Athena's application logic.
 */
int main(int argc, char* argv[])
{
    return athena::cli::run(argc, argv);
}
