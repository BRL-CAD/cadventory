#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include <QDir>
#include <QTemporaryDir>

#include "ModelParser.h"
#include "executeCommand.h"

using namespace std::chrono_literals;

namespace {

const std::string helper = PROCESS_RUNNER_TEST_HELPER;

class CurrentDirectoryGuard {
public:
    explicit CurrentDirectoryGuard(const QString& path)
        : previous(QDir::currentPath()) {
        REQUIRE(QDir::setCurrent(path));
    }

    ~CurrentDirectoryGuard() {
        QDir::setCurrent(previous);
    }

private:
    QString previous;
};

} // namespace

TEST_CASE("Process arguments are passed literally", "[ProcessRunner]") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    CurrentDirectoryGuard cwd(directory.path());

    const std::vector<std::string> arguments = {
        "plain", "two words", "\"quoted\"", "semi;colon", "$(touch injected)"};
    const ProcessResult result = runProcess(helper, arguments);

    REQUIRE(result.success());
    REQUIRE(result.output ==
            "plain\ntwo words\n\"quoted\"\nsemi;colon\n$(touch injected)\n");
    REQUIRE_FALSE(std::filesystem::exists("injected"));
}

TEST_CASE("Process execution supports timeout and cancellation", "[ProcessRunner]") {
    // Leave ample process-start margin for instrumented builds under CI load.
    const ProcessResult timedOut = runProcess(helper, {"--sleep", "10000"}, 2s);
    REQUIRE(timedOut.timedOut);
    REQUIRE_FALSE(timedOut.success());

    std::atomic_bool cancel{false};
    std::thread requestCancellation([&cancel] {
        std::this_thread::sleep_for(50ms);
        cancel.store(true, std::memory_order_relaxed);
    });
    const ProcessResult cancelled = runProcess(
        helper, {"--sleep", "5000"}, 5s, &cancel);
    requestCancellation.join();

    REQUIRE(cancelled.cancelled);
    REQUIRE_FALSE(cancelled.success());
}

TEST_CASE("Process execution reports nonzero exits", "[ProcessRunner]") {
    const ProcessResult result = runProcess(helper, {"--exit", "17"});

    REQUIRE(result.started);
    REQUIRE(result.exitCode == 17);
    REQUIRE_FALSE(result.success());
}

TEST_CASE("ModelParser passes hostile file names safely to mged", "[ProcessRunner][ModelParser]") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    CurrentDirectoryGuard cwd(directory.path());

    const std::filesystem::path modelPath =
        std::filesystem::path(directory.path().toStdString()) /
        "model-$(touch injected).g";
    std::ofstream(modelPath).close();

    const ModelMetadata metadata = ModelParser(helper).parseModel(modelPath.string());

    REQUIRE(metadata.title == "Fixture title");
    REQUIRE(metadata.objectFiles.size() == 9);
    REQUIRE_FALSE(std::filesystem::exists("injected"));
}
