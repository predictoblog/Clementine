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
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMap>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>
#include <QtConcurrentRun>
#include <algorithm>
#include <tuple>

#include "core/appearance.h"
#include "core/application.h"
#include "core/listening.h"
#include "core/timeconstants.h"
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

QImage BrowserPlaceholderCover(const QString& title, bool dark) {
  return PlaceholderCover(title, 8, dark);
}

QPixmap BrowserRoundedCover(const QImage& image, int size, qreal dpr) {
  return RoundedCover(image, size, dpr);
}

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
        case Column_Missing:
          return LibraryBrowser::MissingTags(song).join(", ");
        case Column_Folder:
          return QDir::toNativeSeparators(
              QFileInfo(song.url().toLocalFile()).path());
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
    case Column_Missing:
      return tr("Missing");
    case Column_Folder:
      return tr("Folder");
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
    case Role_Progress:
      return album.progress;
    case Role_Status:
      return album.status;
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
  return QSize(AlbumGridModel::kCoverSize, AlbumGridModel::kCoverSize + 10 +
                                               line * (status_line_ ? 3 : 2) +
                                               6 + (status_line_ ? 2 : 0));
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

  // How far through, along the foot of the cover.
  const double progress = index.data(AlbumGridModel::Role_Progress).toDouble();
  if (progress >= 0) {
    const QRectF track(cover_rect.left() + 10, cover_rect.bottom() - 14,
                       size - 20, 4);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(0, 0, 0, 110));
    painter->drawRoundedRect(track, 2, 2);
    if (progress > 0) {
      QRectF done = track;
      done.setWidth(qMax(4.0, track.width() * qMin(1.0, progress)));
      painter->setBrush(Appearance::AccentColor(option.palette));
      painter->drawRoundedRect(done, 2, 2);
    }
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

  const QString status = index.data(AlbumGridModel::Role_Status).toString();
  if (status_line_ && !status.isEmpty()) {
    text.translate(0, line + 2);
    painter->setPen(progress > 0 && progress < 1
                        ? Appearance::AccentColor(option.palette)
                        : Appearance::QuietTextColor(option.palette));
    painter->drawText(
        text, Qt::AlignLeft | Qt::AlignVCenter,
        option.fontMetrics.elidedText(status, Qt::ElideRight, size));
  }

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
      songs_summary_(nullptr),
      attention_count_(0),
      attention_albums_(new AlbumGridModel(app, this)),
      attention_songs_(new SongTableModel(this)),
      attention_summary_(nullptr),
      attention_all_clear_(nullptr),
      attention_albums_box_(nullptr),
      attention_albums_label_(nullptr),
      attention_songs_box_(nullptr),
      attention_songs_label_(nullptr),
      attention_table_(nullptr) {
  setObjectName("library_browser");
  setAttribute(Qt::WA_StyledBackground);

  QVBoxLayout* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(pages_);

  pages_->addWidget(MakeAlbumsPage());
  pages_->addWidget(MakeAlbumPage());
  pages_->addWidget(MakeSongsPage());
  pages_->addWidget(MakeAttentionPage());
  pages_->addWidget(MakeSmartPage());

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

  grid_ = MakeAlbumGrid(grid_model_);
  connect(grid_, SIGNAL(clicked(QModelIndex)),
          SLOT(AlbumActivated(QModelIndex)));
  connect(grid_, SIGNAL(activated(QModelIndex)),
          SLOT(AlbumActivated(QModelIndex)));
  layout->addWidget(grid_, 1);
  return page;
}

QListView* LibraryBrowser::MakeAlbumGrid(AlbumGridModel* model) {
  QListView* grid_ = new QListView(this);
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
  grid_->setModel(model);
  grid_->setItemDelegate(new AlbumGridDelegate(grid_));
  grid_->setCursor(Qt::PointingHandCursor);
  grid_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(grid_, &QWidget::customContextMenuRequested, this,
          [this, grid_, model](const QPoint& pos) {
            ShowAlbumMenu(grid_, model, pos);
          });
  return grid_;
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
  table->setColumnHidden(SongTableModel::Column_Missing, true);
  table->setColumnHidden(SongTableModel::Column_Folder, true);

  table->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(table, &QWidget::customContextMenuRequested, this,
          [this, table, model](const QPoint& pos) {
            ShowSongMenu(table, model, pos);
          });
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

void LibraryBrowser::ShowAttention() {
  pages_->setCurrentIndex(Page_Attention);
}

void LibraryBrowser::ShowSmart(SmartView view) {
  smart_view_ = view;
  UpdateSmart();
  pages_->setCurrentIndex(Page_Smart);
}

QWidget* LibraryBrowser::MakeSmartPage() {
  QWidget* page = new QWidget(this);
  QVBoxLayout* layout = new QVBoxLayout(page);
  layout->setContentsMargins(28, 22, 20, 0);
  layout->setSpacing(14);

  smart_title_ = new QLabel(page);
  smart_title_->setProperty("page_title", true);
  smart_summary_ = new QLabel(page);
  smart_summary_->setForegroundRole(QPalette::PlaceholderText);
  QVBoxLayout* heading = new QVBoxLayout;
  heading->setSpacing(2);
  heading->addWidget(smart_title_);
  heading->addWidget(smart_summary_);

  smart_songs_ = new SongTableModel(this);
  QPushButton* play = MakeButton(tr("Play"), "media-playback-start", true);
  QPushButton* shuffle =
      MakeButton(tr("Shuffle"), "media-playlist-shuffle", false);
  connect(play, &QPushButton::clicked, this,
          [this]() { emit PlaySongs(smart_songs_->songs(), 0, false); });
  connect(shuffle, &QPushButton::clicked, this,
          [this]() { emit PlaySongs(smart_songs_->songs(), 0, true); });

  QHBoxLayout* header = new QHBoxLayout;
  header->setSpacing(10);
  header->addLayout(heading);
  header->addStretch();
  header->addWidget(play, 0, Qt::AlignTop);
  header->addWidget(shuffle, 0, Qt::AlignTop);
  layout->addLayout(header);

  smart_empty_ = new QLabel(page);
  smart_empty_->setWordWrap(true);
  smart_empty_->setForegroundRole(QPalette::PlaceholderText);
  layout->addWidget(smart_empty_);

  smart_table_ = MakeSongTable(smart_songs_);
  smart_table_->setColumnHidden(SongTableModel::Column_Track, true);
  connect(smart_table_, &QTreeView::activated, this,
          [this](const QModelIndex& i) {
            emit PlaySongs(smart_songs_->songs(), i.row(), false);
          });
  layout->addWidget(smart_table_, 1);
  return page;
}

void LibraryBrowser::UpdateSmart() {
  if (!smart_table_) return;
  const int kLimit = 100;
  SongList songs;
  QString title, empty;

  switch (smart_view_) {
    case Smart_RecentlyAdded:
      title = tr("Recently added");
      empty = tr("Songs you add to your library show up here.");
      songs = songs_;
      std::stable_sort(
          songs.begin(), songs.end(),
          [](const Song& a, const Song& b) { return a.ctime() > b.ctime(); });
      break;
    case Smart_MostPlayed:
      title = tr("Most played");
      empty = tr("The songs you play most show up here.");
      for (const Song& song : songs_) {
        if (song.playcount() > 0) songs << song;
      }
      std::stable_sort(songs.begin(), songs.end(),
                       [](const Song& a, const Song& b) {
                         return a.playcount() > b.playcount();
                       });
      break;
    case Smart_RecentlyPlayed:
      title = tr("Recently played");
      empty = tr("Songs you've played show up here, the latest first.");
      for (const Song& song : songs_) {
        if (song.lastplayed() > 0) songs << song;
      }
      std::stable_sort(songs.begin(), songs.end(),
                       [](const Song& a, const Song& b) {
                         return a.lastplayed() > b.lastplayed();
                       });
      break;
    case Smart_Favorites:
      title = tr("Favorites");
      empty =
          tr("Songs you rate four stars or more, or love with the heart "
             "in the player bar, show up here.");
      for (const Song& song : songs_) {
        if (song.rating() >= 0.8f) songs << song;
      }
      std::stable_sort(
          songs.begin(), songs.end(), [](const Song& a, const Song& b) {
            return std::make_tuple(a.artist(), a.album(), a.disc(), a.track()) <
                   std::make_tuple(b.artist(), b.album(), b.disc(), b.track());
          });
      break;
  }
  songs = songs.mid(0, kLimit);

  smart_title_->setText(title);
  smart_summary_->setText(
      Count(songs.count(), tr("%1 song"), tr("%1 songs")) +
      (songs.isEmpty() ? QString()
                       : QString::fromUtf8(" · ") +
                             Utilities::PrettyTimeNanosec([&songs]() {
                               qint64 total = 0;
                               for (const Song& s : songs)
                                 total += qMax<qint64>(0, s.length_nanosec());
                               return total;
                             }())));
  smart_empty_->setText(empty);
  smart_empty_->setVisible(songs.isEmpty());
  smart_table_->setVisible(!songs.isEmpty());
  smart_table_->setColumnHidden(SongTableModel::Column_Plays,
                                smart_view_ != Smart_MostPlayed);
  smart_songs_->SetSongs(songs);
  smart_songs_->SetCurrentSong(current_song_);
}

QStringList LibraryBrowser::MissingTags(const Song& song) {
  QStringList missing;
  if (song.title().isEmpty()) missing << tr("title");
  if (song.artist().isEmpty() && song.albumartist().isEmpty()) {
    missing << tr("artist");
  }
  if (song.album().isEmpty()) missing << tr("album");
  if (song.track() <= 0) missing << tr("track");
  if (song.year() <= 0) missing << tr("year");
  return missing;
}

bool LibraryBrowser::HasCoverArt(const Song& song) {
  return !song.art_automatic().isEmpty() || !song.art_manual().isEmpty();
}

void LibraryBrowser::ShowSongMenu(QTreeView* table, SongTableModel* model,
                                  const QPoint& pos) {
  // The selected songs, or the one under the pointer.
  SongList songs;
  for (const QModelIndex& index : table->selectionModel()->selectedRows()) {
    songs << model->songs()[index.row()];
  }
  const QModelIndex under = table->indexAt(pos);
  if (songs.isEmpty() && under.isValid()) songs << model->songs()[under.row()];
  if (songs.isEmpty()) return;
  const int start = under.isValid() ? under.row() : 0;

  QMenu menu(this);
  menu.addAction(IconLoader::Load("media-playback-start", IconLoader::Base),
                 tr("Play"), this, [this, model, start, songs]() {
                   if (songs.count() == 1) {
                     emit PlaySongs(model->songs(), start, false);
                   } else {
                     emit PlaySongs(songs, 0, false);
                   }
                 });
  menu.addAction(IconLoader::Load("list-add", IconLoader::Base),
                 tr("Add to queue"), this,
                 [this, songs]() { emit QueueSongs(songs); });
  menu.addSeparator();
  menu.addAction(IconLoader::Load("edit-rename", IconLoader::Base),
                 tr("Edit info..."), this,
                 [this, songs]() { emit EditSongs(songs); });
  menu.addAction(IconLoader::Load("tools-wizard", IconLoader::Base),
                 tr("Look up tags on MusicBrainz..."), this,
                 [this, songs]() { emit FixTags(songs); });
  menu.exec(table->viewport()->mapToGlobal(pos));
}

void LibraryBrowser::ShowAlbumMenu(QListView* grid, AlbumGridModel* model,
                                   const QPoint& pos) {
  const QModelIndex index = grid->indexAt(pos);
  if (!index.isValid()) return;
  const SongList songs = model->albums()[index.row()].songs;

  QMenu menu(this);
  menu.addAction(IconLoader::Load("media-playback-start", IconLoader::Base),
                 tr("Play"), this,
                 [this, songs]() { emit PlaySongs(songs, 0, false); });
  menu.addAction(IconLoader::Load("media-playlist-shuffle", IconLoader::Base),
                 tr("Shuffle"), this,
                 [this, songs]() { emit PlaySongs(songs, 0, true); });
  menu.addAction(IconLoader::Load("list-add", IconLoader::Base),
                 tr("Add to queue"), this,
                 [this, songs]() { emit QueueSongs(songs); });
  menu.addSeparator();
  menu.addAction(IconLoader::Load("edit-rename", IconLoader::Base),
                 tr("Edit info..."), this,
                 [this, songs]() { emit EditSongs(songs); });
  menu.addAction(IconLoader::Load("tools-wizard", IconLoader::Base),
                 tr("Look up tags on MusicBrainz..."), this,
                 [this, songs]() { emit FixTags(songs); });
  menu.exec(grid->viewport()->mapToGlobal(pos));
}

QWidget* LibraryBrowser::MakeAttentionPage() {
  QWidget* page = new QWidget(this);
  QVBoxLayout* layout = new QVBoxLayout(page);
  layout->setContentsMargins(28, 22, 20, 0);
  layout->setSpacing(14);

  QLabel* title = new QLabel(tr("Needs attention"), page);
  title->setProperty("page_title", true);
  attention_summary_ = new QLabel(page);
  attention_summary_->setForegroundRole(QPalette::PlaceholderText);
  QVBoxLayout* heading = new QVBoxLayout;
  heading->setSpacing(2);
  heading->addWidget(title);
  heading->addWidget(attention_summary_);
  layout->addLayout(heading);

  attention_all_clear_ = new QLabel(
      tr("Everything's in order: every album has cover art, and every song "
         "has a title, artist, album, track number and year."),
      page);
  attention_all_clear_->setWordWrap(true);
  attention_all_clear_->setForegroundRole(QPalette::PlaceholderText);
  layout->addWidget(attention_all_clear_);

  // Albums without cover art, in a strip.
  attention_albums_box_ = new QWidget(page);
  QVBoxLayout* albums = new QVBoxLayout(attention_albums_box_);
  albums->setContentsMargins(0, 0, 0, 0);
  albums->setSpacing(8);
  attention_albums_label_ = new QLabel(attention_albums_box_);
  attention_albums_label_->setProperty("section_title", true);
  QPushButton* find_covers =
      MakeButton(tr("Find covers..."), "edit-find", false);
  connect(find_covers, &QPushButton::clicked, this,
          &LibraryBrowser::FindCovers);
  QHBoxLayout* albums_header = new QHBoxLayout;
  albums_header->addWidget(attention_albums_label_);
  albums_header->addStretch();
  albums_header->addWidget(find_covers);
  albums->addLayout(albums_header);
  QListView* strip = MakeAlbumGrid(attention_albums_);
  strip->setFlow(QListView::LeftToRight);
  strip->setWrapping(false);
  strip->setFixedHeight(AlbumGridModel::kCoverSize +
                        strip->fontMetrics().height() * 2 + 44);
  albums->addWidget(strip);
  layout->addWidget(attention_albums_box_);

  // Songs missing tags.
  attention_songs_box_ = new QWidget(page);
  QVBoxLayout* songs = new QVBoxLayout(attention_songs_box_);
  songs->setContentsMargins(0, 0, 0, 0);
  songs->setSpacing(8);
  attention_songs_label_ = new QLabel(attention_songs_box_);
  attention_songs_label_->setProperty("section_title", true);
  QPushButton* fix =
      MakeButton(tr("Fix with MusicBrainz..."), "tools-wizard", true);
  QPushButton* edit = MakeButton(tr("Edit info..."), "edit-rename", false);
  QHBoxLayout* songs_header = new QHBoxLayout;
  songs_header->setSpacing(10);
  songs_header->addWidget(attention_songs_label_);
  songs_header->addStretch();
  songs_header->addWidget(fix);
  songs_header->addWidget(edit);
  songs->addLayout(songs_header);
  attention_table_ = MakeSongTable(attention_songs_);
  attention_table_->setColumnHidden(SongTableModel::Column_Missing, false);
  attention_table_->setColumnHidden(SongTableModel::Column_Plays, true);
  attention_table_->header()->setSectionResizeMode(
      SongTableModel::Column_Missing, QHeaderView::Stretch);
  connect(attention_table_, &QTreeView::activated, this,
          [this](const QModelIndex& i) {
            emit EditSongs(SongList() << attention_songs_->songs()[i.row()]);
          });
  songs->addWidget(attention_table_, 1);
  layout->addWidget(attention_songs_box_, 1);

  // The same song more than once.
  attention_duplicates_ = new SongTableModel(this);
  attention_duplicates_box_ = new QWidget(page);
  QVBoxLayout* duplicates = new QVBoxLayout(attention_duplicates_box_);
  duplicates->setContentsMargins(0, 0, 0, 0);
  duplicates->setSpacing(8);
  attention_duplicates_label_ = new QLabel(attention_duplicates_box_);
  attention_duplicates_label_->setProperty("section_title", true);
  QLabel* duplicates_hint = new QLabel(
      tr("The same artist and title, about the same length. Right-click to "
         "play or edit one."),
      attention_duplicates_box_);
  duplicates_hint->setForegroundRole(QPalette::PlaceholderText);
  duplicates->addWidget(attention_duplicates_label_);
  duplicates->addWidget(duplicates_hint);
  QTreeView* duplicates_table = MakeSongTable(attention_duplicates_);
  duplicates_table->setColumnHidden(SongTableModel::Column_Plays, true);
  duplicates_table->setColumnHidden(SongTableModel::Column_Track, true);
  duplicates_table->setColumnHidden(SongTableModel::Column_Folder, false);
  duplicates_table->setTextElideMode(Qt::ElideMiddle);
  duplicates_table->header()->setSectionResizeMode(
      SongTableModel::Column_Folder, QHeaderView::Stretch);
  duplicates->addWidget(duplicates_table, 1);
  layout->addWidget(attention_duplicates_box_, 1);

  // Both act on the selected songs, or all of them if none are.
  auto chosen = [this]() {
    SongList songs;
    for (const QModelIndex& index :
         attention_table_->selectionModel()->selectedRows()) {
      songs << attention_songs_->songs()[index.row()];
    }
    return songs.isEmpty() ? attention_songs_->songs() : songs;
  };
  connect(fix, &QPushButton::clicked, this,
          [this, chosen]() { emit FixTags(chosen()); });
  connect(edit, &QPushButton::clicked, this,
          [this, chosen]() { emit EditSongs(chosen()); });

  layout->addStretch(0);
  return page;
}

void LibraryBrowser::UpdateAttention() {
  QList<BrowserAlbum> without_art;
  for (const BrowserAlbum& album : grid_model_->albums()) {
    bool has_art = false;
    for (const Song& song : album.songs) has_art = has_art || HasCoverArt(song);
    if (!has_art) without_art << album;
  }
  SongList untagged;
  for (const Song& song : songs_) {
    if (!MissingTags(song).isEmpty()) untagged << song;
  }

  const QList<SongList> groups = FindDuplicates(songs_);
  SongList duplicates;
  for (const SongList& group : groups) duplicates << group;

  attention_albums_->SetAlbums(without_art);
  attention_songs_->SetSongs(untagged);
  attention_duplicates_->SetSongs(duplicates);
  attention_duplicates_box_->setVisible(!groups.isEmpty());
  attention_duplicates_label_->setText(Count(groups.count(),
                                             tr("%1 possible duplicate"),
                                             tr("%1 possible duplicates")));

  attention_albums_box_->setVisible(!without_art.isEmpty());
  attention_albums_label_->setText(Count(without_art.count(),
                                         tr("%1 album without cover art"),
                                         tr("%1 albums without cover art")));
  attention_songs_box_->setVisible(!untagged.isEmpty());
  attention_songs_label_->setText(Count(untagged.count(),
                                        tr("%1 song with missing tags"),
                                        tr("%1 songs with missing tags")));

  const int count = without_art.count() + untagged.count() + groups.count();
  attention_all_clear_->setVisible(count == 0);
  attention_summary_->setText(
      count == 0 ? tr("Nothing to fix")
                 : tr("Fix these and your library looks and sorts the way it "
                      "should"));
  if (count != attention_count_) {
    attention_count_ = count;
    emit AttentionCountChanged(count);
  }
}

QList<SongList> LibraryBrowser::FindDuplicates(const SongList& songs) {
  static const QRegularExpression kNotWord("[^\\w]+");
  auto normal = [](const QString& text) {
    return text.toLower().remove(kNotWord);
  };
  QMap<QString, SongList> by_name;
  for (const Song& song : songs) {
    if (song.title().isEmpty() || song.artist().isEmpty()) continue;
    by_name[normal(song.artist()) + "\n" + normal(song.title())] << song;
  }

  const qint64 tolerance = 3 * kNsecPerSec;
  QList<SongList> groups;
  for (SongList same : by_name) {
    if (same.count() < 2) continue;
    std::sort(same.begin(), same.end(), [](const Song& a, const Song& b) {
      return a.length_nanosec() < b.length_nanosec();
    });
    // Runs of songs whose lengths are close: a live take and the studio
    // one share a title but aren't duplicates.
    SongList run;
    for (const Song& song : same) {
      if (!run.isEmpty() &&
          song.length_nanosec() - run.last().length_nanosec() > tolerance) {
        if (run.count() > 1) groups << run;
        run.clear();
      }
      run << song;
    }
    if (run.count() > 1) groups << run;
  }
  return groups;
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

LibraryBrowser::LoadResult LibraryBrowser::Load(
    Application* app, const QStringList& audiobook_folders) {
  LoadResult result;
  for (const Song& song : app->library_backend()->GetAllSongs()) {
    if (ListeningController::IsAudiobook(song, audiobook_folders)) {
      result.audiobooks << song;
    } else {
      result.songs << song;
    }
  }
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

void LibraryBrowser::SetAudiobookFolders(const QStringList& folders) {
  if (folders == audiobook_folders_) return;
  audiobook_folders_ = folders;
  Reload();
}

void LibraryBrowser::Reload() {
  if (watcher_->isRunning()) {
    // Try again when this one's done.
    reload_timer_->start();
    return;
  }
  watcher_->setFuture(
      QtConcurrent::run(&LibraryBrowser::Load, app_, audiobook_folders_));
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
  UpdateAttention();
  UpdateSmart();
  emit AudiobooksLoaded(result.audiobooks);

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
