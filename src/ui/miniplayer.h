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

#ifndef UI_MINIPLAYER_H
#define UI_MINIPLAYER_H

#include <QWidget>

#include "core/song.h"

class Application;
class QAction;
class QLabel;
class QTimer;
class QToolButton;

// A small window that stays on top: the cover, what's playing, the
// transport and how far through, for when the music should keep out of the
// way. The full window comes back from its expand button.
class MiniPlayer : public QWidget {
  Q_OBJECT

 public:
  MiniPlayer(Application* app, QWidget* parent = nullptr);

  void SetActions(QAction* previous, QAction* play_pause, QAction* next);

 signals:
  // Back to the full window.
  void ExpandRequested();

 protected:
  void closeEvent(QCloseEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  bool eventFilter(QObject* object, QEvent* event) override;

 private slots:
  void SongChanged(const Song& song);
  void ArtLoaded(const Song& song, const QString& uri, const QImage& image);
  void UpdatePosition();

 private:
  QToolButton* MakeButton();

  Application* app_;
  QLabel* cover_;
  QLabel* title_;
  QLabel* artist_;
  QToolButton* previous_;
  QToolButton* play_pause_;
  QToolButton* next_;
  QWidget* progress_;
  QTimer* timer_;

  Song song_;
  double fraction_;
  QPoint drag_offset_;
};

#endif  // UI_MINIPLAYER_H
