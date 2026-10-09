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

#ifndef INTERNET_PODCASTS_PODCASTSVIEW_H
#define INTERNET_PODCASTS_PODCASTSVIEW_H

#include <QWidget>

#include "core/song.h"
#include "internet/podcasts/podcast.h"
#include "internet/podcasts/podcastepisode.h"

class AlbumGridModel;
class Application;
class ListeningController;
class QLabel;
class QListView;
class QPushButton;
class QStackedWidget;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

// Podcasts as a page of their own, rather than a branch of the Internet
// tree: what's new across every show at the top, then the shows, and each
// show's episodes with what you've played and how far into the rest you are.
class PodcastsView : public QWidget {
  Q_OBJECT

 public:
  PodcastsView(Application* app, ListeningController* listening,
               QWidget* parent = nullptr);

  void ShowPodcasts();
  // The page of the first show, for screenshots; false if there are none.
  bool ShowFirstPodcast();

 signals:
  // Play these episodes from `start`.
  void PlayEpisodes(const SongList& songs, int start);

 private slots:
  void Reload();
  void AddPodcast();
  void CheckForEpisodes();
  void ShowActivated(const QModelIndex& index);

 private:
  QWidget* MakePodcastsPage();
  QWidget* MakePodcastPage();
  QTreeWidget* MakeEpisodeTable(bool with_show);
  QPushButton* MakeButton(const QString& text, const QString& icon,
                          bool primary);
  void ShowPodcast(int database_id);
  void UpdatePodcastPage();
  void FillEpisodes(QTreeWidget* table, const PodcastEpisodeList& episodes,
                    bool with_show);
  QString EpisodeState(const PodcastEpisode& episode) const;
  Podcast FindPodcast(int database_id) const;
  void PlayFrom(QTreeWidget* table, QTreeWidgetItem* item);
  void ShowEpisodeMenu(QTreeWidget* table, const QPoint& pos);
  void SetListened(const PodcastEpisodeList& episodes, bool listened);
  PodcastEpisodeList EpisodesIn(QTreeWidget* table) const;

  Application* app_;
  ListeningController* listening_;
  QStackedWidget* pages_;
  QTimer* reload_timer_;

  PodcastList podcasts_;

  // Podcasts
  QLabel* summary_;
  QWidget* empty_;
  QWidget* content_;
  QLabel* new_title_;
  QTreeWidget* new_episodes_;
  AlbumGridModel* grid_model_;
  QListView* grid_;

  // A show
  int open_id_;
  QLabel* show_cover_;
  QLabel* show_title_;
  QLabel* show_author_;
  QLabel* show_meta_;
  QLabel* show_description_;
  QTreeWidget* episodes_;
  quint64 show_cover_request_;
};

#endif  // INTERNET_PODCASTS_PODCASTSVIEW_H
