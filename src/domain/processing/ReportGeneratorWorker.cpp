#include "ReportGeneratorWorker.h"
#include "ProcessGFiles.h"
#include "Logger.h"
#include <QDebug>
#include <filesystem>

namespace fs = std::filesystem;

ReportGeneratorWorker::ReportGeneratorWorker(Model* model, std::string output_directory, std::string label, QObject* parent)
    : QObject(parent),
      label(label),
      output_directory(output_directory),
      model(model)
{
}

void ReportGeneratorWorker::process() {
  LOG_DEBUG << "ReportGeneratorWorker::process() started" << LOG_ENDL;
  ProcessGFiles processor(model);

  // need output_directory

  int num_file = 0;
  std::vector<ModelData> selectedModels = model->getSelectedModels();

  for (const auto& modelData : selectedModels) {
    if(QThread::currentThread()->isInterruptionRequested()){
      LOG_DEBUG << "ReportGeneratorWorker::process() stopping due to interruption request" << LOG_ENDL;
      break;
    }
    std::string g_file_name = modelData.short_name;
    g_file_name.erase(g_file_name.size() - 2);
    std::string path_gist_output =
        output_directory + "/" + g_file_name + "_report.png";

    std::string primary_obj = "";
    std::vector<ObjectData> associatedObjects =
        model->getObjectsForModel(modelData.id);

    if (associatedObjects.empty()) {
      LOG_DEBUG << "No associated objects for this model." << LOG_ENDL;
    } else {
      LOG_DEBUG << "Associated Objects (" << associatedObjects.size() << "):" << LOG_ENDL;

      for (const auto& obj : associatedObjects) {
        if (obj.is_selected) {
          primary_obj = obj.name;
        }
      }
    }


    emit processingGistCall(QString::fromStdString(modelData.file_path));

    // Use the generateGistReport method
    auto [success, errorMessage, command] = processor.generateGistReport(
        modelData.file_path, path_gist_output, primary_obj, label);

    if (success) {
      // emit success
        emit successfulGistCall(QString::fromStdString(path_gist_output));
    } else {
      // Handle the error

      // emit failed
      // std::string fpath = modelData.file_path;
      emit failedGistCall(QString::fromStdString(modelData.file_path), QString::fromStdString(errorMessage), QString::fromStdString(command));
    }
    num_file++;
  }


  // Emit finished signal to indicate processing is complete
  emit finishedReport();
  emit finished();
  LOG_DEBUG << "ReportGeneratorWorker::process() finished" << LOG_ENDL;
}
