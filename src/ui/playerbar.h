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

#ifndef UI_PLAYERBAR_H
#define UI_PLAYERBAR_H

#include <QFrame>
#include <QToolButton>

#include "core/song.h"

class Application;
class QAction;
class QBoxLayout;
class QHBoxLayout;
class QLabel;

// The play button: a filled circle in the accent colour, with its icon drawn
// in the colour of the window behind it.
class AccentPlayButton : public QToolButton {
  Q_OBJECT

 public:
  explicit AccentPlayButton(QWidget* parent = nullptr);

  QSize sizeHint() const override;

 protected:
  void paintEvent(QPaintEvent*) override;
};

// The bar along the bottom of the main window: what's playing on the left,
// the transport and the seek bar in the middle, and the volume and the
// panel toggles on the right.
//
// It owns its buttons, which take their state from the window's actions, and
// is handed the widgets that already exist elsewhere (the seek bar, the
// volume, the shuffle and repeat buttons) to place.
class PlayerBar : public QFrame {
  Q_OBJECT

 public:
  explicit PlayerBar(QWidget* parent = nullptr);

  void SetApplication(Application* app);

  void SetTransportActions(QAction* previous, QAction* play_pause,
                           QAction* next, QAction* love);
  void SetSequenceButtons(QToolButton* shuffle, QToolButton* repeat);
  void SetTrackSlider(QWidget* slider);

  // Widgets on the right, in the order added, before the volume.
  void AddTrailingWidget(QWidget* widget);
  void SetVolume(QWidget* volume);

  QToolButton* next_button() const { return next_; }
  QToolButton* love_button() const { return love_; }

 signals:
  // The cover or the title was clicked.
  void NowPlayingClicked();

 protected:
  bool eventFilter(QObject* object, QEvent* event) override;

 private slots:
  void ArtLoaded(const Song& song, const QString& uri, const QImage& image);
  void Stopped();

 private:
  QToolButton* MakeTransportButton();
  void SetCover(const QImage& image);

  Application* app_;

  QLabel* cover_;
  QLabel* title_;
  QLabel* artist_;
  QToolButton* love_;

  QHBoxLayout* transport_;
  QToolButton* previous_;
  AccentPlayButton* play_pause_;
  QToolButton* next_;
  QBoxLayout* centre_;

  QHBoxLayout* trailing_;
  QWidget* volume_;
};

#endif  // UI_PLAYERBAR_H
