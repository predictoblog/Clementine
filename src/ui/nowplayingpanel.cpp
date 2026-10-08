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

#include "nowplayingpanel.h"

#include <QButtonGroup>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include "ui/iconloader.h"

NowPlayingPanel::NowPlayingPanel(QWidget* parent)
    : QWidget(parent),
      layout_(new QVBoxLayout(this)),
      tabs_(new QButtonGroup(this)),
      pages_(new QStackedWidget(this)),
      details_(new QWidget(this)),
      details_layout_(new QFormLayout(details_)) {
  setObjectName("now_playing_panel");
  setAttribute(Qt::WA_StyledBackground);
  setMinimumWidth(240);

  layout_->setContentsMargins(12, 10, 12, 0);
  layout_->setSpacing(10);

  // Heading, with a button to put the panel away.
  QLabel* heading = new QLabel(tr("Now playing"), this);
  heading->setObjectName("panel_heading");
  heading->setForegroundRole(QPalette::PlaceholderText);
  QFont heading_font(heading->font());
  heading_font.setPointSizeF(heading_font.pointSizeF() * 0.8);
  heading_font.setWeight(QFont::DemiBold);
  heading_font.setCapitalization(QFont::AllUppercase);
  heading_font.setLetterSpacing(QFont::PercentageSpacing, 106);
  heading->setFont(heading_font);

  QToolButton* close = new QToolButton(this);
  close->setAutoRaise(true);
  close->setIcon(IconLoader::Load("cancel", IconLoader::Base));
  close->setIconSize(QSize(14, 14));
  close->setToolTip(tr("Hide the Now playing panel"));
  connect(close, SIGNAL(clicked()), SIGNAL(CloseRequested()));

  QHBoxLayout* header = new QHBoxLayout;
  header->addWidget(heading);
  header->addStretch();
  header->addWidget(close);
  layout_->addLayout(header);

  // The tabs: a segmented control over the pages.
  QWidget* segments = new QWidget(this);
  segments->setObjectName("panel_segments");
  segments->setAttribute(Qt::WA_StyledBackground);
  QHBoxLayout* segments_layout = new QHBoxLayout(segments);
  segments_layout->setContentsMargins(3, 3, 3, 3);
  segments_layout->setSpacing(3);
  const QStringList names = {tr("Lyrics"), tr("Artist"), tr("Details")};
  for (int i = 0; i < names.count(); ++i) {
    QToolButton* button = new QToolButton(segments);
    button->setText(names[i]);
    button->setCheckable(true);
    button->setAutoRaise(true);
    button->setProperty("segment", true);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    tabs_->addButton(button, i);
    tab_buttons_ << button;
    segments_layout->addWidget(button);
    // Placeholders until the real pages arrive.
    pages_->addWidget(new QWidget(pages_));
  }
  tab_buttons_[0]->setChecked(true);
  connect(tabs_, &QButtonGroup::idClicked, this, [this](int id) {
    pages_->setCurrentIndex(id);
    emit CurrentPageChanged(id);
  });
  layout_->addWidget(segments);
  layout_->addWidget(pages_, 1);

  // Details: the playing song's tags and statistics.
  details_layout_->setContentsMargins(4, 8, 4, 8);
  details_layout_->setHorizontalSpacing(16);
  details_layout_->setVerticalSpacing(8);
  details_layout_->setLabelAlignment(Qt::AlignLeft);
  details_empty_ = new QLabel(tr("Nothing is playing."), details_);
  details_empty_->setForegroundRole(QPalette::PlaceholderText);
  details_layout_->addRow(details_empty_);
  title_ = AddDetailRow(tr("Title"));
  artist_ = AddDetailRow(tr("Artist"));
  album_ = AddDetailRow(tr("Album"));
  track_ = AddDetailRow(tr("Track"));
  year_ = AddDetailRow(tr("Year"));
  genre_ = AddDetailRow(tr("Genre"));
  length_ = AddDetailRow(tr("Length"));
  format_ = AddDetailRow(tr("Format"));
  plays_ = AddDetailRow(tr("Plays"));
  rating_ = AddDetailRow(tr("Rating"));

  QScrollArea* details_scroll = new QScrollArea(pages_);
  details_scroll->setWidget(details_);
  details_scroll->setWidgetResizable(true);
  details_scroll->setFrameShape(QFrame::NoFrame);
  QWidget* placeholder = pages_->widget(Page_Details);
  pages_->insertWidget(Page_Details, details_scroll);
  pages_->removeWidget(placeholder);
  placeholder->deleteLater();

  SetSong(Song());
}

QLabel* NowPlayingPanel::AddDetailRow(const QString& label) {
  QLabel* name = new QLabel(label, details_);
  name->setForegroundRole(QPalette::PlaceholderText);
  QLabel* value = new QLabel(details_);
  value->setWordWrap(true);
  value->setTextInteractionFlags(Qt::TextSelectableByMouse);
  details_layout_->addRow(name, value);
  return value;
}

void NowPlayingPanel::SetCoverWidget(QWidget* cover) {
  cover->setParent(this);
  // Under the heading, above the tabs.
  layout_->insertWidget(1, cover);
}

void NowPlayingPanel::SetPage(Page page, QWidget* widget) {
  QWidget* old = pages_->widget(page);
  const bool current = pages_->currentIndex() == page;
  pages_->insertWidget(page, widget);
  pages_->removeWidget(old);
  old->deleteLater();
  if (current) pages_->setCurrentIndex(page);
}

NowPlayingPanel::Page NowPlayingPanel::current_page() const {
  return Page(pages_->currentIndex());
}

void NowPlayingPanel::SetCurrentPage(Page page) {
  tab_buttons_[page]->setChecked(true);
  pages_->setCurrentIndex(page);
  emit CurrentPageChanged(page);
}

void NowPlayingPanel::SetSong(const Song& song) {
  const bool valid = song.is_valid();
  details_empty_->setVisible(!valid);
  // QFormLayout::setRowVisible() needs Qt 6.4, and we support 6.2.
  for (int row = 1; row < details_layout_->rowCount(); ++row) {
    for (QFormLayout::ItemRole role :
         {QFormLayout::LabelRole, QFormLayout::FieldRole}) {
      QLayoutItem* item = details_layout_->itemAt(row, role);
      if (item && item->widget()) item->widget()->setVisible(valid);
    }
  }
  if (!valid) return;

  auto set = [this](QLabel* label, const QString& text) {
    label->setText(text.isEmpty() ? QString::fromUtf8("—") : text);
  };

  set(title_, song.PrettyTitle());
  set(artist_, song.artist().isEmpty() ? song.albumartist() : song.artist());
  set(album_, song.album());
  QString track;
  if (song.track() > 0) {
    track = QString::number(song.track());
    if (song.disc() > 0) {
      track = tr("%1 (disc %2)").arg(song.track()).arg(song.disc());
    }
  }
  set(track_, track);
  set(year_, song.year() > 0 ? QString::number(song.year()) : QString());
  set(genre_, song.genre());
  set(length_, song.PrettyLength());

  QString format = song.TextForFiletype();
  if (song.bitrate() > 0) {
    format += QString::fromUtf8(" · ") + tr("%1 kbps").arg(song.bitrate());
  }
  set(format_, format);
  set(plays_, QString::number(qMax(0, song.playcount())));

  QString rating;
  if (song.rating() >= 0) {
    const int stars = qRound(song.rating() * 5);
    rating = QString(stars, QChar(0x2605)) + QString(5 - stars, QChar(0x2606));
  }
  set(rating_, rating);
}
