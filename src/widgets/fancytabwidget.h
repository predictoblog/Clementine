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

#ifndef FANCYTABWIDGET_H
#define FANCYTABWIDGET_H

#include <QSet>
#include <QTabWidget>
#include <QWidget>
#include <memory>

class QActionGroup;
class QMenu;
class QSettings;

namespace Core {
namespace Internal {

class FancyTabWidget : public QTabWidget {
  Q_OBJECT

 public:
  FancyTabWidget(QWidget* parent = 0);
  int addTab(QWidget* page, const QIcon& icon, const QString& label);
  int insertTab(int index, QWidget* page, const QIcon& icon,
                const QString& label);
  void addBottomWidget(QWidget* widget);

  void setBackgroundPixmap(const QPixmap& pixmap);
  void addSpacer();
  // A caption heading the tabs added after it, in the source list. Like a
  // spacer, it's a disabled tab with an empty page.
  void addSection(const QString& title);
  bool isSection(int index) const;

  // Shows the tab holding `page`, which can be the widget passed to addTab()
  // or anything inside it.
  void setCurrentPage(QWidget* page);

  // A tab that comes and goes as the program runs, like one per open
  // playlist: left out of the saved order, which only the fixed tabs have.
  int insertTransientTab(int index, QWidget* page, const QIcon& icon,
                         const QString& label);
  // A tab's position among the fixed tabs, or -1 for a transient one; and
  // back. Saved positions use these, so transient tabs don't shift them.
  int fixedIndex(int index) const;
  int indexOfFixed(int position) const;

  void loadSettings(const QSettings&);
  void saveSettings(QSettings*);
  // Values are persisted - only add to the end
  enum Mode {
    Mode_None = 0,
    Mode_LargeSidebar,
    Mode_SmallSidebar,
    Mode_Tabs,
    Mode_IconOnlyTabs,
    Mode_PlainSidebar,
    Mode_SourceList,
  };

  static const QSize TabSize_LargeSidebar;

  static const QSize IconSize_LargeSidebar;
  static const QSize IconSize_SmallSidebar;
  static const int kSourceListWidth;

  Mode mode() { return mode_; }

 signals:
  void ModeChanged(FancyTabWidget::Mode mode);
  void CurrentChanged(int);

 public slots:
  void setCurrentIndex(int index);
  void SetMode(Mode mode);
  // Mapper mapped signal needs this convenience function
  void SetMode(int mode) { SetMode(Mode(mode)); }

 private slots:
  void tabBarUpdateGeometry();
  void currentTabChanged(int);

 protected:
  void paintEvent(QPaintEvent*);
  void contextMenuEvent(QContextMenuEvent* e);

 private:
  void addMenuItem(QActionGroup* group, const QString& text, Mode mode);

  QPixmap background_pixmap_;
  QMenu* menu_;
  Mode mode_;
  QWidget* bottom_widget_;
  QSet<QWidget*> sections_;
};

}  // namespace Internal
}  // namespace Core

using Core::Internal::FancyTabWidget;

#endif  // FANCYTABWIDGET_H
