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

#ifndef UI_NOWPLAYINGPANEL_H
#define UI_NOWPLAYINGPANEL_H

#include <QList>
#include <QWidget>

#include "core/song.h"

class QButtonGroup;
class QFormLayout;
class QLabel;
class QStackedWidget;
class QToolButton;
class QVBoxLayout;

// The panel on the right of the main window: the cover of what's playing,
// and under it a row of tabs - Lyrics, Artist, Details, Queue - so lyrics,
// the artist's biography and what plays next sit beside the library rather
// than in its place.
class NowPlayingPanel : public QWidget {
  Q_OBJECT

 public:
  explicit NowPlayingPanel(QWidget* parent = nullptr);

  enum Page {
    Page_Lyrics = 0,
    Page_Artist = 1,
    Page_Details = 2,
    Page_Queue = 3,
  };

  // The cover widget goes above the tabs.
  void SetCoverWidget(QWidget* cover);
  // The pages for the Lyrics, Artist and Queue tabs. Details is built in.
  void SetPage(Page page, QWidget* widget);

  Page current_page() const;
  void SetCurrentPage(Page page);

 public slots:
  // The song the Details tab describes; an invalid song clears it.
  void SetSong(const Song& song);

 signals:
  void CloseRequested();
  void VisualizationsRequested();
  void CurrentPageChanged(int page);

 private:
  QLabel* AddDetailRow(const QString& label);

  QVBoxLayout* layout_;
  QButtonGroup* tabs_;
  QList<QToolButton*> tab_buttons_;
  QStackedWidget* pages_;

  QWidget* details_;
  QFormLayout* details_layout_;
  QLabel* details_empty_;
  QLabel* title_;
  QLabel* artist_;
  QLabel* album_;
  QLabel* track_;
  QLabel* year_;
  QLabel* genre_;
  QLabel* length_;
  QLabel* format_;
  QLabel* plays_;
  QLabel* rating_;
};

#endif  // UI_NOWPLAYINGPANEL_H
