#include "command_line.hpp"

namespace athena::cli {

/*
 * Parse the raw command-line arguments received by main().
 *
 * Athena uses a simple command-oriented syntax:
 *
 *     athena <command> [arguments...]
 *
 * The first argument after the executable name is therefore treated
 * as the command, while all following arguments are stored separately
 * for the command implementation to interpret.
 */
CommandLine parse(
    int argc,
    char* argv[]
)
{
    CommandLine result;

    /*
     * When no command was supplied, return an empty CommandLine.
     *
     * The higher-level CLI layer is responsible for deciding how
     * an empty command should be presented to the user.
     */
    for (int i = 1; i < argc; ++i) {
        const std::string value = argv[i];
        if (value == "--config" || value == "--root") {
            result.arguments.push_back(value);
            if (i + 1 < argc) result.arguments.emplace_back(argv[++i]);
            continue;
        }
        if (result.command.empty()) result.command = value;
        else result.arguments.push_back(value);
    }

    return result;
}

}
