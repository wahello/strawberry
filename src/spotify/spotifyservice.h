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

#ifndef SPOTIFYSERVICE_H
#define SPOTIFYSERVICE_H

#include "config.h"

#include <memory>

#include <QtGlobal>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QList>
#include <QMap>
#include <QVariant>
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QDateTime>
#include <QSslError>

#include "core/song.h"
#include "internet/internetservice.h"
#include "internet/internetsearchview.h"
#include "settings/spotifysettingspage.h"

class QSortFilterProxyModel;
class QNetworkReply;
class QTimer;

class Application;
class NetworkAccessManager;
class SpotifyUrlHandler;
class SpotifyRequest;
class SpotifyFavoriteRequest;
class SpotifyStreamURLRequest;
class CollectionBackend;
class CollectionModel;
class LocalRedirectServer;

class SpotifyService : public InternetService {
  Q_OBJECT

 public:
  explicit SpotifyService(Application *app, QObject *parent);
  ~SpotifyService() override;

  static const Song::Source kSource;
  static const char *kApiUrl;
  static const char *kResourcesUrl;

  void Exit() override;
  void ReloadSettings() override;

  int Search(const QString &text, InternetSearchView::SearchType type) override;
  void CancelSearch() override;

  Application *app() { return app_; }

  int artistssearchlimit() { return artistssearchlimit_; }
  int albumssearchlimit() { return albumssearchlimit_; }
  int songssearchlimit() { return songssearchlimit_; }
  bool fetchalbums() { return fetchalbums_; }
  bool download_album_covers() { return download_album_covers_; }

  QString access_token() { return access_token_; }

  bool authenticated() override { return !access_token_.isEmpty(); }

  CollectionBackend *artists_collection_backend() override { return artists_collection_backend_; }
  CollectionBackend *albums_collection_backend() override { return albums_collection_backend_; }
  CollectionBackend *songs_collection_backend() override { return songs_collection_backend_; }

  CollectionModel *artists_collection_model() override { return artists_collection_model_; }
  CollectionModel *albums_collection_model() override { return albums_collection_model_; }
  CollectionModel *songs_collection_model() override { return songs_collection_model_; }

  QSortFilterProxyModel *artists_collection_sort_model() override { return artists_collection_sort_model_; }
  QSortFilterProxyModel *albums_collection_sort_model() override { return albums_collection_sort_model_; }
  QSortFilterProxyModel *songs_collection_sort_model() override { return songs_collection_sort_model_; }

 public slots:
  void ShowConfig() override;
  void Authenticate();
  void Deauthenticate();
  void GetArtists() override;
  void GetAlbums() override;
  void GetSongs() override;
  void ResetArtistsRequest() override;
  void ResetAlbumsRequest() override;
  void ResetSongsRequest() override;

 private slots:
  void ExitReceived();
  void RedirectArrived();
  void RequestNewAccessToken() { RequestAccessToken(); }
  void HandleLoginSSLErrors(const QList<QSslError> &ssl_errors);
  void AccessTokenRequestFinished(QNetworkReply *reply);
  void StartSearch();
  void ArtistsResultsReceived(const int id, const SongMap &songs, const QString &error);
  void AlbumsResultsReceived(const int id, const SongMap &songs, const QString &error);
  void SongsResultsReceived(const int id, const SongMap &songs, const QString &error);
  void SearchResultsReceived(const int id, const SongMap &songs, const QString &error);
  void ArtistsUpdateStatusReceived(const int id, const QString &text);
  void AlbumsUpdateStatusReceived(const int id, const QString &text);
  void SongsUpdateStatusReceived(const int id, const QString &text);
  void ArtistsProgressSetMaximumReceived(const int id, const int max);
  void AlbumsProgressSetMaximumReceived(const int id, const int max);
  void SongsProgressSetMaximumReceived(const int id, const int max);
  void ArtistsUpdateProgressReceived(const int id, const int progress);
  void AlbumsUpdateProgressReceived(const int id, const int progress);
  void SongsUpdateProgressReceived(const int id, const int progress);

 private:
  typedef QPair<QString, QString> Param;
  typedef QList<Param> ParamList;

  void LoadSession();
  void RequestAccessToken(const QString &code = QString(), const QUrl &redirect_url = QUrl());
  void SendSearch();
  void LoginError(const QString &error = QString(), const QVariant &debug = QVariant());

  static const char *kOAuthAuthorizeUrl;
  static const char *kOAuthAccessTokenUrl;
  static const char *kOAuthRedirectUrl;
  static const char *kAuthUrl;
  static const char *kClientIDB64;
  static const char *kClientSecretB64;

  static const char *kArtistsSongsTable;
  static const char *kAlbumsSongsTable;
  static const char *kSongsTable;

  static const char *kArtistsSongsFtsTable;
  static const char *kAlbumsSongsFtsTable;
  static const char *kSongsFtsTable;

  Application *app_;
  NetworkAccessManager *network_;
  SpotifyUrlHandler *url_handler_;

  CollectionBackend *artists_collection_backend_;
  CollectionBackend *albums_collection_backend_;
  CollectionBackend *songs_collection_backend_;

  CollectionModel *artists_collection_model_;
  CollectionModel *albums_collection_model_;
  CollectionModel *songs_collection_model_;

  QSortFilterProxyModel *artists_collection_sort_model_;
  QSortFilterProxyModel *albums_collection_sort_model_;
  QSortFilterProxyModel *songs_collection_sort_model_;

  QTimer *timer_search_delay_;
  QTimer *timer_refresh_login_;

  std::shared_ptr<SpotifyRequest> artists_request_;
  std::shared_ptr<SpotifyRequest> albums_request_;
  std::shared_ptr<SpotifyRequest> songs_request_;
  std::shared_ptr<SpotifyRequest> search_request_;
  SpotifyFavoriteRequest *favorite_request_;

  bool enabled_;
  int artistssearchlimit_;
  int albumssearchlimit_;
  int songssearchlimit_;
  bool fetchalbums_;
  bool download_album_covers_;

  QString access_token_;
  QString refresh_token_;
  quint64 expires_in_;
  quint64 login_time_;

  int pending_search_id_;
  int next_pending_search_id_;
  QString pending_search_text_;
  InternetSearchView::SearchType pending_search_type_;

  int search_id_;
  QString search_text_;

  QString code_verifier_;
  QString code_challenge_;

  LocalRedirectServer *server_;
  QStringList login_errors_;
  QTimer refresh_login_timer_;

  QList<QObject*> wait_for_exit_;
  QList<QNetworkReply*> replies_;

};

#endif  // SPOTIFYSERVICE_H
