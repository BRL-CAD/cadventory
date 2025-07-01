#include "ReportGeneratorWorker.h"
#include "ProcessGFiles.h"
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
  qDebug() << "ReportGeneratorWorker::process() started";
  ProcessGFiles processor(model);

  // need output_directory

  int num_file = 0;
  std::vector<ModelData> selectedModels = model->getSelectedModels();

  for (const auto& modelData : selectedModels) {
    if(QThread::currentThread()->isInterruptionRequested()){
      std::cout << "gen report interrupted" << std::endl;
      qDebug() << "ReportGeneratorWorker::process() stopping due to interruption request";
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
      std::cout << "No associated objects for this model.\n";
    } else {
      std::cout << "Associated Objects (" << associatedObjects.size() << "):\n";

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
  qDebug() << "ReportGeneratorWorker::process() finished";
}
