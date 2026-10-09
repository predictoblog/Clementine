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

#include "podcastsview.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QPushButton>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTextDocument>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>

#include "core/appearance.h"
#include "core/application.h"
#include "core/listening.h"
#include "core/utilities.h"
#include "covers/albumcoverloader.h"
#include "covers/albumcoverloaderoptions.h"
#include "internet/core/internetmodel.h"
#include "internet/podcasts/podcastbackend.h"
#include "internet/podcasts/podcastdownloader.h"
#include "internet/podcasts/podcastservice.h"
#include "internet/podcasts/podcastupdater.h"
#include "library/librarybrowser.h"
#include "ui/iconloader.h"

namespace {

const int kShowCoverSize = 200;
const int kNewEpisodesShown = 12;
const int kEpisodeIdRole = Qt::UserRole + 1;

QString Count(int n, const QString& one, const QString& many) {
  return (n == 1 ? one : many).arg(n);
}

// "Today", "Yesterday", "Mon 5 Oct", "12 Mar 2024".
QString EpisodeDate(const QDateTime& date) {
  const QDate day = date.date();
  const QDate today = QDate::currentDate();
  if (day == today) return QObject::tr("Today");
  if (day == today.addDays(-1)) return QObject::tr("Yesterday");
  if (day.year() == today.year()) return day.toString("d MMM");
  return day.toString("d MMM yyyy");
}

QString PlainText(const QString& html) {
  QTextDocument document;
  document.setHtml(html);
  return document.toPlainText().simplified();
}

}  // namespace

PodcastsView::PodcastsView(Application* app, ListeningController* listening,
                           QWidget* parent)
    : QWidget(parent),
      app_(app),
      listening_(listening),
      pages_(new QStackedWidget(this)),
      reload_timer_(new QTimer(this)),
      grid_model_(new AlbumGridModel(app, this)),
      open_id_(-1),
      show_cover_request_(0) {
  setObjectName("library_browser");
  setAttribute(Qt::WA_StyledBackground);

  reload_timer_->setSingleShot(true);
  reload_timer_->setInterval(500);
  connect(reload_timer_, SIGNAL(timeout()), SLOT(Reload()));

  PodcastBackend* backend = app_->podcast_backend();
  for (const char* signal : {SIGNAL(SubscriptionAdded(Podcast)),
                             SIGNAL(SubscriptionRemoved(Podcast)),
                             SIGNAL(EpisodesAdded(PodcastEpisodeList)),
                             SIGNAL(EpisodesUpdated(PodcastEpisodeList))}) {
    connect(backend, signal, reload_timer_, SLOT(start()));
  }
  connect(listening_, SIGNAL(ProgressChanged()), reload_timer_, SLOT(start()));

  pages_->addWidget(MakePodcastsPage());
  pages_->addWidget(MakePodcastPage());

  QVBoxLayout* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(pages_);

  connect(app_->album_cover_loader(),
          qOverload<quint64, const QImage&>(&AlbumCoverLoader::ImageLoaded),
          this, [this](quint64 id, const QImage& image) {
            if (id != show_cover_request_ || image.isNull()) return;
            show_cover_->setPixmap(BrowserRoundedCover(image, kShowCoverSize,
                                                       devicePixelRatioF()));
          });

  Reload();
}

QPushButton* PodcastsView::MakeButton(const QString& text, const QString& icon,
                                      bool primary) {
  QPushButton* button = new QPushButton(text, this);
  if (!icon.isEmpty()) {
    button->setIcon(IconLoader::Load(icon, IconLoader::Base));
  }
  button->setProperty(primary ? "primary" : "pill", true);
  button->setCursor(Qt::PointingHandCursor);
  return button;
}

QTreeWidget* PodcastsView::MakeEpisodeTable(bool with_show) {
  QTreeWidget* table = new QTreeWidget(this);
  table->setObjectName("browser_songs");
  table->setRootIsDecorated(false);
  table->setFrameShape(QFrame::NoFrame);
  table->setUniformRowHeights(true);
  table->setSelectionMode(QAbstractItemView::ExtendedSelection);
  QStringList headers = {tr("Episode")};
  if (with_show) headers << tr("Show");
  headers << tr("Date") << tr("Length") << QString();
  table->setHeaderLabels(headers);
  QHeaderView* header = table->header();
  header->setStretchLastSection(false);
  header->setSectionResizeMode(0, QHeaderView::Stretch);
  for (int i = 1; i < headers.count(); ++i) {
    header->setSectionResizeMode(i, QHeaderView::ResizeToContents);
  }
  table->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(table, &QWidget::customContextMenuRequested, this,
          [this, table](const QPoint& pos) { ShowEpisodeMenu(table, pos); });
  connect(table, &QTreeWidget::itemActivated, this,
          [this, table](QTreeWidgetItem* item) { PlayFrom(table, item); });
  return table;
}

QWidget* PodcastsView::MakePodcastsPage() {
  QWidget* page = new QWidget(this);
  QVBoxLayout* layout = new QVBoxLayout(page);
  layout->setContentsMargins(28, 22, 20, 0);
  layout->setSpacing(14);

  QLabel* title = new QLabel(tr("Podcasts"), page);
  title->setProperty("page_title", true);
  summary_ = new QLabel(page);
  summary_->setForegroundRole(QPalette::PlaceholderText);
  QVBoxLayout* heading = new QVBoxLayout;
  heading->setSpacing(2);
  heading->addWidget(title);
  heading->addWidget(summary_);
  QPushButton* refresh =
      MakeButton(tr("Check for new episodes"), "view-refresh", false);
  QPushButton* add = MakeButton(tr("Add podcast..."), "list-add", false);
  connect(refresh, SIGNAL(clicked()), SLOT(CheckForEpisodes()));
  connect(add, SIGNAL(clicked()), SLOT(AddPodcast()));
  QHBoxLayout* header = new QHBoxLayout;
  header->setSpacing(10);
  header->addLayout(heading);
  header->addStretch();
  header->addWidget(refresh, 0, Qt::AlignTop);
  header->addWidget(add, 0, Qt::AlignTop);
  layout->addLayout(header);

  empty_ = new QWidget(page);
  QVBoxLayout* empty_layout = new QVBoxLayout(empty_);
  empty_layout->setContentsMargins(0, 40, 0, 0);
  QLabel* empty_title = new QLabel(tr("No podcasts yet"), empty_);
  empty_title->setProperty("section_title", true);
  empty_title->setAlignment(Qt::AlignCenter);
  QLabel* empty_text = new QLabel(
      tr("Search for a show, or paste its feed address, and new episodes "
         "show up here as they come out."),
      empty_);
  empty_text->setForegroundRole(QPalette::PlaceholderText);
  empty_text->setAlignment(Qt::AlignCenter);
  empty_text->setWordWrap(true);
  QPushButton* empty_add = MakeButton(tr("Add podcast..."), "list-add", true);
  connect(empty_add, SIGNAL(clicked()), SLOT(AddPodcast()));
  empty_layout->addWidget(empty_title);
  empty_layout->addWidget(empty_text);
  empty_layout->addSpacing(8);
  empty_layout->addWidget(empty_add, 0, Qt::AlignCenter);
  empty_layout->addStretch();
  layout->addWidget(empty_, 1);

  content_ = new QWidget(page);
  QVBoxLayout* content = new QVBoxLayout(content_);
  content->setContentsMargins(0, 0, 0, 0);
  content->setSpacing(10);
  layout->addWidget(content_, 1);

  new_title_ = new QLabel(tr("New episodes"), content_);
  new_title_->setProperty("section_title", true);
  content->addWidget(new_title_);
  new_episodes_ = MakeEpisodeTable(true);
  content->addWidget(new_episodes_);

  QLabel* shows_title = new QLabel(tr("Shows"), content_);
  shows_title->setProperty("section_title", true);
  content->addSpacing(6);
  content->addWidget(shows_title);

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
          SLOT(ShowActivated(QModelIndex)));
  connect(grid_, SIGNAL(activated(QModelIndex)),
          SLOT(ShowActivated(QModelIndex)));
  content->addWidget(grid_, 1);
  return page;
}

QWidget* PodcastsView::MakePodcastPage() {
  QWidget* page = new QWidget(this);
  QVBoxLayout* layout = new QVBoxLayout(page);
  layout->setContentsMargins(28, 18, 20, 0);
  layout->setSpacing(18);

  QPushButton* back = new QPushButton(tr("Podcasts"), page);
  back->setIcon(IconLoader::Load("go-previous", IconLoader::Base));
  back->setProperty("link", true);
  back->setCursor(Qt::PointingHandCursor);
  connect(back, &QPushButton::clicked, this, &PodcastsView::ShowPodcasts);
  QHBoxLayout* back_row = new QHBoxLayout;
  back_row->addWidget(back);
  back_row->addStretch();
  layout->addLayout(back_row);

  show_cover_ = new QLabel(page);
  show_cover_->setFixedSize(kShowCoverSize, kShowCoverSize);
  QLabel* kind = new QLabel(tr("Podcast"), page);
  kind->setProperty("caption", true);
  show_title_ = new QLabel(page);
  show_title_->setProperty("album_title", true);
  show_title_->setWordWrap(true);
  show_author_ = new QLabel(page);
  show_meta_ = new QLabel(page);
  show_meta_->setForegroundRole(QPalette::PlaceholderText);
  show_description_ = new QLabel(page);
  show_description_->setWordWrap(true);
  show_description_->setForegroundRole(QPalette::PlaceholderText);
  show_description_->setMaximumHeight(
      show_description_->fontMetrics().lineSpacing() * 3);
  show_description_->setAlignment(Qt::AlignLeft | Qt::AlignTop);

  QPushButton* play =
      MakeButton(tr("Play latest"), "media-playback-start", true);
  QPushButton* played =
      MakeButton(tr("Mark all as played"), "dialog-ok-apply", false);
  QPushButton* update =
      MakeButton(tr("Check for new episodes"), "view-refresh", false);
  QPushButton* unsubscribe =
      MakeButton(tr("Unsubscribe"), "list-remove", false);
  connect(play, &QPushButton::clicked, this, [this]() {
    if (episodes_->topLevelItemCount() > 0) {
      PlayFrom(episodes_, episodes_->topLevelItem(0));
    }
  });
  connect(played, &QPushButton::clicked, this,
          [this]() { SetListened(EpisodesIn(episodes_), true); });
  connect(update, &QPushButton::clicked, this, [this]() {
    app_->podcast_updater()->UpdatePodcastNow(FindPodcast(open_id_));
  });
  connect(unsubscribe, &QPushButton::clicked, this, [this]() {
    const Podcast podcast = FindPodcast(open_id_);
    if (!podcast.is_valid()) return;
    ShowPodcasts();
    app_->podcast_backend()->Unsubscribe(podcast);
  });

  QHBoxLayout* buttons = new QHBoxLayout;
  buttons->setSpacing(10);
  buttons->addWidget(play);
  buttons->addWidget(played);
  buttons->addWidget(update);
  buttons->addWidget(unsubscribe);
  buttons->addStretch();

  QVBoxLayout* text = new QVBoxLayout;
  text->setSpacing(6);
  text->addStretch();
  text->addWidget(kind);
  text->addWidget(show_title_);
  text->addWidget(show_author_);
  text->addWidget(show_meta_);
  text->addWidget(show_description_);
  text->addSpacing(8);
  text->addLayout(buttons);

  QHBoxLayout* header = new QHBoxLayout;
  header->setSpacing(26);
  header->addWidget(show_cover_, 0, Qt::AlignBottom);
  header->addLayout(text, 1);
  layout->addLayout(header);

  episodes_ = MakeEpisodeTable(false);
  layout->addWidget(episodes_, 1);
  return page;
}

QString PodcastsView::EpisodeState(const PodcastEpisode& episode) const {
  const int position = listening_->SavedPosition(
      episode.downloaded() ? episode.local_url() : episode.url());
  // Partway through counts first: Clementine marks an episode played as
  // soon as it starts.
  if (position > 0 && episode.duration_secs() > 0) {
    return tr("%1 left").arg(
        Utilities::PrettyTime(qMax(0, episode.duration_secs() - position)));
  }
  if (episode.listened() || position == ListeningController::kFinished) {
    return tr("Played");
  }
  if (episode.downloaded()) return tr("Downloaded");
  return tr("New");
}

void PodcastsView::FillEpisodes(QTreeWidget* table,
                                const PodcastEpisodeList& episodes,
                                bool with_show) {
  table->clear();
  const QColor accent = Appearance::AccentColor(palette());
  const QColor quiet = Appearance::QuietTextColor(palette());
  for (const PodcastEpisode& episode : episodes) {
    QStringList columns = {episode.title().simplified()};
    if (with_show) {
      columns << FindPodcast(episode.podcast_database_id()).title();
    }
    columns << EpisodeDate(episode.publication_date())
            << (episode.duration_secs() > 0
                    ? Utilities::PrettyTime(episode.duration_secs())
                    : QString())
            << EpisodeState(episode);
    QTreeWidgetItem* item = new QTreeWidgetItem(table, columns);
    item->setData(0, kEpisodeIdRole, episode.database_id());
    item->setToolTip(0, PlainText(episode.description()).left(400));
    const int state = columns.count() - 1;
    const int length = state - 1;
    item->setTextAlignment(length, Qt::AlignRight | Qt::AlignVCenter);
    for (int column = 1; column < columns.count(); ++column) {
      item->setForeground(column, quiet);
    }
    if (episode.listened()) {
      item->setForeground(0, quiet);
    } else {
      QFont font = item->font(0);
      font.setWeight(QFont::DemiBold);
      item->setFont(0, font);
      if (columns[state] != tr("Downloaded")) {
        item->setForeground(state, accent);
      }
    }
  }
}

void PodcastsView::Reload() {
  podcasts_ = app_->podcast_backend()->GetAllSubscriptions();

  PodcastEpisodeList fresh;
  QList<BrowserAlbum> shows;
  for (Podcast& podcast : podcasts_) {
    PodcastEpisodeList episodes =
        app_->podcast_backend()->GetEpisodes(podcast.database_id());
    std::sort(episodes.begin(), episodes.end(),
              [](const PodcastEpisode& a, const PodcastEpisode& b) {
                return a.publication_date() > b.publication_date();
              });
    podcast.set_episodes(episodes);

    int unplayed = 0;
    for (const PodcastEpisode& episode : episodes) {
      if (!episode.listened()) {
        ++unplayed;
        fresh << episode;
      }
    }
    BrowserAlbum show;
    show.title = podcast.title();
    show.artist = podcast.author();
    if (!episodes.isEmpty()) show.songs << episodes.first().ToSong(podcast);
    show.status =
        unplayed > 0
            ? Count(unplayed, tr("%1 new episode"), tr("%1 new episodes"))
            : Count(episodes.count(), tr("%1 episode"), tr("%1 episodes"));
    show.id = podcast.database_id();
    shows << show;
  }
  std::sort(shows.begin(), shows.end(),
            [](const BrowserAlbum& a, const BrowserAlbum& b) {
              return a.title.localeAwareCompare(b.title) < 0;
            });
  grid_model_->SetAlbums(shows);

  std::sort(fresh.begin(), fresh.end(),
            [](const PodcastEpisode& a, const PodcastEpisode& b) {
              return a.publication_date() > b.publication_date();
            });
  const int new_count = fresh.count();
  fresh = fresh.mid(0, kNewEpisodesShown);
  FillEpisodes(new_episodes_, fresh, true);
  const int rows = qMax(1, fresh.count());
  new_episodes_->setFixedHeight(
      new_episodes_->header()->sizeHint().height() +
      rows * qMax(new_episodes_->sizeHintForRow(0), 24) + 4);
  new_title_->setVisible(!fresh.isEmpty());
  new_episodes_->setVisible(!fresh.isEmpty());

  QString summary = Count(podcasts_.count(), tr("%1 show"), tr("%1 shows"));
  if (new_count > 0) {
    summary += QString::fromUtf8(" · ") +
               Count(new_count, tr("%1 new episode"), tr("%1 new episodes"));
  }
  summary_->setText(summary);
  empty_->setVisible(podcasts_.isEmpty());
  content_->setVisible(!podcasts_.isEmpty());

  if (pages_->currentIndex() == 1) {
    if (FindPodcast(open_id_).is_valid()) {
      UpdatePodcastPage();
    } else {
      ShowPodcasts();
    }
  }
}

Podcast PodcastsView::FindPodcast(int database_id) const {
  for (const Podcast& podcast : podcasts_) {
    if (podcast.database_id() == database_id) return podcast;
  }
  return Podcast();
}

void PodcastsView::ShowPodcasts() { pages_->setCurrentIndex(0); }

bool PodcastsView::ShowFirstPodcast() {
  if (grid_model_->albums().isEmpty()) return false;
  ShowPodcast(grid_model_->albums().first().id);
  return true;
}

void PodcastsView::ShowActivated(const QModelIndex& index) {
  if (!index.isValid()) return;
  ShowPodcast(grid_model_->albums()[index.row()].id);
}

void PodcastsView::ShowPodcast(int database_id) {
  const Podcast podcast = FindPodcast(database_id);
  if (!podcast.is_valid()) return;
  open_id_ = database_id;

  show_cover_->setPixmap(BrowserRoundedCover(
      BrowserPlaceholderCover(podcast.title(),
                              Appearance::IsDarkPalette(palette())),
      kShowCoverSize, devicePixelRatioF()));
  if (!podcast.episodes().isEmpty()) {
    AlbumCoverLoaderOptions options;
    options.desired_height_ = kShowCoverSize * 2;
    options.pad_output_image_ = false;
    show_cover_request_ = app_->album_cover_loader()->LoadImageAsync(
        options, podcast.episodes().first().ToSong(podcast));
  }
  UpdatePodcastPage();
  pages_->setCurrentIndex(1);
}

void PodcastsView::UpdatePodcastPage() {
  const Podcast podcast = FindPodcast(open_id_);
  show_title_->setText(podcast.title());
  show_author_->setText(podcast.author());
  int unplayed = 0;
  for (const PodcastEpisode& episode : podcast.episodes()) {
    if (!episode.listened()) ++unplayed;
  }
  QString meta =
      Count(podcast.episodes().count(), tr("%1 episode"), tr("%1 episodes"));
  if (unplayed > 0) {
    meta += QString::fromUtf8(" · ") + tr("%1 new").arg(unplayed);
  }
  show_meta_->setText(meta);
  show_description_->setText(PlainText(podcast.description()));
  FillEpisodes(episodes_, podcast.episodes(), false);
}

PodcastEpisodeList PodcastsView::EpisodesIn(QTreeWidget* table) const {
  PodcastEpisodeList episodes;
  for (int i = 0; i < table->topLevelItemCount(); ++i) {
    const int id = table->topLevelItem(i)->data(0, kEpisodeIdRole).toInt();
    for (const Podcast& podcast : podcasts_) {
      for (const PodcastEpisode& episode : podcast.episodes()) {
        if (episode.database_id() == id) episodes << episode;
      }
    }
  }
  return episodes;
}

void PodcastsView::PlayFrom(QTreeWidget* table, QTreeWidgetItem* item) {
  // The episodes as listed, so the next one plays after this.
  const PodcastEpisodeList episodes = EpisodesIn(table);
  const int start = table->indexOfTopLevelItem(item);
  SongList songs;
  for (const PodcastEpisode& episode : episodes) {
    songs << episode.ToSong(FindPodcast(episode.podcast_database_id()));
  }
  if (!songs.isEmpty()) emit PlayEpisodes(songs, qMax(0, start));
}

void PodcastsView::SetListened(const PodcastEpisodeList& episodes,
                               bool listened) {
  PodcastEpisodeList changed;
  const QDateTime now = QDateTime::currentDateTime();
  for (PodcastEpisode episode : episodes) {
    if (episode.listened() == listened) continue;
    episode.set_listened(listened);
    if (listened) episode.set_listened_date(now);
    changed << episode;
  }
  if (!changed.isEmpty()) app_->podcast_backend()->UpdateEpisodes(changed);
}

void PodcastsView::ShowEpisodeMenu(QTreeWidget* table, const QPoint& pos) {
  QTreeWidgetItem* clicked = table->itemAt(pos);
  if (!clicked) return;
  if (!clicked->isSelected()) table->setCurrentItem(clicked);

  const PodcastEpisodeList all = EpisodesIn(table);
  PodcastEpisodeList selected;
  for (QTreeWidgetItem* item : table->selectedItems()) {
    const int index = table->indexOfTopLevelItem(item);
    if (index >= 0 && index < all.count()) selected << all[index];
  }
  if (selected.isEmpty()) return;

  QMenu menu(this);
  menu.addAction(IconLoader::Load("media-playback-start", IconLoader::Base),
                 tr("Play"), this,
                 [this, table, clicked]() { PlayFrom(table, clicked); });
  menu.addAction(IconLoader::Load("download", IconLoader::Base), tr("Download"),
                 this, [this, selected]() {
                   for (const PodcastEpisode& episode : selected) {
                     if (!episode.downloaded()) {
                       app_->podcast_downloader()->DownloadEpisode(episode);
                     }
                   }
                 });
  menu.addSeparator();
  menu.addAction(tr("Mark as played"), this,
                 [this, selected]() { SetListened(selected, true); });
  menu.addAction(tr("Mark as new"), this,
                 [this, selected]() { SetListened(selected, false); });
  menu.exec(table->viewport()->mapToGlobal(pos));
}

void PodcastsView::AddPodcast() {
  PodcastService* service = InternetModel::Service<PodcastService>();
  if (service) service->AddPodcast();
}

void PodcastsView::CheckForEpisodes() {
  app_->podcast_updater()->UpdateAllPodcastsNow();
}
