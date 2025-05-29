#include "LibraryWindow.h"
#include "IndexingWorker.h"
#include "ProcessGFiles.h"
#include "MainWindow.h"
#include "GeometryBrowserDialog.h"
#include "ModelView.h"
// #include "AdvancedOptionsDialog.h"
#include "ReportGenerationWindow.h"
#include "ReportGeneratorWorker.h"
#include "FileSystemFilterProxyModel.h"

#include <QThread>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListView>
#include <QPushButton>
#include <QComboBox>
#include <QMenuBar>
#include <QLineEdit>
#include <QLabel>
#include <QFileSystemWatcher>
#include <QStandardItem>
#include <QIcon>
#include <QTimer>
#include <QDebug>

#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <set>

#include "CADventory.h"
#include "ModelTagging.h"

namespace fs = std::filesystem;

LibraryWindow::LibraryWindow(QWidget* parent)
    : QWidget(parent),
    library(nullptr),
    mainWindow(nullptr),
    model(nullptr),
    availableModelsProxyModel(new ModelFilterProxyModel(this)),
    modelCardDelegate(new ModelCardDelegate(this)),
    explorerModel(new QStandardItemModel(this)),
    indexingThread(nullptr),
    indexingWorker(nullptr)
{
    ui.setupUi(this);
}

LibraryWindow::~LibraryWindow() {
    qDebug() << "LibraryWindow destructor called";

    // Ensure the indexing thread is stopped if it wasn't already
    if (indexingThread && indexingThread->isRunning()) {
        qDebug() << "Waiting for indexingThread to finish in destructor";
        indexingWorker->stop();
        indexingThread->requestInterruption();
        indexingThread->quit();
        indexingThread->wait();
        qDebug() << "indexingThread finished in destructor";
    }

    // Delete indexingWorker and indexingThread if they exist
    if (indexingWorker) {
        delete indexingWorker;
        indexingWorker = nullptr;
        qDebug() << "indexingWorker deleted in destructor";
    }

    if (indexingThread) {
        delete indexingThread;
        indexingThread = nullptr;
        qDebug() << "indexingThread deleted in destructor";
    }
}

void LibraryWindow::loadFromLibrary(Library* _library) {
    library = _library;
    ui.currentLibrary->setText(library->name());

    // Load models from the library
    model = library->model;

    // Set the source model for proxy model
    availableModelsProxyModel->setSourceModel(model);

    // Now that library is set, set up models and views
    setupModelsAndViews();
    setupExplorerView();

    // Set up connections
    setupConnections();

    // Start indexing to process any already included but unprocessed models
    startIndexing();
}

void LibraryWindow::startIndexing() {
    if (indexingThread && indexingThread->isRunning()) {
        if (indexingWorker) {
            indexingWorker->requestReindex();
        }
        return;
    }

    // Create the indexing worker and thread
    indexingThread = new QThread(this);
    indexingWorker = new IndexingWorker(library);

    indexingWorker->moveToThread(indexingThread);

    // Connect signals and slots
    connect(indexingThread, &QThread::started, indexingWorker, &IndexingWorker::process);
    connect(indexingWorker, &IndexingWorker::modelProcessed, this, &LibraryWindow::onModelProcessed);
    connect(indexingWorker, &IndexingWorker::progressUpdated, this, &LibraryWindow::onProgressUpdated);
    connect(indexingWorker, &IndexingWorker::finished, this, &LibraryWindow::onIndexingComplete);
    connect(indexingWorker, &IndexingWorker::finished, indexingThread, &QThread::quit);
    connect(indexingThread, &QThread::finished, indexingWorker, &QObject::deleteLater);
    connect(indexingThread, &QThread::finished, indexingThread, &QObject::deleteLater);

    // Start the indexing thread
    indexingThread->start();
}

void LibraryWindow::processNextFile() {
    if (currentFileIndex >= static_cast<int>(filesToTag.size())) {
        ui.statusLabel->setText("Tagging complete!");
        ui.generateAllTagsButton->setEnabled(true);

        ui.generateAllTagsButton->show();
        ui.generateAllTagsButton->setEnabled(true);

        ui.pauseButton->hide();
        ui.cancelButton->hide();

		currentFileIndex = -1;
		canceled = false;
		paused = false;
        return;
    }

    std::string filepath = filesToTag[currentFileIndex];

    qDebug() << "Processing file:" << QString::fromStdString(filepath);

    CADventory* app = qobject_cast<CADventory*>(QCoreApplication::instance());
    ModelTagging* modelTagging = app->getModelTagging();
    modelTagging->generateTags(filepath);
}

void LibraryWindow::onTagsGeneratedFromBatch(const std::vector<std::string>& tags) {
    int fileIndex = currentFileIndex;
    if (fileIndex < 0 || fileIndex >= (int)filesToTag.size()) {
        return;
    }
    std::string filepath = filesToTag[fileIndex];

    ModelData data = model->getModelByFilePath(filepath);
    int modelId = data.id;
    if (modelId != -1) {
        std::vector<std::string> existingTags = model->getTagsForModel(modelId);
        std::set<std::string> existing(existingTags.begin(), existingTags.end());
        for (const auto& tag : tags) {
            if (existing.find(tag) == existing.end()) {
                model->addTagToModel(modelId, tag);
            }
        }

        model->refreshModelData();
        availableModelsProxyModel->invalidate();
    }

    int progress = ui.progressBar->value() + 1;
    ui.progressBar->setValue(progress);
    ui.statusLabel->setText(QString("Processed %1/%2")
        .arg(progress)
        .arg(ui.progressBar->maximum()));

    currentFileIndex++;

    QTimer::singleShot(200, this, &LibraryWindow::processNextFile);
}

void LibraryWindow::onResumeTagGenerationClicked() {
    paused = false;

	processNextFile();

    ui.resumeButton->hide();
    ui.pauseButton->show();
    ui.cancelButton->show();

    ui.statusLabel->setText("Resuming tagging...");
}

void LibraryWindow::onPauseTagGenerationClicked() {
    paused = true;
    
    CADventory* app = qobject_cast<CADventory*>(QCoreApplication::instance());
    ModelTagging* modelTagging = app->getModelTagging();
    modelTagging->cancelTagGeneration();

    ui.pauseButton->hide();
    ui.resumeButton->show();
    ui.cancelButton->show();
    ui.statusLabel->setText("Tag generation paused.");
}

void LibraryWindow::onCancelTagGenerationClicked() {
    canceled = true;

	// kill any QProcess that is running
    CADventory* app = qobject_cast<CADventory*>(QCoreApplication::instance());
    ModelTagging* modelTagging = app->getModelTagging();
    modelTagging->cancelTagGeneration();

    ui.pauseButton->hide();
    ui.cancelButton->hide();
    ui.generateAllTagsButton->show();
    ui.generateAllTagsButton->setEnabled(true);
    ui.statusLabel->setText("Tag generation canceled.");
} 

void LibraryWindow::onGenerateAllTagsClicked() {
    // generates all tags for all models
    CADventory* app = qobject_cast<CADventory*>(QCoreApplication::instance());
    ModelTagging* modelTagging = app->getModelTagging();

    // dependency check
    if (!modelTagging->checkOllamaAvailability()) {
        QMessageBox::critical(this, "Missing Dependency", "Ollama is not installed or not available in PATH.");
        return;
    }

    if (!modelTagging->checkModelAvailability("llama3")) {
        QMessageBox::critical(this, "Missing Model", "The 'llama3' model is not available.\nRun: `ollama pull llama3`.");
        return;
    }

    // get all the paths
    std::vector<std::string> relativePaths = library->getModels();

	if (relativePaths.empty()) {
		QMessageBox::information(this, "No Models", "No models found in the library.");
		return;
	}

    ui.generateAllTagsButton->setEnabled(false);
    ui.generateAllTagsButton->hide();

    ui.pauseButton->show();
    ui.cancelButton->show();

    // debug print
	qDebug() << "Generating tags for the following models:";
	for (const std::string& rel : relativePaths) {
		qDebug() << QString::fromStdString(rel);
	}

    ui.progressBar->setMaximum(static_cast<int>(relativePaths.size()));
    ui.progressBar->setValue(0);
    ui.statusLabel->setText("Generating tags...");
    ui.generateAllTagsButton->setEnabled(false);

    filesToTag.clear();
    for (const auto& rel : relativePaths) {
        filesToTag.push_back(library->fullPath + "/" + rel);
    }

    if (canceled || currentFileIndex <= 0) {
        currentFileIndex = 0;
    }

    static bool connectedOnce = false;
    if (!connectedOnce) {
        connect(modelTagging, &ModelTagging::tagsGenerated,
            this, &LibraryWindow::onTagsGeneratedFromBatch);
        connectedOnce = true;
    }

    processNextFile();
}

void LibraryWindow::setMainWindow(MainWindow* mainWindow) {
    this->mainWindow = mainWindow;
    reload = new QAction(tr("&Reload"), this);

    this->mainWindow->editMenu->addAction(reload);
    connect(reload, &QAction::triggered, this, &LibraryWindow::reloadLibrary);
}

void LibraryWindow::setupModelsAndViews() {
    // Configure available models view
    ui.availableModelsView->setModel(availableModelsProxyModel);
    ui.availableModelsView->setItemDelegate(modelCardDelegate);
    ui.availableModelsView->setViewMode(QListView::ListMode);
    ui.availableModelsView->setFlow(QListView::TopToBottom);
    ui.availableModelsView->setWrapping(false);
    ui.availableModelsView->setResizeMode(QListView::Adjust);
    ui.availableModelsView->setSpacing(0);
    ui.availableModelsView->setUniformItemSizes(true);
    ui.availableModelsView->setSelectionMode(QAbstractItemView::NoSelection);
    ui.availableModelsView->setSelectionBehavior(QAbstractItemView::SelectRows);

    QSize itemSize = modelCardDelegate->sizeHint(QStyleOptionViewItem(), QModelIndex());
    ui.availableModelsView->setGridSize(QSize(0, itemSize.height()));

    // Setup file system model with checkboxes
    QString libraryPath = QString::fromStdString(library->fullPath);
    qDebug() << "Library Path in setupModelsAndViews:" << libraryPath;

    // Create the FileSystemModelWithCheckboxes
    fileSystemModel = new FileSystemModelWithCheckboxes(model, libraryPath, this);

    // Create and set up the proxy model to filter .g files
    fileSystemProxyModel = new FileSystemFilterProxyModel(libraryPath, this);
    fileSystemProxyModel->setSourceModel(fileSystemModel);
    fileSystemProxyModel->setRecursiveFilteringEnabled(true);

    // Set the model to the tree view
    ui.fileSystemTreeView->setModel(fileSystemProxyModel);

    // Set the root index of the view to the mapped root index
    QModelIndex rootIndex = fileSystemModel->index(libraryPath);
    QModelIndex proxyRootIndex = fileSystemProxyModel->mapFromSource(rootIndex);
    ui.fileSystemTreeView->setRootIndex(proxyRootIndex);
    qDebug() << "Set root index of fileSystemTreeView to proxyRootIndex.";

    // Hide columns other than the name
    for (int i = 1; i < fileSystemModel->columnCount(); ++i) {
        ui.fileSystemTreeView->hideColumn(i);
    }

    // Set uniform row heights for consistent appearance
    ui.fileSystemTreeView->setUniformRowHeights(true);

    // Set icon size (if desired)
    ui.fileSystemTreeView->setIconSize(QSize(24, 24));

    // Expand all nodes currently loaded
    ui.fileSystemTreeView->expandAll();

    // Connect signals
    connect(fileSystemModel, &QFileSystemModel::directoryLoaded,
            this, &LibraryWindow::onDirectoryLoaded);
    connect(fileSystemModel, &FileSystemModelWithCheckboxes::inclusionChanged,
            this, &LibraryWindow::onInclusionChanged);
}

void LibraryWindow::setupExplorerView() {
    // Configure explorer models view
    ui.explorerModelsView->setModel(explorerModel);
    ui.explorerModelsView->setViewMode(QListView::ListMode);
    ui.explorerModelsView->setFlow(QListView::TopToBottom);
    ui.explorerModelsView->setWrapping(false);
    ui.explorerModelsView->setResizeMode(QListView::Adjust);
    ui.explorerModelsView->setSpacing(2);
    ui.explorerModelsView->setUniformItemSizes(true);
    ui.explorerModelsView->setSelectionMode(QAbstractItemView::NoSelection);
    ui.explorerModelsView->setSelectionBehavior(QAbstractItemView::SelectRows);
    
    // Disable the default selection highlighting
    ui.explorerModelsView->setStyleSheet("QListView::item:selected { background-color: transparent; }");
    
    // Set icon size
    ui.explorerModelsView->setIconSize(QSize(16, 16));
    
    // Populate the explorer model with all models in the library
    populateExplorerModel();
}

void LibraryWindow::populateExplorerModel() {
    // Clear the model and the stored items
    explorerModel->clear();
    allExplorerItems.clear();
    
    // Set up headers
    explorerModel->setHorizontalHeaderLabels(QStringList() << "Models");
    
    // Get all models from the library
    for (int i = 0; i < model->rowCount(); ++i) {
        QModelIndex index = model->index(i, 0);
        
        // Only include processed models
        if (model->data(index, Model::IsProcessedRole).toBool() && 
            model->data(index, Model::IsIncludedRole).toBool()) {
            
            // Get model data
            int modelId = model->data(index, Model::IdRole).toInt();
            QString shortName = model->data(index, Model::ShortNameRole).toString();
            QString title = model->data(index, Model::TitleRole).toString();
            bool isSelected = model->data(index, Model::IsSelectedRole).toBool();
            
            // Remove file extension from shortName if present
            int dotIndex = shortName.lastIndexOf('.');
            if (dotIndex > 0) {
                shortName = shortName.left(dotIndex);
            }
            
            // Create item
            QStandardItem* item = new QStandardItem(shortName);
            item->setData(modelId, Qt::UserRole); // Store model ID
            item->setData(title, Qt::UserRole + 1); // Store title as tooltip
            
            // Set tooltip with title
            item->setToolTip(title);
            
            // Set background color if selected
            if (isSelected) {
                QColor selectedColor = QColor(180, 180, 180); // Darker gray
                item->setBackground(selectedColor);
            } else {
                // Ensure unselected items have transparent background
                item->setBackground(Qt::transparent);
            }
            
            // Add to model
            explorerModel->appendRow(item);
            
            // Store the item for filtering
            allExplorerItems.append(item);
        }
    }
}

void LibraryWindow::onExplorerModelClicked(const QModelIndex& index) {
    // Get the model ID from the item data
    int modelId = explorerModel->data(index, Qt::UserRole).toInt();
    qDebug() << "Explorer model clicked:" << modelId;
    
    // Find the corresponding model in the main model
    for (int i = 0; i < model->rowCount(); ++i) {
        QModelIndex modelIndex = model->index(i, 0);
        if (model->data(modelIndex, Model::IdRole).toInt() == modelId) {
            // Toggle selection state
            bool isSelected = model->data(modelIndex, Model::IsSelectedRole).toBool();
            bool newSelectionState = !isSelected;
            model->setData(modelIndex, newSelectionState, Model::IsSelectedRole);
            
            // Update the available models view to reflect the selection change
            QModelIndex proxyIndex = availableModelsProxyModel->mapFromSource(modelIndex);
            if (proxyIndex.isValid()) {
                availableModelsProxyModel->dataChanged(proxyIndex, proxyIndex, {Model::IsSelectedRole});
            }
            
            // Update the explorer view to highlight the selected item
            QStandardItem* item = explorerModel->itemFromIndex(index);
            if (item) {
                // Set the background color based on selection state
                if (newSelectionState) {
                    // Selected
                    QColor selectedColor = QColor(180, 180, 180); // Darker gray
                    item->setBackground(selectedColor);
                } else {
                    // Deselected
                    item->setBackground(Qt::transparent);
                }
                
                // Update the view to reflect the change
                explorerModel->dataChanged(index, index, {Qt::BackgroundRole});
            }
            break;
        }
    }
}

void LibraryWindow::onExplorerModelDoubleClicked(const QModelIndex& index) {
    // Just call the click handler to select the model
    onExplorerModelClicked(index);
}

void LibraryWindow::setupConnections() {

    ui.pauseButton->hide();
    ui.cancelButton->hide();
    ui.resumeButton->hide();

    // Connect search input
    connect(ui.searchLineEdit, &QLineEdit::textChanged,
            this, &LibraryWindow::onSearchTextChanged);
    connect(ui.searchFieldComboBox, &QComboBox::currentTextChanged,
            this, &LibraryWindow::onSearchFieldChanged);

    // Connect clicks on available models
    connect(ui.availableModelsView, &QListView::clicked,
            this, &LibraryWindow::onAvailableModelClicked);

    // Connect Generate Report button
    connect(ui.generateReportButton, &QPushButton::clicked,
            this, &LibraryWindow::onGenerateReportButtonClicked);

    // Connect geometry browser clicked signal
    connect(modelCardDelegate, &ModelCardDelegate::geometryBrowserClicked,
            this, &LibraryWindow::onGeometryBrowserClicked);

    connect(modelCardDelegate, &ModelCardDelegate::modelViewClicked,
            this, &LibraryWindow::onModelViewClicked);
            
    // Connect explorer view signals
    connect(ui.explorerModelsView, &QListView::clicked,
            this, &LibraryWindow::onExplorerModelClicked);
    connect(ui.explorerModelsView, &QListView::doubleClicked,
            this, &LibraryWindow::onExplorerModelDoubleClicked);

	// Connect generate all tags button
	connect(ui.generateAllTagsButton, &QPushButton::clicked,
		this, &LibraryWindow::onGenerateAllTagsClicked);

	// Connect pause/cancel/resume tag generation buttons
    connect(ui.pauseButton, &QPushButton::clicked,
        this, &LibraryWindow::onPauseTagGenerationClicked);
    connect(ui.cancelButton, &QPushButton::clicked,
        this, &LibraryWindow::onCancelTagGenerationClicked);
    connect(ui.resumeButton, &QPushButton::clicked,
        this, &LibraryWindow::onResumeTagGenerationClicked);

    ui.searchFieldComboBox->clear();
	ui.searchFieldComboBox->addItem("Short Name", Model::ShortNameRole);
    ui.searchFieldComboBox->addItem("Tags", Model::TagsRole);
}

void LibraryWindow::onSearchTextChanged(const QString& text) {
    int role = ui.searchFieldComboBox->currentData().toInt();
    availableModelsProxyModel->setFilterRole(role);
    availableModelsProxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    availableModelsProxyModel->setFilterFixedString(text);
}

void LibraryWindow::onSearchFieldChanged(const QString& field) {
    Q_UNUSED(field);
    // Update filter role based on selected field
    int role = ui.searchFieldComboBox->currentData().toInt();
    availableModelsProxyModel->setFilterRole(role);
    // Re-apply filter
    availableModelsProxyModel->invalidate();
}

void LibraryWindow::onAvailableModelClicked(const QModelIndex& index) {
    // Toggle selection state
    QModelIndex sourceIndex = availableModelsProxyModel->mapToSource(index);
    bool isSelected = model->data(sourceIndex, Model::IsSelectedRole).toBool();
    bool newSelectionState = !isSelected;
    model->setData(sourceIndex, newSelectionState, Model::IsSelectedRole);

    // Update the view to reflect the selection change
    availableModelsProxyModel->dataChanged(index, index, {Model::IsSelectedRole});
    
    // Get the model ID
    int modelId = model->data(sourceIndex, Model::IdRole).toInt();
    
    // Update the explorer view to highlight the selected item
    for (int i = 0; i < explorerModel->rowCount(); ++i) {
        QModelIndex explorerIndex = explorerModel->index(i, 0);
        if (explorerModel->data(explorerIndex, Qt::UserRole).toInt() == modelId) {
            QStandardItem* item = explorerModel->itemFromIndex(explorerIndex);
            if (item) {
                // Set the background color based on selection state
                if (newSelectionState) {
                    // Selected
                    QColor selectedColor = QColor(180, 180, 180); // Darker gray
                    item->setBackground(selectedColor);
                } else {
                    // Deselected
                    item->setBackground(Qt::transparent);
                }
                
                // Update the view to reflect the change
                explorerModel->dataChanged(explorerIndex, explorerIndex, {Qt::BackgroundRole});
            }
            break;
        }
    }
}

void LibraryWindow::onGenerateReportButtonClicked() {
    if (model->getSelectedModels().empty()) {
        QMessageBox::information(this, "Report",
                                 "No models selected for the report.");
        return;
    }

    ReportGenerationWindow* window =
        new ReportGenerationWindow(nullptr, model, library);
    window->show();
}

void LibraryWindow::onSettingsClicked(int modelId) {
    // Handle settings button click
    qDebug() << "Settings button clicked for model ID:" << modelId;
    // Implement settings dialog or other actions here
}

void LibraryWindow::onModelProcessed(int modelId) {
    Q_UNUSED(modelId);
    model->refreshModelData();
    availableModelsProxyModel->invalidate();
    
    // Update explorer model
    populateExplorerModel();
}

void LibraryWindow::on_backButton_clicked() {
    qDebug() << "Back button clicked";

    if (indexingWorker) {
        qDebug() << "Requesting indexingWorker to stop";
        indexingWorker->stop();
        qDebug() << "indexingWorker->stop() called";
    } else {
        qDebug() << "indexingWorker is null or already deleted";
    }

    // Hide the LibraryWindow
    this->hide();
    qDebug() << "LibraryWindow hidden";

    // Show the MainWindow
    if (mainWindow) {
        this->mainWindow->editMenu->removeAction(reload);
        disconnect(reload, nullptr, nullptr, nullptr);
        this->mainWindow->returnCentralWidget();
        qDebug() << "MainWindow shown";
    } else {
        qDebug() << "mainWindow is null";
    }
}

void LibraryWindow::reloadLibrary() {
    std::string path = library->fullPath; //+ "/.cadventory/metadata.db";
    fs::path filePath(path);

    qDebug() << QString::fromStdString(filePath.string());

    // Check if the file exists
    if (fs::exists(filePath)) {

        qDebug() << "reloadLibrary is called" ;
        try {

                // Reload the library
                model->resetDatabase();
                model->refreshModelData();
                availableModelsProxyModel->invalidate();
                fileSystemModel->refresh(); // Custom method to refresh the model
                this->loadFromLibrary(library);

        } catch (const fs::filesystem_error& e) {
            std::cerr << "Error: " << e.what() << std::endl;
        }
    } else {
        std::cout << "'.cadventory' does not exist." << std::endl;
    }
}

void LibraryWindow::onModelViewClicked(int modelId) {
    qDebug() << "Model view clicked for model ID:" << modelId;
    ModelView* modelView = new ModelView(modelId, model, this);

    connect(modelView, &ModelView::tagsUpdated, this, [this]() {
        qDebug() << "Tags updated - refreshing proxy model";
        model->refreshModelData(); 
        availableModelsProxyModel->invalidate();
        });

    modelView->exec();
}

void LibraryWindow::onGeometryBrowserClicked(int modelId) {
    qDebug() << "Geometry browser clicked for model ID:" << modelId;

    GeometryBrowserDialog* dialog = new GeometryBrowserDialog(modelId, model, this);
    dialog->exec();
}

void LibraryWindow::onProgressUpdated(const QString& currentObject, int percentage) {
    ui.progressBar->setValue(percentage);

    if (percentage >= 100) {
        ui.statusLabel->setText("Processing complete");
        ui.progressBar->setVisible(false);
    } else {
        ui.statusLabel->setText(QString("Processing: %1").arg(currentObject));
        ui.progressBar->setVisible(true);
    }
}

void LibraryWindow::onInclusionChanged(const QModelIndex& index, bool /*included*/) {
    Q_UNUSED(index);
    availableModelsProxyModel->invalidate();

    startIndexing();
    model->refreshModelData();
    
    // Update explorer model
    populateExplorerModel();
}

void LibraryWindow::onIndexingComplete() {
    qDebug() << "Indexing complete";
    indexingThread = nullptr;
    indexingWorker = nullptr;

    // Refresh model data
    model->refreshModelData();
    availableModelsProxyModel->invalidate();
    
    // Update explorer model
    populateExplorerModel();

    // Update filesystem view checkboxes
    fileSystemModel->dataChanged(fileSystemModel->index(0, 0),
                                 fileSystemModel->index(fileSystemModel->rowCount() - 1, 0),
                                 {Qt::CheckStateRole});
}

void LibraryWindow::onDirectoryLoaded(const QString& /*path*/) {
    fileSystemProxyModel->invalidate();
    ui.fileSystemTreeView->expandAll();
}

