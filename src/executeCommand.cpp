#include "executeCommand.h"
#include <iostream>
#include <cstdio>  
#include <stdexcept>
#include <fstream>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif


std::string executeCommandNoWindowWithRedirection(const std::string& command,
    const std::string& inputFile,
    const std::string& outputFile) {

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

    std::ifstream outFile(outputFile);
    std::string result((std::istreambuf_iterator<char>(outFile)),
        std::istreambuf_iterator<char>());
    outFile.close();

    return result;
}

std::string executeCommandNoWindow(const std::string& command) {
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
}


