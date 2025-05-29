#ifndef MODELTAGGING_H
#define MODELTAGGING_H

#include <string>
#include <vector>
#include "ModelParser.h"
#include <QProcess>
#include <QObject>
#include "ModelMetadata.h"

/**
 * @file ModelTagging.h
 * @brief AI-powered tagging system for 3D models using Ollama + LLaMA 3
 *
 * This component provides automatic tag generation for 3D models by analyzing
 * their structure and content. It interfaces with Ollama to leverage the LLaMA 3
 * large language model for intelligent tag creation.
 *
 * 
 * Usage example:
 * 
 * ModelTagging tagger;
 * if (tagger.checkOllamaAvailability() && tagger.checkModelAvailability("llama3")) {
 *     tagger.generateTags("/path/to/model.g");
 *     auto tags = tagger.getGeneratedTags();
 *     // Use the generated tags...
 * }
 */
class ModelTagging : public QObject {
    Q_OBJECT
public:
    ModelTagging();

    ~ModelTagging();

    /**
     * @brief Generates tags for a 3D model file
     *
     * Analyzes the provided file using the LLaMA 3 model through Ollama
     * to generate descriptive tags based on model structure and metadata.
     *
     * @param filepath Path to the 3D model file to analyze
     * @return true if tag generation was successful, false otherwise
     */
    void generateTags(const std::string& filepath);

	/**
	 * @brief Cancels the ongoing tag generation process
	 *
	 * If a tag generation process is currently running, this function
	 * will terminate it and emit a signal to notify that the process
	 * has been canceled.
	 */
    void cancelTagGeneration();


    /**
     * @brief Checks if Ollama is available in the system
     *
     * Verifies that the Ollama executable exists and is accessible.
     * This is required for the AI tagging functionality to work.
     *
     * @return true if Ollama is available, false otherwise
     */
    bool checkOllamaAvailability();

	/**
	 * @brief Checks if the specified model is available
	 *
	 * Verifies that the specified AI model is available for use.
	 * If not, it prompts the user to download the model.
	 *
	 * @param modelName Name of the model to check
	 * @return true if the model is available, false otherwise
	 */
    bool checkModelAvailability(const std::string& modelName);

	/**
	 * @returns the path to the Ollama executable
	 */
    QString getOllamaPath() const { return m_ollamaPath; }
signals:
    void tagsGenerated(const std::vector<std::string>& tags);
    void tagGenerationCanceled();

private slots:
    void onTagProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    std::string modelName = "llama3"; // default model name - change if needed
	bool m_generationCanceled = false;
    ModelParser parser;
    QProcess* tagProcess = nullptr;
	QString m_ollamaPath; // path to the ollama executable
    std::string m_promptText;
    QString m_accumulatedOutput;

    void handleProcessOutput();
};

#endif // MODELTAGGING_H