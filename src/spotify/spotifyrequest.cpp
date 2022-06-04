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
#include <QByteArray>
#include <QString>
#include <QUrl>
#include <QImage>
#include <QImageReader>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QtDebug>

#include "core/logging.h"
#include "core/networkaccessmanager.h"
#include "core/song.h"
#include "core/timeconstants.h"
#include "core/application.h"
#include "core/imageutils.h"
#include "covermanager/albumcoverloader.h"
#include "spotifyservice.h"
#include "spotifybaserequest.h"
#include "spotifyrequest.h"

const char *SpotifyRequest::kResourcesUrl = "https://resources.spotify.com";
const int SpotifyRequest::kMaxConcurrentArtistsRequests = 3;
const int SpotifyRequest::kMaxConcurrentAlbumsRequests = 3;
const int SpotifyRequest::kMaxConcurrentSongsRequests = 3;
const int SpotifyRequest::kMaxConcurrentArtistAlbumsRequests = 3;
const int SpotifyRequest::kMaxConcurrentAlbumSongsRequests = 3;
const int SpotifyRequest::kMaxConcurrentAlbumCoverRequests = 1;

SpotifyRequest::SpotifyRequest(SpotifyService *service, Application *app, NetworkAccessManager *network, QueryType type, QObject *parent)
    : SpotifyBaseRequest(service, network, parent),
      service_(service),
      app_(app),
      network_(network),
      type_(type),
      fetchalbums_(service->fetchalbums()),
      query_id_(-1),
      finished_(false),
      artists_requests_active_(0),
      artists_total_(0),
      artists_received_(0),
      albums_requests_active_(0),
      songs_requests_active_(0),
      artist_albums_requests_active_(0),
      artist_albums_requested_(0),
      artist_albums_received_(0),
      album_songs_requests_active_(0),
      album_songs_requested_(0),
      album_songs_received_(0),
      album_covers_requests_active_(),
      album_covers_requested_(0),
      album_covers_received_(0),
      no_results_(false) {}

SpotifyRequest::~SpotifyRequest() {

  while (!replies_.isEmpty()) {
    QNetworkReply *reply = replies_.takeFirst();
    QObject::disconnect(reply, nullptr, this, nullptr);
    if (reply->isRunning()) reply->abort();
    reply->deleteLater();
  }

  while (!album_cover_replies_.isEmpty()) {
    QNetworkReply *reply = album_cover_replies_.takeFirst();
    QObject::disconnect(reply, nullptr, this, nullptr);
    if (reply->isRunning()) reply->abort();
    reply->deleteLater();
  }

}

void SpotifyRequest::Process() {

  if (!service_->authenticated()) {
    emit UpdateStatus(query_id_, tr("Authenticating..."));
    return;
  }

  switch (type_) {
    case QueryType::QueryType_Artists:
      GetArtists();
      break;
    case QueryType::QueryType_Albums:
      GetAlbums();
      break;
    case QueryType::QueryType_Songs:
      GetSongs();
      break;
    case QueryType::QueryType_SearchArtists:
      ArtistsSearch();
      break;
    case QueryType::QueryType_SearchAlbums:
      AlbumsSearch();
      break;
    case QueryType::QueryType_SearchSongs:
      SongsSearch();
      break;
    default:
      Error("Invalid query type.");
      break;
  }

}

void SpotifyRequest::Search(const int query_id, const QString &search_text) {
  query_id_ = query_id;
  search_text_ = search_text;
}

void SpotifyRequest::GetArtists() {

  emit UpdateStatus(query_id_, tr("Retrieving artists..."));
  emit UpdateProgress(query_id_, 0);
  AddArtistsRequest();

}

void SpotifyRequest::AddArtistsRequest(const int offset, const int limit) {

  Request request;
  request.limit = limit;
  request.offset = offset;
  artists_requests_queue_.enqueue(request);
  if (artists_requests_active_ < kMaxConcurrentArtistsRequests) FlushArtistsRequests();

}

void SpotifyRequest::FlushArtistsRequests() {

  while (!artists_requests_queue_.isEmpty() && artists_requests_active_ < kMaxConcurrentArtistsRequests) {

    Request request = artists_requests_queue_.dequeue();
    ++artists_requests_active_;

    ParamList parameters;
    if (type_ == QueryType_SearchArtists) {
      parameters << Param("type", "artist");
      parameters << Param("q", search_text_);
    }
    if (request.limit > 0) parameters << Param("limit", QString::number(request.limit));
    if (request.offset > 0) parameters << Param("offset", QString::number(request.offset));
    QNetworkReply *reply = nullptr;
    if (type_ == QueryType_Artists) {
      reply = CreateRequest("me/artists", parameters);
    }
    if (type_ == QueryType_SearchArtists) {
      reply = CreateRequest("search", parameters);
    }
    if (!reply) continue;
    replies_ << reply;
    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, request]() { ArtistsReplyReceived(reply, request.limit, request.offset); });

  }

}

void SpotifyRequest::GetAlbums() {

  emit UpdateStatus(query_id_, tr("Retrieving albums..."));
  emit UpdateProgress(query_id_, 0);
  AddAlbumsRequest();

}

void SpotifyRequest::AddAlbumsRequest(const int offset, const int limit) {

  Request request;
  request.limit = limit;
  request.offset = offset;
  albums_requests_queue_.enqueue(request);
  if (albums_requests_active_ < kMaxConcurrentAlbumsRequests) FlushAlbumsRequests();

}

void SpotifyRequest::FlushAlbumsRequests() {

  while (!albums_requests_queue_.isEmpty() && albums_requests_active_ < kMaxConcurrentAlbumsRequests) {

    Request request = albums_requests_queue_.dequeue();
    ++albums_requests_active_;

    ParamList parameters;
    if (type_ == QueryType_SearchAlbums) {
      parameters << Param("type", "album");
      parameters << Param("q", search_text_);
    }
    if (request.limit > 0) parameters << Param("limit", QString::number(request.limit));
    if (request.offset > 0) parameters << Param("offset", QString::number(request.offset));
    QNetworkReply *reply = nullptr;
    if (type_ == QueryType_Albums) {
      reply = CreateRequest("me/albums", parameters);
    }
    if (type_ == QueryType_SearchAlbums) {
      reply = CreateRequest("search", parameters);
    }
    if (!reply) continue;
    replies_ << reply;
    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, request]() { AlbumsReplyReceived(reply, request.limit, request.offset); });

  }

}

void SpotifyRequest::GetSongs() {

  emit UpdateStatus(query_id_, tr("Retrieving songs..."));
  emit UpdateProgress(query_id_, 0);
  AddSongsRequest();

}

void SpotifyRequest::AddSongsRequest(const int offset, const int limit) {

  Request request;
  request.limit = limit;
  request.offset = offset;
  songs_requests_queue_.enqueue(request);
  if (songs_requests_active_ < kMaxConcurrentSongsRequests) FlushSongsRequests();

}

void SpotifyRequest::FlushSongsRequests() {

  while (!songs_requests_queue_.isEmpty() && songs_requests_active_ < kMaxConcurrentSongsRequests) {

    Request request = songs_requests_queue_.dequeue();
    ++songs_requests_active_;

    ParamList parameters;
    if (type_ == QueryType_SearchSongs) {
      parameters << Param("type", "track");
      parameters << Param("q", search_text_);
    }
    if (request.limit > 0) parameters << Param("limit", QString::number(request.limit));
    if (request.offset > 0) parameters << Param("offset", QString::number(request.offset));
    QNetworkReply *reply = nullptr;
    if (type_ == QueryType_Songs) {
      reply = CreateRequest("me/tracks", parameters);
    }
    if (type_ == QueryType_SearchSongs) {
      reply = CreateRequest("search", parameters);
    }
    if (!reply) continue;
    replies_ << reply;
    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, request]() { SongsReplyReceived(reply, request.limit, request.offset); });

  }

}

void SpotifyRequest::ArtistsSearch() {

  emit UpdateStatus(query_id_, tr("Searching..."));
  emit UpdateProgress(query_id_, 0);
  AddArtistsSearchRequest();

}

void SpotifyRequest::AddArtistsSearchRequest(const int offset) {

  AddArtistsRequest(offset, service_->artistssearchlimit());

}

void SpotifyRequest::AlbumsSearch() {

  emit UpdateStatus(query_id_, tr("Searching..."));
  emit UpdateProgress(query_id_, 0);
  AddAlbumsSearchRequest();

}

void SpotifyRequest::AddAlbumsSearchRequest(const int offset) {

  AddAlbumsRequest(offset, service_->albumssearchlimit());

}

void SpotifyRequest::SongsSearch() {

  emit UpdateStatus(query_id_, tr("Searching..."));
  emit UpdateProgress(query_id_, 0);
  AddSongsSearchRequest();

}

void SpotifyRequest::AddSongsSearchRequest(const int offset) {

  AddSongsRequest(offset, service_->songssearchlimit());

}

void SpotifyRequest::ArtistsReplyReceived(QNetworkReply *reply, const int limit_requested, const int offset_requested) {

  if (!replies_.contains(reply)) return;
  replies_.removeAll(reply);
  QObject::disconnect(reply, nullptr, this, nullptr);
  reply->deleteLater();

  QByteArray data = GetReplyData(reply);

  --artists_requests_active_;

  if (finished_) return;

  if (data.isEmpty()) {
    ArtistsFinishCheck();
    return;
  }

  QJsonObject json_obj = ExtractJsonObj(data);
  if (json_obj.isEmpty()) {
    ArtistsFinishCheck();
    return;
  }

  if (!json_obj.contains("artists") || !json_obj["artists"].isObject()) {
    Error("Json object missing artists.", json_obj);
    ArtistsFinishCheck();
    return;
  }

  QJsonObject obj_artists = json_obj["artists"].toObject();

  if (!obj_artists.contains("limit") ||
      !obj_artists.contains("offset") ||
      !obj_artists.contains("total") ||
      !obj_artists.contains("items")) {
    Error("Json object missing values.", obj_artists);
    ArtistsFinishCheck();
    return;
  }
  //int limit = json_obj["limit"].toInt();
  int offset = obj_artists["offset"].toInt();
  int artists_total = obj_artists["total"].toInt();

  if (offset_requested == 0) {
    artists_total_ = artists_total;
  }
  else if (artists_total != artists_total_) {
    Error(QString("Total returned does not match previous total! %1 != %2").arg(artists_total).arg(artists_total_));
    ArtistsFinishCheck();
    return;
  }

  if (offset != offset_requested) {
    Error(QString("Offset returned does not match offset requested! %1 != %2").arg(offset).arg(offset_requested));
    ArtistsFinishCheck();
    return;
  }

  if (offset_requested == 0) {
    emit ProgressSetMaximum(query_id_, artists_total_);
    emit UpdateProgress(query_id_, artists_received_);
  }

  QJsonValue value_items = ExtractItems(obj_artists);
  if (!value_items.isArray()) {
    ArtistsFinishCheck();
    return;
  }

  QJsonArray array_items = value_items.toArray();
  if (array_items.isEmpty()) {  // Empty array means no results
    if (offset_requested == 0) no_results_ = true;
    ArtistsFinishCheck();
    return;
  }

  int artists_received = 0;
  for (const QJsonValueRef value_item : array_items) {

    ++artists_received;

    if (!value_item.isObject()) {
      Error("Invalid Json reply, item in array is not a object.");
      continue;
    }
    QJsonObject obj_item = value_item.toObject();

    if (obj_item.contains("item")) {
      QJsonValue json_item = obj_item["item"];
      if (!json_item.isObject()) {
        Error("Invalid Json reply, item in array is not a object.", json_item);
        continue;
      }
      obj_item = json_item.toObject();
    }

    if (!obj_item.contains("id") || !obj_item.contains("name")) {
      Error("Invalid Json reply, item missing id or album.", obj_item);
      continue;
    }

    QString artist_id = obj_item["id"].toString();
    QString artist = obj_item["name"].toString();

    if (artist_albums_requests_pending_.contains(artist_id)) continue;

    ArtistAlbumsRequest request;
    request.artist.artist_id = artist_id;
    request.artist.artist = artist;
    artist_albums_requests_pending_.insert(artist_id, request);

  }
  artists_received_ += artists_received;

  if (offset_requested != 0) emit UpdateProgress(query_id_, artists_received_);

  ArtistsFinishCheck(limit_requested, offset, artists_received);

}

void SpotifyRequest::ArtistsFinishCheck(const int limit, const int offset, const int artists_received) {

  if (finished_) return;

  if ((limit == 0 || limit > artists_received) && artists_received_ < artists_total_) {
    int offset_next = offset + artists_received;
    if (offset_next > 0 && offset_next < artists_total_) {
      if (type_ == QueryType_Artists) AddArtistsRequest(offset_next);
      else if (type_ == QueryType_SearchArtists) AddArtistsSearchRequest(offset_next);
    }
  }

  if (!artists_requests_queue_.isEmpty() && artists_requests_active_ < kMaxConcurrentArtistsRequests) FlushArtistsRequests();

  if (artists_requests_queue_.isEmpty() && artists_requests_active_ <= 0) {  // Artist query is finished, get all albums for all artists.

    // Get artist albums
    QList<ArtistAlbumsRequest> requests = artist_albums_requests_pending_.values();
    for (const ArtistAlbumsRequest &request : requests) {
      AddArtistAlbumsRequest(request.artist);
      ++artist_albums_requested_;
    }
    artist_albums_requests_pending_.clear();

    if (artist_albums_requested_ > 0) {
      if (artist_albums_requested_ == 1) emit UpdateStatus(query_id_, tr("Retrieving albums for %1 artist...").arg(artist_albums_requested_));
      else emit UpdateStatus(query_id_, tr("Retrieving albums for %1 artists...").arg(artist_albums_requested_));
      emit ProgressSetMaximum(query_id_, artist_albums_requested_);
      emit UpdateProgress(query_id_, 0);
    }

  }

  FinishCheck();

}

void SpotifyRequest::AlbumsReplyReceived(QNetworkReply *reply, const int limit_requested, const int offset_requested) {

  --albums_requests_active_;
  AlbumsReceived(reply, Artist(), limit_requested, offset_requested);
  if (!albums_requests_queue_.isEmpty() && albums_requests_active_ < kMaxConcurrentAlbumsRequests) FlushAlbumsRequests();

}

void SpotifyRequest::AddArtistAlbumsRequest(const Artist &artist, const int offset) {

  ArtistAlbumsRequest request;
  request.artist = artist;
  request.offset = offset;
  artist_albums_requests_queue_.enqueue(request);
  if (artist_albums_requests_active_ < kMaxConcurrentArtistAlbumsRequests) FlushArtistAlbumsRequests();

}

void SpotifyRequest::FlushArtistAlbumsRequests() {

  while (!artist_albums_requests_queue_.isEmpty() && artist_albums_requests_active_ < kMaxConcurrentArtistAlbumsRequests) {

    ArtistAlbumsRequest request = artist_albums_requests_queue_.dequeue();
    ++artist_albums_requests_active_;

    ParamList parameters;
    if (request.offset > 0) parameters << Param("offset", QString::number(request.offset));
    QNetworkReply *reply = CreateRequest(QString("artists/%1/albums").arg(request.artist.artist_id), parameters);
    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, request]() { ArtistAlbumsReplyReceived(reply, request.artist, request.offset); });
    replies_ << reply;

  }

}

void SpotifyRequest::ArtistAlbumsReplyReceived(QNetworkReply *reply, const Artist &artist, const int offset_requested) {

  --artist_albums_requests_active_;
  ++artist_albums_received_;
  emit UpdateProgress(query_id_, artist_albums_received_);
  AlbumsReceived(reply, artist, 0, offset_requested);
  if (!artist_albums_requests_queue_.isEmpty() && artist_albums_requests_active_ < kMaxConcurrentArtistAlbumsRequests) FlushArtistAlbumsRequests();

}

void SpotifyRequest::AlbumsReceived(QNetworkReply *reply, const Artist &artist, const int limit_requested, const int offset_requested) {

  if (!replies_.contains(reply)) return;
  replies_.removeAll(reply);
  QObject::disconnect(reply, nullptr, this, nullptr);
  reply->deleteLater();

  QByteArray data = GetReplyData(reply);

  if (finished_) return;

  if (data.isEmpty()) {
    AlbumsFinishCheck(artist);
    return;
  }

  QJsonObject json_obj = ExtractJsonObj(data);
  if (json_obj.isEmpty()) {
    AlbumsFinishCheck(artist);
    return;
  }

  if (json_obj.contains("albums") && json_obj["albums"].isObject()) {
    json_obj = json_obj["albums"].toObject();
  }

  if (!json_obj.contains("limit") ||
      !json_obj.contains("offset") ||
      !json_obj.contains("total") ||
      !json_obj.contains("items")) {
    Error("Json object missing values.", json_obj);
    AlbumsFinishCheck(artist);
    return;
  }

  //int limit = json_obj["limit"].toInt();
  int offset = json_obj["offset"].toInt();
  int albums_total = json_obj["total"].toInt();

  if (offset != offset_requested) {
    Error(QString("Offset returned does not match offset requested! %1 != %2").arg(offset).arg(offset_requested));
    AlbumsFinishCheck(artist);
    return;
  }

  QJsonValue value_items = ExtractItems(json_obj);
  if (!value_items.isArray()) {
    AlbumsFinishCheck(artist);
    return;
  }
  QJsonArray array_items = value_items.toArray();
  if (array_items.isEmpty()) {
    if ((type_ == QueryType_Albums || type_ == QueryType_SearchAlbums || (type_ == QueryType_SearchSongs && fetchalbums_)) && offset_requested == 0) {
      no_results_ = true;
    }
    AlbumsFinishCheck(artist);
    return;
  }

  int albums_received = 0;
  for (const QJsonValueRef value_item : array_items) {

    ++albums_received;

    if (!value_item.isObject()) {
      Error("Invalid Json reply, item in array is not a object.");
      continue;
    }
    QJsonObject obj_item = value_item.toObject();

    if (obj_item.contains("item")) {
      QJsonValue json_item = obj_item["item"];
      if (!json_item.isObject()) {
        Error("Invalid Json reply, item in array is not a object.", json_item);
        continue;
      }
      obj_item = json_item.toObject();
    }

    QString album_id;
    QString album_title;
    QUrl cover_url;
    bool album_explicit = false;
    if (obj_item.contains("type")) {  // This was a albums request or search
      if (!obj_item.contains("id") || !obj_item.contains("name") || !obj_item.contains("images")) {
        Error("Invalid Json reply, item is missing ID, name or images.", obj_item);
        continue;
      }
      album_id = obj_item["id"].toString();
      album_title = obj_item["name"].toString();
      if (obj_item["images"].isArray()) {
        QJsonArray array_images = obj_item["images"].toArray();
        for (const QJsonValueRef value : array_images) {
          if (!value.isObject()) {
            continue;
          }
          QJsonObject obj_image = value.toObject();
          if (obj_image.isEmpty() || !obj_image.contains("url") || !obj_image.contains("width") || !obj_image.contains("height")) continue;
          int width = obj_image["width"].toInt();
          int height = obj_image["height"].toInt();
          if (width <= 300 || height <= 300) {
            continue;
          }
          cover_url = QUrl(obj_image["url"].toString());
        }
      }
    }
    else if (obj_item.contains("album")) {  // This was a tracks request or search
      QJsonValue value_album = obj_item["album"];
      if (!value_album.isObject()) {
        Error("Invalid Json reply, item album is not a object.", value_album);
        continue;
      }
      QJsonObject obj_album = value_album.toObject();
      if (!obj_album.contains("id") || !obj_album.contains("title")) {
        Error("Invalid Json reply, item album is missing ID or title.", obj_album);
        continue;
      }
      if (obj_album["id"].isString()) {
        album_id = obj_album["id"].toString();
      }
      else {
        album_id = QString::number(obj_album["id"].toInt());
      }
      album_title = obj_album["title"].toString();
      if (obj_album.contains("explicit")) {
        album_explicit = obj_album["explicit"].toVariant().toBool();
        if (album_explicit && !album_title.isEmpty()) {
          album_title.append(" (Explicit)");
        }
      }
    }
    else {
      Error("Invalid Json reply, item missing type or album.", obj_item);
      continue;
    }

    if (album_songs_requests_pending_.contains(album_id)) continue;

    AlbumSongsRequest request;
    request.artist = artist;
    request.album.album_id = album_id;
    request.album.album = album_title;
    request.album.cover_url = cover_url;
    album_songs_requests_pending_.insert(album_id, request);

  }

  AlbumsFinishCheck(artist, limit_requested, offset, albums_total, albums_received);

}

void SpotifyRequest::AlbumsFinishCheck(const Artist &artist, const int limit, const int offset, const int albums_total, const int albums_received) {

  if (finished_) return;

  if (limit == 0 || limit > albums_received) {
    int offset_next = offset + albums_received;
    if (offset_next > 0 && offset_next < albums_total) {
      switch (type_) {
        case QueryType_Albums:
          AddAlbumsRequest(offset_next);
          break;
        case QueryType_SearchAlbums:
          AddAlbumsSearchRequest(offset_next);
          break;
        case QueryType_Artists:
        case QueryType_SearchArtists:
          AddArtistAlbumsRequest(artist, offset_next);
          break;
        default:
          break;
      }
    }
  }

  if (
      albums_requests_queue_.isEmpty() &&
      albums_requests_active_ <= 0 &&
      artist_albums_requests_queue_.isEmpty() &&
      artist_albums_requests_active_ <= 0
      ) { // Artist albums query is finished, get all songs for all albums.

    // Get songs for all the albums.

    for (QMap<QString, AlbumSongsRequest> ::iterator it = album_songs_requests_pending_.begin(); it != album_songs_requests_pending_.end(); ++it) {
      AlbumSongsRequest request = it.value();
      AddAlbumSongsRequest(request.artist, request.album);
    }
    album_songs_requests_pending_.clear();

    if (album_songs_requested_ > 0) {
      if (album_songs_requested_ == 1) emit UpdateStatus(query_id_, tr("Retrieving songs for %1 album...").arg(album_songs_requested_));
      else emit UpdateStatus(query_id_, tr("Retrieving songs for %1 albums...").arg(album_songs_requested_));
      emit ProgressSetMaximum(query_id_, album_songs_requested_);
      emit UpdateProgress(query_id_, 0);
    }
  }

  FinishCheck();

}

void SpotifyRequest::SongsReplyReceived(QNetworkReply *reply, const int limit_requested, const int offset_requested) {

  --songs_requests_active_;
  if (type_ == QueryType_SearchSongs && fetchalbums_) {
    AlbumsReceived(reply, Artist(), limit_requested, offset_requested);
  }
  else {
    SongsReceived(reply, Artist(), Album(), limit_requested, offset_requested);
  }

}

void SpotifyRequest::AddAlbumSongsRequest(const Artist &artist, const Album &album, const int offset) {

  AlbumSongsRequest request;
  request.artist = artist;
  request.album = album;
  request.offset = offset;
  album_songs_requests_queue_.enqueue(request);
  ++album_songs_requested_;
  if (album_songs_requests_active_ < kMaxConcurrentAlbumSongsRequests) FlushAlbumSongsRequests();

}

void SpotifyRequest::FlushAlbumSongsRequests() {

  while (!album_songs_requests_queue_.isEmpty() && album_songs_requests_active_ < kMaxConcurrentAlbumSongsRequests) {

    AlbumSongsRequest request = album_songs_requests_queue_.dequeue();
    ++album_songs_requests_active_;
    ParamList parameters;
    if (request.offset > 0) parameters << Param("offset", QString::number(request.offset));
    QNetworkReply *reply = CreateRequest(QString("albums/%1/tracks").arg(request.album.album_id), parameters);
    replies_ << reply;
    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, request]() { AlbumSongsReplyReceived(reply, request.artist, request.album, request.offset); });

  }

}

void SpotifyRequest::AlbumSongsReplyReceived(QNetworkReply *reply, const Artist &artist, const Album &album, const int offset_requested) {

  --album_songs_requests_active_;
  ++album_songs_received_;
  if (offset_requested == 0) {
    emit UpdateProgress(query_id_, album_songs_received_);
  }
  SongsReceived(reply, artist, album, 0, offset_requested);

}

void SpotifyRequest::SongsReceived(QNetworkReply *reply, const Artist &artist, const Album &album, const int limit_requested, const int offset_requested) {

  if (!replies_.contains(reply)) return;
  replies_.removeAll(reply);
  QObject::disconnect(reply, nullptr, this, nullptr);
  reply->deleteLater();

  QByteArray data = GetReplyData(reply);

  if (finished_) return;

  if (data.isEmpty()) {
    SongsFinishCheck(artist, album, limit_requested, offset_requested, 0, 0);
    return;
  }

  QJsonObject json_obj = ExtractJsonObj(data);
  if (json_obj.isEmpty()) {
    SongsFinishCheck(artist, album, limit_requested, offset_requested, 0, 0);
    return;
  }

  if (json_obj.contains("tracks") && json_obj["tracks"].isObject()) {
    json_obj = json_obj["tracks"].toObject();
  }

  if (!json_obj.contains("limit") ||
      !json_obj.contains("offset") ||
      !json_obj.contains("total") ||
      !json_obj.contains("items")) {
    Error("Json object missing values.", json_obj);
    SongsFinishCheck(artist, album, limit_requested, offset_requested, 0, 0);
    return;
  }

  //int limit = json_obj["limit"].toInt();
  int offset = json_obj["offset"].toInt();
  int songs_total = json_obj["total"].toInt();

  if (offset != offset_requested) {
    Error(QString("Offset returned does not match offset requested! %1 != %2").arg(offset).arg(offset_requested));
    SongsFinishCheck(artist, album, limit_requested, offset_requested, songs_total, 0);
    return;
  }

  QJsonValue json_value = ExtractItems(json_obj);
  if (!json_value.isArray()) {
    SongsFinishCheck(artist, album, limit_requested, offset_requested, songs_total, 0);
    return;
  }

  QJsonArray json_items = json_value.toArray();
  if (json_items.isEmpty()) {
    if ((type_ == QueryType_Songs || type_ == QueryType_SearchSongs) && offset_requested == 0) {
      no_results_ = true;
    }
    SongsFinishCheck(artist, album, limit_requested, offset_requested, songs_total, 0);
    return;
  }

  bool compilation = false;
  bool multidisc = false;
  SongList songs;
  int songs_received = 0;
  for (const QJsonValueRef value_item : json_items) {

    if (!value_item.isObject()) {
      Error("Invalid Json reply, track is not a object.");
      continue;
    }
    QJsonObject obj_item = value_item.toObject();

    if (obj_item.contains("item")) {
      QJsonValue item = obj_item["item"];
      if (!item.isObject()) {
        Error("Invalid Json reply, item is not a object.", item);
        continue;
      }
      obj_item = item.toObject();
    }

    ++songs_received;
    Song song(Song::Source_Spotify);
    ParseSong(song, obj_item, artist, album);
    if (!song.is_valid()) continue;
    if (song.disc() >= 2) multidisc = true;
    if (song.is_compilation()) compilation = true;
    songs << song;
  }

  for (Song song : songs) {
    if (compilation) song.set_compilation_detected(true);
    if (!multidisc) song.set_disc(0);
    songs_.insert(song.song_id(), song);
  }

  SongsFinishCheck(artist, album, limit_requested, offset_requested, songs_total, songs_received);

}

void SpotifyRequest::SongsFinishCheck(const Artist &artist, const Album &album, const int limit, const int offset, const int songs_total, const int songs_received) {

  if (finished_) return;

  if (limit == 0 || limit > songs_received) {
    int offset_next = offset + songs_received;
    if (offset_next > 0 && offset_next < songs_total) {
      switch (type_) {
        case QueryType_Songs:
          AddSongsRequest(offset_next);
          break;
        case QueryType_SearchSongs:
          // If artist_id and album_id isn't zero it means that it's a songs search where we fetch all albums too. So fallthrough.
          if (artist.artist_id.isEmpty() && album.album_id.isEmpty()) {
            AddSongsSearchRequest(offset_next);
            break;
          }
          // fallthrough
        case QueryType_Artists:
        case QueryType_SearchArtists:
        case QueryType_Albums:
        case QueryType_SearchAlbums:
          AddAlbumSongsRequest(artist, album, offset_next);
          break;
        default:
          break;
      }
    }
  }

  if (!songs_requests_queue_.isEmpty() && songs_requests_active_ < kMaxConcurrentAlbumSongsRequests) FlushAlbumSongsRequests();
  if (!album_songs_requests_queue_.isEmpty() && album_songs_requests_active_ < kMaxConcurrentAlbumSongsRequests) FlushAlbumSongsRequests();

  if (
      service_->download_album_covers() &&
      IsQuery() &&
      songs_requests_queue_.isEmpty() &&
      songs_requests_active_ <= 0 &&
      album_songs_requests_queue_.isEmpty() &&
      album_songs_requests_active_ <= 0 &&
      album_cover_requests_queue_.isEmpty() &&
      album_covers_received_ <= 0 &&
      album_covers_requests_sent_.isEmpty() &&
      album_songs_received_ >= album_songs_requested_
  ) {
    GetAlbumCovers();
  }

  FinishCheck();

}

QString SpotifyRequest::ParseSong(Song &song, const QJsonObject &json_obj, const Artist &album_artist, const Album &album) {

  if (
      !json_obj.contains("type") ||
      !json_obj.contains("id") ||
      !json_obj.contains("name") ||
      !json_obj.contains("uri") ||
      !json_obj.contains("duration_ms") ||
      !json_obj.contains("track_number") ||
      !json_obj.contains("disc_number") ||
      !json_obj.contains("explicit")
    ) {
    Error("Invalid Json reply, track is missing one or more values.", json_obj);
    return QString();
  }

  QString artist_id;
  QString artist_title;
  if (json_obj.contains("artists") && json_obj["artists"].isArray()) {
    QJsonArray array_artists = json_obj["artists"].toArray();
    for (const QJsonValueRef value_artist : array_artists) {
      if (!value_artist.isObject()) continue;
       QJsonObject obj_artist = value_artist.toObject();
       if (!obj_artist.contains("type") || !obj_artist.contains("id") || !obj_artist.contains("name")) {
         continue;
       }
       artist_id = obj_artist["id"].toString();
       artist_title = obj_artist["name"].toString();
       break;
    }
  }
  
  QString album_id;
  QString album_title;
  QUrl cover_url;
  if (json_obj.contains("album") && json_obj["album"].isObject()) {
    QJsonObject obj_album = json_obj["album"].toObject();
    if (obj_album.contains("type") && obj_album.contains("id") && obj_album.contains("name")) {
      album_id = obj_album["id"].toString();
      album_title = obj_album["name"].toString();
      if (obj_album.contains("images") && obj_album["images"].isArray()) {
        QJsonArray array_images = obj_album["images"].toArray();
        for (const QJsonValueRef value : array_images) {
          if (!value.isObject()) {
            continue;
          }
          QJsonObject obj_image = value.toObject();
          if (obj_image.isEmpty() || !obj_image.contains("url") || !obj_image.contains("width") || !obj_image.contains("height")) continue;
          int width = obj_image["width"].toInt();
          int height = obj_image["height"].toInt();
          if (width <= 300 || height <= 300) {
            continue;
          }
          cover_url = QUrl(obj_image["url"].toString());
        }
      }
    }
  }

  if (artist_id.isEmpty() || artist_title.isEmpty()) {
    artist_id = album_artist.artist_id;
    artist_title = album_artist.artist;
  }
  
  if (album_id.isEmpty() || album_title.isEmpty() || cover_url.isEmpty()) {
    album_id = album.album_id;
    album_title = album.album;
    cover_url = album.cover_url;
  }

  QJsonValue json_duration = json_obj["duration"];

  QString song_id = json_obj["id"].toString();
  QString title = json_obj["name"].toString();
  QString uri = json_obj["uri"].toString();
  qint64 duration = json_obj["duration"].toVariant().toLongLong() * kNsecPerSec;
  int track = json_obj["track_number"].toInt();
  int disc = json_obj["disc_number"].toInt();

  QUrl url(uri);

  title.remove(Song::kTitleRemoveMisc);

  song.set_source(Song::Source_Spotify);
  song.set_song_id(song_id);
  song.set_album_id(album_id);
  song.set_artist_id(artist_id);
  if (album_artist.artist != artist_title) song.set_albumartist(album_artist.artist);
  song.set_album(album_title);
  song.set_artist(artist_title);
  song.set_title(title);
  song.set_track(track);
  song.set_disc(disc);
  song.set_url(url);
  song.set_length_nanosec(duration);
  song.set_art_automatic(cover_url);
  song.set_directory_id(0);
  song.set_filetype(Song::FileType_Stream);
  song.set_filesize(0);
  song.set_mtime(0);
  song.set_ctime(0);
  song.set_valid(true);

  return song_id;

}

void SpotifyRequest::GetAlbumCovers() {

  const SongList songs = songs_.values();
  for (const Song &song : songs) {
    AddAlbumCoverRequest(song);
  }
  FlushAlbumCoverRequests();

  if (album_covers_requested_ == 1) emit UpdateStatus(query_id_, tr("Retrieving album cover for %1 album...").arg(album_covers_requested_));
  else emit UpdateStatus(query_id_, tr("Retrieving album covers for %1 albums...").arg(album_covers_requested_));
  emit ProgressSetMaximum(query_id_, album_covers_requested_);
  emit UpdateProgress(query_id_, 0);

}

void SpotifyRequest::AddAlbumCoverRequest(const Song &song) {

  if (album_covers_requests_sent_.contains(song.album_id())) {
    album_covers_requests_sent_.insert(song.album_id(), song.song_id());
    return;
  }

  AlbumCoverRequest request;
  request.album_id = song.album_id();
  request.url = song.art_automatic();
  request.filename = app_->album_cover_loader()->CoverFilePath(song.source(), song.effective_albumartist(), song.effective_album(), song.album_id(), QString(), request.url);
  if (request.filename.isEmpty()) return;

  album_covers_requests_sent_.insert(song.album_id(), song.song_id());
  ++album_covers_requested_;

  album_cover_requests_queue_.enqueue(request);

}

void SpotifyRequest::FlushAlbumCoverRequests() {

  while (!album_cover_requests_queue_.isEmpty() && album_covers_requests_active_ < kMaxConcurrentAlbumCoverRequests) {

    AlbumCoverRequest request = album_cover_requests_queue_.dequeue();
    ++album_covers_requests_active_;

    QNetworkRequest req(request.url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = network_->get(req);
    album_cover_replies_ << reply;
    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, request]() { AlbumCoverReceived(reply, request.album_id, request.url, request.filename); });

  }

}

void SpotifyRequest::AlbumCoverReceived(QNetworkReply *reply, const QString &album_id, const QUrl &url, const QString &filename) {

  if (album_cover_replies_.contains(reply)) {
    album_cover_replies_.removeAll(reply);
    QObject::disconnect(reply, nullptr, this, nullptr);
    reply->deleteLater();
  }
  else {
    AlbumCoverFinishCheck();
    return;
  }

  --album_covers_requests_active_;
  ++album_covers_received_;

  if (finished_) return;

  emit UpdateProgress(query_id_, album_covers_received_);

  if (!album_covers_requests_sent_.contains(album_id)) {
    AlbumCoverFinishCheck();
    return;
  }

  if (reply->error() != QNetworkReply::NoError) {
    Error(QString("%1 (%2)").arg(reply->errorString()).arg(reply->error()));
    album_covers_requests_sent_.remove(album_id);
    AlbumCoverFinishCheck();
    return;
  }

  if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) {
    Error(QString("Received HTTP code %1 for %2.").arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()).arg(url.toString()));
    if (album_covers_requests_sent_.contains(album_id)) album_covers_requests_sent_.remove(album_id);
    AlbumCoverFinishCheck();
    return;
  }

  QString mimetype = reply->header(QNetworkRequest::ContentTypeHeader).toString();
  if (mimetype.contains(';')) {
    mimetype = mimetype.left(mimetype.indexOf(';'));
  }
  if (!ImageUtils::SupportedImageMimeTypes().contains(mimetype, Qt::CaseInsensitive) && !ImageUtils::SupportedImageFormats().contains(mimetype, Qt::CaseInsensitive)) {
    Error(QString("Unsupported mimetype for image reader %1 for %2").arg(mimetype, url.toString()));
    if (album_covers_requests_sent_.contains(album_id)) album_covers_requests_sent_.remove(album_id);
    AlbumCoverFinishCheck();
    return;
  }

  QByteArray data = reply->readAll();
  if (data.isEmpty()) {
    Error(QString("Received empty image data for %1").arg(url.toString()));
    if (album_covers_requests_sent_.contains(album_id)) album_covers_requests_sent_.remove(album_id);
    AlbumCoverFinishCheck();
    return;
  }

  QList<QByteArray> format_list = ImageUtils::ImageFormatsForMimeType(mimetype.toUtf8());
  char *format = nullptr;
  if (!format_list.isEmpty()) {
    format = format_list.first().data();
  }

  QImage image;
  if (image.loadFromData(data, format)) {
    if (image.save(filename, format)) {
      while (album_covers_requests_sent_.contains(album_id)) {
        const QString song_id = album_covers_requests_sent_.take(album_id);
        if (songs_.contains(song_id)) {
          songs_[song_id].set_art_automatic(QUrl::fromLocalFile(filename));
        }
      }
    }
    else {
      Error(QString("Error saving image data to %1").arg(filename));
      if (album_covers_requests_sent_.contains(album_id)) album_covers_requests_sent_.remove(album_id);
    }
  }
  else {
    if (album_covers_requests_sent_.contains(album_id)) album_covers_requests_sent_.remove(album_id);
    Error(QString("Error decoding image data from %1").arg(url.toString()));
  }

  AlbumCoverFinishCheck();

}

void SpotifyRequest::AlbumCoverFinishCheck() {

  if (!album_cover_requests_queue_.isEmpty() && album_covers_requests_active_ < kMaxConcurrentAlbumCoverRequests)
    FlushAlbumCoverRequests();

  FinishCheck();

}

void SpotifyRequest::FinishCheck() {

  if (
      !finished_ &&
      albums_requests_queue_.isEmpty() &&
      artists_requests_queue_.isEmpty() &&
      songs_requests_queue_.isEmpty() &&
      artist_albums_requests_queue_.isEmpty() &&
      album_songs_requests_queue_.isEmpty() &&
      album_cover_requests_queue_.isEmpty() &&
      artist_albums_requests_pending_.isEmpty() &&
      album_songs_requests_pending_.isEmpty() &&
      album_covers_requests_sent_.isEmpty() &&
      artists_requests_active_ <= 0 &&
      albums_requests_active_ <= 0 &&
      songs_requests_active_ <= 0 &&
      artist_albums_requests_active_ <= 0 &&
      artist_albums_received_ >= artist_albums_requested_ &&
      album_songs_requests_active_ <= 0 &&
      album_songs_received_ >= album_songs_requested_ &&
      album_covers_requested_ <= album_covers_received_ &&
      album_covers_requests_active_ <= 0 &&
      album_covers_received_ >= album_covers_requested_
  ) {
    finished_ = true;
    if (no_results_ && songs_.isEmpty()) {
      if (IsSearch())
        emit Results(query_id_, SongMap(), tr("No match."));
      else
        emit Results(query_id_, SongMap(), QString());
    }
    else {
      if (songs_.isEmpty() && errors_.isEmpty()) {
        emit Results(query_id_, songs_, tr("Unknown error"));
      }
      else {
        emit Results(query_id_, songs_, ErrorsToHTML(errors_));
      }
    }
  }

}

void SpotifyRequest::Error(const QString &error, const QVariant &debug) {

  if (!error.isEmpty()) {
    errors_ << error;
    qLog(Error) << "Spotify:" << error;
  }

  if (debug.isValid()) qLog(Debug) << debug;

  FinishCheck();

}

void SpotifyRequest::Warn(const QString &error, const QVariant &debug) {

  qLog(Error) << "Spotify:" << error;
  if (debug.isValid()) qLog(Debug) << debug;

}
