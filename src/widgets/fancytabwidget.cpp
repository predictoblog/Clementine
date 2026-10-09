/* This file is part of Clementine.
   Copyright 2018, Vikram Ambrose <ambroseworks@gmail.com>

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

#include "fancytabwidget.h"

#include <QActionGroup>
#include <QDebug>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QSettings>
#include <QStylePainter>
#include <QTabBar>
#include <QTimer>
#include <QVBoxLayout>

#include "core/appearance.h"
#include "core/logging.h"
#include "stylehelper.h"

const QSize FancyTabWidget::IconSize_LargeSidebar = QSize(24, 24);
const QSize FancyTabWidget::IconSize_SmallSidebar = QSize(22, 22);

const QSize FancyTabWidget::TabSize_LargeSidebar = QSize(70, 47);

// The source list: a labelled row per page, a caption per section.
const int FancyTabWidget::kSourceListWidth = 188;
static const int kSourceListRowHeight = 32;

// Where the tabs' order is saved. Renamed when the source list added
// sections and moved Song info and Artist info out, so an order saved for the
// old set of tabs isn't applied to the new one.
static const char* kTabOrderKey = "tab_order4_";
static const int kSourceListSectionHeight = 34;
static const int kSourceListSpacerHeight = 10;
static const int kSourceListIconSize = 18;

class FancyTabBar : public QTabBar {
 private:
  int mouseHoverTabIndex = -1;
  bool isTextHiddenInToolTip = false;

 public:
  explicit FancyTabBar(QWidget* parent = 0) : QTabBar(parent) {
    setMouseTracking(true);
  }

  QSize sizeHint() const {
    QSize size(QTabBar::sizeHint());

    FancyTabWidget* tabWidget = (FancyTabWidget*)parentWidget();
    if (tabWidget->mode() == FancyTabWidget::Mode_Tabs ||
        tabWidget->mode() == FancyTabWidget::Mode_IconOnlyTabs)
      return size;

    QSize tabSize(tabSizeHint(0));
    size.setWidth(tabSize.width());
    int guessHeight = 0;
    for (int i = 0; i < count(); ++i) {
      if (isTabVisible(i)) guessHeight += tabSizeHint(i).height();
    }
    if (guessHeight > size.height()) size.setHeight(guessHeight);
    return size;
  }

  int width() { return tabSizeHint(0).width(); }

 protected:
  QSize tabSizeHint(int index) const {
    FancyTabWidget* tabWidget = (FancyTabWidget*)parentWidget();
    QSize size = FancyTabWidget::TabSize_LargeSidebar;

    if (tabWidget->mode() == FancyTabWidget::Mode_SourceList) {
      int height = kSourceListRowHeight;
      if (tabWidget->isSection(index)) {
        height = kSourceListSectionHeight;
      } else if (tabText(index).isEmpty()) {
        height = kSourceListSpacerHeight;
      }
      return QSize(FancyTabWidget::kSourceListWidth, height);
    }

    if (tabWidget->mode() != FancyTabWidget::Mode_LargeSidebar) {
      size = QTabBar::tabSizeHint(index);
    }

    return size;
  }

  void leaveEvent(QEvent* event) {
    mouseHoverTabIndex = -1;
    update();
  }

  void mouseMoveEvent(QMouseEvent* event) {
    QPoint pos = event->pos();

    mouseHoverTabIndex = tabAt(pos);
    if (mouseHoverTabIndex > -1) update();
    QTabBar::mouseMoveEvent(event);
  }

  void paintEvent(QPaintEvent* pe) {
    FancyTabWidget* tabWidget = (FancyTabWidget*)parentWidget();

    bool verticalTextTabs = false;

    if (tabWidget->mode() == FancyTabWidget::Mode_SmallSidebar)
      verticalTextTabs = true;

    // Restore any label text that was hidden/cached for the IconOnlyTabs mode
    if (isTextHiddenInToolTip &&
        tabWidget->mode() != FancyTabWidget::Mode_IconOnlyTabs) {
      for (int i = 0; i < count(); i++) {
        setTabText(i, tabToolTip(i));
        setTabToolTip(i, "");
      }
      isTextHiddenInToolTip = false;
    }
    if (tabWidget->mode() == FancyTabWidget::Mode_SourceList) {
      PaintSourceList(tabWidget);
      return;
    }
    if (tabWidget->mode() != FancyTabWidget::Mode_LargeSidebar &&
        tabWidget->mode() != FancyTabWidget::Mode_SmallSidebar) {
      // Cache and hide label text for IconOnlyTabs mode
      if (tabWidget->mode() == FancyTabWidget::Mode_IconOnlyTabs &&
          !isTextHiddenInToolTip) {
        for (int i = 0; i < count(); i++) {
          isTextHiddenInToolTip = true;
          setTabToolTip(i, tabText(i));
          setTabText(i, "");
        }
      }
      QTabBar::paintEvent(pe);
      return;
    }

    QStylePainter p(this);

    for (int index = 0; index < count(); index++) {
      const bool selected = tabWidget->currentIndex() == index;
      ;

      QRect tabrect = tabRect(index);

      QRect selectionRect = tabrect;

      // Selected and hovered tabs get a rounded fill in the palette's raised
      // colour, inset from the sidebar's edges: flat, like a source list,
      // rather than the old glossy bevel.
      const QRect pill = selectionRect.adjusted(4, 2, -4, -2);
      const bool hovered =
          !selected && index == mouseHoverTabIndex && isTabEnabled(index);
      if (selected || hovered) {
        QColor fill = palette().color(QPalette::Button);
        if (hovered) fill.setAlphaF(0.6);
        p.save();
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawRoundedRect(pill, 6, 6);
        p.restore();
      }

      // Label (Icon and Text)
      {
        p.save();
        QTransform m;
        int textFlags;
        Qt::Alignment iconFlags;

        QRect tabrectText;
        QRect tabrectLabel;

        if (verticalTextTabs) {
          m = QTransform::fromTranslate(tabrect.left(), tabrect.bottom());
          m.rotate(-90);
          textFlags = Qt::AlignLeft | Qt::AlignVCenter;
          iconFlags = Qt::AlignLeft | Qt::AlignVCenter;

          tabrectLabel = QRect(QPoint(0, 0), m.mapRect(tabrect).size());

          tabrectText = tabrectLabel;
          tabrectText.translate(30, 0);
        } else {
          m = QTransform::fromTranslate(tabrect.left(), tabrect.top());
          textFlags = Qt::AlignHCenter | Qt::AlignBottom;
          iconFlags = Qt::AlignHCenter | Qt::AlignTop;

          tabrectLabel = QRect(QPoint(0, 0), m.mapRect(tabrect).size());

          tabrectText = tabrectLabel;
          tabrectText.translate(0, -5);
        }

        p.setTransform(m);

        QFont font(p.font());
        font.setPointSizeF(Utils::StyleHelper::sidebarFontSize());
        font.setWeight(selected ? QFont::DemiBold : QFont::Normal);
        p.setFont(font);

        // The selected tab takes the accent colour; the icon below is drawn
        // with the same pen, so a monochrome icon follows the text.
        p.translate(0, 2);
        p.setPen(selected ? Appearance::AccentColor(palette())
                          : palette().color(QPalette::WindowText));
        p.drawText(tabrectText, textFlags, tabText(index));

        // Draw the icon
        QRect tabrectIcon;
        const int PADDING = 5;
        if (verticalTextTabs) {
          tabrectIcon = tabrectLabel;
          tabrectIcon.setSize(FancyTabWidget::IconSize_SmallSidebar);
          tabrectIcon.translate(PADDING, PADDING);
        } else {
          tabrectIcon = tabrectLabel;
          tabrectIcon.setSize(FancyTabWidget::IconSize_LargeSidebar);
          // Center the icon
          const int moveRight =
              (FancyTabWidget::TabSize_LargeSidebar.width() -
               FancyTabWidget::IconSize_LargeSidebar.width() - 1) /
              2;
          tabrectIcon.translate(moveRight, PADDING);
        }
        tabIcon(index).paint(&p, tabrectIcon, iconFlags);
        p.restore();
      }
    }
  }

 private:
  // The source list: a labelled row per page with its icon on the left, and
  // small uppercase captions for the sections, like iTunes' source list.
  void PaintSourceList(FancyTabWidget* tabWidget) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor text = palette().color(QPalette::WindowText);
    const QColor quiet = Appearance::QuietTextColor(palette());
    const QColor accent = Appearance::AccentColor(palette());

    for (int index = 0; index < count(); index++) {
      if (!isTabVisible(index)) continue;
      const QRect rect = tabRect(index);
      const QString label = tabText(index);
      if (label.isEmpty()) continue;  // a spacer

      if (tabWidget->isSection(index)) {
        QFont font(this->font());
        font.setPointSizeF(font.pointSizeF() * 0.8);
        font.setWeight(QFont::DemiBold);
        font.setLetterSpacing(QFont::PercentageSpacing, 106);
        font.setCapitalization(QFont::AllUppercase);
        p.setFont(font);
        p.setPen(quiet);
        p.drawText(rect.adjusted(14, 0, -8, -6),
                   Qt::AlignLeft | Qt::AlignBottom, label);
        continue;
      }

      const bool selected = tabWidget->currentIndex() == index;
      const bool hovered =
          !selected && index == mouseHoverTabIndex && isTabEnabled(index);
      if (selected || hovered) {
        QColor fill = palette().color(QPalette::Button);
        if (hovered) fill.setAlphaF(0.6);
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawRoundedRect(QRectF(rect).adjusted(8, 2, -8, -2), 6, 6);
      }

      const QColor color = selected ? accent : text;
      // From the widget, not the painter, which a section caption may have
      // left small and in capitals.
      QFont font(this->font());
      font.setWeight(selected ? QFont::DemiBold : QFont::Normal);
      p.setFont(font);

      // Monochrome icons take the pen's colour.
      p.setPen(color);
      const QRect icon_rect(
          rect.left() + 16,
          rect.top() + (rect.height() - kSourceListIconSize) / 2,
          kSourceListIconSize, kSourceListIconSize);
      tabIcon(index).paint(&p, icon_rect);

      const QRect text_rect =
          rect.adjusted(16 + kSourceListIconSize + 10, 0, -12, 0);
      p.drawText(text_rect, Qt::AlignLeft | Qt::AlignVCenter,
                 QFontMetrics(font).elidedText(label, Qt::ElideRight,
                                               text_rect.width()));
    }
  }
};

// Spacers are just disabled pages
void FancyTabWidget::addSpacer() {
  QWidget* spacer = new QWidget();
  const int index = addTab(spacer, QIcon(), QString());
  setTabEnabled(index, false);
}

void FancyTabWidget::addSection(const QString& title) {
  QWidget* section = new QWidget();
  const int index = addTab(section, QIcon(), title);
  setTabEnabled(index, false);
  sections_.insert(widget(index));
}

bool FancyTabWidget::isSection(int index) const {
  return sections_.contains(widget(index));
}

void FancyTabWidget::setCurrentPage(QWidget* page) {
  // Each tab's widget is a wrapper holding the page (see insertTab), so
  // QTabWidget::setCurrentWidget(page) alone never finds it.
  for (int i = 0; i < count(); ++i) {
    QWidget* wrapper = widget(i);
    if (wrapper == page || wrapper->isAncestorOf(page)) {
      setCurrentIndex(i);
      return;
    }
  }
}

void FancyTabWidget::setBackgroundPixmap(const QPixmap& pixmap) {
  background_pixmap_ = pixmap;
  update();
}

void FancyTabWidget::setCurrentIndex(int index) {
  QWidget* currentPage = widget(index);

  QLayout* layout = currentPage->layout();
  if (bottom_widget_ != nullptr) layout->addWidget(bottom_widget_);

  QTabWidget::setCurrentIndex(index);
}

// Slot
void FancyTabWidget::currentTabChanged(int index) {
  QWidget* currentPage = currentWidget();

  QLayout* layout = currentPage->layout();
  if (bottom_widget_ != nullptr) layout->addWidget(bottom_widget_);
  emit CurrentChanged(index);
}

FancyTabWidget::FancyTabWidget(QWidget* parent)
    : QTabWidget(parent),
      menu_(nullptr),
      mode_(Mode_None),
      bottom_widget_(nullptr) {
  FancyTabBar* tabBar = new FancyTabBar(this);

  setTabBar(tabBar);
  setTabPosition(QTabWidget::West);
  setMovable(true);

  connect(tabBar, SIGNAL(currentChanged(int)), this,
          SLOT(currentTabChanged(int)));
}

void FancyTabWidget::loadSettings(const QSettings& settings) {
  for (int i = 0; i < count(); i++) {
    int originalIndex = tabBar()->tabData(i).toInt();
    QString k = kTabOrderKey + QString::number(originalIndex);

    int newIndex = settings.value(k, i).toInt();

    if (newIndex >= 0)
      tabBar()->moveTab(i, newIndex);
    else
      removeTab(i);  // Does not delete page
  }
}

void FancyTabWidget::saveSettings(QSettings* settings) {
  // Positions among the fixed tabs only: the ones added and removed as the
  // program runs aren't there when the order is loaded back.
  for (int i = 0; i < count(); i++) {
    const int position = fixedIndex(i);
    if (position < 0) continue;
    int originalIndex = tabBar()->tabData(i).toInt();
    QString k = kTabOrderKey + QString::number(originalIndex);

    settings->setValue(k, position);
  }
}

int FancyTabWidget::insertTransientTab(int index, QWidget* page,
                                       const QIcon& icon,
                                       const QString& label) {
  const int actual = insertTab(index, page, icon, label);
  tabBar()->setTabData(actual, QVariant(-1));
  return actual;
}

int FancyTabWidget::fixedIndex(int index) const {
  if (index < 0 || index >= count()) return -1;
  if (tabBar()->tabData(index).toInt() < 0) return -1;
  int position = 0;
  for (int i = 0; i < index; ++i) {
    if (tabBar()->tabData(i).toInt() >= 0) ++position;
  }
  return position;
}

int FancyTabWidget::indexOfFixed(int position) const {
  for (int i = 0; i < count(); ++i) {
    if (fixedIndex(i) == position) return i;
  }
  return -1;
}

void FancyTabWidget::addBottomWidget(QWidget* widget) {
  bottom_widget_ = widget;
}

int FancyTabWidget::addTab(QWidget* page, const QIcon& icon,
                           const QString& label) {
  return insertTab(count(), page, icon, label);
}

int FancyTabWidget::insertTab(int index, QWidget* page, const QIcon& icon,
                              const QString& label) {
  // In order to achieve the same effect as the "Bottom Widget" of the
  // old Nokia based FancyTabWidget a VBoxLayout is used on each page
  QVBoxLayout* layout = new QVBoxLayout();
  layout->setSpacing(0);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(page);

  QWidget* newPage = new QWidget();
  newPage->setLayout(layout);

  const int actualIndex = QTabWidget::insertTab(index, newPage, icon, label);

  // Remember the original index. Needed to save order of tabs
  tabBar()->setTabData(actualIndex, QVariant(actualIndex));
  return actualIndex;
}

void FancyTabWidget::paintEvent(QPaintEvent* pe) {
  if (mode() != FancyTabWidget::Mode_LargeSidebar &&
      mode() != FancyTabWidget::Mode_SmallSidebar &&
      mode() != FancyTabWidget::Mode_SourceList) {
    QTabWidget::paintEvent(pe);
    return;
  }
  QStylePainter p(this);

  // A flat panel in the palette's window colour, with a hairline on the
  // edge it shares with the content.
  QRect backgroundRect = rect();
  backgroundRect.setWidth(((FancyTabBar*)tabBar())->width());
  p.fillRect(backgroundRect, palette().color(QPalette::Window));

  p.setPen(palette().color(QPalette::Mid));
  p.drawLine(backgroundRect.topRight(), backgroundRect.bottomRight());
}

void FancyTabWidget::tabBarUpdateGeometry() { tabBar()->updateGeometry(); }

void FancyTabWidget::SetMode(FancyTabWidget::Mode mode) {
  mode_ = mode;

  if (mode == FancyTabWidget::Mode_Tabs ||
      mode == FancyTabWidget::Mode_IconOnlyTabs) {
    setTabPosition(QTabWidget::North);
  } else {
    setTabPosition(QTabWidget::West);
  }

  tabBar()->updateGeometry();
  updateGeometry();

  // There appears to be a bug in QTabBar which causes tabSizeHint
  // to be ignored thus the need for this second shot repaint
  QTimer::singleShot(1, this, SLOT(tabBarUpdateGeometry()));

  emit ModeChanged(mode);
}

void FancyTabWidget::addMenuItem(QActionGroup* group, const QString& text,
                                 Mode mode) {
  QAction* action = group->addAction(text);
  action->setCheckable(true);
  connect(action, &QAction::triggered, [this, mode]() { SetMode(mode); });

  if (mode == mode_) action->setChecked(true);
}

void FancyTabWidget::contextMenuEvent(QContextMenuEvent* e) {
  if (!menu_) {
    menu_ = new QMenu(this);

    QActionGroup* group = new QActionGroup(this);
    addMenuItem(group, tr("Source list"), Mode_SourceList);
    addMenuItem(group, tr("Large sidebar"), Mode_LargeSidebar);
    addMenuItem(group, tr("Small sidebar"), Mode_SmallSidebar);
    addMenuItem(group, tr("Plain sidebar"), Mode_PlainSidebar);
    addMenuItem(group, tr("Tabs on top"), Mode_Tabs);
    addMenuItem(group, tr("Icons on top"), Mode_IconOnlyTabs);
    menu_->addActions(group->actions());
  }

  menu_->popup(e->globalPos());
}
