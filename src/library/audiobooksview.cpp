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

#include "audiobooksview.h"

#include <QButtonGroup>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListView>
#include <QMap>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>

#include "core/appearance.h"
#include "core/application.h"
#include "core/listening.h"
#include "core/timeconstants.h"
#include "core/utilities.h"
#include "covers/albumcoverloader.h"
#include "covers/albumcoverloaderoptions.h"
#include "library/librarybrowser.h"
#include "library/librarydirectorymodel.h"
#include "ui/iconloader.h"

namespace {

const int kHeroCoverSize = 132;
const int kBookCoverSize = 200;

enum Filter { Filter_All, Filter_InProgress, Filter_NotStarted, Filter_Done };

// "5 h 37 m", "42 m": how long, at a glance.
QString HoursMinutes(qint64 seconds) {
  const qint64 minutes = (seconds + 30) / 60;
  if (minutes < 60) return QObject::tr("%1 m").arg(qMax<qint64>(minutes, 1));
  return QObject::tr("%1 h %2 m").arg(minutes / 60).arg(minutes % 60);
}

QString Count(int n, const QString& one, const QString& many) {
  return (n == 1 ? one : many).arg(n);
}

}  // namespace

double AudiobooksView::Book::progress() const {
  if (state == Finished) return 1.0;
  if (length <= 0) return 0.0;
  return qBound(0.0, double(elapsed) / double(length), 1.0);
}

QString AudiobooksView::Book::StatusText() const {
  switch (state) {
    case NotStarted:
      return tr("Not started");
    case Finished:
      return tr("Finished");
    case InProgress:
      break;
  }
  return tr("%1%").arg(int(progress() * 100)) + QString::fromUtf8(" · ") +
         tr("%1 left").arg(HoursMinutes(qMax<qint64>(0, length - elapsed)));
}

AudiobooksView::AudiobooksView(Application* app, ListeningController* listening,
                               QWidget* parent)
    : QWidget(parent),
      app_(app),
      listening_(listening),
      pages_(new QStackedWidget(this)),
      rebuild_timer_(new QTimer(this)),
      hero_cover_request_(0),
      grid_model_(new AlbumGridModel(app, this)),
      book_cover_request_(0) {
  setObjectName("library_browser");
  setAttribute(Qt::WA_StyledBackground);

  rebuild_timer_->setSingleShot(true);
  rebuild_timer_->setInterval(1000);
  connect(rebuild_timer_, SIGNAL(timeout()), SLOT(Rebuild()));
  connect(listening_, SIGNAL(ProgressChanged()), rebuild_timer_, SLOT(start()));

  pages_->addWidget(MakeBooksPage());
  pages_->addWidget(MakeBookPage());

  QVBoxLayout* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(pages_);

  connect(app_->album_cover_loader(),
          qOverload<quint64, const QImage&>(&AlbumCoverLoader::ImageLoaded),
          this, [this](quint64 id, const QImage& image) {
            if (image.isNull()) return;
            if (id == hero_cover_request_) {
              hero_cover_->setPixmap(BrowserRoundedCover(image, kHeroCoverSize,
                                                         devicePixelRatioF()));
            } else if (id == book_cover_request_) {
              book_cover_->setPixmap(BrowserRoundedCover(image, kBookCoverSize,
                                                         devicePixelRatioF()));
            }
          });

  Rebuild();
}

QPushButton* AudiobooksView::MakeButton(const QString& text,
                                        const QString& icon, bool primary) {
  QPushButton* button = new QPushButton(text, this);
  if (!icon.isEmpty()) {
    button->setIcon(IconLoader::Load(icon, IconLoader::Base));
  }
  button->setProperty(primary ? "primary" : "pill", true);
  button->setCursor(Qt::PointingHandCursor);
  return button;
}

QWidget* AudiobooksView::MakeBooksPage() {
  QWidget* page = new QWidget(this);
  QVBoxLayout* layout = new QVBoxLayout(page);
  layout->setContentsMargins(28, 22, 20, 0);
  layout->setSpacing(14);

  // Heading
  QLabel* title = new QLabel(tr("Audiobooks"), page);
  title->setProperty("page_title", true);
  summary_ = new QLabel(page);
  summary_->setForegroundRole(QPalette::PlaceholderText);
  QVBoxLayout* heading = new QVBoxLayout;
  heading->setSpacing(2);
  heading->addWidget(title);
  heading->addWidget(summary_);
  QPushButton* add = MakeButton(tr("Add folder..."), "folder-new", false);
  connect(add, SIGNAL(clicked()), SLOT(AddFolder()));
  QHBoxLayout* header = new QHBoxLayout;
  header->addLayout(heading);
  header->addStretch();
  header->addWidget(add, 0, Qt::AlignTop);
  layout->addLayout(header);

  // With no books yet: what makes something a book here.
  empty_ = new QWidget(page);
  QVBoxLayout* empty_layout = new QVBoxLayout(empty_);
  empty_layout->setContentsMargins(0, 40, 0, 0);
  QLabel* empty_title = new QLabel(tr("No audiobooks yet"), empty_);
  empty_title->setProperty("section_title", true);
  empty_title->setAlignment(Qt::AlignCenter);
  QLabel* empty_text = new QLabel(
      tr("Add the folder your audiobooks are in, and each book shows up here "
         "with where you left off.\nFiles tagged with the genre Audiobook, "
         "and .m4b files, are found anywhere in your library."),
      empty_);
  empty_text->setForegroundRole(QPalette::PlaceholderText);
  empty_text->setAlignment(Qt::AlignCenter);
  empty_text->setWordWrap(true);
  QPushButton* empty_add =
      MakeButton(tr("Add audiobook folder..."), "folder-new", true);
  connect(empty_add, SIGNAL(clicked()), SLOT(AddFolder()));
  empty_layout->addWidget(empty_title);
  empty_layout->addWidget(empty_text);
  empty_layout->addSpacing(8);
  empty_layout->addWidget(empty_add, 0, Qt::AlignCenter);
  empty_layout->addStretch();
  layout->addWidget(empty_, 1);

  content_ = new QWidget(page);
  QVBoxLayout* content = new QVBoxLayout(content_);
  content->setContentsMargins(0, 0, 0, 0);
  content->setSpacing(12);
  layout->addWidget(content_, 1);

  // Continue listening: the book you were last partway through.
  hero_ = new QFrame(content_);
  hero_->setObjectName("listening_hero");
  hero_->setAttribute(Qt::WA_StyledBackground);
  hero_cover_ = new QLabel(hero_);
  hero_cover_->setFixedSize(kHeroCoverSize, kHeroCoverSize);
  QLabel* hero_caption = new QLabel(tr("Continue listening"), hero_);
  hero_caption->setProperty("caption", true);
  hero_title_ = new QLabel(hero_);
  hero_title_->setProperty("section_title", true);
  hero_author_ = new QLabel(hero_);
  hero_author_->setForegroundRole(QPalette::PlaceholderText);
  hero_chapter_ = new QLabel(hero_);
  hero_progress_ = new QProgressBar(hero_);
  hero_progress_->setObjectName("listening_progress");
  hero_progress_->setTextVisible(false);
  hero_progress_->setRange(0, 1000);
  hero_progress_->setFixedHeight(6);
  hero_progress_->setMaximumWidth(420);
  hero_status_ = new QLabel(hero_);
  hero_status_->setForegroundRole(QPalette::PlaceholderText);
  QPushButton* resume = MakeButton(tr("Resume"), "media-playback-start", true);
  QPushButton* details =
      MakeButton(tr("Chapters"), "view-media-playlist", false);
  connect(resume, &QPushButton::clicked, this, [this]() {
    if (const Book* book = FindBook(hero_key_)) Play(*book, book->chapter);
  });
  connect(details, &QPushButton::clicked, this,
          [this]() { ShowBook(hero_key_); });

  QHBoxLayout* hero_buttons = new QHBoxLayout;
  hero_buttons->setSpacing(10);
  hero_buttons->addWidget(resume);
  hero_buttons->addWidget(details);
  hero_buttons->addStretch();
  QVBoxLayout* hero_text = new QVBoxLayout;
  hero_text->setSpacing(4);
  hero_text->addWidget(hero_caption);
  hero_text->addWidget(hero_title_);
  hero_text->addWidget(hero_author_);
  hero_text->addSpacing(6);
  hero_text->addWidget(hero_chapter_);
  hero_text->addWidget(hero_progress_);
  hero_text->addWidget(hero_status_);
  hero_text->addStretch();
  hero_text->addLayout(hero_buttons);
  QHBoxLayout* hero_layout = new QHBoxLayout(hero_);
  hero_layout->setContentsMargins(16, 16, 16, 16);
  hero_layout->setSpacing(20);
  hero_layout->addWidget(hero_cover_, 0, Qt::AlignTop);
  hero_layout->addLayout(hero_text, 1);
  content->addWidget(hero_);

  // Your books, with the filters beside the heading.
  QLabel* books_title = new QLabel(tr("Your books"), content_);
  books_title->setProperty("section_title", true);
  QHBoxLayout* books_header = new QHBoxLayout;
  books_header->setSpacing(8);
  books_header->addWidget(books_title);
  books_header->addSpacing(12);
  filters_ = new QButtonGroup(this);
  const QStringList filter_names = {tr("All"), tr("In progress"),
                                    tr("Not started"), tr("Finished")};
  for (int i = 0; i < filter_names.count(); ++i) {
    QPushButton* filter = new QPushButton(filter_names[i], content_);
    filter->setProperty("filter", true);
    filter->setCheckable(true);
    filter->setChecked(i == Filter_All);
    filter->setCursor(Qt::PointingHandCursor);
    filters_->addButton(filter, i);
    books_header->addWidget(filter);
  }
  books_header->addStretch();
  connect(filters_, SIGNAL(idClicked(int)), SLOT(FilterChanged()));
  content->addLayout(books_header);

  grid_ = new QListView(content_);
  grid_->setObjectName("album_grid");
  grid_->setViewMode(QListView::IconMode);
  grid_->setResizeMode(QListView::Adjust);
  grid_->setMovement(QListView::Static);
  grid_->setUniformItemSizes(true);
  grid_->setSpacing(10);
  grid_->setFrameShape(QFrame::NoFrame);
  grid_->setMouseTracking(true);
  grid_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  grid_->verticalScrollBar()->setSingleStep(24);
  grid_->setModel(grid_model_);
  grid_->setItemDelegate(new AlbumGridDelegate(grid_, true));
  grid_->setCursor(Qt::PointingHandCursor);
  connect(grid_, SIGNAL(clicked(QModelIndex)),
          SLOT(BookActivated(QModelIndex)));
  connect(grid_, SIGNAL(activated(QModelIndex)),
          SLOT(BookActivated(QModelIndex)));
  content->addWidget(grid_, 1);

  return page;
}

QWidget* AudiobooksView::MakeBookPage() {
  QWidget* page = new QWidget(this);
  QVBoxLayout* layout = new QVBoxLayout(page);
  layout->setContentsMargins(28, 18, 20, 0);
  layout->setSpacing(18);

  QPushButton* back = new QPushButton(tr("Audiobooks"), page);
  back->setIcon(IconLoader::Load("go-previous", IconLoader::Base));
  back->setProperty("link", true);
  back->setCursor(Qt::PointingHandCursor);
  connect(back, &QPushButton::clicked, this, &AudiobooksView::ShowBooks);
  QHBoxLayout* back_row = new QHBoxLayout;
  back_row->addWidget(back);
  back_row->addStretch();
  layout->addLayout(back_row);

  book_cover_ = new QLabel(page);
  book_cover_->setFixedSize(kBookCoverSize, kBookCoverSize);
  QLabel* kind = new QLabel(tr("Audiobook"), page);
  kind->setProperty("caption", true);
  book_title_ = new QLabel(page);
  book_title_->setProperty("album_title", true);
  book_title_->setWordWrap(true);
  book_author_ = new QLabel(page);
  book_meta_ = new QLabel(page);
  book_meta_->setForegroundRole(QPalette::PlaceholderText);
  book_progress_ = new QProgressBar(page);
  book_progress_->setObjectName("listening_progress");
  book_progress_->setTextVisible(false);
  book_progress_->setRange(0, 1000);
  book_progress_->setFixedHeight(6);
  book_progress_->setMaximumWidth(420);

  book_play_ = MakeButton(tr("Play"), "media-playback-start", true);
  book_restart_ = MakeButton(tr("Start over"), "media-skip-backward", false);
  book_finished_ = MakeButton(tr("Mark as finished"), "dialog-ok-apply", false);
  connect(book_play_, &QPushButton::clicked, this, [this]() {
    const Book* book = FindBook(open_key_);
    if (!book) return;
    if (book->state == Book::Finished) {
      // Listening again starts at the beginning.
      listening_->ResetBook(book->key, book->chapters);
      Play(*book, 0);
    } else {
      Play(*book, book->chapter);
    }
  });
  connect(book_restart_, &QPushButton::clicked, this, [this]() {
    const Book* book = FindBook(open_key_);
    if (!book) return;
    const Book copy = *book;
    listening_->ResetBook(copy.key, copy.chapters);
    Play(copy, 0);
  });
  connect(book_finished_, &QPushButton::clicked, this, [this]() {
    const Book* book = FindBook(open_key_);
    if (book) listening_->MarkBookFinished(book->key, book->chapters);
  });

  QHBoxLayout* buttons = new QHBoxLayout;
  buttons->setSpacing(10);
  buttons->addWidget(book_play_);
  buttons->addWidget(book_restart_);
  buttons->addWidget(book_finished_);
  buttons->addStretch();

  QVBoxLayout* text = new QVBoxLayout;
  text->setSpacing(6);
  text->addStretch();
  text->addWidget(kind);
  text->addWidget(book_title_);
  text->addWidget(book_author_);
  text->addWidget(book_meta_);
  text->addSpacing(4);
  text->addWidget(book_progress_);
  text->addSpacing(8);
  text->addLayout(buttons);

  QHBoxLayout* header = new QHBoxLayout;
  header->setSpacing(26);
  header->addWidget(book_cover_, 0, Qt::AlignBottom);
  header->addLayout(text, 1);
  layout->addLayout(header);

  chapters_ = new QTreeWidget(page);
  chapters_->setObjectName("browser_songs");
  chapters_->setRootIsDecorated(false);
  chapters_->setFrameShape(QFrame::NoFrame);
  chapters_->setUniformRowHeights(true);
  chapters_->setAlternatingRowColors(false);
  chapters_->setHeaderLabels({"#", tr("Chapter"), tr("Length"), QString()});
  chapters_->header()->setStretchLastSection(false);
  chapters_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  chapters_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
  chapters_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  chapters_->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
  connect(chapters_, &QTreeWidget::itemActivated, this,
          [this](QTreeWidgetItem* item) {
            const Book* book = FindBook(open_key_);
            if (book) Play(*book, chapters_->indexOfTopLevelItem(item));
          });
  layout->addWidget(chapters_, 1);
  return page;
}

void AudiobooksView::SetSongs(const SongList& songs) {
  songs_ = songs;
  Rebuild();
}

AudiobooksView::Book AudiobooksView::MakeBook(const QString& key,
                                              SongList chapters) const {
  std::stable_sort(chapters.begin(), chapters.end(),
                   [](const Song& a, const Song& b) {
                     if (a.disc() != b.disc()) return a.disc() < b.disc();
                     if (a.track() != b.track()) return a.track() < b.track();
                     return a.url().toString() < b.url().toString();
                   });

  Book book;
  book.key = key;
  book.chapters = chapters;
  const Song& first = chapters.first();
  book.title = first.album().isEmpty()
                   ? QFileInfo(first.url().toLocalFile()).dir().dirName()
                   : first.album();
  book.author = first.effective_albumartist();
  if (book.author.isEmpty()) book.author = tr("Unknown author");

  QList<qint64> lengths;
  for (const Song& chapter : chapters) {
    lengths << qMax<qint64>(0, chapter.length_nanosec() / kNsecPerSec);
    book.length += lengths.last();
  }
  book.last_listened = listening_->LastListened(key);

  const QUrl last = listening_->LastChapter(key);
  int index = -1;
  for (int i = 0; i < chapters.count(); ++i) {
    if (chapters[i].url() == last) index = i;
  }
  if (index < 0) return book;  // not started

  qint64 before = 0;
  for (int i = 0; i < index; ++i) before += lengths[i];
  const int position = listening_->SavedPosition(last);
  if (position == ListeningController::kFinished) {
    if (index == chapters.count() - 1) {
      book.state = Book::Finished;
      book.elapsed = book.length;
      return book;
    }
    book.chapter = index + 1;
    book.elapsed = before + lengths[index];
  } else {
    book.chapter = index;
    book.elapsed = before + position;
  }
  book.state = Book::InProgress;
  return book;
}

void AudiobooksView::Rebuild() {
  QMap<QString, SongList> by_book;
  for (const Song& song : songs_) {
    by_book[ListeningController::BookKey(song)] << song;
  }
  books_.clear();
  int in_progress = 0;
  for (auto it = by_book.begin(); it != by_book.end(); ++it) {
    books_ << MakeBook(it.key(), it.value());
    if (books_.last().state == Book::InProgress) ++in_progress;
  }
  std::stable_sort(books_.begin(), books_.end(),
                   [](const Book& a, const Book& b) {
                     return a.title.localeAwareCompare(b.title) < 0;
                   });

  QString summary = Count(books_.count(), tr("%1 book"), tr("%1 books"));
  if (in_progress > 0) {
    summary += QString::fromUtf8(" · ") + tr("%1 in progress").arg(in_progress);
  }
  summary_->setText(summary);
  empty_->setVisible(books_.isEmpty());
  content_->setVisible(!books_.isEmpty());

  UpdateHero();
  FilterChanged();
  if (pages_->currentIndex() == 1) {
    if (FindBook(open_key_)) {
      UpdateBookPage();
    } else {
      ShowBooks();
    }
  }
}

void AudiobooksView::UpdateHero() {
  const Book* latest = nullptr;
  for (const Book& book : books_) {
    if (book.state != Book::InProgress) continue;
    if (!latest || book.last_listened > latest->last_listened) latest = &book;
  }
  hero_->setVisible(latest != nullptr);
  if (!latest) return;

  const bool changed = hero_key_ != latest->key;
  hero_key_ = latest->key;
  hero_title_->setText(latest->title);
  hero_author_->setText(latest->author);
  const Song& chapter = latest->chapters[latest->chapter];
  QString chapter_text = tr("Chapter %1 of %2")
                             .arg(latest->chapter + 1)
                             .arg(latest->chapters.count());
  if (!chapter.title().isEmpty()) {
    chapter_text += QString::fromUtf8(" · ") + chapter.title();
  }
  hero_chapter_->setText(chapter_text);
  hero_progress_->setValue(int(latest->progress() * 1000));
  hero_status_->setText(latest->StatusText());
  if (changed)
    LoadCover(hero_cover_, chapter, kHeroCoverSize, &hero_cover_request_);
}

void AudiobooksView::FilterChanged() {
  const int filter = filters_->checkedId();
  QList<BrowserAlbum> shown;
  for (const Book& book : books_) {
    if ((filter == Filter_InProgress && book.state != Book::InProgress) ||
        (filter == Filter_NotStarted && book.state != Book::NotStarted) ||
        (filter == Filter_Done && book.state != Book::Finished)) {
      continue;
    }
    BrowserAlbum album;
    album.title = book.title;
    album.artist = book.author;
    album.songs = book.chapters;
    album.progress = book.state == Book::NotStarted ? -1 : book.progress();
    album.status = book.StatusText();
    shown << album;
  }
  grid_model_->SetAlbums(shown);
}

void AudiobooksView::BookActivated(const QModelIndex& index) {
  if (!index.isValid()) return;
  const BrowserAlbum& album = grid_model_->albums()[index.row()];
  ShowBook(ListeningController::BookKey(album.songs.first()));
}

const AudiobooksView::Book* AudiobooksView::FindBook(const QString& key) const {
  for (const Book& book : books_) {
    if (book.key == key) return &book;
  }
  return nullptr;
}

void AudiobooksView::ShowBooks() { pages_->setCurrentIndex(0); }

bool AudiobooksView::ShowFirstBook() {
  if (books_.isEmpty()) return false;
  // The one you're partway through, if there is one.
  ShowBook(hero_->isVisibleTo(this) && !hero_key_.isEmpty()
               ? hero_key_
               : books_.first().key);
  return true;
}

void AudiobooksView::ShowBook(const QString& key) {
  const Book* book = FindBook(key);
  if (!book) return;
  open_key_ = key;
  LoadCover(book_cover_, book->chapters.first(), kBookCoverSize,
            &book_cover_request_);
  UpdateBookPage();
  pages_->setCurrentIndex(1);
}

void AudiobooksView::UpdateBookPage() {
  const Book* book = FindBook(open_key_);
  if (!book) return;
  book_title_->setText(book->title);
  book_author_->setText(book->author);
  book_meta_->setText(
      Count(book->chapters.count(), tr("%1 chapter"), tr("%1 chapters")) +
      QString::fromUtf8(" · ") + HoursMinutes(book->length) +
      QString::fromUtf8(" · ") + book->StatusText());
  book_progress_->setVisible(book->state == Book::InProgress);
  book_progress_->setValue(int(book->progress() * 1000));

  switch (book->state) {
    case Book::NotStarted:
      book_play_->setText(tr("Play"));
      break;
    case Book::InProgress:
      book_play_->setText(tr("Resume"));
      break;
    case Book::Finished:
      book_play_->setText(tr("Listen again"));
      break;
  }
  book_restart_->setVisible(book->state == Book::InProgress);
  book_finished_->setVisible(book->state != Book::Finished);

  const QColor accent = Appearance::AccentColor(palette());
  chapters_->clear();
  for (int i = 0; i < book->chapters.count(); ++i) {
    const Song& chapter = book->chapters[i];
    const int position = listening_->SavedPosition(chapter.url());
    QString status;
    if (book->state == Book::Finished ||
        position == ListeningController::kFinished) {
      status = tr("Played");
    } else if (position > 0) {
      status = tr("%1 in").arg(Utilities::PrettyTime(position));
    }
    QString title = chapter.title();
    if (title.isEmpty()) title = chapter.basefilename();
    QTreeWidgetItem* item = new QTreeWidgetItem(
        chapters_,
        {QString::number(i + 1), title,
         Utilities::PrettyTimeNanosec(chapter.length_nanosec()), status});
    item->setTextAlignment(0, Qt::AlignRight | Qt::AlignVCenter);
    item->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
    item->setForeground(3, Appearance::QuietTextColor(palette()));
    if (book->state == Book::InProgress && i == book->chapter) {
      // Where you're up to, in the accent.
      for (int column = 0; column < 4; ++column) {
        item->setForeground(column, accent);
      }
      QFont font = item->font(1);
      font.setWeight(QFont::DemiBold);
      item->setFont(1, font);
    }
  }
}

void AudiobooksView::LoadCover(QLabel* label, const Song& song, int size,
                               quint64* request) {
  label->setPixmap(BrowserRoundedCover(
      BrowserPlaceholderCover(
          song.album().isEmpty() ? song.title() : song.album(),
          Appearance::IsDarkPalette(palette())),
      size, devicePixelRatioF()));
  AlbumCoverLoaderOptions options;
  options.desired_height_ = size * 2;
  options.pad_output_image_ = false;
  *request = app_->album_cover_loader()->LoadImageAsync(options, song);
}

void AudiobooksView::Play(const Book& book, int chapter) {
  emit PlayBook(book.chapters, qBound(0, chapter, book.chapters.count() - 1));
}

void AudiobooksView::AddFolder() {
  const QString path = QFileDialog::getExistingDirectory(
      this, tr("Add audiobook folder"), QDir::homePath());
  if (path.isEmpty()) return;
  listening_->AddAudiobookFolder(path);

  // It has to be in the library too, unless it's already inside a folder
  // that is.
  LibraryDirectoryModel* directories = app_->directory_model();
  const QString clean = QDir::cleanPath(path);
  for (int i = 0; i < directories->rowCount(); ++i) {
    const QString existing = QDir::cleanPath(
        directories->data(directories->index(i, 0), Qt::DisplayRole)
            .toString());
    if (clean == existing || clean.startsWith(existing + "/")) return;
  }
  directories->AddDirectory(clean);
}
