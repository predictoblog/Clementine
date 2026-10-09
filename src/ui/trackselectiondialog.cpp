/* This file is part of Clementine.
   Copyright 2010, David Sansome <me@davidsansome.com>

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

#include "ui/trackselectiondialog.h"

#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QShortcut>
#include <QTreeWidget>
#include <QUrl>
#include <QtConcurrentRun>
#include <QtDebug>

#include "core/tagreaderclient.h"
#include "ui/iconloader.h"
#include "ui_trackselectiondialog.h"

TrackSelectionDialog::TrackSelectionDialog(QWidget* parent)
    : QDialog(parent), ui_(new Ui_TrackSelectionDialog), save_on_close_(false) {
  // Setup dialog window
  ui_->setupUi(this);

  connect(ui_->song_list, SIGNAL(currentRowChanged(int)), SLOT(UpdateStack()));
  connect(ui_->results,
          SIGNAL(currentItemChanged(QTreeWidgetItem*, QTreeWidgetItem*)),
          SLOT(ResultSelected()));

  ui_->splitter->setSizes(QList<int>() << 200 << width() - 200);
  SetLoading(QString());

  // Add the next/previous buttons
  previous_button_ = new QPushButton(
      IconLoader::Load("go-previous", IconLoader::Base), tr("Previous"), this);
  next_button_ = new QPushButton(IconLoader::Load("go-next", IconLoader::Base),
                                 tr("Next"), this);
  ui_->button_box->addButton(previous_button_, QDialogButtonBox::ResetRole);
  ui_->button_box->addButton(next_button_, QDialogButtonBox::ResetRole);

  connect(previous_button_, SIGNAL(clicked()), SLOT(PreviousSong()));
  connect(next_button_, SIGNAL(clicked()), SLOT(NextSong()));

  // Set some shortcuts for the buttons
  new QShortcut(QKeySequence::Back, previous_button_, SLOT(click()));
  new QShortcut(QKeySequence::Forward, next_button_, SLOT(click()));
  new QShortcut(QKeySequence::MoveToPreviousPage, previous_button_,
                SLOT(click()));
  new QShortcut(QKeySequence::MoveToNextPage, next_button_, SLOT(click()));

  // Under the matches, what picking one would change, field by field, each
  // with a tick to keep or leave out.
  ui_->label->setText(tr("Pick the best match"));
  changes_label_ = new QLabel(tr("Changes to make"), ui_->results_page);
  changes_label_->setStyleSheet("QLabel { font-weight: bold; }");
  changes_ = new QTreeWidget(ui_->results_page);
  changes_->setRootIsDecorated(false);
  changes_->setAllColumnsShowFocus(true);
  changes_->setSelectionMode(QAbstractItemView::NoSelection);
  changes_->setHeaderLabels(QStringList() << tr("Field") << tr("In your file")
                                          << tr("Suggested"));
  changes_->setColumnWidth(0, 120);
  changes_->setColumnWidth(1, 220);
  changes_->setMinimumHeight(150);
  ui_->results_page->layout()->addWidget(changes_label_);
  ui_->results_page->layout()->addWidget(changes_);
  connect(changes_, SIGNAL(itemChanged(QTreeWidgetItem*, int)),
          SLOT(ChangeToggled(QTreeWidgetItem*, int)));
  if (QPushButton* ok = ui_->button_box->button(QDialogButtonBox::Ok)) {
    ok->setText(tr("Apply"));
  }

  // Resize columns
  ui_->results->setColumnWidth(0, 50);   // Track column
  ui_->results->setColumnWidth(1, 50);   // Year column
  ui_->results->setColumnWidth(2, 160);  // Title column
  ui_->results->setColumnWidth(3, 160);  // Artist column
  ui_->results->setColumnWidth(4, 160);  // Album column
}

TrackSelectionDialog::~TrackSelectionDialog() { delete ui_; }

void TrackSelectionDialog::Init(const SongList& songs) {
  ui_->song_list->clear();
  ui_->stack->setCurrentWidget(ui_->loading_page);
  data_.clear();

  for (const Song& song : songs) {
    Data data;
    data.original_song_ = song;
    data_ << data;

    QListWidgetItem* item = new QListWidgetItem(ui_->song_list);
    item->setText(QFileInfo(song.url().toLocalFile()).fileName());
    item->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
  }

  const bool multiple = songs.count() > 1;
  ui_->song_list->setVisible(multiple);
  next_button_->setEnabled(multiple);
  previous_button_->setEnabled(multiple);

  ui_->song_list->setCurrentRow(0);
}

void TrackSelectionDialog::FetchTagProgress(const Song& original_song,
                                            const QString& progress) {
  // Find the item with this filename
  int row = -1;
  for (int i = 0; i < data_.count(); ++i) {
    if (data_[i].original_song_.url() == original_song.url()) {
      row = i;
      break;
    }
  }

  if (row == -1) return;

  data_[row].progress_string_ = progress;

  // If it's the current item, update the display
  if (ui_->song_list->currentIndex().row() == row) {
    UpdateStack();
  }
}

void TrackSelectionDialog::FetchTagFinished(const Song& original_song,
                                            const SongList& songs_guessed) {
  // Find the item with this filename
  int row = -1;
  for (int i = 0; i < data_.count(); ++i) {
    if (data_[i].original_song_.url() == original_song.url()) {
      row = i;
      break;
    }
  }

  if (row == -1) return;

  // Set the color back to black
  ui_->song_list->item(row)->setForeground(palette().text());

  // Add the results to the list
  data_[row].pending_ = false;
  data_[row].results_ = songs_guessed;

  // If it's the current item, update the display
  if (ui_->song_list->currentIndex().row() == row) {
    UpdateStack();
  }
}

void TrackSelectionDialog::UpdateStack() {
  const int row = ui_->song_list->currentRow();
  if (row < 0 || row >= data_.count()) return;

  const Data& data = data_[row];

  UpdateChanges();
  if (data.pending_) {
    ui_->stack->setCurrentWidget(ui_->loading_page);
    ui_->progress->set_text(data.progress_string_ + "...");
    return;
  } else if (data.results_.isEmpty()) {
    ui_->stack->setCurrentWidget(ui_->error_page);
    return;
  }
  ui_->stack->setCurrentWidget(ui_->results_page);

  // Clear tree widget
  ui_->results->clear();

  // Put the original tags at the top
  AddDivider(tr("Original tags"), ui_->results);
  AddSong(data.original_song_, -1, ui_->results);

  // Fill tree view with songs
  AddDivider(tr("Suggested tags"), ui_->results);

  int song_index = 0;
  for (const Song& song : data.results_) {
    AddSong(song, song_index++, ui_->results);
  }

  // Find the item that was selected last time
  for (int i = 0; i < ui_->results->model()->rowCount(); ++i) {
    const QModelIndex index = ui_->results->model()->index(i, 0);
    const QVariant id = index.data(Qt::UserRole);
    if (!id.isNull() && id.toInt() == data.selected_result_) {
      ui_->results->setCurrentIndex(index);
      break;
    }
  }
}

void TrackSelectionDialog::AddDivider(const QString& text,
                                      QTreeWidget* parent) const {
  QTreeWidgetItem* item = new QTreeWidgetItem(parent);
  item->setFirstColumnSpanned(true);
  item->setText(0, text);
  item->setFlags(Qt::NoItemFlags);
  item->setForeground(0, palette().color(QPalette::Disabled, QPalette::Text));

  QFont bold_font(font());
  bold_font.setBold(true);
  item->setFont(0, bold_font);
}

void TrackSelectionDialog::AddSong(const Song& song, int result_index,
                                   QTreeWidget* parent) const {
  QStringList values;
  values << ((song.track() > 0) ? QString::number(song.track()) : QString())
         << ((song.year() > 0) ? QString::number(song.year()) : QString())
         << song.title() << song.artist() << song.album();

  QTreeWidgetItem* item = new QTreeWidgetItem(parent, values);
  item->setData(0, Qt::UserRole, result_index);
  item->setData(0, Qt::TextAlignmentRole, Qt::AlignRight);
}

void TrackSelectionDialog::ResultSelected() {
  if (!ui_->results->currentItem()) return;

  const int song_row = ui_->song_list->currentRow();
  if (song_row == -1) return;

  const int result_index =
      ui_->results->currentItem()->data(0, Qt::UserRole).toInt();
  data_[song_row].selected_result_ = result_index;
  UpdateChanges();
}

QString TrackSelectionDialog::FieldName(int field) {
  switch (field) {
    case Field_Title:
      return tr("Title");
    case Field_Artist:
      return tr("Artist");
    case Field_Album:
      return tr("Album");
    case Field_Track:
      return tr("Track");
    case Field_Year:
      return tr("Year");
  }
  return QString();
}

QString TrackSelectionDialog::FieldValue(const Song& song, int field) {
  switch (field) {
    case Field_Title:
      return song.title();
    case Field_Artist:
      return song.artist();
    case Field_Album:
      return song.album();
    case Field_Track:
      return song.track() > 0 ? QString::number(song.track()) : QString();
    case Field_Year:
      return song.year() > 0 ? QString::number(song.year()) : QString();
  }
  return QString();
}

Song TrackSelectionDialog::Merge(const Song& original, const Song& suggestion,
                                 const QSet<int>& skipped) {
  Song merged(original);
  if (!skipped.contains(Field_Title)) merged.set_title(suggestion.title());
  if (!skipped.contains(Field_Artist)) merged.set_artist(suggestion.artist());
  if (!skipped.contains(Field_Album)) merged.set_album(suggestion.album());
  if (!skipped.contains(Field_Track)) merged.set_track(suggestion.track());
  if (!skipped.contains(Field_Year)) merged.set_year(suggestion.year());
  return merged;
}

void TrackSelectionDialog::UpdateChanges() {
  changes_->blockSignals(true);
  changes_->clear();

  const int row = ui_->song_list->currentRow();
  const bool have_result =
      row >= 0 && row < data_.count() && !data_[row].pending_ &&
      data_[row].selected_result_ >= 0 &&
      data_[row].selected_result_ < data_[row].results_.count();
  changes_->setVisible(have_result);
  changes_label_->setText(have_result ? tr("Changes to make")
                                      : tr("Keeping the file's own tags"));

  if (have_result) {
    const Data& data = data_[row];
    const Song& suggestion = data.results_[data.selected_result_];
    const QColor quiet = palette().color(QPalette::Disabled, QPalette::Text);
    for (int field = 0; field < FieldCount; ++field) {
      const QString before = FieldValue(data.original_song_, field);
      const QString after = FieldValue(suggestion, field);
      QTreeWidgetItem* item = new QTreeWidgetItem(changes_);
      item->setData(0, Qt::UserRole, field);
      item->setText(0, FieldName(field));
      item->setText(1, before.isEmpty() ? tr("Empty") : before);
      if (before == after) {
        item->setText(2, tr("No change"));
        item->setFlags(Qt::ItemIsEnabled);
        for (int column = 0; column < 3; ++column) {
          item->setForeground(column, quiet);
        }
      } else {
        item->setText(2, after.isEmpty() ? tr("Empty") : after);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        item->setCheckState(0, data.skipped_fields_.contains(field)
                                   ? Qt::Unchecked
                                   : Qt::Checked);
        if (before.isEmpty()) item->setForeground(1, quiet);
      }
    }
  }
  changes_->blockSignals(false);
}

void TrackSelectionDialog::ChangeToggled(QTreeWidgetItem* item, int column) {
  if (column != 0) return;
  const int row = ui_->song_list->currentRow();
  if (row < 0 || row >= data_.count()) return;
  const int field = item->data(0, Qt::UserRole).toInt();
  if (item->checkState(0) == Qt::Checked) {
    data_[row].skipped_fields_.remove(field);
  } else {
    data_[row].skipped_fields_.insert(field);
  }
}

void TrackSelectionDialog::SetLoading(const QString& message) {
  const bool loading = !message.isEmpty();

  ui_->button_box->setEnabled(!loading);
  ui_->splitter->setEnabled(!loading);
  ui_->loading_label->setVisible(loading);
  ui_->loading_label->set_text(message);
}

void TrackSelectionDialog::SaveData(const QList<Data>& data) {
  for (int i = 0; i < data.count(); ++i) {
    const Data& ref = data[i];
    if (ref.pending_ || ref.results_.isEmpty() || ref.selected_result_ == -1)
      continue;

    // Only the fields left ticked in Changes to make.
    const Song copy =
        Merge(ref.original_song_, ref.results_[ref.selected_result_],
              ref.skipped_fields_);

    if (!TagReaderClient::Instance()->SaveFileBlocking(copy.url().toLocalFile(),
                                                       copy)) {
      emit Error(tr("Failed to write new auto-tags to '%1'")
                     .arg(copy.url().toLocalFile()));
    }
  }
}

void TrackSelectionDialog::accept() {
  if (save_on_close_) {
    SetLoading(tr("Saving tracks") + "...");

    // Save tags in the background
    QFuture<void> future =
        QtConcurrent::run(&TrackSelectionDialog::SaveData, this, data_);
    NewClosure(future, this, &TrackSelectionDialog::AcceptFinished);
    return;
  }

  QDialog::accept();

  for (const Data& data : data_) {
    if (data.pending_ || data.results_.isEmpty() || data.selected_result_ == -1)
      continue;

    emit SongChosen(
        data.original_song_,
        Merge(data.original_song_, data.results_[data.selected_result_],
              data.skipped_fields_));
  }
}

void TrackSelectionDialog::AcceptFinished() {
  SetLoading(QString());
  QDialog::accept();
}

void TrackSelectionDialog::NextSong() {
  int row = (ui_->song_list->currentRow() + 1) % ui_->song_list->count();
  ui_->song_list->setCurrentRow(row);
}

void TrackSelectionDialog::PreviousSong() {
  int row = (ui_->song_list->currentRow() - 1 + ui_->song_list->count()) %
            ui_->song_list->count();
  ui_->song_list->setCurrentRow(row);
}
