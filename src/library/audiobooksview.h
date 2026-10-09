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

#ifndef LIBRARY_AUDIOBOOKSVIEW_H
#define LIBRARY_AUDIOBOOKSVIEW_H

#include <QList>
#include <QWidget>

#include "core/song.h"

class AlbumGridModel;
class Application;
class ListeningController;
class QButtonGroup;
class QLabel;
class QListView;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTimer;
class QTreeWidget;

// Audiobooks get a page of their own rather than sitting among the albums:
// the book you're partway through at the top, ready to pick up, then all of
// them with how far you've got, and each book's chapters.
//
// A book is the files that share an album (or, untagged, a folder); its
// chapters are those files in track order.
class AudiobooksView : public QWidget {
  Q_OBJECT

 public:
  AudiobooksView(Application* app, ListeningController* listening,
                 QWidget* parent = nullptr);

  void ShowBooks();
  // The page of the first book, for screenshots; false if there are none.
  bool ShowFirstBook();

  struct Book {
    enum State { NotStarted, InProgress, Finished };

    QString key;
    QString title;
    QString author;
    SongList chapters;
    qint64 length = 0;   // seconds
    qint64 elapsed = 0;  // seconds
    // The chapter to carry on from.
    int chapter = 0;
    State state = NotStarted;
    qint64 last_listened = 0;

    double progress() const;
    QString StatusText() const;
  };

 public slots:
  // The library's audiobooks, from LibraryBrowser::AudiobooksLoaded().
  void SetSongs(const SongList& songs);

 signals:
  // Play the book's chapters from `start`; where in that chapter is up to
  // the ListeningController.
  void PlayBook(const SongList& chapters, int start);

 private slots:
  void Rebuild();
  void AddFolder();
  void FilterChanged();
  void BookActivated(const QModelIndex& index);

 private:
  QWidget* MakeBooksPage();
  QWidget* MakeBookPage();
  QPushButton* MakeButton(const QString& text, const QString& icon,
                          bool primary);
  Book MakeBook(const QString& key, SongList chapters) const;
  void ShowBook(const QString& key);
  void UpdateBookPage();
  void UpdateHero();
  void LoadCover(QLabel* label, const Song& song, int size, quint64* request);
  const Book* FindBook(const QString& key) const;
  void Play(const Book& book, int chapter);

  Application* app_;
  ListeningController* listening_;
  QStackedWidget* pages_;
  QTimer* rebuild_timer_;

  SongList songs_;
  QList<Book> books_;

  // Books
  QLabel* summary_;
  QWidget* empty_;
  QWidget* content_;
  QWidget* hero_;
  QLabel* hero_cover_;
  QLabel* hero_title_;
  QLabel* hero_author_;
  QLabel* hero_chapter_;
  QProgressBar* hero_progress_;
  QLabel* hero_status_;
  QString hero_key_;
  quint64 hero_cover_request_;
  QButtonGroup* filters_;
  AlbumGridModel* grid_model_;
  QListView* grid_;

  // A book
  QString open_key_;
  QLabel* book_cover_;
  QLabel* book_title_;
  QLabel* book_author_;
  QLabel* book_meta_;
  QProgressBar* book_progress_;
  QPushButton* book_play_;
  QPushButton* book_restart_;
  QPushButton* book_finished_;
  QTreeWidget* chapters_;
  quint64 book_cover_request_;
};

#endif  // LIBRARY_AUDIOBOOKSVIEW_H
