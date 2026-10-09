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

#ifndef CORE_LISTENING_H
#define CORE_LISTENING_H

#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

#include "core/song.h"

class Application;
class QTimer;

// What makes audiobooks and podcasts different from music: they pick up
// where you left off, play faster or slower, skip back 15 seconds and
// forward 30, and can stop after a while when you fall asleep.
//
// It watches what's playing; while that's an audiobook or a podcast episode
// ("spoken"), it applies the listening speed, keeps the position, and the
// player bar shows the listening controls.
class ListeningController : public QObject {
  Q_OBJECT

 public:
  explicit ListeningController(Application* app, QObject* parent = nullptr);

  static const char* kSettingsGroup;
  static const int kSkipBackSeconds;
  static const int kSkipForwardSeconds;
  static QList<double> Speeds();
  static QString SpeedText(double speed);

  // An audiobook: a file in one of the audiobook folders, an .m4b, or one
  // tagged with an audiobook or spoken word genre.
  bool IsAudiobook(const Song& song) const;
  static bool IsAudiobook(const Song& song, const QStringList& folders);
  bool IsPodcast(const Song& song) const;
  bool IsSpoken(const Song& song) const;

  QStringList audiobook_folders() const { return audiobook_folders_; }
  void AddAudiobookFolder(const QString& path);

  // Whether what's playing is spoken.
  bool spoken() const { return spoken_; }
  double speed() const { return speed_; }

  // Where you left off in a file, in seconds: 0 if not started, -1 once
  // finished.
  int SavedPosition(const QUrl& url) const;
  static const int kFinished;

  // A book's chapters are its files; the book remembers the last one.
  static QString BookKey(const Song& song);
  QUrl LastChapter(const QString& book_key) const;
  void MarkBookFinished(const QString& book_key, const SongList& chapters);
  // Forgets where you were, to start again from the beginning.
  void ResetBook(const QString& book_key, const SongList& chapters);
  // When the book was last played, in seconds since the epoch; 0 if never.
  qint64 LastListened(const QString& book_key) const;

  // Minutes left on the sleep timer; 0 when off, -1 when it stops at the
  // end of the chapter.
  int sleep_minutes_left() const;

 public slots:
  void SetSpeed(double speed);
  void SkipBack();
  void SkipForward();
  // 0 turns it off; -1 stops at the end of this chapter or episode.
  void SetSleepTimer(int minutes);

 signals:
  void SpokenChanged(bool spoken);
  void SpeedChanged(double speed);
  void SleepTimerChanged();
  // A saved position or a book's progress changed.
  void ProgressChanged();
  void AudiobookFoldersChanged();

 private slots:
  void CurrentSongChanged(const Song& song);
  void Playing();
  void SavePosition();
  void SleepTimeout();

 private:
  void ApplySpeed();
  void Store(const QUrl& url, int seconds);
  void Persist();
  void Record(const Song& song, int position);

  Application* app_;
  QStringList audiobook_folders_;
  double speed_;

  Song current_;
  bool spoken_;
  int pending_resume_;
  // Where the current song was at the last save.
  int last_position_;

  QVariantMap positions_;
  QVariantMap books_;
  QVariantMap books_played_;
  QTimer* save_timer_;

  QTimer* sleep_timer_;
  bool sleep_end_of_chapter_;
};

#endif  // CORE_LISTENING_H
