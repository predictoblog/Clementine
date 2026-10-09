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

#include "playlisttitlebar.h"

#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QPushButton>
#include <QRandomGenerator>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "core/application.h"
#include "core/player.h"
#include "core/utilities.h"
#include "playlist/playlist.h"
#include "playlist/playlistmanager.h"
#include "playlist/playlistsequence.h"
#include "ui/iconloader.h"

PlaylistTitleBar::PlaylistTitleBar(QWidget* parent)
    : QWidget(parent),
      app_(nullptr),
      update_timer_(new QTimer(this)),
      title_(new QLabel(this)),
      summary_(new QLabel(this)) {
  setObjectName("playlist_title_bar");
  setAttribute(Qt::WA_StyledBackground);

  update_timer_->setSingleShot(true);
  update_timer_->setInterval(50);
  connect(update_timer_, SIGNAL(timeout()), SLOT(Update()));

  title_->setProperty("playlist_title", true);
  title_->setTextFormat(Qt::PlainText);
  summary_->setForegroundRole(QPalette::PlaceholderText);

  QVBoxLayout* text = new QVBoxLayout;
  text->setSpacing(2);
  text->addWidget(title_);
  text->addWidget(summary_);

  auto pill = [this](const QString& text, const QString& icon, bool primary) {
    QPushButton* button = new QPushButton(text, this);
    button->setIcon(IconLoader::Load(icon, IconLoader::Base));
    button->setProperty(primary ? "primary" : "pill", true);
    button->setCursor(Qt::PointingHandCursor);
    return button;
  };
  auto icon_button = [this](const QString& icon, const QString& tip) {
    QToolButton* button = new QToolButton(this);
    button->setIcon(IconLoader::Load(icon, IconLoader::Base));
    button->setIconSize(QSize(18, 18));
    button->setToolTip(tip);
    button->setAutoRaise(true);
    return button;
  };

  QPushButton* play = pill(tr("Play"), "media-playback-start", true);
  QPushButton* shuffle = pill(tr("Shuffle"), "media-playlist-shuffle", false);
  QToolButton* save = icon_button("document-save", tr("Save playlist..."));
  QToolButton* rename = icon_button("edit-rename", tr("Rename playlist..."));
  QToolButton* duplicate = icon_button("edit-copy", tr("Duplicate playlist"));
  connect(play, SIGNAL(clicked()), SLOT(Play()));
  connect(shuffle, SIGNAL(clicked()), SLOT(Shuffle()));
  connect(save, SIGNAL(clicked()), SLOT(Save()));
  connect(rename, SIGNAL(clicked()), SLOT(Rename()));
  connect(duplicate, SIGNAL(clicked()), SLOT(Duplicate()));

  QHBoxLayout* layout = new QHBoxLayout(this);
  layout->setContentsMargins(20, 14, 16, 10);
  layout->setSpacing(10);
  layout->addLayout(text, 1);
  layout->addWidget(play);
  layout->addWidget(shuffle);
  layout->addSpacing(4);
  layout->addWidget(save);
  layout->addWidget(rename);
  layout->addWidget(duplicate);
}

void PlaylistTitleBar::SetApplication(Application* app) {
  app_ = app;
  PlaylistManager* manager = app_->playlist_manager();
  connect(manager, SIGNAL(CurrentChanged(Playlist*)), update_timer_,
          SLOT(start()));
  connect(manager, SIGNAL(PlaylistChanged(Playlist*)), update_timer_,
          SLOT(start()));
  connect(manager, SIGNAL(PlaylistRenamed(int, QString)), update_timer_,
          SLOT(start()));
  Update();
}

void PlaylistTitleBar::Update() {
  PlaylistManager* manager = app_->playlist_manager();
  Playlist* playlist = manager->current();
  if (!playlist) return;

  title_->setText(manager->GetPlaylistName(playlist->id()));

  const int count = playlist->rowCount();
  QString summary = count == 1 ? tr("1 song") : tr("%1 songs").arg(count);
  const quint64 nanoseconds = playlist->GetTotalLength();
  if (count > 0 && nanoseconds > 0) {
    summary += QString::fromUtf8(" \u00b7 ") +
               Utilities::PrettyTimeNanosec(nanoseconds);
  }
  summary_->setText(summary);
}

void PlaylistTitleBar::Play() {
  PlaylistManager* manager = app_->playlist_manager();
  Playlist* playlist = manager->current();
  if (!playlist || playlist->rowCount() == 0) return;
  manager->SetActivePlaylist(playlist->id());
  app_->player()->PlayAt(0, Engine::Manual, true);
}

void PlaylistTitleBar::Shuffle() {
  PlaylistManager* manager = app_->playlist_manager();
  Playlist* playlist = manager->current();
  if (!playlist || playlist->rowCount() == 0) return;
  // Shuffle on, from a random song: the playlist keeps its own order.
  manager->sequence()->SetShuffleMode(PlaylistSequence::Shuffle_All);
  manager->SetActivePlaylist(playlist->id());
  app_->player()->PlayAt(
      QRandomGenerator::global()->bounded(playlist->rowCount()), Engine::Manual,
      true);
}

void PlaylistTitleBar::Save() {
  PlaylistManager* manager = app_->playlist_manager();
  manager->SaveWithUI(manager->current_id(),
                      manager->GetPlaylistName(manager->current_id()));
}

void PlaylistTitleBar::Rename() {
  PlaylistManager* manager = app_->playlist_manager();
  const int id = manager->current_id();
  bool ok = false;
  const QString name = QInputDialog::getText(this, tr("Rename playlist"),
                                             tr("New name:"), QLineEdit::Normal,
                                             manager->GetPlaylistName(id), &ok);
  if (ok && !name.trimmed().isEmpty()) manager->Rename(id, name.trimmed());
}

void PlaylistTitleBar::Duplicate() {
  PlaylistManager* manager = app_->playlist_manager();
  Playlist* playlist = manager->current();
  if (!playlist) return;
  manager->New(tr("%1 (copy)").arg(manager->GetPlaylistName(playlist->id())),
               playlist->GetAllSongs());
}
