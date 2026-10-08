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

#include "playerbar.h"

#include <QAction>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

#include "core/appearance.h"
#include "core/application.h"
#include "core/player.h"
#include "covers/currentartloader.h"

namespace {

const int kBarHeight = 84;
const int kCoverSize = 56;
const int kCoverRadius = 6;
const int kPlayButtonSize = 40;
const int kTransportButtonSize = 34;
const int kTransportIconSize = 20;
const int kSideWidth = 280;

}  // namespace

AccentPlayButton::AccentPlayButton(QWidget* parent) : QToolButton(parent) {
  setAutoRaise(true);
  setCursor(Qt::PointingHandCursor);
  setIconSize(QSize(kTransportIconSize, kTransportIconSize));
}

QSize AccentPlayButton::sizeHint() const {
  return QSize(kPlayButtonSize, kPlayButtonSize);
}

void AccentPlayButton::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  QColor fill = Appearance::AccentColor(palette());
  if (!isEnabled()) {
    fill = palette().color(QPalette::Button);
  } else if (isDown()) {
    fill = fill.darker(115);
  } else if (underMouse()) {
    fill = fill.lighter(110);
  }

  const int size = qMin(width(), height()) - 2;
  const QRect circle((width() - size) / 2, (height() - size) / 2, size, size);
  p.setPen(Qt::NoPen);
  p.setBrush(fill);
  p.drawEllipse(circle);

  // The icon in the colour of the window behind the button, so it reads as a
  // cut-out. Line icons take the painter's pen colour.
  p.setPen(palette().color(QPalette::Base));
  const QRect icon_rect(circle.center().x() - iconSize().width() / 2 + 1,
                        circle.center().y() - iconSize().height() / 2 + 1,
                        iconSize().width(), iconSize().height());
  icon().paint(&p, icon_rect);
}

PlayerBar::PlayerBar(QWidget* parent)
    : QFrame(parent),
      app_(nullptr),
      cover_(new QLabel(this)),
      title_(new QLabel(this)),
      artist_(new QLabel(this)),
      love_(new QToolButton(this)),
      transport_(new QHBoxLayout),
      previous_(nullptr),
      play_pause_(new AccentPlayButton(this)),
      next_(nullptr),
      centre_(new QVBoxLayout),
      trailing_(new QHBoxLayout),
      volume_(nullptr) {
  setObjectName("player_bar");
  setFixedHeight(kBarHeight);

  // What's playing.
  cover_->setFixedSize(kCoverSize, kCoverSize);
  cover_->setCursor(Qt::PointingHandCursor);
  cover_->installEventFilter(this);

  QFont title_font(title_->font());
  title_font.setWeight(QFont::DemiBold);
  title_->setFont(title_font);
  title_->setCursor(Qt::PointingHandCursor);
  title_->installEventFilter(this);
  artist_->setForegroundRole(QPalette::PlaceholderText);
  for (QLabel* label : {title_, artist_}) {
    label->setTextFormat(Qt::PlainText);
    label->setMinimumWidth(40);
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  }

  QVBoxLayout* text = new QVBoxLayout;
  text->setSpacing(2);
  text->addStretch();
  text->addWidget(title_);
  text->addWidget(artist_);
  text->addStretch();

  love_->setAutoRaise(true);
  love_->setIconSize(QSize(18, 18));

  QWidget* now_playing = new QWidget(this);
  now_playing->setFixedWidth(kSideWidth);
  QHBoxLayout* now_playing_layout = new QHBoxLayout(now_playing);
  now_playing_layout->setContentsMargins(0, 0, 0, 0);
  now_playing_layout->setSpacing(12);
  now_playing_layout->addWidget(cover_);
  now_playing_layout->addLayout(text, 1);
  now_playing_layout->addWidget(love_);

  // The transport, with the seek bar under it.
  transport_->setSpacing(10);
  transport_->addStretch();
  transport_->addWidget(play_pause_);
  transport_->addStretch();

  centre_->setSpacing(2);
  centre_->addStretch();
  centre_->addLayout(transport_);
  centre_->addStretch();

  // The volume and toggles.
  QWidget* trailing = new QWidget(this);
  trailing->setFixedWidth(kSideWidth);
  trailing->setLayout(trailing_);
  trailing_->setContentsMargins(0, 0, 0, 0);
  trailing_->setSpacing(4);
  trailing_->addStretch();

  QHBoxLayout* layout = new QHBoxLayout(this);
  layout->setContentsMargins(16, 6, 16, 6);
  layout->setSpacing(16);
  layout->addWidget(now_playing);
  layout->addLayout(centre_, 1);
  layout->addWidget(trailing);

  Stopped();
}

void PlayerBar::SetApplication(Application* app) {
  app_ = app;
  connect(app_->current_art_loader(), SIGNAL(ArtLoaded(Song, QString, QImage)),
          SLOT(ArtLoaded(Song, QString, QImage)));
  connect(app_->player(), SIGNAL(Stopped()), SLOT(Stopped()));
}

QToolButton* PlayerBar::MakeTransportButton() {
  QToolButton* button = new QToolButton(this);
  button->setAutoRaise(true);
  button->setIconSize(QSize(kTransportIconSize, kTransportIconSize));
  button->setFixedSize(kTransportButtonSize, kTransportButtonSize);
  return button;
}

void PlayerBar::SetTransportActions(QAction* previous, QAction* play_pause,
                                    QAction* next, QAction* love) {
  previous_ = MakeTransportButton();
  previous_->setDefaultAction(previous);
  next_ = MakeTransportButton();
  next_->setDefaultAction(next);
  // The menu (Stop after this track and so on) opens on a long press.
  next_->setPopupMode(QToolButton::DelayedPopup);

  play_pause_->setDefaultAction(play_pause);
  play_pause_->setFixedSize(kPlayButtonSize, kPlayButtonSize);

  const int play_index = transport_->indexOf(play_pause_);
  transport_->insertWidget(play_index, previous_);
  transport_->insertWidget(transport_->indexOf(play_pause_) + 1, next_);

  love_->setDefaultAction(love);
}

void PlayerBar::SetSequenceButtons(QToolButton* shuffle, QToolButton* repeat) {
  for (QToolButton* button : {shuffle, repeat}) {
    button->setParent(this);
    button->setAutoRaise(true);
    button->setIconSize(QSize(16, 16));
    button->setMinimumHeight(30);
    button->setProperty("sequence", true);
  }
  // Shuffle before the transport, repeat after it, like a phone's player.
  transport_->insertWidget(transport_->indexOf(previous_), shuffle);
  transport_->insertWidget(transport_->indexOf(next_) + 1, repeat);
}

void PlayerBar::SetTrackSlider(QWidget* slider) {
  slider->setParent(this);
  slider->setMaximumWidth(640);
  QHBoxLayout* row = new QHBoxLayout;
  row->addStretch();
  row->addWidget(slider, 10);
  row->addStretch();
  centre_->insertLayout(centre_->indexOf(transport_) + 1, row);
}

void PlayerBar::AddTrailingWidget(QWidget* widget) {
  widget->setParent(this);
  const int volume_index =
      volume_ ? trailing_->indexOf(volume_) : trailing_->count();
  trailing_->insertWidget(volume_index, widget);
}

void PlayerBar::SetVolume(QWidget* volume) {
  volume->setParent(this);
  volume->setFixedWidth(100);
  volume_ = volume;
  trailing_->addWidget(volume);
}

bool PlayerBar::eventFilter(QObject* object, QEvent* event) {
  if ((object == cover_ || object == title_) &&
      event->type() == QEvent::MouseButtonRelease) {
    emit NowPlayingClicked();
    return true;
  }
  return QFrame::eventFilter(object, event);
}

void PlayerBar::ArtLoaded(const Song& song, const QString&,
                          const QImage& image) {
  title_->setText(song.PrettyTitle());
  title_->setToolTip(song.PrettyTitle());
  const QString artist =
      song.artist().isEmpty() ? song.albumartist() : song.artist();
  artist_->setText(artist);
  artist_->setToolTip(artist);
  SetCover(image);
}

void PlayerBar::Stopped() {
  title_->setText(QString());
  artist_->setText(QString());
  SetCover(QImage());
}

void PlayerBar::SetCover(const QImage& image) {
  const qreal dpr = devicePixelRatioF();
  QPixmap pixmap(QSize(kCoverSize, kCoverSize) * dpr);
  pixmap.setDevicePixelRatio(dpr);
  pixmap.fill(Qt::transparent);

  QPainter p(&pixmap);
  p.setRenderHint(QPainter::Antialiasing);
  p.setRenderHint(QPainter::SmoothPixmapTransform);
  QPainterPath clip;
  clip.addRoundedRect(QRectF(0, 0, kCoverSize, kCoverSize), kCoverRadius,
                      kCoverRadius);
  p.setClipPath(clip);

  if (image.isNull()) {
    // A quiet placeholder until something plays.
    p.fillRect(QRect(0, 0, kCoverSize, kCoverSize),
               palette().color(QPalette::Button));
  } else {
    const QImage scaled = image.scaled(
        QSize(kCoverSize, kCoverSize) * dpr, Qt::KeepAspectRatioByExpanding,
        Qt::SmoothTransformation);
    const QRect source((scaled.width() - kCoverSize * dpr) / 2,
                       (scaled.height() - kCoverSize * dpr) / 2,
                       kCoverSize * dpr, kCoverSize * dpr);
    p.drawImage(QRectF(0, 0, kCoverSize, kCoverSize), scaled, source);
  }
  p.end();

  cover_->setPixmap(pixmap);
}
