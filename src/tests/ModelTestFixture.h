#ifndef MODEL_TEST_FIXTURE_H
#define MODEL_TEST_FIXTURE_H

class ModelTestFixture {
public:
    ModelTestFixture(const std::string& test_name) {
        auto uuid = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        tempDir = std::filesystem::temp_directory_path() / (test_name + "_" + uuid);
        std::filesystem::remove_all(tempDir);   // make sure dir is empty (this is safe even if dir doesn't exist yet)
        std::filesystem::create_directories(tempDir);

        model = std::make_unique<Model>(tempDir.string());
    }

    ~ModelTestFixture() {
        model.reset();  // ensure our model is cleaned up before removing tempDir
        std::filesystem::remove_all(tempDir);
    }

    std::filesystem::path tempDir;
    std::unique_ptr<Model> model;
};

#endif // MODEL_TEST_FIXTURE_H