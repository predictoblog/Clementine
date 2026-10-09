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

// A cover's stand-in while it loads, or when there's none: a soft colour
// picked from the title.
QImage BrowserPlaceholderCover(const QString& title, bool dark);
// The image cropped to a square of `size` with rounded corners.
QPixmap BrowserRoundedCover(const QImage& image, int size, qreal dpr);

// One album in the browser: its songs in disc and track order, and what it
// says about itself.
struct BrowserAlbum {
  QString title;
  QString artist;  // the album artist, or "Various artists"
  QString genre;
  int year = 0;
  uint added = 0;  // the newest song's ctime
  SongList songs;
  // For audiobooks: how far through (0 to 1, or -1 for not shown) and a line
  // saying so.
  double progress = -1;
  QString status;
  // Whatever the page showing it needs to find it again.
  int id = -1;

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
    Column_Missing,  // which tags a song lacks, for Needs attention
    Column_Folder,   // where the file is, to tell duplicates apart
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
  enum Role {
    Role_Artist = Qt::UserRole + 1,
    Role_Year,
    Role_Progress,
    Role_Status
  };

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
  // With a status line, a third line under the artist - for audiobooks,
  // where they're up to - and a progress bar across the foot of the cover.
  explicit AlbumGridDelegate(QObject* parent, bool status_line = false)
      : QStyledItemDelegate(parent), status_line_(status_line) {}

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override;
  QSize sizeHint(const QStyleOptionViewItem& option,
                 const QModelIndex& index) const override;

 private:
  bool status_line_;
};

// The library as pages that fill the middle of the window: a grid of
// albums, an album's own page, and the songs under a Genre, Artist and Album
// column browser. It plays and queues through signals, leaving where the
// songs go to the window.
class LibraryBrowser : public QWidget {
  Q_OBJECT

 public:
  enum Page {
    Page_Albums = 0,
    Page_Album,
    Page_Songs,
    Page_Attention,
    Page_Smart
  };

  // Lists the library keeps up to date by itself.
  enum SmartView {
    Smart_RecentlyAdded = 0,
    Smart_MostPlayed,
    Smart_RecentlyPlayed,
    Smart_Favorites
  };

  LibraryBrowser(Application* app, QWidget* parent = nullptr);

  void ShowAlbums();
  void ShowSongs();
  // What's missing: albums without cover art, songs without their tags.
  void ShowAttention();
  void ShowSmart(SmartView view);
  int attention_count() const { return attention_count_; }

  // The tags a song should have and doesn't: title, artist, album, track,
  // year - the ones a MusicBrainz lookup can fill in.
  static QStringList MissingTags(const Song& song);
  static bool HasCoverArt(const Song& song);
  // Songs that look like the same recording: the same artist and title
  // (ignoring case and punctuation) and lengths within a few seconds. Each
  // group's songs are next to each other.
  static QList<SongList> FindDuplicates(const SongList& songs);
  // The album page for the first album with this title, if there is one.
  bool ShowAlbum(const QString& title);
  // The album page for the first album in the grid; false if there's none.
  bool ShowFirstAlbum();

  Page page() const;

  // Songs in these folders (and those tagged as audiobooks) are books, not
  // music: they're left out of these pages and go to AudiobooksLoaded().
  void SetAudiobookFolders(const QStringList& folders);

 signals:
  // Play `songs` from `start`, in order or shuffled.
  void PlaySongs(const SongList& songs, int start, bool shuffle);
  // Add `songs` to the queue, after whatever is queued already.
  void QueueSongs(const SongList& songs);
  // Open the tag editor on these songs.
  void EditSongs(const SongList& songs);
  // Look these songs up on MusicBrainz.
  void FixTags(const SongList& songs);
  // Open the cover manager.
  void FindCovers();
  // How many albums and songs Needs attention lists.
  void AttentionCountChanged(int count);
  // The library's audiobooks, each time it loads.
  void AudiobooksLoaded(const SongList& songs);

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
    SongList audiobooks;
  };
  static LoadResult Load(Application* app,
                         const QStringList& audiobook_folders);

  QWidget* MakeAlbumsPage();
  QWidget* MakeAlbumPage();
  QWidget* MakeSongsPage();
  QTreeView* MakeSongTable(SongTableModel* model);
  QWidget* MakeAttentionPage();
  QWidget* MakeSmartPage();
  void UpdateSmart();
  QListView* MakeAlbumGrid(AlbumGridModel* model);
  void UpdateAttention();
  void ShowSongMenu(QTreeView* table, SongTableModel* model, const QPoint& pos);
  void ShowAlbumMenu(QListView* grid, AlbumGridModel* model, const QPoint& pos);
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
  QStringList audiobook_folders_;

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

  // Needs attention
  int attention_count_;
  AlbumGridModel* attention_albums_;
  SongTableModel* attention_songs_;
  QLabel* attention_summary_;
  QLabel* attention_all_clear_;
  QWidget* attention_albums_box_;
  QLabel* attention_albums_label_;
  QWidget* attention_songs_box_;
  // Smart views
  SmartView smart_view_ = Smart_RecentlyAdded;
  QLabel* smart_title_ = nullptr;
  QLabel* smart_summary_ = nullptr;
  QLabel* smart_empty_ = nullptr;
  QTreeView* smart_table_ = nullptr;
  SongTableModel* smart_songs_ = nullptr;

  QWidget* attention_duplicates_box_ = nullptr;
  QLabel* attention_duplicates_label_ = nullptr;
  SongTableModel* attention_duplicates_ = nullptr;
  QLabel* attention_songs_label_;
  QTreeView* attention_table_;
};

#endif  // LIBRARY_LIBRARYBROWSER_H
