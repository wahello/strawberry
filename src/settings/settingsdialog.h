/*
 * Strawberry Music Player
 * This file was part of Clementine.
 * Copyright 2010, David Sansome <me@davidsansome.com>
 * Copyright 2019-2021, Jonas Kvinge <jonas@jkvinge.net>
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

#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include "config.h"

#include <QObject>
#include <QDialog>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QStyleOption>
#include <QMap>
#include <QSize>
#include <QString>
#include <QSettings>

#include "engine/engine_fwd.h"
#include "osd/osdbase.h"

class QMainWindow;
class QWidget;
class QModelIndex;
class QPainter;
class QTreeWidgetItem;
class QComboBox;
class QScrollArea;
class QAbstractButton;
class QShowEvent;
class QCloseEvent;

class Application;
class Player;
class Appearance;
class CollectionDirectoryModel;
class GlobalShortcutsManager;
class SettingsPage;

class Ui_SettingsDialog;


class SettingsItemDelegate : public QStyledItemDelegate {
  Q_OBJECT

 public:
  explicit SettingsItemDelegate(QObject *parent);
  QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &idx) const override;
  void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &idx) const override;
};


class SettingsDialog : public QDialog {
  Q_OBJECT

 public:
  explicit SettingsDialog(Application *app, OSDBase *osd, QMainWindow *mainwindow, QWidget *parent = nullptr);
  ~SettingsDialog() override;

  enum Page {
    Page_Behaviour,
    Page_Collection,
    Page_Backend,
    Page_Playback,
    Page_Playlist,
    Page_Scrobbler,
    Page_Covers,
    Page_Lyrics,
    Page_Transcoding,
    Page_Proxy,
    Page_Appearance,
    Page_Context,
    Page_Notifications,
    Page_GlobalShortcuts,
    Page_Moodbar,
    Page_Subsonic,
    Page_Tidal,
    Page_Qobuz,
    Page_Spotify,
  };

  enum Role {
    Role_IsSeparator = Qt::UserRole
  };

  void SetGlobalShortcutManager(GlobalShortcutsManager *manager) { manager_ = manager; }

  bool is_loading_settings() const { return loading_settings_; }

  Application *app() const { return app_; }
  OSDBase *osd() const { return osd_; }
  Player *player() const { return player_; }
  EngineBase *engine() const { return engine_; }
  CollectionDirectoryModel *collection_directory_model() const { return model_; }
  GlobalShortcutsManager *global_shortcuts_manager() const { return manager_; }
  Appearance *appearance() const { return appearance_; }

  void OpenAtPage(Page page);

 protected:
  void showEvent(QShowEvent *e) override;
  void closeEvent(QCloseEvent*) override;

 private:
  struct PageData {
    PageData() : item_(nullptr), scroll_area_(nullptr), page_(nullptr) {}
    QTreeWidgetItem *item_;
    QScrollArea *scroll_area_;
    SettingsPage *page_;
  };

  // QDialog
  void accept() override;
  void reject() override;

  void LoadGeometry();
  void SaveGeometry();

  QTreeWidgetItem *AddCategory(const QString &name);
  void AddPage(const Page id, SettingsPage *page, QTreeWidgetItem *parent = nullptr);

  void Apply();
  void Save();

 signals:
  void ReloadSettings();
  void NotificationPreview(OSDBase::Behaviour, QString, QString);

 private slots:
  void CurrentItemChanged(QTreeWidgetItem *item);
  void DialogButtonClicked(QAbstractButton *button);

 private:
  static const char *kSettingsGroup;

  QMainWindow *mainwindow_;
  Application *app_;
  OSDBase *osd_;
  Player *player_;
  EngineBase *engine_;
  CollectionDirectoryModel *model_;
  GlobalShortcutsManager *manager_;
  Appearance *appearance_;

  Ui_SettingsDialog *ui_;
  bool loading_settings_;

  QMap<Page, PageData> pages_;
};

#endif  // SETTINGSDIALOG_H
