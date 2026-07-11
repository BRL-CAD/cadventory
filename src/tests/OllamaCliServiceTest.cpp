#include <catch2/catch_test_macros.hpp>

#include <QTemporaryDir>

#include <filesystem>
#include <fstream>

#include "OllamaCLIService.h"

namespace {

std::filesystem::path makeFakeOllama(QTemporaryDir& directory) {
    const std::filesystem::path executable =
        std::filesystem::path(directory.path().toStdString()) / "ollama";
    std::ofstream script(executable);
    script << "#!/bin/sh\n"
              "case \"$1\" in\n"
              "  list) echo 'NAME ID SIZE MODIFIED'; exit 0 ;;\n"
              "  show) [ \"$2\" = \"installed-model\" ] && exit 0; echo 'model not found' >&2; exit 1 ;;\n"
              "  run) printf 'fixture\\nassembly\\n'; exit 0 ;;\n"
              "esac\n"
              "exit 1\n";
    script.close();
    std::filesystem::permissions(executable,
                                 std::filesystem::perms::owner_exec |
                                     std::filesystem::perms::owner_read |
                                     std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace);
    return executable;
}

}

TEST_CASE("OllamaCliService reports a missing configured executable", "[Ollama]") {
    OllamaCliService service("/path/that/does/not/exist/ollama");

    REQUIRE_FALSE(service.isAvailable());
    REQUIRE(service.availabilityError().contains("Configured Ollama executable"));
}

TEST_CASE("OllamaCliService uses an explicit executable and does not pull missing models", "[Ollama]") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const auto executable = makeFakeOllama(directory);
    OllamaCliService service(QString::fromStdString(executable.string()));

    REQUIRE(service.isAvailable());

    LLMRequest installed;
    installed.model = "installed-model";
    installed.prompt = "tag this";
    installed.timeoutMs = 1000;
    const LLMReply reply = service.sendPrompt(installed);
    REQUIRE(reply.success);
    REQUIRE(reply.raw == "fixture\nassembly\n");

    LLMRequest missing;
    missing.model = "missing-model";
    missing.prompt = "tag this";
    missing.timeoutMs = 1000;
    const LLMReply missingReply = service.sendPrompt(missing);
    REQUIRE_FALSE(missingReply.success);
    REQUIRE(missingReply.error.contains("ollama pull missing-model"));
}
