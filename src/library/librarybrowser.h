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

#ifndef LIBRARY_LIBRARYBROWSER_H
#define LIBRARY_LIBRARYBROWSER_H

#include <QAbstractListModel>
#include <QAbstractTableModel>
#include <QFutureWatcher>
#include <QHash>
#include <QImage>
#include <QSet>
#include <QStyledItemDelegate>
#include <QUrl>
#include <QWidget>

#include "core/song.h"

class Application;
class QComboBox;
class QLabel;
class QLineEdit;
class QListView;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QTimer;
class QTreeView;

// One album in the browser: its songs in disc and track order, and what it
// says about itself.
struct BrowserAlbum {
  QString title;
  QString artist;  // the album artist, or "Various artists"
  QString genre;
  int year = 0;
  uint added = 0;  // the newest song's ctime
  SongList songs;

  qint64 length_nanosec() const;
};

// The songs of an album or a selection, as table rows.
class SongTableModel : public QAbstractTableModel {
  Q_OBJECT

 public:
  enum Column {
    Column_Track = 0,
    Column_Title,
    Column_Artist,
    Column_Album,
    Column_Plays,
    Column_Length,
    ColumnCount
  };

  explicit SongTableModel(QObject* parent = nullptr);

  void SetSongs(const SongList& songs);
  const SongList& songs() const { return songs_; }

  // The song playing now, drawn in the accent colour.
  void SetCurrentSong(const Song& song);

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  int columnCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role) const override;

 private:
  SongList songs_;
  QUrl current_url_;
};

// The albums, as a grid of covers that load as they scroll into view.
class AlbumGridModel : public QAbstractListModel {
  Q_OBJECT

 public:
  enum Role { Role_Artist = Qt::UserRole + 1, Role_Year };

  AlbumGridModel(Application* app, QObject* parent = nullptr);

  void SetAlbums(const QList<BrowserAlbum>& albums);
  const QList<BrowserAlbum>& albums() const { return albums_; }

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  Qt::ItemFlags flags(const QModelIndex& index) const override;

  static const int kCoverSize;

 private slots:
  void ImageLoaded(quint64 id, const QImage& image);

 private:
  void LoadCover(int row) const;

  Application* app_;
  QList<BrowserAlbum> albums_;

  // Keyed on the album's position in albums_. Mutable: covers are asked for
  // from data(), as the view paints them.
  mutable QHash<int, QImage> covers_;
  mutable QHash<quint64, int> pending_;
  mutable QSet<int> requested_;
};

class AlbumGridDelegate : public QStyledItemDelegate {
 public:
  using QStyledItemDelegate::QStyledItemDelegate;

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override;
  QSize sizeHint(const QStyleOptionViewItem& option,
                 const QModelIndex& index) const override;
};

// The library as pages that fill the middle of the window: a grid of
// albums, an album's own page, and the songs under a Genre, Artist and Album
// column browser. It plays and queues through signals, leaving where the
// songs go to the window.
class LibraryBrowser : public QWidget {
  Q_OBJECT

 public:
  enum Page { Page_Albums = 0, Page_Album, Page_Songs };

  LibraryBrowser(Application* app, QWidget* parent = nullptr);

  void ShowAlbums();
  void ShowSongs();
  // The album page for the first album with this title, if there is one.
  bool ShowAlbum(const QString& title);
  // The album page for the first album in the grid; false if there's none.
  bool ShowFirstAlbum();

  Page page() const;

 signals:
  // Play `songs` from `start`, in order or shuffled.
  void PlaySongs(const SongList& songs, int start, bool shuffle);
  // Add `songs` to the queue, after whatever is queued already.
  void QueueSongs(const SongList& songs);

 private slots:
  void Reload();
  void Loaded();
  void CurrentSongChanged(const Song& song);

  void Sort();
  void AlbumActivated(const QModelIndex& index);

  void GenreChanged();
  void ArtistChanged();
  void AlbumChanged();
  void UpdateSongList();

 private:
  struct LoadResult {
    SongList songs;
    QList<BrowserAlbum> albums;
  };
  static LoadResult Load(Application* app);

  QWidget* MakeAlbumsPage();
  QWidget* MakeAlbumPage();
  QWidget* MakeSongsPage();
  QTreeView* MakeSongTable(SongTableModel* model);
  QPushButton* MakeButton(const QString& text, const QString& icon,
                          bool primary);
  void ShowAlbum(int row);
  void FillColumn(QListWidget* list, const QStringList& values,
                  const QString& all_text);
  SongList FilteredSongs() const;

  Application* app_;
  QStackedWidget* pages_;
  QTimer* reload_timer_;
  QFutureWatcher<LoadResult>* watcher_;

  SongList songs_;
  Song current_song_;

  // Albums
  AlbumGridModel* grid_model_;
  QListView* grid_;
  QComboBox* sort_;
  QLabel* albums_summary_;

  // Album: the open one, by title and artist, so it survives a reload.
  QString open_album_title_;
  QString open_album_artist_;
  QLabel* album_cover_;
  QLabel* album_title_;
  QLabel* album_meta_;
  SongTableModel* album_tracks_;
  quint64 album_cover_request_;

  // Songs
  QLineEdit* filter_;
  QListWidget* genres_;
  QListWidget* artists_;
  QListWidget* albums_;
  SongTableModel* songs_model_;
  QLabel* songs_summary_;
};

#endif  // LIBRARY_LIBRARYBROWSER_H
