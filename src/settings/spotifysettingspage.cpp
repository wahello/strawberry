/*
 * Strawberry Music Player
 * Copyright 2022, Jonas Kvinge <jonas@jkvinge.net>
 *
 * Strawberry is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Strawberry is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Strawberry.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include <QObject>
#include <QVariant>
#include <QByteArray>
#include <QString>
#include <QSettings>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QMessageBox>
#include <QEvent>

#include "settingsdialog.h"
#include "spotifysettingspage.h"
#include "ui_spotifysettingspage.h"
#include "core/application.h"
#include "core/iconloader.h"
#include "internet/internetservices.h"
#include "spotify/spotifyservice.h"
#include "widgets/loginstatewidget.h"

const char *SpotifySettingsPage::kSettingsGroup = "Spotify";

SpotifySettingsPage::SpotifySettingsPage(SettingsDialog *dialog, QWidget *parent)
    : SettingsPage(dialog, parent),
      ui_(new Ui::SpotifySettingsPage),
      service_(dialog->app()->internet_services()->Service<SpotifyService>()) {

  ui_->setupUi(this);
  setWindowIcon(IconLoader::Load("spotify"));

  QObject::connect(ui_->button_login, &QPushButton::clicked, this, &SpotifySettingsPage::LoginClicked);
  QObject::connect(ui_->login_state, &LoginStateWidget::LogoutClicked, this, &SpotifySettingsPage::LogoutClicked);

  QObject::connect(this, &SpotifySettingsPage::Authorize, service_, &SpotifyService::Authenticate);

  QObject::connect(service_, &InternetService::LoginFailure, this, &SpotifySettingsPage::LoginFailure);
  QObject::connect(service_, &InternetService::LoginSuccess, this, &SpotifySettingsPage::LoginSuccess);

  dialog->installEventFilter(this);

  ui_->quality->addItem("Low", "LOW");
  ui_->quality->addItem("High", "HIGH");
  ui_->quality->addItem("Lossless", "LOSSLESS");
  ui_->quality->addItem("Hi resolution", "HI_RES");

  ui_->coversize->addItem("160x160", "160x160");
  ui_->coversize->addItem("320x320", "320x320");
  ui_->coversize->addItem("640x640", "640x640");
  ui_->coversize->addItem("750x750", "750x750");
  ui_->coversize->addItem("1280x1280", "1280x1280");

}

SpotifySettingsPage::~SpotifySettingsPage() { delete ui_; }

void SpotifySettingsPage::Load() {

  QSettings s;
  s.beginGroup(kSettingsGroup);
  ui_->enable->setChecked(s.value("enabled", false).toBool());

  ui_->username->setText(s.value("username").toString());
  QByteArray password = s.value("password").toByteArray();
  if (password.isEmpty()) ui_->password->clear();
  else ui_->password->setText(QString::fromUtf8(QByteArray::fromBase64(password)));

  ComboBoxLoadFromSettings(s, ui_->quality, "quality", "LOSSLESS");
  ui_->searchdelay->setValue(s.value("searchdelay", 1500).toInt());
  ui_->artistssearchlimit->setValue(s.value("artistssearchlimit", 4).toInt());
  ui_->albumssearchlimit->setValue(s.value("albumssearchlimit", 10).toInt());
  ui_->songssearchlimit->setValue(s.value("songssearchlimit", 10).toInt());
  ui_->checkbox_fetchalbums->setChecked(s.value("fetchalbums", false).toBool());
  ui_->checkbox_download_album_covers->setChecked(s.value("downloadalbumcovers", true).toBool());
  ComboBoxLoadFromSettings(s, ui_->coversize, "coversize", "640x640");

  ui_->checkbox_album_explicit->setChecked(s.value("album_explicit", false).toBool());

  s.endGroup();

  if (service_->authenticated()) ui_->login_state->SetLoggedIn(LoginStateWidget::LoggedIn);

  Init(ui_->layout_spotifysettingspage->parentWidget());

  if (!QSettings().childGroups().contains(kSettingsGroup)) set_changed();

}

void SpotifySettingsPage::Save() {

  QSettings s;
  s.beginGroup(kSettingsGroup);
  s.setValue("enabled", ui_->enable->isChecked());

  s.setValue("username", ui_->username->text());
  s.setValue("password", QString::fromUtf8(ui_->password->text().toUtf8().toBase64()));

  s.setValue("quality", ui_->quality->itemData(ui_->quality->currentIndex()));
  s.setValue("searchdelay", ui_->searchdelay->value());
  s.setValue("artistssearchlimit", ui_->artistssearchlimit->value());
  s.setValue("albumssearchlimit", ui_->albumssearchlimit->value());
  s.setValue("songssearchlimit", ui_->songssearchlimit->value());
  s.setValue("fetchalbums", ui_->checkbox_fetchalbums->isChecked());
  s.setValue("downloadalbumcovers", ui_->checkbox_download_album_covers->isChecked());
  s.setValue("coversize", ui_->coversize->itemData(ui_->coversize->currentIndex()));
  s.setValue("album_explicit", ui_->checkbox_album_explicit->isChecked());
  s.endGroup();

}

void SpotifySettingsPage::LoginClicked() {

  emit Authorize();

  ui_->button_login->setEnabled(false);

}

bool SpotifySettingsPage::eventFilter(QObject *object, QEvent *event) {

  if (object == dialog() && event->type() == QEvent::Enter) {
    ui_->button_login->setEnabled(true);
  }

  return SettingsPage::eventFilter(object, event);

}

void SpotifySettingsPage::LogoutClicked() {

  service_->Deauthenticate();
  ui_->button_login->setEnabled(true);
  ui_->login_state->SetLoggedIn(LoginStateWidget::LoggedOut);

}

void SpotifySettingsPage::LoginSuccess() {

  if (!isVisible()) return;
  ui_->login_state->SetLoggedIn(LoginStateWidget::LoggedIn);
  ui_->button_login->setEnabled(true);

}

void SpotifySettingsPage::LoginFailure(const QString &failure_reason) {

  if (!isVisible()) return;
  QMessageBox::warning(this, tr("Authentication failed"), failure_reason);
  ui_->button_login->setEnabled(true);

}
