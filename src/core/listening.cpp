/* This file is part of Clementine.

   Clementine is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Clementine is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Clementine.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "listening.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QTimer>

#include "core/application.h"
#include "core/player.h"
#include "core/timeconstants.h"
#include "engines/enginebase.h"
#include "internet/podcasts/podcastbackend.h"
#include "internet/podcasts/podcastepisode.h"
#include "playlist/playlist.h"
#include "playlist/playlistmanager.h"

const char* ListeningController::kSettingsGroup = "Listening";
const int ListeningController::kSkipBackSeconds = 15;
const int ListeningController::kSkipForwardSeconds = 30;
const int ListeningController::kFinished = -1;

namespace {
// Positions this close to the start aren't worth resuming from, and this
// close to the end count as finished.
const int kMinResumeSeconds = 5;
const int kFinishedWithinSeconds = 20;
}  // namespace

ListeningController::ListeningController(Application* app, QObject* parent)
    : QObject(parent),
      app_(app),
      speed_(1.0),
      spoken_(false),
      pending_resume_(0),
      last_position_(0),
      save_timer_(new QTimer(this)),
      sleep_timer_(new QTimer(this)),
      sleep_end_of_chapter_(false) {
  QSettings s;
  s.beginGroup(kSettingsGroup);
  audiobook_folders_ = s.value("audiobook_folders").toStringList();
  speed_ = s.value("speed", 1.0).toDouble();
  positions_ = s.value("positions").toMap();
  books_ = s.value("books").toMap();
  books_played_ = s.value("books_played").toMap();

  save_timer_->setInterval(2000);
  connect(save_timer_, SIGNAL(timeout()), SLOT(SavePosition()));

  sleep_timer_->setSingleShot(true);
  connect(sleep_timer_, SIGNAL(timeout()), SLOT(SleepTimeout()));

  connect(app_->playlist_manager(), SIGNAL(CurrentSongChanged(Song)),
          SLOT(CurrentSongChanged(Song)));
  connect(app_->player(), SIGNAL(Playing()), SLOT(Playing()));
  connect(app_->player(), SIGNAL(Paused()), SLOT(SavePosition()));
  connect(app_->player(), &PlayerInterface::Stopped, this, [this]() {
    save_timer_->stop();
    if (sleep_end_of_chapter_) {
      sleep_end_of_chapter_ = false;
      emit SleepTimerChanged();
    }
  });
}

QList<double> ListeningController::Speeds() {
  return {0.75, 1.0, 1.1, 1.25, 1.5, 1.75, 2.0};
}

QString ListeningController::SpeedText(double speed) {
  return QString::number(speed, 'g', 3) + QString::fromUtf8("×");
}

bool ListeningController::IsAudiobook(const Song& song) const {
  return IsAudiobook(song, audiobook_folders_);
}

bool ListeningController::IsAudiobook(const Song& song,
                                      const QStringList& folders) {
  if (!song.is_valid()) return false;
  static const QStringList kGenres = {"audiobook",      "audiobooks",
                                      "audio book",     "spoken word",
                                      "spoken & audio", "speech"};
  if (kGenres.contains(song.genre().trimmed().toLower())) return true;

  if (!song.url().isLocalFile()) return false;
  const QString path = song.url().toLocalFile();
  if (path.endsWith(".m4b", Qt::CaseInsensitive)) return true;
  for (const QString& folder : folders) {
    if (path.startsWith(QDir::cleanPath(folder) + "/")) return true;
  }
  return false;
}

bool ListeningController::IsPodcast(const Song& song) const {
  if (!song.is_valid()) return false;
  return app_->podcast_backend()
      ->GetEpisodeByUrlOrLocalUrl(song.url())
      .is_valid();
}

bool ListeningController::IsSpoken(const Song& song) const {
  return IsAudiobook(song) || IsPodcast(song);
}

void ListeningController::AddAudiobookFolder(const QString& path) {
  const QString clean = QDir::cleanPath(path);
  if (audiobook_folders_.contains(clean)) return;
  audiobook_folders_ << clean;
  QSettings s;
  s.beginGroup(kSettingsGroup);
  s.setValue("audiobook_folders", audiobook_folders_);
  emit AudiobookFoldersChanged();
}

int ListeningController::SavedPosition(const QUrl& url) const {
  return positions_.value(url.toString(), 0).toInt();
}

QString ListeningController::BookKey(const Song& song) {
  if (!song.album().isEmpty()) {
    return song.effective_albumartist() + "\n" + song.album();
  }
  // Untagged: the folder the chapters sit in is the book.
  return QFileInfo(song.url().toLocalFile()).path();
}

QUrl ListeningController::LastChapter(const QString& book_key) const {
  return QUrl(books_.value(book_key).toString());
}

void ListeningController::MarkBookFinished(const QString& book_key,
                                           const SongList& chapters) {
  for (const Song& chapter : chapters) {
    positions_[chapter.url().toString()] = kFinished;
  }
  if (!chapters.isEmpty()) {
    books_[book_key] = chapters.last().url().toString();
  }
  Persist();
  emit ProgressChanged();
}

void ListeningController::ResetBook(const QString& book_key,
                                    const SongList& chapters) {
  for (const Song& chapter : chapters) {
    positions_.remove(chapter.url().toString());
  }
  books_.remove(book_key);
  Persist();
  emit ProgressChanged();
}

qint64 ListeningController::LastListened(const QString& book_key) const {
  return books_played_.value(book_key, 0).toLongLong();
}

void ListeningController::Persist() {
  QSettings s;
  s.beginGroup(kSettingsGroup);
  s.setValue("positions", positions_);
  s.setValue("books", books_);
  s.setValue("books_played", books_played_);
}

int ListeningController::sleep_minutes_left() const {
  if (sleep_end_of_chapter_) return -1;
  if (!sleep_timer_->isActive()) return 0;
  return qMax(1, (sleep_timer_->remainingTime() + 59999) / 60000);
}

void ListeningController::SetSpeed(double speed) {
  speed_ = qBound(0.5, speed, 3.0);
  QSettings s;
  s.beginGroup(kSettingsGroup);
  s.setValue("speed", speed_);
  ApplySpeed();
  emit SpeedChanged(speed_);
}

void ListeningController::ApplySpeed() {
  app_->player()->engine()->SetPlaybackRate(spoken_ ? speed_ : 1.0);
}

void ListeningController::SkipBack() {
  const qint64 position =
      app_->player()->engine()->position_nanosec() / kNsecPerSec;
  app_->player()->SeekTo(qMax<qint64>(0, position - kSkipBackSeconds));
}

void ListeningController::SkipForward() {
  const qint64 position =
      app_->player()->engine()->position_nanosec() / kNsecPerSec;
  const qint64 length = current_.length_nanosec() / kNsecPerSec;
  qint64 target = position + kSkipForwardSeconds;
  if (length > 0) target = qMin(target, length - 1);
  app_->player()->SeekTo(target);
}

void ListeningController::SetSleepTimer(int minutes) {
  Playlist* active = app_->playlist_manager()->active();
  const bool had_end_of_chapter = sleep_end_of_chapter_;
  sleep_timer_->stop();
  sleep_end_of_chapter_ = false;

  if (minutes > 0) {
    sleep_timer_->start(minutes * 60 * 1000);
  } else if (minutes < 0 && active) {
    sleep_end_of_chapter_ = true;
    if (!active->stop_after_current()) {
      active->StopAfter(active->current_row());
    }
  }
  // Turning "end of chapter" off takes the stop back out of the playlist.
  if (had_end_of_chapter && !sleep_end_of_chapter_ && active &&
      active->stop_after_current()) {
    active->StopAfter(-1);
  }
  emit SleepTimerChanged();
}

void ListeningController::SleepTimeout() {
  app_->player()->Pause();
  emit SleepTimerChanged();
}

void ListeningController::Store(const QUrl& url, int seconds) {
  const QString key = url.toString();
  if (positions_.value(key).toInt() == seconds && seconds != 0) return;
  if (seconds == 0) {
    positions_.remove(key);
  } else {
    positions_[key] = seconds;
  }
  Persist();
  emit ProgressChanged();
}

void ListeningController::SavePosition() {
  if (!spoken_ || !current_.is_valid()) return;
  const Engine::State state = app_->player()->GetState();
  if (state != Engine::Playing && state != Engine::Paused) return;

  last_position_ = static_cast<int>(
      app_->player()->engine()->position_nanosec() / kNsecPerSec);
  Record(current_, last_position_);
}

void ListeningController::Record(const Song& song, int position) {
  const int length = static_cast<int>(song.length_nanosec() / kNsecPerSec);
  if (IsAudiobook(song)) {
    books_[BookKey(song)] = song.url().toString();
    books_played_[BookKey(song)] = QDateTime::currentSecsSinceEpoch();
  }
  if (length > 0 && position >= length - kFinishedWithinSeconds) {
    Store(song.url(), kFinished);
  } else if (position >= kMinResumeSeconds) {
    Store(song.url(), position);
  }
}

void ListeningController::CurrentSongChanged(const Song& song) {
  // The same file with new tags isn't a new song.
  if (song.url() == current_.url()) {
    current_ = song;
    return;
  }

  // By now the engine is on the new song, so the previous one is recorded
  // where it was last seen: at its end if it played through.
  if (spoken_ && current_.is_valid()) Record(current_, last_position_);

  current_ = song;
  last_position_ = 0;
  const bool spoken = IsSpoken(song);
  pending_resume_ = 0;
  if (spoken) {
    const int saved = SavedPosition(song.url());
    if (saved >= kMinResumeSeconds) pending_resume_ = saved;
  }

  if (sleep_end_of_chapter_) {
    // The chapter it was to stop after has ended without stopping (it was
    // skipped); stop after this one instead.
    Playlist* active = app_->playlist_manager()->active();
    if (active && !active->stop_after_current()) {
      active->StopAfter(active->current_row());
    }
  }

  if (spoken != spoken_) {
    spoken_ = spoken;
    emit SpokenChanged(spoken_);
  }
  ApplySpeed();
}

void ListeningController::Playing() {
  if (!spoken_) {
    save_timer_->stop();
    return;
  }
  save_timer_->start();
  ApplySpeed();
  if (pending_resume_ > 0) {
    const int seconds = pending_resume_;
    pending_resume_ = 0;
    app_->player()->SeekTo(seconds);
  }
}
