#ifndef EXECUTE_COMMAND_H
#define EXECUTE_COMMAND_H

#include <string>

/**
 * @brief Executes external commands in a platform-independent manner
 *
 * Provides a unified interface for executing Ollama commands across
 * Windows, macOS, and Linux platforms, handling platform-specific
 * differences transparently.
 *
 * @param command The command to execute
 * @param args Command-line arguments
 * @return The command output as string
 */
std::string executeCommandNoWindow(const std::string& command);

/**
 * @brief Executes an external command with input and output redirection
 *
 * This function executes a command in a hidden window, redirecting
 * input and output to specified files.
 *
 * @param command The command to execute
 * @param inputFile The file to read input from
 * @param outputFile The file to write output to
 * @return The command output as string
 */
std::string executeCommandNoWindowWithRedirection(const std::string& command,
    const std::string& inputFile,
    const std::string& outputFile);

#endif // EXECUTE_COMMAND_H