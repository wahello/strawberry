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

#include <QtGlobal>
#include <QObject>
#include <QPair>
#include <QList>
#include <QMap>
#include <QMultiMap>
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QtDebug>

#include "core/logging.h"
#include "core/networkaccessmanager.h"
#include "core/song.h"
#include "spotifyservice.h"
#include "spotifybaserequest.h"
#include "spotifyfavoriterequest.h"

SpotifyFavoriteRequest::SpotifyFavoriteRequest(SpotifyService *service, NetworkAccessManager *network, QObject *parent)
    : SpotifyBaseRequest(service, network, parent),
      service_(service),
      network_(network) {}

SpotifyFavoriteRequest::~SpotifyFavoriteRequest() {

  while (!replies_.isEmpty()) {
    QNetworkReply *reply = replies_.takeFirst();
    QObject::disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
  }

}

QString SpotifyFavoriteRequest::FavoriteText(const FavoriteType type) {

  switch (type) {
    case FavoriteType_Artists:
      return "artists";
    case FavoriteType_Albums:
      return "albums";
    case FavoriteType_Songs:
      return "tracks";
  }

  return QString();

}

QString SpotifyFavoriteRequest::FavoriteMethod(const FavoriteType type) {

  switch (type) {
    case FavoriteType_Artists:
      return "artistIds";
    case FavoriteType_Albums:
      return "albumIds";
    case FavoriteType_Songs:
      return "trackIds";
  }

  return QString();

}

void SpotifyFavoriteRequest::AddArtists(const SongList &songs) {
  AddFavorites(FavoriteType_Artists, songs);
}

void SpotifyFavoriteRequest::AddAlbums(const SongList &songs) {
  AddFavorites(FavoriteType_Albums, songs);
}

void SpotifyFavoriteRequest::AddSongs(const SongMap &songs) {
  AddFavoritesRequest(FavoriteType_Songs, songs.keys(), songs.values());
}

void SpotifyFavoriteRequest::AddFavorites(const FavoriteType type, const SongList &songs) {

  QStringList id_list;
  for (const Song &song : songs) {
    QString id;
    switch (type) {
      case FavoriteType_Artists:
        if (song.artist_id().isEmpty()) continue;
        id = song.artist_id();
        break;
      case FavoriteType_Albums:
        if (song.album_id().isEmpty()) continue;
        id = song.album_id();
        break;
      case FavoriteType_Songs:
        if (song.song_id().isEmpty()) continue;
        id = song.song_id();
        break;
    }
    if (!id.isEmpty() && !id_list.contains(id)) {
      id_list << id;
    }
  }

  if (id_list.isEmpty()) return;

  AddFavoritesRequest(type, id_list, songs);

}

void SpotifyFavoriteRequest::AddFavoritesRequest(const FavoriteType type, const QStringList &id_list, const SongList &songs) {

  ParamList params = ParamList() << Param(FavoriteMethod(type), id_list.join(','));

  QUrlQuery url_query;
  for (const Param &param : params) {
    url_query.addQueryItem(QUrl::toPercentEncoding(param.first), QUrl::toPercentEncoding(param.second));
  }

  QUrl url(SpotifyService::kApiUrl + QString("/") + "users/" + "" + "/favorites/" + FavoriteText(type));
  QNetworkRequest req(url);
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
  req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
  if (!access_token().isEmpty()) req.setRawHeader("authorization", "Bearer " + access_token().toUtf8());
  QByteArray query = url_query.toString(QUrl::FullyEncoded).toUtf8();
  QNetworkReply *reply = network_->post(req, query);
  QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, type, songs]() { AddFavoritesReply(reply, type, songs); });
  replies_ << reply;

  qLog(Debug) << "Spotify: Sending request" << url << query;

}

void SpotifyFavoriteRequest::AddFavoritesReply(QNetworkReply *reply, const FavoriteType type, const SongList &songs) {

  if (replies_.contains(reply)) {
    replies_.removeAll(reply);
    QObject::disconnect(reply, nullptr, this, nullptr);
    reply->deleteLater();
  }
  else {
    return;
  }

  GetReplyData(reply);
  if (reply->error() != QNetworkReply::NoError) {
    return;
  }

  qLog(Debug) << "Spotify:" << songs.count() << "songs added to" << FavoriteText(type) << "favorites.";

  switch (type) {
    case FavoriteType_Artists:
      emit ArtistsAdded(songs);
      break;
    case FavoriteType_Albums:
      emit AlbumsAdded(songs);
      break;
    case FavoriteType_Songs:
      emit SongsAdded(songs);
      break;
  }

}

void SpotifyFavoriteRequest::RemoveArtists(const SongList &songs) {
  RemoveFavorites(FavoriteType_Artists, songs);
}

void SpotifyFavoriteRequest::RemoveAlbums(const SongList &songs) {
  RemoveFavorites(FavoriteType_Albums, songs);
}

void SpotifyFavoriteRequest::RemoveSongs(const SongList &songs) {
  RemoveFavorites(FavoriteType_Songs, songs);
}

void SpotifyFavoriteRequest::RemoveSongs(const SongMap &songs) {

  SongList songs_list = songs.values();
  for (const Song &song : songs_list) {
    RemoveFavoritesRequest(FavoriteType_Songs, song.song_id(), SongList() << song);
  }

}

void SpotifyFavoriteRequest::RemoveFavorites(const FavoriteType type, const SongList &songs) {

  QMultiMap<QString, Song> songs_map;
  for (const Song &song : songs) {
    QString id;
    switch (type) {
      case FavoriteType_Artists:
        if (song.artist_id().isEmpty()) continue;
        id = song.artist_id();
        break;
      case FavoriteType_Albums:
        if (song.album_id().isEmpty()) continue;
        id = song.album_id();
        break;
      case FavoriteType_Songs:
        if (song.song_id().isEmpty()) continue;
        id = song.song_id();
        break;
    }
    if (!id.isEmpty()) {
      songs_map.insert(id, song);
    }
  }

  QStringList ids = songs_map.uniqueKeys();
  for (const QString &id : ids) {
    RemoveFavoritesRequest(type, id, songs_map.values(id));
  }

}

void SpotifyFavoriteRequest::RemoveFavoritesRequest(const FavoriteType type, const QString &id, const SongList &songs) {

  ParamList params;

  QUrlQuery url_query;
  for (const Param &param : params) {
    url_query.addQueryItem(QUrl::toPercentEncoding(param.first), QUrl::toPercentEncoding(param.second));
  }

  QUrl url(SpotifyService::kApiUrl + QString("/") + "users/" + "" + "/favorites/" + FavoriteText(type) + QString("/") + id);
  url.setQuery(url_query);
  QNetworkRequest req(url);
#if QT_VERSION >= QT_VERSION_CHECK(5, 9, 0)
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
#else
  req.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
#endif
  req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
  if (!access_token().isEmpty()) req.setRawHeader("authorization", "Bearer " + access_token().toUtf8());
  QNetworkReply *reply = network_->deleteResource(req);
  QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, type, songs]() { RemoveFavoritesReply(reply, type, songs); });
  replies_ << reply;

  qLog(Debug) << "Spotify: Sending request" << url << "with" << songs.count() << "songs";

}

void SpotifyFavoriteRequest::RemoveFavoritesReply(QNetworkReply *reply, const FavoriteType type, const SongList &songs) {

  if (replies_.contains(reply)) {
    replies_.removeAll(reply);
    QObject::disconnect(reply, nullptr, this, nullptr);
    reply->deleteLater();
  }
  else {
    return;
  }

  GetReplyData(reply);
  if (reply->error() != QNetworkReply::NoError) {
    return;
  }

  qLog(Debug) << "Spotify:" << songs.count() << "songs removed from" << FavoriteText(type) << "favorites.";

  switch (type) {
    case FavoriteType_Artists:
      emit ArtistsRemoved(songs);
      break;
    case FavoriteType_Albums:
      emit AlbumsRemoved(songs);
      break;
    case FavoriteType_Songs:
      emit SongsRemoved(songs);
      break;
  }

}

void SpotifyFavoriteRequest::Error(const QString &error, const QVariant &debug) {

  qLog(Error) << "Spotify:" << error;
  if (debug.isValid()) qLog(Debug) << debug;

}
