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

#include "settingsdialog.h"

#include "appearancesettingspage.h"
#include "backgroundstreamssettingspage.h"
#include "behavioursettingspage.h"
#include "config.h"
#include "core/application.h"
#include "core/backgroundstreams.h"
#include "core/logging.h"
#include "core/networkproxyfactory.h"
#include "core/player.h"
#include "engines/enginebase.h"
#include "engines/gstengine.h"
#include "globalsearch/globalsearchsettingspage.h"
#include "globalshortcutssettingspage.h"
#include "iconloader.h"
#include "internet/core/internetsettingscategory.h"
#include "library/librarysettingspage.h"
#include "mainwindow.h"
#include "networkproxysettingspage.h"
#include "networkremotesettingspage.h"
#include "notificationssettingspage.h"
#include "playbacksettingspage.h"
#include "playlist/playlistview.h"
#include "settingscategory.h"
#include "songinfo/songinfosettingspage.h"
#include "songmetadatasettingspage.h"
#include "transcoder/transcodersettingspage.h"
#include "ui_settingsdialog.h"
#include "widgets/groupediconview.h"
#include "widgets/osdpretty.h"

#ifdef HAVE_WIIMOTEDEV
#include "wiimotedev/wiimotesettingspage.h"
#endif

#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QVBoxLayout>
#include <QWindow>

SettingsItemDelegate::SettingsItemDelegate(QObject* parent)
    : QStyledItemDelegate(parent) {}

QSize SettingsItemDelegate::sizeHint(const QStyleOptionViewItem& option,
                                     const QModelIndex& index) const {
  const bool is_separator =
      index.data(SettingsDialog::Role_IsSeparator).toBool();
  QSize ret = QStyledItemDelegate::sizeHint(option, index);

  if (is_separator) {
    ret.setHeight(ret.height() * 2);
  }

  return ret;
}

void SettingsItemDelegate::paint(QPainter* painter,
                                 const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const {
  const bool is_separator =
      index.data(SettingsDialog::Role_IsSeparator).toBool();

  if (is_separator) {
    GroupedIconView::DrawHeader(painter, option.rect, option.font,
                                option.palette, index.data().toString(),
                                option.state & QStyle::State_Selected);
  } else {
    QStyledItemDelegate::paint(painter, option, index);
  }
}

SettingsDialog::SettingsDialog(Application* app, BackgroundStreams* streams,
                               QWidget* parent)
    : QDialog(parent),
      app_(app),
      model_(app_->directory_model()),
      gst_engine_(app_->player()->gst_engine()),
      song_info_view_(nullptr),
      streams_(streams),
      global_search_(app_->global_search()),
      appearance_(app_->appearance()),
      ui_(new Ui_SettingsDialog),
      filter_(nullptr),
      loading_settings_(false) {
  ui_->setupUi(this);
  ui_->list->setItemDelegate(new SettingsItemDelegate(this));

  // uic's generated setupUi() types its parameter as the .ui file's nominal
  // base class (QDialog), so a .ui-file <connections> entry targeting a
  // SettingsDialog-only slot like this one can't resolve at compile time -
  // connect it explicitly here instead.
  connect(ui_->buttonBox, SIGNAL(clicked(QAbstractButton*)),
          SLOT(DialogButtonClicked(QAbstractButton*)));

  // Grouped by what you're trying to do, the everyday ones first and the
  // ones most people never touch under Advanced.
  SettingsCategory* playback = new SettingsCategory(tr("Playback"), this);
  AddCategory(playback);
  playback->AddPage(Page_Playback, new PlaybackSettingsPage(this));
  playback->AddPage(Page_Behaviour, new BehaviourSettingsPage(this));
  playback->AddPage(Page_BackgroundStreams,
                    new BackgroundStreamsSettingsPage(this));

  SettingsCategory* library = new SettingsCategory(tr("Library"), this);
  AddCategory(library);
  library->AddPage(Page_Library, new LibrarySettingsPage(this));
  library->AddPage(Page_SongMetadata, new SongMetadataSettingsPage(this));
  library->AddPage(Page_SongInformation, new SongInfoSettingsPage(this));
  library->AddPage(Page_GlobalSearch, new GlobalSearchSettingsPage(this));

  SettingsCategory* look = new SettingsCategory(tr("Appearance"), this);
  AddCategory(look);
  look->AddPage(Page_Appearance, new AppearanceSettingsPage(this));
  NotificationsSettingsPage* notification_page =
      new NotificationsSettingsPage(this);
  look->AddPage(Page_Notifications, notification_page);
  connect(notification_page,
          SIGNAL(NotificationPreview(OSD::Behaviour, QString, QString)),
          SIGNAL(NotificationPreview(OSD::Behaviour, QString, QString)));

  SettingsCategory* controls =
      new SettingsCategory(tr("Shortcuts and remotes"), this);
  AddCategory(controls);
  controls->AddPage(Page_GlobalShortcuts,
                    new GlobalShortcutsSettingsPage(this));
  controls->AddPage(Page_NetworkRemote, new NetworkRemoteSettingsPage(this));
#ifdef HAVE_WIIMOTEDEV
  WiimoteSettingsPage* wii_page = new WiimoteSettingsPage(this);
  controls->AddPage(Page_Wiimotedev, wii_page);
  connect(wii_page, SIGNAL(SetWiimotedevInterfaceActived(bool)),
          SIGNAL(SetWiimotedevInterfaceActived(bool)));
#endif

  // Internet services: Last.fm, podcasts, cloud storage and the rest.
  AddCategory(new InternetSettingsCategory(this));

  SettingsCategory* advanced = new SettingsCategory(tr("Advanced"), this);
  AddCategory(advanced);
  advanced->AddPage(Page_Proxy, new NetworkProxySettingsPage(this));
  advanced->AddPage(Page_Transcoding, new TranscoderSettingsPage(this));

  // The pages' own icons are a mix of styles; with the line icons on, each
  // gets the line icon for what it's about. Services keep their logos.
  if (IconLoader::UsesLineIcons()) {
    const QMap<Page, QString> icons = {
        {Page_Playback, "media-playback-start"},
        {Page_Behaviour, "configure"},
        {Page_BackgroundStreams, "weather-showers-scattered"},
        {Page_Library, "folder-sound"},
        {Page_SongMetadata, "edit-rename"},
        {Page_SongInformation, "view-media-lyrics"},
        {Page_GlobalSearch, "system-search"},
        {Page_Appearance, "view-media-visualization"},
        {Page_Notifications, "x-clementine-notification"},
        {Page_GlobalShortcuts, "input-keyboard"},
        {Page_NetworkRemote, "ipodtouchicon"},
        {Page_Wiimotedev, "multimedia-player-ipod-mini-blue"},
        {Page_Proxy, "applications-internet"},
        {Page_Transcoding, "tools-wizard"},
    };
    for (auto it = icons.begin(); it != icons.end(); ++it) {
      if (pages_.contains(it.key())) {
        pages_[it.key()].item_->setIcon(
            0, IconLoader::Load(it.value(), IconLoader::Base));
      }
    }
  }

  // Search, above the list.
  filter_ = new QLineEdit(this);
  filter_->setPlaceholderText(tr("Search settings"));
  filter_->setClearButtonEnabled(true);
  filter_->addAction(IconLoader::Load("system-search", IconLoader::Base),
                     QLineEdit::LeadingPosition);
  connect(filter_, SIGNAL(textChanged(QString)), SLOT(Filter(QString)));
  // Pages have lists named "list" of their own.
  ui_->list->setObjectName("settings_list");
  ui_->list->setFrameShape(QFrame::NoFrame);
  ui_->list->setIconSize(QSize(18, 18));
  QWidget* side = new QWidget(this);
  QVBoxLayout* side_layout = new QVBoxLayout(side);
  side_layout->setContentsMargins(0, 0, 0, 0);
  side_layout->setSpacing(8);
  ui_->horizontalLayout_2->replaceWidget(ui_->list, side);
  side_layout->addWidget(filter_);
  side_layout->addWidget(ui_->list);

  // The page's name, as a heading.
  QFont title_font = ui_->title->font();
  title_font.setPointSizeF(title_font.pointSizeF() * 1.5);
  title_font.setWeight(QFont::Bold);
  ui_->title->setFont(title_font);

  // List box
  connect(ui_->list,
          SIGNAL(currentItemChanged(QTreeWidgetItem*, QTreeWidgetItem*)),
          SLOT(CurrentItemChanged(QTreeWidgetItem*)));
  ui_->list->setCurrentItem(pages_[Page_Playback].item_);

  // Make sure the list is big enough to show all the items, and the search
  // field above it no wider.
  const int list_width =
      static_cast<QAbstractItemView*>(ui_->list)->sizeHintForColumn(0) +
      ui_->list->verticalScrollBar()->sizeHint().width() + 8;
  ui_->list->setMinimumWidth(list_width);
  side->setFixedWidth(list_width);

  ui_->buttonBox->button(QDialogButtonBox::Cancel)
      ->setShortcut(QKeySequence::Close);
}

SettingsDialog::~SettingsDialog() { delete ui_; }

void SettingsDialog::AddCategory(SettingsCategory* category) {
  ui_->list->invisibleRootItem()->addChild(category);
  // This must not be called before it's added to the parent.
  category->setExpanded(true);
}

void SettingsDialog::AddPageToStack(Page id, SettingsPage* page,
                                    QTreeWidgetItem* item) {
  // Create a scroll area containing the page
  QScrollArea* area = new QScrollArea;
  area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  area->setWidget(page);
  area->setWidgetResizable(true);
  area->setFrameShape(QFrame::NoFrame);
  area->setMinimumWidth(page->layout()->minimumSize().width());

  // Stop value widgets from stealing wheel events while scrolling the page.
  for (QWidget* widget : page->findChildren<QWidget*>()) {
    if (IgnoresWheelEvents(widget)) {
      widget->installEventFilter(this);
    }
  }

  // Add the page to the stack
  ui_->stacked_widget->addWidget(area);

  // Remember where the page is
  PageData data;
  data.item_ = item;
  data.scroll_area_ = area;
  data.page_ = page;
  pages_[id] = data;
}

bool SettingsDialog::IgnoresWheelEvents(const QWidget* widget) {
  return qobject_cast<const QComboBox*>(widget) ||
         qobject_cast<const QSlider*>(widget) ||
         qobject_cast<const QAbstractSpinBox*>(widget);
}

bool SettingsDialog::eventFilter(QObject* object, QEvent* event) {
  if (event->type() == QEvent::Wheel) {
    QWidget* widget = qobject_cast<QWidget*>(object);
    if (widget && widget->window() == this && IgnoresWheelEvents(widget)) {
      // Scroll the page containing the widget instead of changing its value.
      for (QWidget* w = widget->parentWidget(); w; w = w->parentWidget()) {
        if (QScrollArea* area = qobject_cast<QScrollArea*>(w)) {
          QCoreApplication::sendEvent(area->verticalScrollBar(), event);
          break;
        }
      }
      return true;
    }
  }
  return QDialog::eventFilter(object, event);
}

void SettingsDialog::accept() {
  for (const PageData& data : pages_.values()) {
    data.page_->Accept();
  }
  QDialog::accept();
}

void SettingsDialog::reject() {
  // Notify each page that user clicks on Cancel
  for (const PageData& data : pages_.values()) {
    data.page_->Reject();
  }

  QDialog::reject();
}

void SettingsDialog::DialogButtonClicked(QAbstractButton* button) {
  // While we only connect Apply at the moment, this might change in the future
  if (ui_->buttonBox->button(QDialogButtonBox::Apply) == button) {
    for (const PageData& data : pages_.values()) {
      data.page_->Apply();
    }
  }
}

void SettingsDialog::showEvent(QShowEvent* e) {
  // Load settings
  loading_settings_ = true;
  for (const PageData& data : pages_.values()) {
    data.page_->Load();
  }
  loading_settings_ = false;

  // Resize the dialog if it's too big
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
  QScreen* screen = QWidget::screen();
#else
  QScreen* screen =
      (window() && window()->windowHandle() ? window()->windowHandle()->screen()
                                            : QGuiApplication::primaryScreen());
#endif
  if (screen) {
    const QRect available = screen->availableGeometry();
    if (available.height() < height()) {
      resize(width(), sizeHint().height());
    }
  }

  QDialog::showEvent(e);
}

void SettingsDialog::OpenAtPage(Page page) {
  if (!pages_.contains(page)) {
    return;
  }

  ui_->list->setCurrentItem(pages_[page].item_);
  show();
}

bool SettingsDialog::PageMentions(const PageData& data, const QString& text) {
  auto has = [&text](QString words) {
    // Mnemonics and markup aren't part of what people search for.
    words.remove('&');
    words.remove(QRegularExpression("<[^>]*>"));
    return words.contains(text, Qt::CaseInsensitive);
  };
  if (has(data.item_->text(0))) return true;
  for (QLabel* label : data.page_->findChildren<QLabel*>()) {
    if (has(label->text())) return true;
  }
  for (QAbstractButton* button : data.page_->findChildren<QAbstractButton*>()) {
    if (has(button->text())) return true;
  }
  for (QGroupBox* group : data.page_->findChildren<QGroupBox*>()) {
    if (has(group->title())) return true;
  }
  return false;
}

void SettingsDialog::Filter(const QString& text) {
  const QString query = text.trimmed();
  QTreeWidgetItem* first = nullptr;
  for (const PageData& data : pages_.values()) {
    const bool shown = query.isEmpty() || PageMentions(data, query);
    data.item_->setHidden(!shown);
  }

  // A category shows while anything in it does; a category that's a page
  // itself (Internet services) shows its children's matches too.
  QTreeWidgetItem* root = ui_->list->invisibleRootItem();
  for (int i = 0; i < root->childCount(); ++i) {
    QTreeWidgetItem* category = root->child(i);
    bool any = false;
    for (int j = 0; j < category->childCount(); ++j) {
      QTreeWidgetItem* child = category->child(j);
      if (!child->isHidden()) {
        any = true;
        if (!first) first = child;
      }
    }
    const bool is_page = category->flags() & Qt::ItemIsSelectable;
    if (is_page && any) category->setHidden(false);
    if (!is_page) category->setHidden(!any);
    if (is_page && !category->isHidden() && !first) first = category;
  }

  QTreeWidgetItem* current = ui_->list->currentItem();
  if ((!current || current->isHidden()) && first) {
    ui_->list->setCurrentItem(first);
  }
}

void SettingsDialog::CurrentItemChanged(QTreeWidgetItem* item) {
  if (!(item->flags() & Qt::ItemIsSelectable)) {
    return;
  }

  // Set the title
  ui_->title->setText(item->text(0));

  // Display the right page
  for (const PageData& data : pages_.values()) {
    if (data.item_ == item) {
      ui_->stacked_widget->setCurrentWidget(data.scroll_area_);
      return;
    }
  }
  qLog(Debug) << "Didn't find page for item!";
}
