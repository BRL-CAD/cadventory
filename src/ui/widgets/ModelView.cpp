#include "ModelView.h"

#include <qboxlayout.h>

#include <QFileDialog>
#include <iostream>

#include "Model.h"
#include "GeometryBrowserDialog.h"
#include "ui_modelview.h"
#include "CADventory.h"
#include "AIModelTagging.h"
#include "QtJobServiceBase.h"
#include "Logger.h"

#include <QtConcurrent>     
#include <QFuture>          
#include <QFutureWatcher> 
#include <QTimer>  
#include <QMessageBox>

ModelView::ModelView(int modelId, Model* model, QWidget* parent)
    : QDialog(parent), modelId(modelId), model(model) {
  ui.setupUi(this);
  currModel = model->getModelById(modelId).value_or(ModelData{});

  geometryBrowser = new GeometryBrowserDialog(modelId, model, this);
  geometryBrowser->setWindowFlags(Qt::Widget);
  ui.geometryLayout->addWidget(geometryBrowser);
  //dont inclue extension in model name
  std::string shortName = currModel.short_name;
  size_t dotPos = shortName.find_last_of('.');
  if (dotPos != std::string::npos) {
    shortName = shortName.substr(0, dotPos);
  }
  ui.modelName->setText(QString::fromStdString(shortName));

  loadPreviewImage();
  populateProperties();
  populateTags();

  // disconnect default accept behavior so we control window with onOkClicked()
  disconnect(ui.buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);

  // Connect signals
  connect(ui.addTagButton, &QPushButton::clicked, this,
          &ModelView::onAddTagClicked);
  connect(ui.valuesList, &QListWidget::itemChanged, this,
          &ModelView::onPropertyChanged);
  connect(ui.buttonBox->button(QDialogButtonBox::Ok), &QPushButton::clicked,
          this, &ModelView::onOkClicked);
  connect(ui.generateTagsButton, &QPushButton::clicked, this, &ModelView::onGenerateTagsClicked);
  connect(ui.cancelTagButton, &QPushButton::clicked, this, &ModelView::onCancelTagGenerationClicked);

  // connect job service so we can refresh view on job completions
  // NOTE: this is VERY lazy and non-performant - ANY job completion refreshes the current view
  if (auto* svc = dynamic_cast<QtJobServiceBase*>(CADventory::instance()->getJobService())) {
    connect(svc, &QtJobServiceBase::directiveFinished, this,
    [this](const QString& directive, const QString& /*jobid*/, bool success) {
        if (!success)
            return;

        // refresh model
        if (auto md = this->model->getModelById(this->modelId)) {
            this->currModel = *md;
            loadPreviewImage();
        }
    },
    Qt::QueuedConnection);

    // refresh our current view if our service suggests it
    connect(svc, &QtJobServiceBase::refreshSuggested, this,
    [this]() {
        // refresh model
        if (auto md = this->model->getModelById(this->modelId)) {
            this->currModel = *md;
            loadPreviewImage();
        }
    },
    Qt::QueuedConnection);
  }

  // connect geometryBrowser to clear our thumbnail on changes
  connect(geometryBrowser, &GeometryBrowserDialog::selectionChanged,
        this, [this](int changedModelId, const QString&){
            if (changedModelId != this->modelId) 
                return;

            // clear preview when selection changes
            ui.previewLabel->setPixmap(QPixmap());
            ui.previewLabel->setText("Updating preview...");
        });
}

void ModelView::loadPreviewImage() {
  QPixmap thumbnail;
  thumbnail.loadFromData(
      reinterpret_cast<const uchar*>(currModel.thumbnail.data()),
      currModel.thumbnail.size());

  if (!thumbnail)   // if we don't have a thumbnail leave label text
      return;

  thumbnail = thumbnail.scaled(ui.previewLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
  ui.previewLabel->setPixmap(thumbnail);
}

void ModelView::populateProperties() {
  ui.keysList->clear();
  ui.valuesList->clear();
  int i = 0;
  const std::vector<std::string> editableProperties = {
      "short_name", "long_name", "modelers", "model_type",
      "aliases", "owner_org", "source_org"};

  // add is_included checkbox
  QListWidgetItem* isIncluded_key = new QListWidgetItem(tr("is_included"), ui.keysList);
  isIncluded_key->setFlags(isIncluded_key->flags() & ~Qt::ItemIsEditable);
  QListWidgetItem* isIncluded_val = new QListWidgetItem(ui.valuesList);
  // checkable; not text-editable
  isIncluded_val->setFlags((isIncluded_val->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
  isIncluded_val->setCheckState(currModel.is_included ? Qt::Checked : Qt::Unchecked);
  isIncluded_val->setData(Qt::UserRole, QStringLiteral("is_included"));

  // add the rest of the properties
  for (const auto& [key, value] : model->getPropertiesForModel(modelId)) {
    LOG_DEBUG << "Key: " << key << ":: Value: " << value << "  i" << i++ << LOG_ENDL;
    QListWidgetItem* keyItem = new QListWidgetItem(QString::fromStdString(key), ui.keysList);
    const bool isEditable =
        std::find(editableProperties.begin(), editableProperties.end(), key) !=
        editableProperties.end();
    QString displayValue;
    if (value.empty() && !isEditable) {
      displayValue = "(unknown)";
    } else {
      displayValue = QString::fromStdString(value);
    }
    QListWidgetItem* valueItem = new QListWidgetItem(displayValue, ui.valuesList);
    keyItem->setFlags(keyItem->flags() & ~Qt::ItemIsEditable);
    if (isEditable) {
      valueItem->setFlags(valueItem->flags() | Qt::ItemIsEditable);
      valueItem->setForeground(Qt::blue);
    }
  }
}

void ModelView::onPropertyChanged(QListWidgetItem* item) {
  int row = ui.valuesList->row(item);  // Get the row of the changed item
  QString key = ui.keysList->item(row)->text();  // Get the corresponding key

  QString value;
  if (item->flags().testFlag(Qt::ItemIsUserCheckable)) {
      // stringify checkable items to 'true' / 'false'
      value = (item->checkState() == Qt::Checked) ? "true" : "false";
  } else {
      value = item->text();
  }

  // update in-memory (rely on 'ok' click to save to db)
  properties[key] = value;
}

void ModelView::populateTags() {
  ui.tagsList->clear();
  std::vector<std::string> gottenTags = model->getTagsForModel(modelId);
  for (const std::string& tag : gottenTags) {
    addTagItem(QString::fromStdString(tag));
  }
}

void ModelView::addTagItem(const QString& tagText) {
  QListWidgetItem* item = new QListWidgetItem(ui.tagsList);
  QWidget* widget = new QWidget();
  QPushButton* removeButton = new QPushButton("x");
  removeButton->setFixedWidth(20);
  QLabel* tagLabel = new QLabel(tagText);
  QHBoxLayout* layout = new QHBoxLayout(widget);

  layout->addWidget(removeButton);
  layout->addWidget(tagLabel);
  layout->setContentsMargins(0, 0, 0, 0);
  widget->setLayout(layout);

  item->setSizeHint(widget->sizeHint());
  ui.tagsList->setItemWidget(item, widget);

  // Connect remove button
  connect(removeButton, &QPushButton::clicked, this,
          [this, item]() { onRemoveTagClicked(item); });
}

void ModelView::onAddTagClicked() {
  QString newTag = ui.newTagLine->text();
  if (!newTag.isEmpty()) {
    currModel.tags.push_back(newTag.toStdString());
    addTagItem(newTag);
    ui.newTagLine->clear();
  }
}

void ModelView::onRemoveTagClicked(QListWidgetItem* item) {
  int row = ui.tagsList->row(item);
  ui.tagsList->takeItem(row);
  delete item;
}

void ModelView::onOkClicked() {
  // freeze the window and show wait cursor to make it obvious we're updating
  this->setEnabled(false);
  QApplication::setOverrideCursor(Qt::WaitCursor);
  auto restoreUi = [this]() {
      QApplication::restoreOverrideCursor();
      this->setEnabled(true);
      QDialog::accept();         // accept and close window
  };

  // if we're no longer included dont bother with checking the rest of the properties
  if (properties["is_included"] == "false") {
      model->setModelIncluded(modelId, false);
      restoreUi();
      return;
  }

  // Update currModel properties
  const std::vector<std::string> editableProperties = {
      "short_name", "long_name", "modelers", "model_type",
      "aliases", "owner_org", "source_org"};
  for (int i = 0; i < ui.valuesList->count(); ++i) {
    QListWidgetItem* keyItem = ui.keysList->item(i);
    QListWidgetItem* valueItem = ui.valuesList->item(i);
    QString key = keyItem->text();
    QString value = valueItem->text();

    if (std::find(editableProperties.begin(), editableProperties.end(),
                  key.toStdString()) == editableProperties.end()) {
      continue;
    }

    model->setPropertyForModel(modelId, key.toStdString(), value.toStdString());
  }

  // Update tags
  model->removeAllTagsFromModel(modelId);
  for (int i = 0; i < ui.tagsList->count(); ++i) {
    QListWidgetItem* item = ui.tagsList->item(i);
    QWidget* widget = ui.tagsList->itemWidget(item);
    QLabel* tagLabel = widget->findChild<QLabel*>();
    QString tagText = tagLabel->text();
    model->addTagToModel(modelId, tagText.toStdString());
  }

  // assume something changed and we need to re-process
  model->setModelProcessed(modelId, false);

  // done
  emit tagsUpdated();
  restoreUi();
}

void ModelView::onCancelTagGenerationClicked() {
    // get our AI tagger
    AIModelTagging* tagger = CADventory::instance()->getTagger();

    // signal cancel
    tagger->cancel();

    // ui updates
    ui.tagStatusLabel->setText("Tagging canceled.");
    ui.cancelTagButton->setVisible(false);
    ui.generateTagsButton->setEnabled(true);
    ui.generateTagsButton->setVisible(true);
}

void ModelView::onGenerateTagsClicked() {
    // get our AI tagger
    AIModelTagging* tagger = CADventory::instance()->getTagger();

    // check the tagger was started successfully
    if (!tagger->taggingEnabled()) {
        QMessageBox::critical(this, "Missing Dependency", "Unable to use the AI tagger.\nPlease ensure installation and setup is complete.");
        return;
    }

    // ui updates
    ui.tagStatusLabel->setText("Generating tags...");
    ui.generateTagsButton->setEnabled(false);
    ui.generateTagsButton->setVisible(false);
    ui.cancelTagButton->setVisible(true);

    // setup connection
    connect(tagger, &AIModelTagging::tagsReady, this,
        [=](const QStringList& tags) {
            // Add the tags to the UI (avoid duplicates)
            for (const QString& tag : tags) {
                bool alreadyExists = false;
                for (int i = 0; i < ui.tagsList->count(); ++i) {
                    if (ui.tagsList->item(i)->text() == tag) {
                        alreadyExists = true;
                        break;
                    }
                }
                if (!alreadyExists) {
                    currModel.tags.push_back(tag.toStdString());
                    addTagItem(tag);

		    model->addTagToModel(modelId, tag.toStdString());
                    model->refreshModelData();
                }
            }

            // ui update for completion
            ui.tagStatusLabel->setText("Tags generated!");
            ui.generateTagsButton->setEnabled(true);
	    ui.generateTagsButton->setVisible(true);
	    ui.cancelTagButton->setVisible(false);

            // clear 'done' after a few seconds
            QTimer::singleShot(3000, this, [=]() { ui.tagStatusLabel->clear(); });

            // clenup connection
            disconnect(tagger, &AIModelTagging::tagsReady, this, nullptr);
        });

    // kick off generation
    QString filepath = QString::fromStdString(currModel.file_path);
    tagger->generateTags(filepath);
}


ModelView::~ModelView() { LOG_DEBUG << "ModelView destructor called" << LOG_ENDL; }
