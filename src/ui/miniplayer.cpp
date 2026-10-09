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

#include "miniplayer.h"

#include <QAction>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "core/appearance.h"
#include "core/application.h"
#include "core/player.h"
#include "core/timeconstants.h"
#include "covers/currentartloader.h"
#include "engines/enginebase.h"
#include "playlist/playlistmanager.h"
#include "ui/iconloader.h"

namespace {
const int kCoverSize = 64;
const int kProgressHeight = 4;
}  // namespace

MiniPlayer::MiniPlayer(Application* app, QWidget* parent)
    : QWidget(parent, Qt::Window | Qt::WindowStaysOnTopHint),
      app_(app),
      cover_(new QLabel(this)),
      title_(new QLabel(this)),
      artist_(new QLabel(this)),
      previous_(nullptr),
      play_pause_(nullptr),
      next_(nullptr),
      progress_(new QWidget(this)),
      timer_(new QTimer(this)),
      fraction_(0) {
  setObjectName("mini_player");
  setWindowTitle(tr("Clementine"));
  setAttribute(Qt::WA_StyledBackground);

  cover_->setFixedSize(kCoverSize, kCoverSize);
  QFont title_font = title_->font();
  title_font.setWeight(QFont::DemiBold);
  title_->setFont(title_font);
  title_->setTextFormat(Qt::PlainText);
  artist_->setTextFormat(Qt::PlainText);
  artist_->setForegroundRole(QPalette::PlaceholderText);
  for (QLabel* label : {title_, artist_}) {
    label->setMinimumWidth(80);
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  }

  // How far through, as a thin bar you can click to seek.
  progress_->setFixedHeight(kProgressHeight + 8);
  progress_->setCursor(Qt::PointingHandCursor);
  progress_->installEventFilter(this);

  QVBoxLayout* text = new QVBoxLayout;
  text->setSpacing(1);
  text->addStretch();
  text->addWidget(title_);
  text->addWidget(artist_);
  text->addWidget(progress_);
  text->addStretch();

  previous_ = MakeButton();
  play_pause_ = MakeButton();
  next_ = MakeButton();
  QToolButton* expand = MakeButton();
  expand->setIcon(IconLoader::Load("view-fullscreen", IconLoader::Base));
  expand->setIconSize(QSize(16, 16));
  expand->setToolTip(tr("Show the full window"));
  connect(expand, SIGNAL(clicked()), SIGNAL(ExpandRequested()));

  QHBoxLayout* layout = new QHBoxLayout(this);
  layout->setContentsMargins(12, 12, 12, 12);
  layout->setSpacing(12);
  layout->addWidget(cover_);
  layout->addLayout(text, 1);
  layout->addWidget(previous_);
  layout->addWidget(play_pause_);
  layout->addWidget(next_);
  layout->addSpacing(4);
  layout->addWidget(expand, 0, Qt::AlignTop);

  setFixedHeight(kCoverSize + 24);
  resize(420, height());

  timer_->setInterval(500);
  connect(timer_, SIGNAL(timeout()), SLOT(UpdatePosition()));

  connect(app_->playlist_manager(), SIGNAL(CurrentSongChanged(Song)),
          SLOT(SongChanged(Song)));
  connect(app_->current_art_loader(), SIGNAL(ArtLoaded(Song, QString, QImage)),
          SLOT(ArtLoaded(Song, QString, QImage)));
  connect(app_->player(), &PlayerInterface::Stopped, this,
          [this]() { SongChanged(Song()); });

  SongChanged(Song());
}

QToolButton* MiniPlayer::MakeButton() {
  QToolButton* button = new QToolButton(this);
  button->setAutoRaise(true);
  button->setIconSize(QSize(22, 22));
  return button;
}

void MiniPlayer::SetActions(QAction* previous, QAction* play_pause,
                            QAction* next) {
  previous_->setDefaultAction(previous);
  play_pause_->setDefaultAction(play_pause);
  next_->setDefaultAction(next);
}

void MiniPlayer::SongChanged(const Song& song) {
  song_ = song;
  if (!song.is_valid()) {
    title_->setText(tr("Nothing playing"));
    artist_->clear();
    fraction_ = 0;
    progress_->update();
    timer_->stop();
    return;
  }
  title_->setText(song.PrettyTitle());
  QString artist = song.artist();
  if (!song.album().isEmpty()) {
    artist += (artist.isEmpty() ? "" : QString::fromUtf8(" — ")) + song.album();
  }
  artist_->setText(artist);
  timer_->start();
  UpdatePosition();
}

void MiniPlayer::ArtLoaded(const Song&, const QString&, const QImage& image) {
  const qreal dpr = devicePixelRatioF();
  QPixmap pixmap(QSize(kCoverSize, kCoverSize) * dpr);
  pixmap.setDevicePixelRatio(dpr);
  pixmap.fill(Qt::transparent);
  QPainter p(&pixmap);
  p.setRenderHint(QPainter::Antialiasing);
  p.setRenderHint(QPainter::SmoothPixmapTransform);
  QPainterPath clip;
  clip.addRoundedRect(QRectF(0, 0, kCoverSize, kCoverSize), 6, 6);
  p.setClipPath(clip);
  if (image.isNull()) {
    p.fillRect(QRectF(0, 0, kCoverSize, kCoverSize),
               palette().color(QPalette::Button));
  } else {
    p.drawImage(
        QRectF(0, 0, kCoverSize, kCoverSize),
        image.scaled(QSize(kCoverSize, kCoverSize) * dpr,
                     Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
  }
  p.end();
  cover_->setPixmap(pixmap);
}

void MiniPlayer::UpdatePosition() {
  const qint64 length = song_.length_nanosec();
  const qint64 position = app_->player()->engine()->position_nanosec();
  fraction_ = length > 0 ? qBound(0.0, double(position) / length, 1.0) : 0;
  progress_->update();
}

bool MiniPlayer::eventFilter(QObject* object, QEvent* event) {
  if (object != progress_) return QWidget::eventFilter(object, event);

  if (event->type() == QEvent::Paint) {
    QPainter p(progress_);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    const QRectF track(0, (progress_->height() - kProgressHeight) / 2.0,
                       progress_->width(), kProgressHeight);
    p.setBrush(palette().color(QPalette::Mid));
    p.drawRoundedRect(track, 2, 2);
    if (fraction_ > 0) {
      QRectF done = track;
      done.setWidth(qMax(kProgressHeight * 1.0, track.width() * fraction_));
      p.setBrush(Appearance::AccentColor(palette()));
      p.drawRoundedRect(done, 2, 2);
    }
    return true;
  }
  if (event->type() == QEvent::MouseButtonPress && song_.is_valid() &&
      song_.length_nanosec() > 0) {
    QMouseEvent* mouse = static_cast<QMouseEvent*>(event);
    const double at =
        qBound(0.0, mouse->position().x() / progress_->width(), 1.0);
    app_->player()->SeekTo(int(at * song_.length_nanosec() / kNsecPerSec));
    return true;
  }
  return QWidget::eventFilter(object, event);
}

void MiniPlayer::mousePressEvent(QMouseEvent* event) {
  // Drag it from anywhere that isn't a button.
  drag_offset_ = event->globalPosition().toPoint() - frameGeometry().topLeft();
}

void MiniPlayer::mouseMoveEvent(QMouseEvent* event) {
  if (event->buttons() & Qt::LeftButton) {
    move(event->globalPosition().toPoint() - drag_offset_);
  }
}

void MiniPlayer::closeEvent(QCloseEvent* event) {
  // Closing the mini player brings the full window back rather than
  // leaving Clementine with no window at all.
  event->ignore();
  emit ExpandRequested();
}
