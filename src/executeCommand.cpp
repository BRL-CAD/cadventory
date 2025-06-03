#include "executeCommand.h"
#include <iostream>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <fstream>
#include <array>

#ifdef _WIN32
#include <windows.h>
#else
    #include <fcntl.h>
    #include <sys/types.h>
    #include <sys/wait.h>
    #include <unistd.h>
#endif


std::string executeCommandNoWindowWithRedirection(const std::string& command,
    const std::string& inputFile,
    const std::string& outputFile) {
#ifdef _WIN32
    /* Windows */
    HANDLE hInput = CreateFileA(inputFile.c_str(), GENERIC_READ, FILE_SHARE_READ,
        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hInput == INVALID_HANDLE_VALUE) {
        std::cerr << "Failed to open input file: " << inputFile << std::endl;
        return "";
    }

    HANDLE hOutput = CreateFileA(outputFile.c_str(), GENERIC_WRITE, 0,
        NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hOutput == INVALID_HANDLE_VALUE) {
        std::cerr << "Failed to open output file: " << outputFile << std::endl;
        CloseHandle(hInput);
        return "";
    }

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = hInput;
    si.hStdOutput = hOutput;
    si.hStdError = hOutput;

    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessA(NULL,
        const_cast<char*>(command.c_str()),
        NULL, NULL, TRUE,
        CREATE_NO_WINDOW,
        NULL, NULL,
        &si, &pi)) {
        std::cerr << "CreateProcess failed\n";
        CloseHandle(hInput);
        CloseHandle(hOutput);
        return "";
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hInput);
    CloseHandle(hOutput);
#else
    /* linux */
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return "";
    }

    if (pid == 0) {
        // redirect stdin
        int fdIn = open(inputFile.c_str(), O_RDONLY);
        if (fdIn < 0) {
            perror(("open " + inputFile).c_str());
            _exit(1);
        }
        dup2(fdIn, STDIN_FILENO);
        close(fdIn);

        // redirect stdout / stderr
        int fdOut = open(outputFile.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (fdOut < 0) {
            perror(("open " + outputFile).c_str());
            _exit(1);
        }
        dup2(fdOut, STDOUT_FILENO);
        dup2(fdOut, STDERR_FILENO);
        close(fdOut);

        // Execute through the shell so users can pass complex commands.
        execl("/bin/sh", "sh", "-c", command.c_str(), (char*)nullptr);
        _exit(127); // only reached if exec fails
    }

    // ---- parent ----
    int status = 0;
    if (waitpid(pid, &status, 0) == -1) {
        perror("waitpid");
        return {};
    }
#endif

    // read back output file into a string
    std::ifstream outFile(outputFile);
    std::string result((std::istreambuf_iterator<char>(outFile)),
        std::istreambuf_iterator<char>());
    outFile.close();

    return result;
}

std::string executeCommandNoWindow(const std::string& command) {
#ifdef _WIN32
    /* Windows */
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    SECURITY_ATTRIBUTES sa;
    HANDLE hStdOutRead, hStdOutWrite;
    std::string result;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW; 
    si.wShowWindow = SW_HIDE;          

    ZeroMemory(&pi, sizeof(pi));

    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;
    if (!CreatePipe(&hStdOutRead, &hStdOutWrite, &sa, 0)) {
        std::cerr << "Stdout pipe creation failed\n";
        return "";
    }

    si.hStdOutput = hStdOutWrite;
    si.hStdError = hStdOutWrite;
    si.dwFlags |= STARTF_USESTDHANDLES;

    if (!CreateProcessA(NULL,
        const_cast<char*>(command.c_str()),
        NULL,
        NULL,
        TRUE,
        CREATE_NO_WINDOW,
        NULL,
        NULL,
        &si,
        &pi)) {
        std::cerr << "CreateProcess failed\n";
        CloseHandle(hStdOutWrite);
        CloseHandle(hStdOutRead);
        return "";
    }

    CloseHandle(hStdOutWrite);

    char buffer[128];
    DWORD bytesRead;
    while (ReadFile(hStdOutRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        result += buffer;
    }
    CloseHandle(hStdOutRead);

    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return result;
#else
    /* linux */
    std::string result;
    std::array<char, 256> buffer{};

    // redirect stderr / stdout
    std::string fullCmd = command + " 2>&1";
    FILE* pipe = popen(fullCmd.c_str(), "r");
    if (!pipe) {
        perror("popen");
        return "";
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
        result.append(buffer.data());
    }

    (void)pclose(pipe);

    return result;
#endif
}
