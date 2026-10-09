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

#ifndef PLAYLIST_PLAYLISTTITLEBAR_H
#define PLAYLIST_PLAYLISTTITLEBAR_H

#include <QWidget>

class Application;
class Playlist;
class QLabel;
class QTimer;

// Above the playlist: its name, how many songs and how long, and what you
// can do with it - play, shuffle, save, rename, duplicate - as buttons
// rather than unlabelled icons and menus.
class PlaylistTitleBar : public QWidget {
  Q_OBJECT

 public:
  explicit PlaylistTitleBar(QWidget* parent = nullptr);

  void SetApplication(Application* app);

 private slots:
  void Update();
  void Play();
  void Shuffle();
  void Save();
  void Rename();
  void Duplicate();

 private:
  Application* app_;
  QTimer* update_timer_;
  QLabel* title_;
  QLabel* summary_;
};

#endif  // PLAYLIST_PLAYLISTTITLEBAR_H
