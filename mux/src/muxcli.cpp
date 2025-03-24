/*! \file muxcli.cpp
 * \brief MUX command-line option parser.
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <cstring>

// Enum for argument types
enum class ArgType {
    NonOption = 0,     // A non-option argument
    ShortOption = 1,   // A short-option argument  (e.g., -c)
    LongOption = 2,    // A long-option argument   (e.g., --config)
    EndOfOptions = 3   // An 'end of options' indicator (--)
};

// Get the type of an argument
static ArgType getArgType(const std::string& arg)
{
    // Check if argument starts with "-" or "--"
    if (arg.empty() || arg[0] != '-') {
        return ArgType::NonOption;
    }

    // Single "-" is a special case, treated as non-option
    if (arg.length() == 1) {
        return ArgType::NonOption;
    }

    // "--" alone means end of options
    if (arg == "--") {
        return ArgType::EndOfOptions;
    }

    // Check for long option (starts with "--")
    if (arg.length() > 2 && arg[1] == '-') {
        return ArgType::LongOption;
    }

    return ArgType::ShortOption;
}

// Process a long option argument
static void processLongOption(
    const std::string& arg,
    std::vector<CLI_OptionEntry>& optionTable,
    CLI_CALLBACKFUNC* pFunc,
    int& minNonOption,
    int argc,
    char* argv[]
)
{
    // Skip the "--" prefix
    std::string flagName = arg.substr(2);

    // Handle option=value format
    size_t equalPos = flagName.find('=');
    std::string optionName = (equalPos != std::string::npos)
                          ? flagName.substr(0, equalPos)
                          : flagName;

    // Find matching option in table
    auto it = std::find_if(optionTable.begin(), optionTable.end(),
        [&optionName](const CLI_OptionEntry& entry) {
            return std::string(entry.m_Flag) == optionName;
        });

    if (it != optionTable.end()) {
        switch (it->m_ArgControl) {
        case CLI_NONE:
            // Option takes no argument
            pFunc(&(*it), nullptr);
            break;

        case CLI_OPTIONAL:
        case CLI_REQUIRED:
            // Option takes an argument
            if (equalPos != std::string::npos) {
                // Value provided as --option=value
                pFunc(&(*it), flagName.substr(equalPos + 1).c_str());
            } else {
                // Look for a value in the next argument
                bool found = false;
                for (; minNonOption < argc; minNonOption++) {
                    ArgType nextArgType = getArgType(argv[minNonOption]);
                    if (nextArgType == ArgType::NonOption) {
                        pFunc(&(*it), argv[minNonOption]);
                        minNonOption++;
                        found = true;
                        break;
                    } else if (nextArgType == ArgType::EndOfOptions) {
                        // End of options marker
                        break;
                    }
                }

                // Handle optional arguments that weren't provided
                if (!found && it->m_ArgControl == CLI_OPTIONAL) {
                    pFunc(&(*it), nullptr);
                }
            }
            break;
        }
    }
}

// Process a short option argument
static void processShortOption(
    const std::string& arg,
    std::vector<CLI_OptionEntry>& optionTable,
    CLI_CALLBACKFUNC* pFunc,
    int& minNonOption,
    int argc,
    char* argv[]
)
{
    // Skip the leading '-'
    std::string options = arg.substr(1);

    // Process each character as a separate option
    for (size_t i = 0; i < options.length(); ++i) {
        char currentOpt = options[i];

        // Find the option in the table
        auto it = std::find_if(optionTable.begin(), optionTable.end(),
            [currentOpt](const CLI_OptionEntry& entry) {
                return entry.m_Flag[0] == currentOpt && entry.m_Flag[1] == '\0';
            });

        if (it != optionTable.end()) {
            switch (it->m_ArgControl) {
            case CLI_NONE:
                // Option takes no argument
                pFunc(&(*it), nullptr);
                break;

            case CLI_OPTIONAL:
            case CLI_REQUIRED:
                // Option takes an argument
                if (i + 1 < options.length()) {
                    // Remaining characters form the argument (e.g., -ovalue)
                    const char* value = options.c_str() + i + 1;

                    // If next character is '=', skip it
                    if (value[0] == '=') {
                        value++;
                    }

                    pFunc(&(*it), value);
                    i = options.length(); // Skip remaining characters
                } else {
                    // Look for a value in the next argument
                    bool found = false;
                    for (; minNonOption < argc; minNonOption++) {
                        ArgType nextArgType = getArgType(argv[minNonOption]);
                        if (nextArgType == ArgType::NonOption) {
                            pFunc(&(*it), argv[minNonOption]);
                            minNonOption++;
                            found = true;
                            break;
                        } else if (nextArgType == ArgType::EndOfOptions) {
                            // End of options marker
                            break;
                        }
                    }

                    // Handle optional arguments that weren't provided
                    if (!found && it->m_ArgControl == CLI_OPTIONAL) {
                        pFunc(&(*it), nullptr);
                    }
                }
                break;
            }
        }
    }
}

// Examples:
//
// 1. prog -c123   --> (c,123)
// 2. prog -c 123  --> (c,123)
// 3. prog -c=123  --> (c,123)
// 4. prog -cs 123 --> (c,123) (s)
// 5. prog -sc=123 --> (s) (c,123)
// 6. prog -cs123 --> (c,s123)
//
void CLI_Process
(
    int argc,
    char *argv[],
    CLI_OptionEntry *aOptionTable,
    int nOptionTable,
    CLI_CALLBACKFUNC *pFunc
)
{
    // Convert option table to vector for easier manipulation
    std::vector<CLI_OptionEntry> optionTableVec(aOptionTable, aOptionTable + nOptionTable);

    int minNonOption = 0;
    bool endOfOptions = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        ArgType argType = ArgType::NonOption;

        if (!endOfOptions) {
            argType = getArgType(arg);
        }

        if (argType == ArgType::NonOption) {
            // Non-option argument
            if (minNonOption <= i) {
                // Not associated with an option yet, pass it by itself
                pFunc(nullptr, argv[i]);
            }
            continue;
        }

        if (minNonOption < i + 1) {
            minNonOption = i + 1;
        }

        if (argType == ArgType::EndOfOptions) {
            // A "--" causes remaining arguments to be treated as non-options
            endOfOptions = true;
            continue;
        }

        if (argType == ArgType::LongOption) {
            processLongOption(arg, optionTableVec, pFunc, minNonOption, argc, argv);
        } else if (argType == ArgType::ShortOption) {
            processShortOption(arg, optionTableVec, pFunc, minNonOption, argc, argv);
        }
    }
}
