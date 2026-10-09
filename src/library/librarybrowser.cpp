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

#include "librarybrowser.h"

#include <QApplication>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMap>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollBar>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>
#include <QtConcurrentRun>
#include <algorithm>

#include "core/appearance.h"
#include "core/application.h"
#include "core/utilities.h"
#include "covers/albumcoverloader.h"
#include "covers/albumcoverloaderoptions.h"
#include "library/librarybackend.h"
#include "playlist/playlistmanager.h"
#include "ui/iconloader.h"

namespace {

const int kGridSpacing = 20;
const int kAlbumCoverSize = 200;
const int kCoverRadius = 6;

// "1 song", "4 songs": spelled out rather than with tr()'s %n, whose
// plural forms only appear when a translation supplies them.
QString Count(int n, const QString& one, const QString& many) {
  return (n == 1 ? one : many).arg(n);
}

QString SongTime(const Song& song) {
  return Utilities::PrettyTimeNanosec(song.length_nanosec());
}

// A cover's place before it loads, and for albums without one: a soft
// square in a colour picked from the title, so a grid of them isn't flat.
QImage PlaceholderCover(const QString& title, int size, bool dark) {
  QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
  const int hue = int(qHash(title) % 360);
  image.fill(QColor::fromHsl(hue, dark ? 40 : 50, dark ? 70 : 200));
  return image;
}

// The image drawn into a rounded square.
QPixmap RoundedCover(const QImage& image, int size, qreal dpr) {
  QPixmap pixmap(QSize(size, size) * dpr);
  pixmap.setDevicePixelRatio(dpr);
  pixmap.fill(Qt::transparent);
  QPainter p(&pixmap);
  p.setRenderHint(QPainter::Antialiasing);
  p.setRenderHint(QPainter::SmoothPixmapTransform);
  QPainterPath clip;
  clip.addRoundedRect(QRectF(0, 0, size, size), kCoverRadius, kCoverRadius);
  p.setClipPath(clip);
  const QImage scaled =
      image.scaled(QSize(size, size) * dpr, Qt::KeepAspectRatioByExpanding,
                   Qt::SmoothTransformation);
  p.drawImage(
      QRectF(0, 0, size, size), scaled,
      QRectF((scaled.width() - size * dpr) / 2,
             (scaled.height() - size * dpr) / 2, size * dpr, size * dpr));
  return pixmap;
}

}  // namespace

qint64 BrowserAlbum::length_nanosec() const {
  qint64 total = 0;
  for (const Song& song : songs)
    total += qMax(qint64(0), song.length_nanosec());
  return total;
}

// SongTableModel ------------------------------------------------------------

SongTableModel::SongTableModel(QObject* parent) : QAbstractTableModel(parent) {}

void SongTableModel::SetSongs(const SongList& songs) {
  beginResetModel();
  songs_ = songs;
  endResetModel();
}

void SongTableModel::SetCurrentSong(const Song& song) {
  current_url_ = song.url();
  if (!songs_.isEmpty()) {
    emit dataChanged(index(0, 0), index(songs_.count() - 1, ColumnCount - 1));
  }
}

int SongTableModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : songs_.count();
}

int SongTableModel::columnCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : ColumnCount;
}

QVariant SongTableModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() >= songs_.count()) return QVariant();
  const Song& song = songs_[index.row()];
  const bool current = !current_url_.isEmpty() && song.url() == current_url_;

  switch (role) {
    case Qt::DisplayRole:
      switch (index.column()) {
        case Column_Track:
          if (current) return QString::fromUtf8("▶");
          return song.track() > 0 ? QVariant(song.track()) : QVariant();
        case Column_Title:
          return song.PrettyTitle();
        case Column_Artist:
          return song.artist();
        case Column_Album:
          return song.album();
        case Column_Plays:
          return song.playcount() > 0 ? QVariant(song.playcount()) : QVariant();
        case Column_Length:
          return SongTime(song);
      }
      break;

    case Qt::TextAlignmentRole:
      if (index.column() == Column_Track || index.column() == Column_Plays ||
          index.column() == Column_Length) {
        return int(Qt::AlignRight | Qt::AlignVCenter);
      }
      return int(Qt::AlignLeft | Qt::AlignVCenter);

    case Qt::ForegroundRole:
      if (current) {
        return Appearance::AccentColor(QApplication::palette());
      }
      if (index.column() != Column_Title) {
        return Appearance::QuietTextColor(QApplication::palette());
      }
      break;

    case Qt::FontRole:
      if (current || index.column() == Column_Title) {
        QFont font;
        if (current) font.setWeight(QFont::DemiBold);
        return font;
      }
      break;
  }
  return QVariant();
}

QVariant SongTableModel::headerData(int section, Qt::Orientation orientation,
                                    int role) const {
  if (orientation != Qt::Horizontal) return QVariant();
  if (role == Qt::TextAlignmentRole) {
    if (section == Column_Track || section == Column_Plays ||
        section == Column_Length) {
      return int(Qt::AlignRight | Qt::AlignVCenter);
    }
    return int(Qt::AlignLeft | Qt::AlignVCenter);
  }
  if (role != Qt::DisplayRole) return QVariant();
  switch (section) {
    case Column_Track:
      return "#";
    case Column_Title:
      return tr("Title");
    case Column_Artist:
      return tr("Artist");
    case Column_Album:
      return tr("Album");
    case Column_Plays:
      return tr("Plays");
    case Column_Length:
      return tr("Time");
  }
  return QVariant();
}

// AlbumGridModel ------------------------------------------------------------

const int AlbumGridModel::kCoverSize = 168;

AlbumGridModel::AlbumGridModel(Application* app, QObject* parent)
    : QAbstractListModel(parent), app_(app) {
  connect(app_->album_cover_loader(), SIGNAL(ImageLoaded(quint64, QImage)),
          SLOT(ImageLoaded(quint64, QImage)));
}

void AlbumGridModel::SetAlbums(const QList<BrowserAlbum>& albums) {
  beginResetModel();
  QSet<quint64> stale;
  for (auto it = pending_.begin(); it != pending_.end(); ++it) {
    stale << it.key();
  }
  app_->album_cover_loader()->CancelTasks(stale);
  pending_.clear();
  requested_.clear();
  covers_.clear();
  albums_ = albums;
  endResetModel();
}

int AlbumGridModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : albums_.count();
}

Qt::ItemFlags AlbumGridModel::flags(const QModelIndex& index) const {
  return QAbstractListModel::flags(index) | Qt::ItemIsDragEnabled;
}

QVariant AlbumGridModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() >= albums_.count()) return QVariant();
  const BrowserAlbum& album = albums_[index.row()];

  switch (role) {
    case Qt::DisplayRole:
    case Qt::ToolTipRole:
      return album.title;
    case Role_Artist:
      return album.artist;
    case Role_Year:
      return album.year;
    case Qt::DecorationRole:
      if (covers_.contains(index.row())) return covers_[index.row()];
      LoadCover(index.row());
      return QVariant();
  }
  return QVariant();
}

void AlbumGridModel::LoadCover(int row) const {
  if (requested_.contains(row)) return;
  requested_ << row;

  const BrowserAlbum& album = albums_[row];
  if (album.songs.isEmpty()) return;

  AlbumCoverLoaderOptions options;
  options.desired_height_ = kCoverSize * 2;  // sharp on hidpi screens
  options.scale_output_image_ = true;
  options.pad_output_image_ = false;
  const quint64 id =
      app_->album_cover_loader()->LoadImageAsync(options, album.songs.first());
  pending_[id] = row;
}

void AlbumGridModel::ImageLoaded(quint64 id, const QImage& image) {
  if (!pending_.contains(id)) return;
  const int row = pending_.take(id);
  if (image.isNull() || row >= albums_.count()) return;
  covers_[row] = image;
  const QModelIndex changed = index(row);
  emit dataChanged(changed, changed, {Qt::DecorationRole});
}

// AlbumGridDelegate ---------------------------------------------------------

QSize AlbumGridDelegate::sizeHint(const QStyleOptionViewItem& option,
                                  const QModelIndex&) const {
  const int line = option.fontMetrics.height();
  return QSize(AlbumGridModel::kCoverSize,
               AlbumGridModel::kCoverSize + 10 + line * 2 + 6);
}

void AlbumGridDelegate::paint(QPainter* painter,
                              const QStyleOptionViewItem& option,
                              const QModelIndex& index) const {
  painter->save();
  painter->setRenderHint(QPainter::Antialiasing);

  const QRect rect = option.rect;
  const int size = AlbumGridModel::kCoverSize;
  const QRect cover_rect(rect.left() + (rect.width() - size) / 2, rect.top(),
                         size, size);
  const qreal dpr =
      painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
  const bool dark = Appearance::IsDarkPalette(option.palette);

  QImage image = index.data(Qt::DecorationRole).value<QImage>();
  const bool has_cover = !image.isNull();
  if (!has_cover) {
    image = PlaceholderCover(index.data().toString(), 8, dark);
  }
  painter->drawPixmap(cover_rect.topLeft(), RoundedCover(image, size, dpr));

  if (!has_cover) {
    // The title on the placeholder, as a printed sleeve would have it.
    QFont font(option.font);
    font.setPointSizeF(font.pointSizeF() * 1.25);
    font.setWeight(QFont::DemiBold);
    painter->setFont(font);
    painter->setPen(dark ? QColor(245, 245, 247) : QColor(29, 29, 31));
    painter->drawText(cover_rect.adjusted(14, 12, -14, -12),
                      Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                      index.data().toString());
  }

  // Hovered or selected: a ring in the accent.
  if (option.state & (QStyle::State_MouseOver | QStyle::State_Selected)) {
    QColor ring = Appearance::AccentColor(option.palette);
    if (!(option.state & QStyle::State_Selected)) ring.setAlphaF(0.6);
    painter->setPen(QPen(ring, 2));
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(QRectF(cover_rect).adjusted(1, 1, -1, -1),
                             kCoverRadius, kCoverRadius);
  }

  // Title, then artist and year.
  const int line = option.fontMetrics.height();
  QRect text(cover_rect.left(), cover_rect.bottom() + 10, size, line);
  QFont title_font(option.font);
  title_font.setWeight(QFont::DemiBold);
  painter->setFont(title_font);
  painter->setPen(option.palette.color(QPalette::Text));
  painter->drawText(
      text, Qt::AlignLeft | Qt::AlignVCenter,
      QFontMetrics(title_font)
          .elidedText(index.data().toString(), Qt::ElideRight, size));

  QString detail = index.data(AlbumGridModel::Role_Artist).toString();
  const int year = index.data(AlbumGridModel::Role_Year).toInt();
  if (year > 0) detail += QString::fromUtf8(" · ") + QString::number(year);
  text.translate(0, line + 2);
  painter->setFont(option.font);
  painter->setPen(Appearance::QuietTextColor(option.palette));
  painter->drawText(
      text, Qt::AlignLeft | Qt::AlignVCenter,
      option.fontMetrics.elidedText(detail, Qt::ElideRight, size));

  painter->restore();
}

// LibraryBrowser ------------------------------------------------------------

LibraryBrowser::LibraryBrowser(Application* app, QWidget* parent)
    : QWidget(parent),
      app_(app),
      pages_(new QStackedWidget(this)),
      reload_timer_(new QTimer(this)),
      watcher_(new QFutureWatcher<LoadResult>(this)),
      grid_model_(new AlbumGridModel(app, this)),
      grid_(nullptr),
      sort_(nullptr),
      albums_summary_(nullptr),
      album_cover_(nullptr),
      album_title_(nullptr),
      album_meta_(nullptr),
      album_tracks_(new SongTableModel(this)),
      album_cover_request_(0),
      filter_(nullptr),
      genres_(nullptr),
      artists_(nullptr),
      albums_(nullptr),
      songs_model_(new SongTableModel(this)),
      songs_summary_(nullptr) {
  setObjectName("library_browser");
  setAttribute(Qt::WA_StyledBackground);

  QVBoxLayout* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(pages_);

  pages_->addWidget(MakeAlbumsPage());
  pages_->addWidget(MakeAlbumPage());
  pages_->addWidget(MakeSongsPage());

  // Reload when the library changes, once things settle: a scan reports
  // songs in batches.
  reload_timer_->setSingleShot(true);
  reload_timer_->setInterval(800);
  connect(reload_timer_, SIGNAL(timeout()), SLOT(Reload()));
  LibraryBackend* backend = app_->library_backend();
  for (const char* signal :
       {SIGNAL(SongsDiscovered(SongList)), SIGNAL(SongsDeleted(SongList)),
        SIGNAL(SongsStatisticsChanged(SongList))}) {
    connect(backend, signal, reload_timer_, SLOT(start()));
  }
  connect(backend, SIGNAL(DatabaseReset()), reload_timer_, SLOT(start()));

  connect(watcher_, SIGNAL(finished()), SLOT(Loaded()));
  connect(app_->playlist_manager(), SIGNAL(CurrentSongChanged(Song)),
          SLOT(CurrentSongChanged(Song)));
  connect(app_->album_cover_loader(),
          qOverload<quint64, const QImage&>(&AlbumCoverLoader::ImageLoaded),
          this, [this](quint64 id, const QImage& image) {
            if (id != album_cover_request_ || image.isNull()) return;
            album_cover_->setPixmap(
                RoundedCover(image, kAlbumCoverSize, devicePixelRatioF()));
          });

  Reload();
}

QPushButton* LibraryBrowser::MakeButton(const QString& text,
                                        const QString& icon, bool primary) {
  QPushButton* button = new QPushButton(text, this);
  if (!icon.isEmpty()) {
    button->setIcon(IconLoader::Load(icon, IconLoader::Base));
  }
  button->setProperty(primary ? "primary" : "pill", true);
  button->setCursor(Qt::PointingHandCursor);
  return button;
}

QWidget* LibraryBrowser::MakeAlbumsPage() {
  QWidget* page = new QWidget(this);
  QVBoxLayout* layout = new QVBoxLayout(page);
  layout->setContentsMargins(28, 22, 20, 0);
  layout->setSpacing(14);

  QLabel* title = new QLabel(tr("Albums"), page);
  title->setProperty("page_title", true);
  albums_summary_ = new QLabel(page);
  albums_summary_->setForegroundRole(QPalette::PlaceholderText);

  sort_ = new QComboBox(page);
  sort_->addItem(tr("Artist"));
  sort_->addItem(tr("Title"));
  sort_->addItem(tr("Year"));
  sort_->addItem(tr("Recently added"));
  connect(sort_, SIGNAL(currentIndexChanged(int)), SLOT(Sort()));

  QVBoxLayout* heading = new QVBoxLayout;
  heading->setSpacing(2);
  heading->addWidget(title);
  heading->addWidget(albums_summary_);
  QHBoxLayout* header = new QHBoxLayout;
  header->addLayout(heading);
  header->addStretch();
  QLabel* sort_label = new QLabel(tr("Sort by"), page);
  sort_label->setForegroundRole(QPalette::PlaceholderText);
  header->addWidget(sort_label);
  header->addWidget(sort_);
  layout->addLayout(header);

  grid_ = new QListView(page);
  grid_->setObjectName("album_grid");
  grid_->setViewMode(QListView::IconMode);
  grid_->setResizeMode(QListView::Adjust);
  grid_->setMovement(QListView::Static);
  grid_->setUniformItemSizes(true);
  grid_->setSpacing(kGridSpacing / 2);
  grid_->setFrameShape(QFrame::NoFrame);
  grid_->setMouseTracking(true);
  grid_->setSelectionMode(QAbstractItemView::SingleSelection);
  grid_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  grid_->verticalScrollBar()->setSingleStep(24);
  grid_->setModel(grid_model_);
  grid_->setItemDelegate(new AlbumGridDelegate(grid_));
  grid_->setCursor(Qt::PointingHandCursor);
  connect(grid_, SIGNAL(clicked(QModelIndex)),
          SLOT(AlbumActivated(QModelIndex)));
  connect(grid_, SIGNAL(activated(QModelIndex)),
          SLOT(AlbumActivated(QModelIndex)));
  layout->addWidget(grid_, 1);
  return page;
}

QWidget* LibraryBrowser::MakeAlbumPage() {
  QWidget* page = new QWidget(this);
  QVBoxLayout* layout = new QVBoxLayout(page);
  layout->setContentsMargins(28, 18, 20, 0);
  layout->setSpacing(18);

  QPushButton* back = new QPushButton(tr("Albums"), page);
  back->setIcon(IconLoader::Load("go-previous", IconLoader::Base));
  back->setProperty("link", true);
  back->setCursor(Qt::PointingHandCursor);
  connect(back, &QPushButton::clicked, this, &LibraryBrowser::ShowAlbums);
  QHBoxLayout* back_row = new QHBoxLayout;
  back_row->addWidget(back);
  back_row->addStretch();
  layout->addLayout(back_row);

  album_cover_ = new QLabel(page);
  album_cover_->setFixedSize(kAlbumCoverSize, kAlbumCoverSize);

  QLabel* kind = new QLabel(tr("Album"), page);
  kind->setProperty("caption", true);
  album_title_ = new QLabel(page);
  album_title_->setProperty("album_title", true);
  album_title_->setWordWrap(true);
  album_meta_ = new QLabel(page);
  album_meta_->setWordWrap(true);

  QPushButton* play = MakeButton(tr("Play"), "media-playback-start", true);
  QPushButton* shuffle =
      MakeButton(tr("Shuffle"), "media-playlist-shuffle", false);
  QPushButton* queue = MakeButton(tr("Add to queue"), "list-add", false);
  connect(play, &QPushButton::clicked, this,
          [this]() { emit PlaySongs(album_tracks_->songs(), 0, false); });
  connect(shuffle, &QPushButton::clicked, this,
          [this]() { emit PlaySongs(album_tracks_->songs(), 0, true); });
  connect(queue, &QPushButton::clicked, this,
          [this]() { emit QueueSongs(album_tracks_->songs()); });

  QHBoxLayout* buttons = new QHBoxLayout;
  buttons->setSpacing(10);
  buttons->addWidget(play);
  buttons->addWidget(shuffle);
  buttons->addWidget(queue);
  buttons->addStretch();

  QVBoxLayout* text = new QVBoxLayout;
  text->setSpacing(8);
  text->addStretch();
  text->addWidget(kind);
  text->addWidget(album_title_);
  text->addWidget(album_meta_);
  text->addSpacing(6);
  text->addLayout(buttons);

  QHBoxLayout* header = new QHBoxLayout;
  header->setSpacing(26);
  header->addWidget(album_cover_, 0, Qt::AlignBottom);
  header->addLayout(text, 1);
  layout->addLayout(header);

  QTreeView* tracks = MakeSongTable(album_tracks_);
  tracks->setColumnHidden(SongTableModel::Column_Album, true);
  connect(tracks, &QTreeView::activated, this, [this](const QModelIndex& i) {
    emit PlaySongs(album_tracks_->songs(), i.row(), false);
  });
  layout->addWidget(tracks, 1);
  return page;
}

QWidget* LibraryBrowser::MakeSongsPage() {
  QWidget* page = new QWidget(this);
  QVBoxLayout* layout = new QVBoxLayout(page);
  layout->setContentsMargins(28, 22, 20, 0);
  layout->setSpacing(14);

  QLabel* title = new QLabel(tr("Songs"), page);
  title->setProperty("page_title", true);
  songs_summary_ = new QLabel(page);
  songs_summary_->setForegroundRole(QPalette::PlaceholderText);
  QVBoxLayout* heading = new QVBoxLayout;
  heading->setSpacing(2);
  heading->addWidget(title);
  heading->addWidget(songs_summary_);

  filter_ = new QLineEdit(page);
  filter_->setPlaceholderText(tr("Filter songs"));
  filter_->setClearButtonEnabled(true);
  filter_->setFixedWidth(220);
  connect(filter_, SIGNAL(textChanged(QString)), SLOT(UpdateSongList()));

  QPushButton* play = MakeButton(tr("Play"), "media-playback-start", true);
  QPushButton* shuffle =
      MakeButton(tr("Shuffle"), "media-playlist-shuffle", false);
  connect(play, &QPushButton::clicked, this,
          [this]() { emit PlaySongs(songs_model_->songs(), 0, false); });
  connect(shuffle, &QPushButton::clicked, this,
          [this]() { emit PlaySongs(songs_model_->songs(), 0, true); });

  QHBoxLayout* header = new QHBoxLayout;
  header->setSpacing(10);
  header->addLayout(heading);
  header->addStretch();
  header->addWidget(filter_);
  header->addWidget(play);
  header->addWidget(shuffle);
  layout->addLayout(header);

  // Genre, Artist, Album: each narrows the next, like iTunes' browser.
  QWidget* columns = new QWidget(page);
  columns->setObjectName("column_browser");
  QHBoxLayout* columns_layout = new QHBoxLayout(columns);
  columns_layout->setContentsMargins(0, 0, 0, 0);
  columns_layout->setSpacing(1);
  auto make_column = [&](const QString& name) {
    QWidget* column = new QWidget(columns);
    QVBoxLayout* column_layout = new QVBoxLayout(column);
    column_layout->setContentsMargins(0, 0, 0, 0);
    column_layout->setSpacing(0);
    QLabel* label = new QLabel(name, column);
    label->setProperty("column_heading", true);
    QListWidget* list = new QListWidget(column);
    list->setFrameShape(QFrame::NoFrame);
    list->setUniformItemSizes(true);
    column_layout->addWidget(label);
    column_layout->addWidget(list);
    columns_layout->addWidget(column, 1);
    return list;
  };
  genres_ = make_column(tr("Genres"));
  artists_ = make_column(tr("Artists"));
  albums_ = make_column(tr("Albums"));
  columns->setFixedHeight(190);
  connect(genres_, SIGNAL(currentRowChanged(int)), SLOT(GenreChanged()));
  connect(artists_, SIGNAL(currentRowChanged(int)), SLOT(ArtistChanged()));
  connect(albums_, SIGNAL(currentRowChanged(int)), SLOT(AlbumChanged()));
  layout->addWidget(columns);

  QTreeView* table = MakeSongTable(songs_model_);
  connect(table, &QTreeView::activated, this, [this](const QModelIndex& i) {
    emit PlaySongs(songs_model_->songs(), i.row(), false);
  });
  layout->addWidget(table, 1);
  return page;
}

QTreeView* LibraryBrowser::MakeSongTable(SongTableModel* model) {
  QTreeView* table = new QTreeView(this);
  table->setObjectName("browser_songs");
  table->setModel(model);
  table->setRootIsDecorated(false);
  table->setUniformRowHeights(true);
  table->setAlternatingRowColors(false);
  table->setFrameShape(QFrame::NoFrame);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setSelectionMode(QAbstractItemView::ExtendedSelection);
  table->setAllColumnsShowFocus(true);
  table->setMouseTracking(true);

  QHeaderView* header = table->header();
  header->setStretchLastSection(false);
  header->setSectionResizeMode(QHeaderView::Interactive);
  // Title, artist and album share the width; the numbers keep to theirs.
  for (int column :
       {int(SongTableModel::Column_Title), int(SongTableModel::Column_Artist),
        int(SongTableModel::Column_Album)}) {
    header->setSectionResizeMode(column, QHeaderView::Stretch);
  }
  header->resizeSection(SongTableModel::Column_Track, 44);
  header->resizeSection(SongTableModel::Column_Plays, 56);
  header->resizeSection(SongTableModel::Column_Length, 60);
  return table;
}

void LibraryBrowser::ShowAlbums() { pages_->setCurrentIndex(Page_Albums); }

void LibraryBrowser::ShowSongs() { pages_->setCurrentIndex(Page_Songs); }

LibraryBrowser::Page LibraryBrowser::page() const {
  return Page(pages_->currentIndex());
}

bool LibraryBrowser::ShowAlbum(const QString& title) {
  const QList<BrowserAlbum>& albums = grid_model_->albums();
  for (int row = 0; row < albums.count(); ++row) {
    if (albums[row].title == title) {
      ShowAlbum(row);
      return true;
    }
  }
  return false;
}

bool LibraryBrowser::ShowFirstAlbum() {
  if (grid_model_->albums().isEmpty()) return false;
  ShowAlbum(0);
  return true;
}

void LibraryBrowser::AlbumActivated(const QModelIndex& index) {
  if (index.isValid()) ShowAlbum(index.row());
}

void LibraryBrowser::ShowAlbum(int row) {
  const BrowserAlbum& album = grid_model_->albums()[row];
  open_album_title_ = album.title;
  open_album_artist_ = album.artist;
  album_title_->setText(album.title);

  QStringList meta;
  meta << QString("<b>%1</b>").arg(album.artist.toHtmlEscaped());
  if (album.year > 0) meta << QString::number(album.year);
  if (!album.genre.isEmpty()) meta << album.genre.toHtmlEscaped();
  meta << Count(album.songs.count(), tr("%1 song"), tr("%1 songs"));
  meta << Utilities::PrettyTimeNanosec(album.length_nanosec());
  album_meta_->setText(meta.join(QString::fromUtf8(" · ")));

  album_tracks_->SetSongs(album.songs);
  album_tracks_->SetCurrentSong(current_song_);

  album_cover_->setPixmap(RoundedCover(
      PlaceholderCover(album.title, 8, Appearance::IsDarkPalette(palette())),
      kAlbumCoverSize, devicePixelRatioF()));
  if (!album.songs.isEmpty()) {
    AlbumCoverLoaderOptions options;
    options.desired_height_ = kAlbumCoverSize * 2;
    options.pad_output_image_ = false;
    album_cover_request_ = app_->album_cover_loader()->LoadImageAsync(
        options, album.songs.first());
  }

  pages_->setCurrentIndex(Page_Album);
}

// Loading -------------------------------------------------------------------

LibraryBrowser::LoadResult LibraryBrowser::Load(Application* app) {
  LoadResult result;
  result.songs = app->library_backend()->GetAllSongs();
  const QString various = tr("Various artists");

  QMap<QPair<QString, QString>, BrowserAlbum> albums;
  for (const Song& song : result.songs) {
    if (song.album().isEmpty()) continue;
    const QString artist =
        song.is_compilation() ? various : song.effective_albumartist();
    BrowserAlbum& album = albums[qMakePair(artist.toLower(), song.album())];
    if (album.songs.isEmpty()) {
      album.title = song.album();
      album.artist = artist;
    }
    if (album.genre.isEmpty()) album.genre = song.genre();
    if (song.year() > 0 && (album.year == 0 || song.year() < album.year)) {
      album.year = song.year();
    }
    album.added = qMax(album.added, song.ctime());
    album.songs << song;
  }

  for (BrowserAlbum& album : albums) {
    std::stable_sort(album.songs.begin(), album.songs.end(),
                     [](const Song& a, const Song& b) {
                       if (a.disc() != b.disc()) return a.disc() < b.disc();
                       return a.track() < b.track();
                     });
    result.albums << album;
  }
  return result;
}

void LibraryBrowser::Reload() {
  if (watcher_->isRunning()) {
    // Try again when this one's done.
    reload_timer_->start();
    return;
  }
  watcher_->setFuture(QtConcurrent::run(&LibraryBrowser::Load, app_));
}

void LibraryBrowser::Loaded() {
  const LoadResult result = watcher_->result();
  songs_ = result.songs;

  grid_model_->SetAlbums(result.albums);
  Sort();

  // Keep the open album open, with its songs as they are now.
  bool found = false;
  const QList<BrowserAlbum>& albums = grid_model_->albums();
  for (const BrowserAlbum& album : albums) {
    if (album.title == open_album_title_ &&
        album.artist == open_album_artist_) {
      album_tracks_->SetSongs(album.songs);
      found = true;
      break;
    }
  }
  if (!found && pages_->currentIndex() == Page_Album) ShowAlbums();

  QSet<QString> artists;
  for (const BrowserAlbum& album : albums) artists << album.artist;
  albums_summary_->setText(
      Count(albums.count(), tr("%1 album"), tr("%1 albums")) +
      QString::fromUtf8(" · ") +
      Count(artists.count(), tr("%1 artist"), tr("%1 artists")));

  // The column browser, keeping what was picked where it still exists.
  QSet<QString> genres;
  for (const Song& song : songs_) {
    if (!song.genre().isEmpty()) genres << song.genre();
  }
  QStringList genre_list = genres.values();
  genre_list.sort(Qt::CaseInsensitive);
  FillColumn(
      genres_, genre_list,
      Count(genre_list.count(), tr("All (%1 genre)"), tr("All (%1 genres)")));
}

void LibraryBrowser::Sort() {
  QList<BrowserAlbum> albums = grid_model_->albums();
  auto by_artist = [](const BrowserAlbum& a, const BrowserAlbum& b) {
    const int c = QString::localeAwareCompare(a.artist, b.artist);
    if (c != 0) return c < 0;
    if (a.year != b.year) return a.year < b.year;
    return QString::localeAwareCompare(a.title, b.title) < 0;
  };
  switch (sort_->currentIndex()) {
    case 1:
      std::stable_sort(albums.begin(), albums.end(),
                       [](const BrowserAlbum& a, const BrowserAlbum& b) {
                         return QString::localeAwareCompare(a.title, b.title) <
                                0;
                       });
      break;
    case 2:
      std::stable_sort(albums.begin(), albums.end(),
                       [](const BrowserAlbum& a, const BrowserAlbum& b) {
                         return a.year < b.year;
                       });
      break;
    case 3:
      std::stable_sort(albums.begin(), albums.end(),
                       [](const BrowserAlbum& a, const BrowserAlbum& b) {
                         return a.added > b.added;
                       });
      break;
    default:
      std::stable_sort(albums.begin(), albums.end(), by_artist);
      break;
  }
  grid_model_->SetAlbums(albums);
}

void LibraryBrowser::CurrentSongChanged(const Song& song) {
  current_song_ = song;
  album_tracks_->SetCurrentSong(song);
  songs_model_->SetCurrentSong(song);
}

// The column browser ---------------------------------------------------------

void LibraryBrowser::FillColumn(QListWidget* list, const QStringList& values,
                                const QString& all_text) {
  const QString previous =
      list->currentRow() > 0 ? list->currentItem()->text() : QString();
  list->blockSignals(true);
  list->clear();
  list->addItem(all_text);
  list->addItems(values);
  const int keep = values.indexOf(previous);
  list->setCurrentRow(keep >= 0 ? keep + 1 : 0);
  list->blockSignals(false);

  // Each column narrows the next.
  if (list == genres_) {
    GenreChanged();
  } else if (list == artists_) {
    ArtistChanged();
  } else {
    AlbumChanged();
  }
}

void LibraryBrowser::GenreChanged() {
  const QString genre =
      genres_->currentRow() > 0 ? genres_->currentItem()->text() : QString();
  QSet<QString> artists;
  for (const Song& song : songs_) {
    if (!genre.isEmpty() && song.genre() != genre) continue;
    artists << song.effective_albumartist();
  }
  QStringList list = artists.values();
  list.sort(Qt::CaseInsensitive);
  FillColumn(
      artists_, list,
      Count(list.count(), tr("All (%1 artist)"), tr("All (%1 artists)")));
}

void LibraryBrowser::ArtistChanged() {
  const QString genre =
      genres_->currentRow() > 0 ? genres_->currentItem()->text() : QString();
  const QString artist =
      artists_->currentRow() > 0 ? artists_->currentItem()->text() : QString();
  QSet<QString> albums;
  for (const Song& song : songs_) {
    if (!genre.isEmpty() && song.genre() != genre) continue;
    if (!artist.isEmpty() && song.effective_albumartist() != artist) continue;
    if (!song.album().isEmpty()) albums << song.album();
  }
  QStringList list = albums.values();
  list.sort(Qt::CaseInsensitive);
  FillColumn(albums_, list,
             Count(list.count(), tr("All (%1 album)"), tr("All (%1 albums)")));
}

void LibraryBrowser::AlbumChanged() { UpdateSongList(); }

SongList LibraryBrowser::FilteredSongs() const {
  const QString genre =
      genres_->currentRow() > 0 ? genres_->currentItem()->text() : QString();
  const QString artist =
      artists_->currentRow() > 0 ? artists_->currentItem()->text() : QString();
  const QString album =
      albums_->currentRow() > 0 ? albums_->currentItem()->text() : QString();
  const QString filter = filter_->text().trimmed();

  SongList songs;
  for (const Song& song : songs_) {
    if (!genre.isEmpty() && song.genre() != genre) continue;
    if (!artist.isEmpty() && song.effective_albumartist() != artist) continue;
    if (!album.isEmpty() && song.album() != album) continue;
    if (!filter.isEmpty() &&
        !song.PrettyTitle().contains(filter, Qt::CaseInsensitive) &&
        !song.artist().contains(filter, Qt::CaseInsensitive) &&
        !song.album().contains(filter, Qt::CaseInsensitive)) {
      continue;
    }
    songs << song;
  }

  std::stable_sort(songs.begin(), songs.end(),
                   [](const Song& a, const Song& b) {
                     int c = QString::localeAwareCompare(
                         a.effective_albumartist(), b.effective_albumartist());
                     if (c != 0) return c < 0;
                     c = QString::localeAwareCompare(a.album(), b.album());
                     if (c != 0) return c < 0;
                     if (a.disc() != b.disc()) return a.disc() < b.disc();
                     return a.track() < b.track();
                   });
  return songs;
}

void LibraryBrowser::UpdateSongList() {
  const SongList songs = FilteredSongs();
  songs_model_->SetSongs(songs);
  songs_model_->SetCurrentSong(current_song_);

  qint64 total = 0;
  for (const Song& song : songs)
    total += qMax(qint64(0), song.length_nanosec());
  songs_summary_->setText(Count(songs.count(), tr("%1 song"), tr("%1 songs")) +
                          QString::fromUtf8(" · ") +
                          Utilities::PrettyTimeNanosec(total));
}
