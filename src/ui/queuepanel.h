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

#ifndef UI_QUEUEPANEL_H
#define UI_QUEUEPANEL_H

#include <QPointer>
#include <QStyledItemDelegate>
#include <QWidget>

class Application;
class Playlist;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QTimer;

// Two lines per row: the title, and under it the artist in the quiet colour.
class TwoLineDelegate : public QStyledItemDelegate {
 public:
  using QStyledItemDelegate::QStyledItemDelegate;

  static const int kSubtitleRole;

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override;
  QSize sizeHint(const QStyleOptionViewItem& option,
                 const QModelIndex& index) const override;
};

// What plays next, in the Now playing panel: the song playing now, the
// songs you've queued (drag to reorder, Delete to take one out), and the
// songs that follow in the playlist. It follows the playlist that's
// playing, and replaces the Queue Manager dialog for everyday use.
class QueuePanel : public QWidget {
  Q_OBJECT

 public:
  explicit QueuePanel(QWidget* parent = nullptr);

  void SetApplication(Application* app);

  static const int kUpcoming;

 protected:
  bool eventFilter(QObject* object, QEvent* event) override;

 private slots:
  void ActiveChanged(Playlist* playlist);
  void Update();
  void QueueReordered();
  void RemoveSelected();
  void Clear();
  void SaveAsPlaylist();
  void ItemActivated(QListWidgetItem* item);

 private:
  QListWidget* MakeList(bool reorderable);
  void FillRow(QListWidgetItem* item, int source_row);
  void FitHeight(QListWidget* list);

  Application* app_;
  QPointer<Playlist> playlist_;
  QTimer* update_timer_;
  bool updating_;

  QListWidget* now_;
  QLabel* queued_label_;
  QListWidget* queued_;
  QLabel* queued_empty_;
  QLabel* next_label_;
  QListWidget* next_;
  QLabel* next_note_;
  QPushButton* clear_;
  QPushButton* save_;
};

#endif  // UI_QUEUEPANEL_H
