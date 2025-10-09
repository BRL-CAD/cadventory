#include "SplashDialog.h"
#include "CADventory.h"
#include "./ui_splash.h"

SplashDialog::SplashDialog(QWidget *parent) : QDialog(parent), dialog(std::make_unique<Ui::Dialog>())
{
  dialog->setupUi(this);

  // Add our current version
  QString ver = QString::fromStdString(CADventory::instance()->version());
  dialog->title->setText(QStringLiteral("CADventory v%1").arg(ver));
}


SplashDialog::~SplashDialog() = default;
