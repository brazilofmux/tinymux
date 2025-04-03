/*! \file muxcli.cpp
 * \brief MUX command-line option parser.
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"
#include "muxcli.h"

#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>

// Enum for argument types
enum class ArgType {
    NonOption = 0,     // A non-option argument
    ShortOption = 1,   // A short-option argument  (e.g., -c)
    LongOption = 2,    // A long-option argument   (e.g., --config)
    EndOfOptions = 3   // An 'end of options' indicator (--)
};

// Storage for argument values derived from argv strings (e.g., --opt=value, -ovalue)
// Made static within the cpp file scope. Cleared on each CLI_Process call.
static std::vector<std::string> persistentStrings;

// Get the type of an argument
static ArgType getArgType(const std::string& arg)
{
    // Check if argument starts with "-" or "--"
    if (arg.empty() || arg[0] != '-') {
        return ArgType::NonOption;
    }

    // Single "-" is a special case, treated as non-option (often stdin/stdout)
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

    // Otherwise, it's a short option (or group)
    return ArgType::ShortOption;
}

// Find an option entry by its flag name (long version)
static std::vector<CLI_OptionEntry>::iterator findLongOption(
    const std::string& name,
    std::vector<CLI_OptionEntry>& optionTable)
{
    return std::find_if(optionTable.begin(), optionTable.end(),
        [&name](const CLI_OptionEntry& entry) {
            // Ensure m_Flag is not null and compare
            return entry.m_Flag && entry.m_Flag[0] == '-' && entry.m_Flag[1] == '-' &&
                name == (entry.m_Flag + 2); // Skip leading -- in definition if present
            // Or assume definition doesn't include --
            // return entry.m_Flag && name == entry.m_Flag; // Adjust based on how flags are defined
        });
    // Let's assume flags in the table *don't* include "--" based on original code
    return std::find_if(optionTable.begin(), optionTable.end(),
        [&name](const CLI_OptionEntry& entry) {
            return entry.m_Flag && std::strlen(entry.m_Flag) > 1 && name == entry.m_Flag;
        });
}

// Find an option entry by its flag character (short version)
static std::vector<CLI_OptionEntry>::iterator findShortOption(
    char flagChar,
    std::vector<CLI_OptionEntry>& optionTable)
{
    return std::find_if(optionTable.begin(), optionTable.end(),
        [flagChar](const CLI_OptionEntry& entry) {
            // Ensure m_Flag is not null, has length 1, and matches
            return entry.m_Flag && entry.m_Flag[0] == flagChar && entry.m_Flag[1] == '\0';
        });
}


// Process a long option argument
static void processLongOption(
    const std::string& arg,
    std::vector<CLI_OptionEntry>& optionTable,
    CLI_CALLBACKFUNC* pFunc,
    int& currentArgIndex, // Pass current index for context
    int& minNonOption,    // Index of next potential non-option arg
    int argc,
    char* argv[]
)
{
    // Skip the "--" prefix
    std::string flagPart = arg.substr(2);
    std::string optionName = flagPart;
    const char* attachedValue = nullptr;

    // Handle option=value format
    size_t equalPos = flagPart.find('=');
    if (equalPos != std::string::npos) {
        optionName = flagPart.substr(0, equalPos);
        // Store the attached value persistently
        persistentStrings.push_back(flagPart.substr(equalPos + 1));
        attachedValue = persistentStrings.back().c_str();
    }

    // Find matching option in table
    auto it = findLongOption(optionName, optionTable);

    if (it != optionTable.end()) {
        bool requiresArg = (it->m_ArgControl == CLI_REQUIRED);
        bool optionalArg = (it->m_ArgControl == CLI_OPTIONAL);
        const char* finalValue = nullptr;
        bool valueFound = false;

        if (it->m_ArgControl == CLI_NONE) {
            if (attachedValue) {
                // Error: Option takes no argument but got one attached (--flag=value)
                std::cerr << "Warning: Option '--" << optionName << "' does not take an argument, but got '" << attachedValue << "'" << std::endl;
                // Optionally: Treat as error and exit, or call callback differently
            }
            pFunc(&(*it), nullptr);
            return; // Processed
        }

        // Option requires or accepts an argument
        if (attachedValue) {
            finalValue = attachedValue;
            valueFound = true;
        }
        else if (requiresArg || optionalArg) {
            // Look for a value in the next argument if not attached
            if (minNonOption < argc) {
                ArgType nextArgType = getArgType(argv[minNonOption]);
                if (nextArgType == ArgType::NonOption) {
                    finalValue = argv[minNonOption];
                    valueFound = true;
                    minNonOption++; // Consume this argument
                }
            }
        }

        // Call callback
        if (valueFound) {
            pFunc(&(*it), finalValue);
        }
        else {
            // No value found
            if (requiresArg) {
                // Error: Missing required argument
                std::cerr << "Warning: Option '--" << optionName << "' requires an argument, but none was found." << std::endl;
                pFunc(&(*it), nullptr); // Signal error state to callback
            }
            else { // Optional Arg
                pFunc(&(*it), nullptr); // No optional arg provided
            }
        }

    }
    else {
        // Unknown long option
        std::cerr << "Warning: Unknown option '" << arg << "'" << std::endl;
        // Optionally call pFunc(nullptr, arg.c_str()) to let caller handle?
    }
}

// Process a short option argument (potentially a cluster like -abc)
static void processShortOption(
    const std::string& arg,
    std::vector<CLI_OptionEntry>& optionTable,
    CLI_CALLBACKFUNC* pFunc,
    int& currentArgIndex, // Pass current index for context
    int& minNonOption,    // Index of next potential non-option arg
    int argc,
    char* argv[]
)
{
    // Skip the leading '-'
    std::string options = arg.substr(1);

    // Process each character as a potential option
    for (size_t i = 0; i < options.length(); ++i) {
        char currentOptChar = options[i];
        auto it = findShortOption(currentOptChar, optionTable);

        if (it != optionTable.end()) {
            bool requiresArg = (it->m_ArgControl == CLI_REQUIRED);
            bool optionalArg = (it->m_ArgControl == CLI_OPTIONAL);
            const char* finalValue = nullptr;
            bool valueFound = false;
            bool consumedRestOfString = false;

            if (it->m_ArgControl == CLI_NONE) {
                // Option takes no argument (e.g., -v in -vcf)
                pFunc(&(*it), nullptr);
                // Continue to the next character in the string (if any)
                continue;
            }

            // Option requires or accepts an argument (-f file or -ffile)
            // Check for attached argument ONLY if characters remain *in this string*
            if (i + 1 < options.length()) {
                // Argument is attached (-ffile). Rest of string is the value.
                persistentStrings.push_back(options.substr(i + 1));
                finalValue = persistentStrings.back().c_str();
                valueFound = true;
                consumedRestOfString = true; // Signal that we consumed the rest
            }
            else {
                // No attached argument in *this* string. Look at the *next* argv element.
                if (minNonOption < argc) {
                    ArgType nextArgType = getArgType(argv[minNonOption]);
                    if (nextArgType == ArgType::NonOption) {
                        finalValue = argv[minNonOption];
                        valueFound = true;
                        minNonOption++; // Consume this argument
                    }
                    // If next is --, or another option, the argument is missing/not provided here
                }
            }

            // Call the callback
            if (valueFound) {
                pFunc(&(*it), finalValue);
            }
            else {
                // No value found
                if (requiresArg) {
                    std::cerr << "Warning: Option '-" << currentOptChar << "' requires an argument, but none was found." << std::endl;
                    pFunc(&(*it), nullptr); // Signal error state
                }
                else { // Optional Arg
                    pFunc(&(*it), nullptr); // No optional arg provided
                }
            }

            // POSIX Rule: If a short option takes an argument (required or optional),
            // it consumes the rest of the current argv string if attached (-ffile),
            // OR it looks at the next argv string (-f file). In EITHER case,
            // no further option characters from the *same* argv string cluster are processed.
            // We break out of the inner loop processing characters.
            break; // Stop processing characters in this cluster (e.g., stop after 'f' in '-fbar')

        }
        else {
            // Unknown short option character
            std::cerr << "Warning: Unknown option '-" << currentOptChar << "' in argument '" << arg << "'" << std::endl;
            // POSIX getopt often errors out here. We can choose to stop processing this cluster.
            break; // Stop processing this cluster on unknown option
        }
    }
}

// Main processing function
void CLI_Process
(
    int argc,
    char* argv[],
    CLI_OptionEntry* aOptionTable,
    int nOptionTable,
    CLI_CALLBACKFUNC* pFunc
)
{
    // Clear persistent strings from any previous runs (safer design)
    persistentStrings.clear();

    // Convert C-style array to vector for easier use with algorithms
    std::vector<CLI_OptionEntry> optionTableVec(aOptionTable, aOptionTable + nOptionTable);

    int minNonOption = 1; // Index of the next argv element that *could* be a value for an option
    bool endOfOptions = false; // Set to true when '--' is encountered

    for (int i = 1; i < argc; i++)
    {
        // If we previously consumed this arg as a value for an option, skip it.
        if (i < minNonOption) {
            continue;
        }

        std::string arg = argv[i];
        ArgType argType = ArgType::NonOption; // Assume non-option by default

        if (!endOfOptions) {
            argType = getArgType(arg);
        }

        switch (argType) {
        case ArgType::NonOption:
            // Non-option argument or something after '--'
            // Pass it to the callback with a null option pointer
            pFunc(nullptr, argv[i]);
            // Advance minNonOption as this is now processed.
            // Important: Only advance if we are processing the element *at* minNonOption.
            // This handles cases where non-options appear *before* their expected position.
            if (i == minNonOption) {
                minNonOption++;
            }
            break;

        case ArgType::EndOfOptions:
            // A "--" signals the end of options. All subsequent args are non-options.
            endOfOptions = true;
            // Advance minNonOption past the '--' itself.
            if (i == minNonOption) { // Should always be true unless args are pathologically ordered
                minNonOption++;
            }
            break;

        case ArgType::LongOption:
            // Update minNonOption past the current option itself *before* processing
            // so the option processor knows where to look for subsequent values.
            minNonOption = i + 1;
            processLongOption(arg, optionTableVec, pFunc, i, minNonOption, argc, argv);
            break;

        case ArgType::ShortOption:
            // Update minNonOption past the current option itself *before* processing.
            minNonOption = i + 1;
            processShortOption(arg, optionTableVec, pFunc, i, minNonOption, argc, argv);
            break;
        }
    }
}