#include "SplashDialog.h"
#include "./ui_splash.h"

SplashDialog::SplashDialog(QWidget *parent) : QDialog(parent), dialog(std::make_unique<Ui::Dialog>())
{
  dialog->setupUi(this);
}


SplashDialog::~SplashDialog() = default;
