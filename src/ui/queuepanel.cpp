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

#include "queuepanel.h"

#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

#include "core/appearance.h"
#include "core/application.h"
#include "core/player.h"
#include "core/song.h"
#include "core/utilities.h"
#include "playlist/playlist.h"
#include "playlist/playlistitem.h"
#include "playlist/playlistmanager.h"
#include "playlist/playlistsequence.h"
#include "playlist/queue.h"
#include "ui/iconloader.h"

namespace {

// Which playlist row a list row stands for.
const int kSourceRowRole = Qt::UserRole + 10;

QLabel* SectionLabel(const QString& text, QWidget* parent) {
  QLabel* label = new QLabel(text, parent);
  label->setProperty("caption", true);
  return label;
}

}  // namespace

const int TwoLineDelegate::kSubtitleRole = Qt::UserRole + 11;
const int QueuePanel::kUpcoming = 8;

QSize TwoLineDelegate::sizeHint(const QStyleOptionViewItem& option,
                                const QModelIndex&) const {
  return QSize(option.rect.width(), option.fontMetrics.height() * 2 + 14);
}

void TwoLineDelegate::paint(QPainter* painter,
                            const QStyleOptionViewItem& option,
                            const QModelIndex& index) const {
  painter->save();
  painter->setRenderHint(QPainter::Antialiasing);

  const QRect rect = option.rect.adjusted(2, 1, -2, -1);
  if (option.state & (QStyle::State_Selected | QStyle::State_MouseOver)) {
    QColor fill = option.palette.color(QPalette::Button);
    if (!(option.state & QStyle::State_Selected)) fill.setAlphaF(0.6);
    painter->setPen(Qt::NoPen);
    painter->setBrush(fill);
    painter->drawRoundedRect(rect, 6, 6);
  }

  const bool accent = index.data(Qt::ForegroundRole).isValid();
  const int line = option.fontMetrics.height();
  const QRect text = rect.adjusted(10, 6, -10, -6);

  QFont title_font(option.font);
  title_font.setWeight(QFont::DemiBold);
  painter->setFont(title_font);
  painter->setPen(accent ? Appearance::AccentColor(option.palette)
                         : option.palette.color(QPalette::Text));
  painter->drawText(
      QRect(text.left(), text.top(), text.width(), line),
      Qt::AlignLeft | Qt::AlignVCenter,
      QFontMetrics(title_font)
          .elidedText(index.data().toString(), Qt::ElideRight, text.width()));

  painter->setFont(option.font);
  painter->setPen(Appearance::QuietTextColor(option.palette));
  painter->drawText(
      QRect(text.left(), text.top() + line + 1, text.width(), line),
      Qt::AlignLeft | Qt::AlignVCenter,
      option.fontMetrics.elidedText(index.data(kSubtitleRole).toString(),
                                    Qt::ElideRight, text.width()));
  painter->restore();
}

QueuePanel::QueuePanel(QWidget* parent)
    : QWidget(parent),
      app_(nullptr),
      update_timer_(new QTimer(this)),
      updating_(false),
      now_(nullptr),
      queued_label_(nullptr),
      queued_(nullptr),
      queued_empty_(nullptr),
      next_label_(nullptr),
      next_(nullptr),
      next_note_(nullptr),
      clear_(nullptr),
      save_(nullptr) {
  update_timer_->setSingleShot(true);
  update_timer_->setInterval(50);
  connect(update_timer_, SIGNAL(timeout()), SLOT(Update()));

  QWidget* content = new QWidget(this);
  QVBoxLayout* layout = new QVBoxLayout(content);
  layout->setContentsMargins(0, 8, 0, 8);
  layout->setSpacing(6);

  layout->addWidget(SectionLabel(tr("Now playing"), content));
  now_ = MakeList(false);
  layout->addWidget(now_);

  layout->addSpacing(8);
  queued_label_ = SectionLabel(tr("Queued by you"), content);
  clear_ = new QPushButton(tr("Clear"), content);
  clear_->setProperty("link", true);
  clear_->setCursor(Qt::PointingHandCursor);
  connect(clear_, SIGNAL(clicked()), SLOT(Clear()));
  QHBoxLayout* queued_header = new QHBoxLayout;
  queued_header->addWidget(queued_label_);
  queued_header->addStretch();
  queued_header->addWidget(clear_);
  layout->addLayout(queued_header);
  queued_ = MakeList(true);
  layout->addWidget(queued_);
  queued_empty_ = new QLabel(
      tr("Nothing queued. Use Add to queue on an album, or right-click a "
         "song in the playlist and choose Queue track."),
      content);
  queued_empty_->setWordWrap(true);
  queued_empty_->setForegroundRole(QPalette::PlaceholderText);
  layout->addWidget(queued_empty_);

  layout->addSpacing(8);
  next_label_ = SectionLabel(QString(), content);
  layout->addWidget(next_label_);
  next_note_ = new QLabel(content);
  next_note_->setWordWrap(true);
  next_note_->setForegroundRole(QPalette::PlaceholderText);
  layout->addWidget(next_note_);
  next_ = MakeList(false);
  layout->addWidget(next_);
  layout->addStretch();

  for (QListWidget* list : {now_, queued_, next_}) {
    list->setParent(content);
  }

  QScrollArea* scroll = new QScrollArea(this);
  scroll->setWidget(content);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  save_ = new QPushButton(tr("Save as playlist"), this);
  save_->setProperty("pill", true);
  save_->setCursor(Qt::PointingHandCursor);
  connect(save_, SIGNAL(clicked()), SLOT(SaveAsPlaylist()));

  QVBoxLayout* outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 10);
  outer->setSpacing(8);
  outer->addWidget(scroll, 1);
  outer->addWidget(save_);
}

QListWidget* QueuePanel::MakeList(bool reorderable) {
  QListWidget* list = new QListWidget(this);
  list->setItemDelegate(new TwoLineDelegate(list));
  list->setFrameShape(QFrame::NoFrame);
  list->setMouseTracking(true);
  list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  list->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
  list->setSelectionMode(QAbstractItemView::ExtendedSelection);
  list->setAutoFillBackground(false);
  list->viewport()->setAutoFillBackground(false);
  connect(list, SIGNAL(itemActivated(QListWidgetItem*)),
          SLOT(ItemActivated(QListWidgetItem*)));

  if (reorderable) {
    list->setDragDropMode(QAbstractItemView::InternalMove);
    list->setDefaultDropAction(Qt::MoveAction);
    list->installEventFilter(this);
    list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(list, &QWidget::customContextMenuRequested, this,
            [this, list](const QPoint& pos) {
              if (!list->itemAt(pos)) return;
              QMenu menu(this);
              menu.addAction(IconLoader::Load("list-remove", IconLoader::Base),
                             tr("Remove from queue"), this,
                             SLOT(RemoveSelected()));
              menu.exec(list->viewport()->mapToGlobal(pos));
            });
    connect(list->model(),
            SIGNAL(rowsMoved(QModelIndex, int, int, QModelIndex, int)),
            SLOT(QueueReordered()));
  }
  return list;
}

void QueuePanel::SetApplication(Application* app) {
  app_ = app;
  PlaylistManager* manager = app_->playlist_manager();
  connect(manager, SIGNAL(ActiveChanged(Playlist*)),
          SLOT(ActiveChanged(Playlist*)));
  connect(manager, SIGNAL(CurrentSongChanged(Song)), update_timer_,
          SLOT(start()));
  connect(manager, SIGNAL(PlaylistChanged(Playlist*)), update_timer_,
          SLOT(start()));
  connect(manager->sequence(),
          SIGNAL(ShuffleModeChanged(PlaylistSequence::ShuffleMode)),
          update_timer_, SLOT(start()));
  if (manager->active()) ActiveChanged(manager->active());
}

void QueuePanel::ActiveChanged(Playlist* playlist) {
  if (playlist_) {
    disconnect(playlist_->queue(), nullptr, update_timer_, nullptr);
  }
  playlist_ = playlist;
  if (playlist_) {
    Queue* queue = playlist_->queue();
    for (const char* signal : {SIGNAL(rowsInserted(QModelIndex, int, int)),
                               SIGNAL(rowsRemoved(QModelIndex, int, int)),
                               SIGNAL(layoutChanged()), SIGNAL(modelReset())}) {
      connect(queue, signal, update_timer_, SLOT(start()));
    }
  }
  update_timer_->start();
}

void QueuePanel::FillRow(QListWidgetItem* item, int source_row) {
  const Song song = playlist_->item_at(source_row)->Metadata();
  item->setText(song.PrettyTitle());
  QString subtitle = song.artist();
  const QString length = song.PrettyLength();
  if (!length.isEmpty()) {
    subtitle +=
        (subtitle.isEmpty() ? QString() : QString::fromUtf8(" · ")) + length;
  }
  item->setData(TwoLineDelegate::kSubtitleRole, subtitle);
  item->setData(kSourceRowRole, source_row);
  item->setToolTip(song.artist().isEmpty()
                       ? song.PrettyTitle()
                       : song.artist() + QString::fromUtf8(" – ") +
                             song.PrettyTitle());
}

void QueuePanel::FitHeight(QListWidget* list) {
  int height = 0;
  for (int i = 0; i < list->count(); ++i) {
    height += list->sizeHintForRow(i);
  }
  list->setFixedHeight(height + 2 * list->frameWidth());
  list->setVisible(list->count() > 0);
}

void QueuePanel::Update() {
  if (!playlist_) return;
  updating_ = true;

  // Now playing.
  now_->clear();
  const int current = playlist_->current_row();
  if (current >= 0 && current < playlist_->rowCount()) {
    QListWidgetItem* item = new QListWidgetItem(now_);
    FillRow(item, current);
    item->setData(Qt::ForegroundRole, true);  // drawn in the accent
  }
  FitHeight(now_);

  // Queued by you.
  queued_->clear();
  Queue* queue = playlist_->queue();
  QSet<int> queued_rows;
  for (int row = 0; row < queue->rowCount(); ++row) {
    const int source_row = queue->mapToSource(queue->index(row, 0)).row();
    if (source_row < 0) continue;
    queued_rows << source_row;
    FillRow(new QListWidgetItem(queued_), source_row);
  }
  FitHeight(queued_);
  queued_empty_->setVisible(queue->rowCount() == 0);
  clear_->setVisible(queue->rowCount() > 0);

  // Next from the playlist.
  next_->clear();
  next_label_->setText(
      tr("Next from %1")
          .arg(app_->playlist_manager()->GetPlaylistName(playlist_->id())));
  const bool shuffled = app_->playlist_manager()->sequence()->shuffle_mode() !=
                        PlaylistSequence::Shuffle_Off;
  next_note_->setVisible(shuffled);
  next_note_->setText(
      tr("Shuffle is on, so the rest of the playlist plays in a random "
         "order."));
  if (!shuffled) {
    for (int row = current + 1;
         row < playlist_->rowCount() && next_->count() < kUpcoming; ++row) {
      if (queued_rows.contains(row)) continue;
      FillRow(new QListWidgetItem(next_), row);
    }
  }
  FitHeight(next_);

  save_->setEnabled(queue->rowCount() > 0);
  updating_ = false;
}

void QueuePanel::QueueReordered() {
  if (updating_ || !playlist_) return;
  // Rebuild the queue in the order the list now shows: move each song in
  // turn to its new place.
  Queue* queue = playlist_->queue();
  for (int target = 0; target < queued_->count(); ++target) {
    const int source_row = queued_->item(target)->data(kSourceRowRole).toInt();
    for (int row = target; row < queue->rowCount(); ++row) {
      if (queue->mapToSource(queue->index(row, 0)).row() == source_row) {
        if (row != target) queue->Move(QList<int>() << row, target);
        break;
      }
    }
  }
  update_timer_->start();
}

void QueuePanel::RemoveSelected() {
  if (!playlist_) return;
  QList<int> rows;
  for (QListWidgetItem* item : queued_->selectedItems()) {
    rows << queued_->row(item);
  }
  if (rows.isEmpty()) return;
  playlist_->queue()->Remove(rows);
  update_timer_->start();
}

void QueuePanel::Clear() {
  if (playlist_) playlist_->queue()->Clear();
  update_timer_->start();
}

void QueuePanel::SaveAsPlaylist() {
  if (!playlist_) return;
  SongList songs;
  for (int row = 0; row < queued_->count(); ++row) {
    const int source_row = queued_->item(row)->data(kSourceRowRole).toInt();
    songs << playlist_->item_at(source_row)->Metadata();
  }
  if (!songs.isEmpty()) app_->playlist_manager()->New(tr("Queue"), songs);
}

void QueuePanel::ItemActivated(QListWidgetItem* item) {
  if (!playlist_ || !item) return;
  app_->player()->PlayAt(item->data(kSourceRowRole).toInt(), Engine::Manual,
                         true);
}

bool QueuePanel::eventFilter(QObject* object, QEvent* event) {
  if (object == queued_ && event->type() == QEvent::KeyPress) {
    QKeyEvent* key = static_cast<QKeyEvent*>(event);
    if (key->key() == Qt::Key_Delete || key->key() == Qt::Key_Backspace) {
      RemoveSelected();
      return true;
    }
  }
  return QWidget::eventFilter(object, event);
}
