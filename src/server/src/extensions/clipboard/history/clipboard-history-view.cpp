#include "clipboard-history-view.hpp"
#include "actions/root-search/root-search-actions.hpp"
#include "actions/files/file-actions.hpp"
#include "builtin_icon.hpp"
#include "clipboard-actions.hpp"
#include "common.hpp"
#include "environment.hpp"
#include "extensions/clipboard/history/clipboard-history-model.hpp"
#include "lib/keyboard/keyboard.hpp"
#include "navigation-controller.hpp"
#include "services/clipboard/clipboard-db.hpp"
#include "services/clipboard/clipboard-service.hpp"
#include "services/toast/toast-service.hpp"
#include "layout.hpp"
#include "theme.hpp"
#include "ui/detail/detail-widget.hpp"
#include "ui/empty-view/empty-view.hpp"
#include "ui/text-file-viewer/text-file-viewer.hpp"
#include "ui/action-pannel/push-action.hpp"
#include "ui/alert/alert.hpp"
#include "ui/form/text-area.hpp"
#include "ui/views/form-view.hpp"
#include "utils.hpp"
#include "services/app-service/app-service.hpp"
#include <algorithm>
#include <qapplication.h>
#include <qevent.h>
#include <qguiapplication.h>
#include <qlabel.h>
#include <qmimedata.h>
#include <qmimedatabase.h>
#include <qmimetype.h>
#include <qnamespace.h>
#include <qpainter.h>
#include <qscreen.h>
#include <qscrollarea.h>
#include <qscrollbar.h>
#include <qwidget.h>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

class TextContainer : public QWidget {
  QVBoxLayout *_layout;

public:
  void setWidget(QWidget *widget) { _layout->addWidget(widget); }

  TextContainer() {
    _layout = new QVBoxLayout;
    setLayout(_layout);
  }
};

class PasteClipboardSelection : public PasteToFocusedWindowAction {
  QString m_id;

  void execute(ApplicationContext *ctx) override {
    setConcealed();
    loadClipboardData(Clipboard::SelectionRecordHandle(m_id));
    PasteToFocusedWindowAction::execute(ctx);
  }

public:
  PasteClipboardSelection(const QString &id) : PasteToFocusedWindowAction(), m_id(id) {}
};

class PasteAsTextAction : public AbstractAction {
  QString m_id;

  void execute(ApplicationContext *ctx) override {
    auto clipman = ctx->services->clipman();
    auto wm = ctx->services->windowManager();
    auto selection = clipman->retrieveSelectionById(m_id);

    if (!selection) {
      ctx->services->toastService()->failure("Failed to retrieve selection");
      return;
    }

    // Find the text content (prefer text/plain, then text/uri-list)
    QString textContent;
    for (const auto &offer : selection->offers) {
      if (Utils::isTextMimeType(offer.mimeType) && !offer.data.isEmpty()) {
        textContent = QString::fromUtf8(offer.data);
        break;
      }
    }

    if (textContent.isEmpty()) {
      // Try text/uri-list as fallback
      for (const auto &offer : selection->offers) {
        if (offer.mimeType == "text/uri-list" && !offer.data.isEmpty()) {
          textContent = QString::fromUtf8(offer.data);
          break;
        }
      }
    }

    if (textContent.isEmpty()) {
      ctx->services->toastService()->failure("No text content to paste");
      return;
    }

    // Copy text to clipboard and paste
    clipman->copyContent(Clipboard::Text{textContent}, {.concealed = true});
    ctx->navigation->closeWindow();

    if (wm->canPaste()) {
      QTimer::singleShot(Environment::pasteDelay(), [wm]() { wm->provider()->pasteToWindow(nullptr, nullptr); });
    }
  }

public:
  PasteAsTextAction(const QString &id)
      : AbstractAction("Paste as text", ImageURL::builtin("text")), m_id(id) {}
};

class CopyClipboardSelection : public AbstractAction {
  QString m_id;

  void execute(ApplicationContext *ctx) override {
    auto clipman = ctx->services->clipman();
    auto toast = ctx->services->toastService();

    if (clipman->copySelectionRecord(m_id, {.concealed = true})) {
      ctx->navigation->showHud("Selection copied to clipboard");
      return;
    }

    toast->failure("Failed to copy to clipboard");
  }

public:
  CopyClipboardSelection(const QString &id)
      : AbstractAction("Copy to clipboard", BuiltinIcon::CopyClipboard), m_id(id) {}
};

// Image preview overlay - hovers above launcher, not limited by its size
class ImagePreviewWindow : public QWidget {
  Q_OBJECT

  QLabel *m_imageLabel;
  QPixmap m_originalPixmap;
  QWidget *m_launcherWindow;
  bool m_appEventFilterInstalled = false;

  bool isOwnEventTarget(QObject *obj) const {
    auto widget = qobject_cast<QWidget *>(obj);
    if (!widget) return false;
    return widget == this || isAncestorOf(widget);
  }

protected:
  bool eventFilter(QObject *watched, QEvent *event) override {
    if (event->type() != QEvent::KeyPress) { return QWidget::eventFilter(watched, event); }

    auto *keyEvent = static_cast<QKeyEvent *>(event);
    auto key = keyEvent->key();

    bool shouldClose = key == Qt::Key_Escape || key == Qt::Key_Up || key == Qt::Key_Down ||
                       key == Qt::Key_Left || key == Qt::Key_Right;
    if (!shouldClose) { return QWidget::eventFilter(watched, event); }

    // Let our own keyPressEvent handle forwarding behavior.
    if (isOwnEventTarget(watched)) { return QWidget::eventFilter(watched, event); }

    close();
    // Don't consume: allow the launcher to handle the same key press (navigation).
    return false;
  }

  void showEvent(QShowEvent *event) override {
    QWidget::showEvent(event);
    if (!m_appEventFilterInstalled) {
      qApp->installEventFilter(this);
      m_appEventFilterInstalled = true;
    }
  }

  void closeEvent(QCloseEvent *event) override {
    if (m_appEventFilterInstalled) {
      qApp->removeEventFilter(this);
      m_appEventFilterInstalled = false;
    }
    QWidget::closeEvent(event);
  }

  void keyPressEvent(QKeyEvent *event) override {
    // Close on Escape
    if (event->key() == Qt::Key_Escape) {
      close();
      return;
    }
    // Close and forward arrow keys to launcher for navigation
    if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down ||
        event->key() == Qt::Key_Left || event->key() == Qt::Key_Right) {
      close();
      if (m_launcherWindow) {
        QApplication::sendEvent(m_launcherWindow, event);
      }
      return;
    }
    QWidget::keyPressEvent(event);
  }

  void mousePressEvent(QMouseEvent *event) override {
    // Close on any mouse click (left or right)
    if (event->button() == Qt::LeftButton || event->button() == Qt::RightButton) {
      close();
      return;
    }
    QWidget::mousePressEvent(event);
  }

  void paintEvent(QPaintEvent *event) override {
    QPainter painter(this);
    // Draw dark background only
    painter.fillRect(rect(), QColor(30, 30, 30));
  }

  void resizeEvent(QResizeEvent *event) override {
    QWidget::resizeEvent(event);
    updateImageDisplay();
  }

  void updateImageDisplay() {
    if (m_originalPixmap.isNull()) return;

    // Calculate available space for image (minus margins)
    int availableWidth = width() - 30;
    int availableHeight = height() - 30;

    if (availableWidth <= 0 || availableHeight <= 0) return;

    // Scale image to fit while keeping aspect ratio
    QPixmap scaled = m_originalPixmap.scaled(
        availableWidth, availableHeight,
        Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_imageLabel->setPixmap(scaled);
  }

public:
  ImagePreviewWindow(const ImageURL &url, QWidget *launcherWindow = nullptr)
      : QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint | Qt::BypassWindowManagerHint),
        m_launcherWindow(launcherWindow) {
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_ShowWithoutActivating, false);
    setFocusPolicy(Qt::StrongFocus);

    m_imageLabel = new QLabel;
    m_imageLabel->setAlignment(Qt::AlignCenter);

    // Load image
    if (url.type() == ImageURLType::Local) {
      QString path = url.name();
      if (!path.isEmpty()) {
        m_originalPixmap = QPixmap(path);
      }
    }

    auto layout = new QVBoxLayout;
    layout->setContentsMargins(15, 15, 15, 15);
    layout->setSpacing(0);
    layout->addWidget(m_imageLabel);
    setLayout(layout);

    // Calculate window size - fit image while respecting screen bounds and aspect ratio
    QSize imageSize = m_originalPixmap.size();
    QScreen *screen = QGuiApplication::primaryScreen();
    QRect screenGeometry = screen->availableGeometry();

    int maxWidth = screenGeometry.width() * 0.85;
    int maxHeight = screenGeometry.height() * 0.85;

    // Start with image size + margins
    int windowWidth = imageSize.width() + 30;
    int windowHeight = imageSize.height() + 30;

    // Scale down if too large for screen, keeping aspect ratio
    if (windowWidth > maxWidth || windowHeight > maxHeight) {
      double scaleW = (double)maxWidth / windowWidth;
      double scaleH = (double)maxHeight / windowHeight;
      double scale = qMin(scaleW, scaleH);
      windowWidth = windowWidth * scale;
      windowHeight = windowHeight * scale;
    }

    // Minimum size
    windowWidth = qMax(windowWidth, 300);
    windowHeight = qMax(windowHeight, 200);

    resize(windowWidth, windowHeight);

    // Position centered on launcher window
    if (launcherWindow) {
      QRect launcherGeometry = launcherWindow->geometry();
      QPoint center = launcherGeometry.center();
      int x = center.x() - windowWidth / 2;
      int y = center.y() - windowHeight / 2;

      // Keep within screen bounds
      x = qMax(screenGeometry.left(), qMin(x, screenGeometry.right() - windowWidth));
      y = qMax(screenGeometry.top(), qMin(y, screenGeometry.bottom() - windowHeight));

      move(x, y);
    } else {
      move(screenGeometry.center() - rect().center());
    }

    // Initial display
    updateImageDisplay();
  }
};

class ClickableImageWidget : public QWidget {
  Q_OBJECT

  ImageWidget *m_image;
  ImageURL m_url;

protected:
  void mousePressEvent(QMouseEvent *event) override {
    if (event->button() == Qt::LeftButton) {
      emit clicked();
      return;
    }
    QWidget::mousePressEvent(event);
  }

  void enterEvent(QEnterEvent *event) override {
    QWidget::enterEvent(event);
    setCursor(Qt::PointingHandCursor);
  }

  void leaveEvent(QEvent *event) override {
    QWidget::leaveEvent(event);
    setCursor(Qt::ArrowCursor);
  }

public:
  ClickableImageWidget(QWidget *parent = nullptr) : QWidget(parent) {
    m_image = new ImageWidget;
    auto layout = new QVBoxLayout;
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_image);
    setLayout(layout);
  }

  void setUrl(const ImageURL &url) {
    m_url = url;
    m_image->setUrl(url);
  }

  void setContentsMargins(int left, int top, int right, int bottom) {
    m_image->setContentsMargins(left, top, right, bottom);
  }

  ImageURL url() const { return m_url; }

signals:
  void clicked();
};

class ClipboardHistoryDetail : public DetailWidget {
  QTemporaryFile m_tmpFile;

  std::vector<MetadataItem> createEntryMetadata(const ClipboardHistoryEntry &entry) const {
    auto mime = MetadataLabel{
        .text = entry.mimeType,
        .title = "Mime",
    };

    if (entry.encryption != ClipboardEncryptionType::None) {
      mime.icon = ImageURL::builtin("key").setFill(SemanticColor::Green);
    }

    auto size = MetadataLabel{
        .text = formatSize(entry.size),
        .title = "Size",
    };
    auto copiedAt = MetadataLabel{
        .text = QDateTime::fromSecsSinceEpoch(entry.updatedAt).toString(),
        .title = "Copied at",
    };
    auto checksum = MetadataLabel{
        .text = entry.md5sum,
        .title = "MD5",
    };

    return {mime, size, copiedAt, checksum};
  }

  QWidget *detailForFilePath(const fs::path &path) {
    auto mime = QMimeDatabase().mimeTypeForFile(path.c_str());

    if (mime.name().startsWith("image/")) {
      auto imageUrl = ImageURL::local(path);
      auto clickable = new ClickableImageWidget;
      clickable->setContentsMargins(10, 10, 10, 10);
      clickable->setUrl(imageUrl);
      connect(clickable, &ClickableImageWidget::clicked, this, [clickable, imageUrl]() {
        QWidget *topLevel = clickable->window();
        auto *previewWindow = new ImagePreviewWindow(imageUrl, topLevel);
        previewWindow->show();
        previewWindow->raise();
        previewWindow->activateWindow();
      });
      return clickable;
    }

    if (Utils::isTextMimeType(mime)) {
      auto viewer = new TextFileViewer;
      viewer->load(path);
      return VStack().add(viewer).buildWidget();
    }

    return detailForUnmatchedMime(mime);
  }

  QWidget *detailForUnmatchedMime(const QMimeType &mime) {
    auto icon = new ImageWidget;
    icon->setUrl(ImageURL::system(mime.genericIconName()));
    return icon;
  }

  QWidget *detailForMime(const QByteArray &data, const QString &mimeName) {
    QMimeType mime = QMimeDatabase().mimeTypeForName(Utils::normalizeMimeName(mimeName));

    if (mimeName == "text/uri-list") {
      QString text(data);
      auto paths = text.split("\r\n", Qt::SkipEmptyParts);

      if (paths.size() == 1) {
        QUrl url(paths.at(0));

        if (url.scheme() == "file") {
          std::error_code ec;
          fs::path path = url.path().toStdString();
          if (fs::is_regular_file(path, ec)) { return detailForFilePath(path); }
        }
      }
    }

    if (mimeName.startsWith("image/")) {
      if (!m_tmpFile.open()) {
        qWarning() << "Failed to open file";
        return detailForUnmatchedMime(mime);
      }

      m_tmpFile.write(data);
      m_tmpFile.close();
      auto imageUrl = ImageURL::local(m_tmpFile.filesystemFileName());
      auto clickable = new ClickableImageWidget;
      clickable->setContentsMargins(10, 10, 10, 10);
      clickable->setUrl(imageUrl);
      connect(clickable, &ClickableImageWidget::clicked, this, [clickable, imageUrl]() {
        QWidget *topLevel = clickable->window();
        auto *previewWindow = new ImagePreviewWindow(imageUrl, topLevel);
        previewWindow->show();
        previewWindow->raise();
        previewWindow->activateWindow();
      });
      return clickable;
    }

    if (Utils::isTextMimeType(mimeName) || mimeName == "text/uri-list") {
      auto viewer = new TextFileViewer;
      viewer->load(data);
      return VStack().add(viewer).buildWidget();
    }

    return detailForUnmatchedMime(mime);
  }

  QWidget *detailForFailedDecryption() {
    auto empty = new EmptyViewWidget;
    empty->setIcon(ImageURL::builtin("key").setFill(SemanticColor::Red));
    empty->setTitle("Decryption failed");
    empty->setDescription(
        "Vicinae could not decrypt the data for this selection. This is most likely caused by a "
        "keychain software change. To fix this disable encryption in the clipboard extension settings.");

    return empty;
  }

  QWidget *detailForMissingEncryption() {
    auto empty = new EmptyViewWidget;
    empty->setIcon(ImageURL::builtin("key").setFill(SemanticColor::Orange));
    empty->setTitle("Data is encrypted");
    empty->setDescription(
        "Data for this selection was previously encrypted but the clipboard is not currently "
        "configured to use encryption. You should be able to fix this by enabling it in the clipboard "
        "extension settings.");

    return empty;
  }

  QWidget *detailForError(ClipboardService::OfferDecryptionError error) {
    switch (error) {
    case ClipboardService::OfferDecryptionError::DecryptionRequired:
      return detailForMissingEncryption();
    case ClipboardService::OfferDecryptionError::DecryptionFailed:
      return detailForFailedDecryption();
    }
    return nullptr;
  }

  QWidget *createEntryWidget(const ClipboardHistoryEntry &entry) {
    auto clipman = ServiceRegistry::instance()->clipman();
    auto data = clipman->getMainOfferData(entry.id);

    if (!data) { return detailForError(data.error()); }
    return detailForMime(data.value(), entry.mimeType);
  }

public:
  void setEntry(const ClipboardHistoryEntry &entry) {
    if (auto previous = content()) { previous->deleteLater(); }

    auto widget = createEntryWidget(entry);
    auto metadata = createEntryMetadata(entry);

    setContent(widget);
    setMetadata(metadata);
  }
};

class RemoveSelectionAction : public AbstractAction {
  QString _id;

  void execute(ApplicationContext *ctx) override {
    auto clipman = ctx->services->clipman();
    auto toast = ctx->services->toastService();

    if (clipman->removeSelection(_id)) {
      toast->setToast("Entry removed");
    } else {
      toast->setToast("Failed to remove entry", ToastStyle::Danger);
    }
  }

public:
  RemoveSelectionAction(const QString &id)
      : AbstractAction("Remove entry", ImageURL::builtin("trash")), _id(id) {
    setStyle(AbstractAction::Style::Danger);
  }
};

class PinClipboardAction : public AbstractAction {
  QString _id;
  bool _value;

  void execute(ApplicationContext *ctx) override {
    QString action = _value ? "pinned" : "unpinned";

    if (ctx->services->clipman()->setPinned(_id, _value)) {
      ctx->services->toastService()->success(QString("Selection %1").arg(action));
    } else {
      ctx->services->toastService()->failure("Failed to change pin status");
    }
  }

public:
  QString entryId() const { return _id; }

  PinClipboardAction(const QString &id, bool value)
      : AbstractAction(value ? "Pin" : "Unpin", ImageURL::builtin("pin")), _id(id), _value(value) {}
};

class EditClipboardSelectionKeywordsView : public ManagedFormView {
  TextArea *m_keywords = new TextArea;
  QString m_selectionId;

  void onSubmit() override {
    auto clipman = context()->services->clipman();
    auto toast = context()->services->toastService();

    if (clipman->setKeywords(m_selectionId, m_keywords->text())) {
      toast->setToast("Keywords edited", ToastStyle::Success);
      popSelf();
    } else {
      toast->setToast("Failed to edit keywords", ToastStyle::Danger);
    }
  }

  void initializeForm() override {
    auto clipman = context()->services->clipman();

    m_keywords->setText(clipman->retrieveKeywords(m_selectionId).value_or(""));
    m_keywords->textEdit()->selectAll();
  }

public:
  EditClipboardSelectionKeywordsView(const QString &id) : m_selectionId(id) {
    auto inputField = new FormField();

    inputField->setWidget(m_keywords);
    inputField->setName("Keywords");
    inputField->setInfo("Additional keywords that will be used to index this selection.");

    form()->addField(inputField);
  }
};

class EditClipboardKeywordsAction : public PushAction<EditClipboardSelectionKeywordsView, QString> {
  QString title() const override { return "Edit keywords"; }
  std::optional<ImageURL> icon() const override { return ImageURL::builtin("text"); }

public:
  EditClipboardKeywordsAction(const QString &id) : PushAction(id) {}
};

class RevealInFileExplorerAction : public AbstractAction {
  QString m_id;
  std::filesystem::path m_path;

  void execute(ApplicationContext *ctx) override {
    auto appDb = ctx->services->appDb();
    auto fileBrowser = appDb->fileBrowser();

    if (!fileBrowser) {
      ctx->services->toastService()->failure("No file browser available");
      return;
    }

    QString browserId = fileBrowser->id();
    QString pathStr = QString::fromStdString(m_path.string());

    std::vector<QString> cmdline;

    // Handle different file managers to properly select the file
    if (browserId == "org.kde.dolphin.desktop") {
      cmdline = {"dolphin", "--select", pathStr};
    } else if (browserId == "org.gnome.Nautilus.desktop" || browserId == "nautilus.desktop") {
      cmdline = {"nautilus", pathStr};
    } else if (browserId == "thunar.desktop" || browserId == "org.xfce.Thunar.desktop") {
      cmdline = {"thunar", pathStr};
    } else if (browserId == "pcmanfm.desktop") {
      cmdline = {"pcmanfm", pathStr};
    } else if (browserId == "pcmanfm-qt.desktop") {
      cmdline = {"pcmanfm-qt", pathStr};
    } else if (browserId == "nemo.desktop" || browserId == "org.cinnamon.Nemo.desktop") {
      cmdline = {"nemo", pathStr};
    } else {
      cmdline = {fileBrowser->program(), pathStr};
    }

    appDb->launchRaw(cmdline);
    ctx->navigation->closeWindow();
  }

public:
  RevealInFileExplorerAction(const QString &id, const std::filesystem::path &path)
      : AbstractAction("Reveal in file explorer", ImageURL::builtin("folder")), m_id(id), m_path(path) {}
};

class ToggleMultiSelectModeAction : public AbstractAction {
  ClipboardHistoryView &m_view;

  void execute(ApplicationContext *ctx) override { m_view.toggleMultiSelectMode(); }

public:
  ToggleMultiSelectModeAction(ClipboardHistoryView &view)
      : AbstractAction(view.isMultiSelectMode() ? "Exit Multi-Select" : "Toggle Multi-Select",
                       ImageURL::builtin("check-list")),
        m_view(view) {}
};

class ToggleItemSelectionAction : public AbstractAction {
  ClipboardHistoryView &m_view;
  QString m_id;

  void execute(ApplicationContext *ctx) override {
    m_view.toggleItemSelection(m_id);
    m_view.refreshSelection();
  }

public:
  ToggleItemSelectionAction(ClipboardHistoryView &view, const QString &id)
      : AbstractAction(view.isItemSelected(id) ? "Deselect Item" : "Select Item",
                       ImageURL::builtin(view.isItemSelected(id) ? "xmark" : "checkmark")),
        m_view(view), m_id(id) {
    setAutoClose(false);
  }
};

class PasteMultipleSelectionsAsTextAction : public AbstractAction {
  std::vector<QString> m_ids;
  ClipboardHistoryView &m_view;

  void execute(ApplicationContext *ctx) override {
    auto clipman = ctx->services->clipman();
    auto wm = ctx->services->windowManager();

    // Collect text content from all selections
    QStringList textParts;
    for (const auto &id : m_ids) {
      auto selection = clipman->retrieveSelectionById(id);
      if (!selection) continue;

      for (const auto &offer : selection->offers) {
        if (Utils::isTextMimeType(offer.mimeType) && !offer.data.isEmpty()) {
          textParts.append(QString::fromUtf8(offer.data));
          break;
        }
        // Fallback to text/uri-list
        if (offer.mimeType == "text/uri-list" && !offer.data.isEmpty()) {
          textParts.append(QString::fromUtf8(offer.data));
          break;
        }
      }
    }

    if (textParts.isEmpty()) {
      ctx->services->toastService()->failure("No text content to paste");
      return;
    }

    QString combinedText = textParts.join("\n");
    clipman->copyContent(Clipboard::Text{combinedText}, {.concealed = true});
    m_view.clearMultiSelection();
    ctx->navigation->closeWindow();

    if (wm->canPaste()) {
      QTimer::singleShot(Environment::pasteDelay(), [wm]() { wm->provider()->pasteToWindow(nullptr, nullptr); });
    }
  }

public:
  PasteMultipleSelectionsAsTextAction(ClipboardHistoryView &view, const std::vector<QString> &ids)
      : AbstractAction(QString("Paste %1 items as text").arg(ids.size()), ImageURL::builtin("text")),
        m_ids(ids), m_view(view) {}
};

class PasteMultipleSelectionsAction : public PasteToFocusedWindowAction {
  std::vector<QString> m_ids;
  ClipboardHistoryView &m_view;

  void execute(ApplicationContext *ctx) override {
    ctx->services->clipman()->copyMultipleSelections(m_ids, {.concealed = true});
    m_view.clearMultiSelection();
    PasteToFocusedWindowAction::execute(ctx);
  }

public:
  PasteMultipleSelectionsAction(ClipboardHistoryView &view, const std::vector<QString> &ids)
      : PasteToFocusedWindowAction(), m_ids(ids), m_view(view) {
    m_title = QString("Paste %1 items").arg(ids.size());
  }
};

class CopyMultipleSelectionsAction : public AbstractAction {
  std::vector<QString> m_ids;
  ClipboardHistoryView &m_view;

  void execute(ApplicationContext *ctx) override {
    auto clipman = ctx->services->clipman();

    if (clipman->copyMultipleSelections(m_ids, {.concealed = true})) {
      m_view.clearMultiSelection();
      ctx->navigation->showHud(QString("%1 items copied to clipboard").arg(m_ids.size()));
      return;
    }

    ctx->services->toastService()->failure("Failed to copy to clipboard");
  }

public:
  CopyMultipleSelectionsAction(ClipboardHistoryView &view, const std::vector<QString> &ids)
      : AbstractAction(QString("Copy %1 items").arg(ids.size()), BuiltinIcon::CopyClipboard),
        m_ids(ids), m_view(view) {}
};

class RemoveAllSelectionsAction : public AbstractAction {
  void execute(ApplicationContext *ctx) override {
    auto alert = new CallbackAlertWidget();

    alert->setTitle("Are you sure?");
    alert->setMessage("All your clipboard history will be lost forever");
    alert->setConfirmText("Delete all", SemanticColor::Red);
    alert->setConfirmCallback([ctx]() {
      auto toast = ctx->services->toastService();
      auto clipman = ctx->services->clipman();

      if (clipman->removeAllSelections()) {
        toast->success("All selections were removed");
      } else {
        toast->failure("Failed to remove all selections");
      }
    });
    ctx->navigation->setDialog(alert);
  }

public:
  QString title() const override { return "Remove all"; }
  std::optional<ImageURL> icon() const override { return ImageURL::builtin("trash"); }

  RemoveAllSelectionsAction() { setStyle(AbstractAction::Style::Danger); }
};

static const std::vector<Preference::DropdownData::Option> filterSelectorOptions = {
    {"All", "all"}, {"Text", "text"}, {"Images", "image"}, {"Links", "link"}, {"Files", "file"},
};

static const std::unordered_map<QString, ClipboardOfferKind> typeToOfferKind{
    {"image", ClipboardOfferKind::Image},
    {"link", ClipboardOfferKind::Link},
    {"text", ClipboardOfferKind::Text},
    {"file", ClipboardOfferKind::File},
};

QWidget *ClipboardHistoryView::wrapUI(QWidget *content) {
  return VStack().add(m_statusToolbar).add(content, 1).divided(1).buildWidget();
}

ClipboardHistoryView::ClipboardHistoryView() {
  auto clipman = ServiceRegistry::instance()->clipman();

  m_statusToolbar = new ClipboardStatusToolbar;

  if (!clipman->supportsMonitoring()) {
    m_statusToolbar->setClipboardStatus(ClipboardStatusToolbar::ClipboardStatus::Unavailable);
  } else {
    handleMonitoringChanged(clipman->monitoring());
  }

  m_filterInput->setMinimumWidth(200);
  m_filterInput->setFocusPolicy(Qt::NoFocus);
  m_filterInput->setOptions(filterSelectorOptions);

  connect(clipman, &ClipboardService::monitoringChanged, this,
          &ClipboardHistoryView::handleMonitoringChanged);
  connect(m_statusToolbar, &ClipboardStatusToolbar::statusIconClicked, this,
          &ClipboardHistoryView::handleStatusClipboard);
  connect(m_filterInput, &SelectorInput::selectionChanged, this, &ClipboardHistoryView::handleFilterChange);
}

void ClipboardHistoryView::initialize() {
  TypedListView::initialize();
  auto preferences = command()->preferenceValues();

  m_model = new ClipboardHistoryModel(this);
  m_controller = new ClipboardHistoryController(context()->services->clipman(), m_model, this);
  setLoading(true);
  setModel(m_model);
  m_defaultAction = parseDefaultAction(preferences.value("defaultAction").toString());
  setSearchPlaceholderText("Browse clipboard history...");
  m_statusToolbar->setLeftText("Loading...");
  textChanged("");
  m_filterInput->setValue(getSavedDropdownFilter().value_or("all"));
  handleFilterChange(*m_filterInput->value());

  connect(m_model, &ClipboardHistoryModel::dataChanged, this, [this]() { refreshCurrent(); });
  connect(m_controller, &ClipboardHistoryController::dataLoadingChanged, this, &BaseView::setLoading);
  connect(m_controller, &ClipboardHistoryController::dataRetrieved, this,
          [this](const PaginatedResponse<ClipboardHistoryEntry> &page) {
            m_statusToolbar->setLeftText(QString("%1 Items").arg(page.totalCount));
          });

  // Handle Shift+Click for multi-select
  connect(m_model, &ClipboardHistoryModel::itemShiftClicked, this, [this](const QString &id, int index) {
    qDebug() << "ClipboardHistoryView: Received itemShiftClicked for" << id << "at index" << index;
    if (!m_multiSelectMode) { m_multiSelectMode = true; }
    toggleItemSelection(id);
    updateMultiSelectStatusText();
    // Also select the item in the list so the action panel updates
    if (index >= 0) {
      m_list->setSelected(index);
    }
  });

  // Handle multi-selection changes - refresh widgets without scrolling
  connect(m_model, &ClipboardHistoryModel::multiSelectionChanged, this, [this]() { m_list->refreshAll(); });

  // Reset multi-select state when window closes/hides
  connect(context()->navigation.get(), &NavigationController::windowVisiblityChanged, this, [this](bool visible) {
    if (!visible && m_multiSelectMode) {
      m_multiSelectMode = false;
      m_selectedIds.clear();
      m_model->setMultiSelectedIds(m_selectedIds);
      // Reload to refresh the status text with item count
      m_controller->reloadSearch();
    }
  });
}

std::unique_ptr<ActionPanelState> ClipboardHistoryView::createActionPanel(const ItemType &info) const {
  auto panel = std::make_unique<ListActionPanelState>();
  auto clipman = context()->services->clipman();
  auto mainSection = panel->createSection();
  bool isCopyable = info->encryption == ClipboardEncryptionType::None || clipman->isEncryptionReady();

  if (!isCopyable) { mainSection->addAction(new OpenItemPreferencesAction(EntrypointId{"clipboard", ""})); }

  auto wm = context()->services->windowManager();
  auto pin = new PinClipboardAction(info->id, !info->pinnedAt);
  auto editKeywords = new EditClipboardKeywordsAction(info->id);
  auto remove = new RemoveSelectionAction(info->id);
  auto removeAll = new RemoveAllSelectionsAction();

  editKeywords->setShortcut(Keybind::EditAction);
  remove->setStyle(AbstractAction::Style::Danger);
  remove->setShortcut(Keybind::RemoveAction);
  removeAll->setShortcut(Keybind::DangerousRemoveAction);
  pin->setShortcut(Keybind::PinAction);

  // Multi-select actions
  auto multiSelectSection = panel->createSection();
  auto toggleMultiSelect =
      new ToggleMultiSelectModeAction(*const_cast<ClipboardHistoryView *>(this));
  toggleMultiSelect->setShortcut(Keyboard::Shortcut(Qt::Key_M, Qt::ControlModifier));
  multiSelectSection->addAction(toggleMultiSelect);

  if (m_multiSelectMode) {
    auto toggleSelection =
        new ToggleItemSelectionAction(*const_cast<ClipboardHistoryView *>(this), info->id);
    toggleSelection->setShortcut(Keyboard::Shortcut(Qt::Key_Space));
    multiSelectSection->addAction(toggleSelection);

    if (!m_selectedIds.empty()) {
      auto copyMultiple =
          new CopyMultipleSelectionsAction(*const_cast<ClipboardHistoryView *>(this), m_selectedIds);
      copyMultiple->addShortcut(Keybind::CopyAction);

      if (wm->canPaste()) {
        auto pasteMultiple =
            new PasteMultipleSelectionsAction(*const_cast<ClipboardHistoryView *>(this), m_selectedIds);
        pasteMultiple->addShortcut(Keybind::PasteAction);

        auto pasteMultipleAsText =
            new PasteMultipleSelectionsAsTextAction(*const_cast<ClipboardHistoryView *>(this), m_selectedIds);
        pasteMultipleAsText->setShortcut(Keyboard::Shortcut(Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier));

        mainSection->addAction(pasteMultiple);
        mainSection->addAction(copyMultiple);
        mainSection->addAction(pasteMultipleAsText);
      } else {
        mainSection->addAction(copyMultiple);
      }
    }
  }

  if (isCopyable && !m_multiSelectMode) {
    auto copy = new CopyClipboardSelection(info->id);
    copy->addShortcut(Keybind::CopyAction);

    if (wm->canPaste()) {
      auto paste = new PasteClipboardSelection(info->id);
      paste->addShortcut(Keybind::PasteAction);

      auto pasteAsText = new PasteAsTextAction(info->id);
      pasteAsText->setShortcut(Keyboard::Shortcut(Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier));

      if (m_defaultAction == ClipboardHistoryView::DefaultAction::Copy) {
        mainSection->addAction(copy);
        mainSection->addAction(paste);
        mainSection->addAction(pasteAsText);
      } else {
        mainSection->addAction(paste);
        mainSection->addAction(copy);
        mainSection->addAction(pasteAsText);
      }
    } else {
      mainSection->addAction(copy);
    }
  } else if (isCopyable && m_multiSelectMode && m_selectedIds.empty()) {
    // In multi-select mode but no items selected yet - show single item actions
    auto copy = new CopyClipboardSelection(info->id);
    copy->addShortcut(Keybind::CopyAction);

    if (wm->canPaste()) {
      auto paste = new PasteClipboardSelection(info->id);
      paste->addShortcut(Keybind::PasteAction);

      auto pasteAsText = new PasteAsTextAction(info->id);
      pasteAsText->setShortcut(Keyboard::Shortcut(Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier));

      mainSection->addAction(paste);
      mainSection->addAction(copy);
      mainSection->addAction(pasteAsText);
    } else {
      mainSection->addAction(copy);
    }
  }

  auto toolsSection = panel->createSection();
  auto dangerSection = panel->createSection();

  // Add reveal in file explorer action for File and Image items
  if (info->kind == ClipboardOfferKind::File || info->kind == ClipboardOfferKind::Image) {
    std::optional<fs::path> filePath;

    if (info->mimeType == "text/uri-list") {
      auto data = clipman->getMainOfferData(info->id);
      if (data) {
        QString text = QString::fromUtf8(data.value());
        auto uris = text.split("\r\n", Qt::SkipEmptyParts);
        if (!uris.isEmpty()) {
          QUrl url(uris.first().trimmed());
          if (url.isLocalFile()) {
            std::error_code ec;
            fs::path path = url.toLocalFile().toStdString();
            if (fs::exists(path, ec)) {
              filePath = path;
            }
          }
        }
      }
    } else if (info->mimeType.startsWith("image/")) {
      auto selection = clipman->retrieveSelectionById(info->id);
      if (selection) {
        for (const auto &offer : selection->offers) {
          if (offer.mimeType == "text/uri-list" && !offer.data.isEmpty()) {
            QString text = QString::fromUtf8(offer.data);
            auto uris = text.split("\r\n", Qt::SkipEmptyParts);
            if (!uris.isEmpty()) {
              QUrl url(uris.first().trimmed());
              if (url.isLocalFile()) {
                std::error_code ec;
                fs::path path = url.toLocalFile().toStdString();
                if (fs::exists(path, ec)) {
                  filePath = path;
                  break;
                }
              }
            }
          }
        }
      }
    }

    if (filePath) {
      auto reveal = new RevealInFileExplorerAction(info->id, *filePath);
      reveal->setShortcut(Keyboard::Shortcut(Qt::Key_R, Qt::ControlModifier | Qt::AltModifier));
      toolsSection->addAction(reveal);
    }
  }

  toolsSection->addAction(pin);
  toolsSection->addAction(editKeywords);
  dangerSection->addAction(remove);
  dangerSection->addAction(removeAll);

  return panel;
}

QWidget *ClipboardHistoryView::generateDetail(const ItemType &item) const {
  auto detail = new ClipboardHistoryDetail;
  detail->setEntry(*item);
  return detail;
}

void ClipboardHistoryView::textChanged(const QString &value) {
  m_controller->setFilter(value);
  m_list->selectFirst();
}

void ClipboardHistoryView::onDeactivate() {
  // Reset multi-select state when the view is deactivated (e.g., another view pushed)
  if (m_multiSelectMode) {
    m_multiSelectMode = false;
    m_selectedIds.clear();
    m_model->setMultiSelectedIds(m_selectedIds);
    // Reload to refresh the status text with item count
    m_controller->reloadSearch();
  }
  TypedListView::onDeactivate();
}

void ClipboardHistoryView::handleMonitoringChanged(bool monitor) {
  if (monitor) {
    m_statusToolbar->setClipboardStatus(ClipboardStatusToolbar::ClipboardStatus::Monitoring);
    return;
  }

  m_statusToolbar->setClipboardStatus(ClipboardStatusToolbar::ClipboardStatus::Paused);
}

void ClipboardHistoryView::handleStatusClipboard() {
  QJsonObject patch;

  if (m_statusToolbar->clipboardStatus() == ClipboardStatusToolbar::Paused) {
    patch["monitoring"] = true;
  } else {
    patch["monitoring"] = false;
  }

  command()->setPreferenceValues(patch);
}

void ClipboardHistoryView::handleFilterChange(const SelectorInput::AbstractItem &item) {
  saveDropdownFilter(item.id());

  if (auto it = typeToOfferKind.find(item.id()); it != typeToOfferKind.end()) {
    m_controller->setKindFilter(it->second);
  } else {
    m_controller->setKindFilter({});
  }

  if (!searchText().isEmpty()) { clearSearchText(); }
}

ClipboardHistoryView::DefaultAction ClipboardHistoryView::parseDefaultAction(const QString &str) {
  if (str == "paste") return DefaultAction::Paste;
  return DefaultAction::Copy;
}

void ClipboardHistoryView::saveDropdownFilter(const QString &value) {
  command()->storage().setItem("filter", value);
}

std::optional<QString> ClipboardHistoryView::getSavedDropdownFilter() {
  auto value = command()->storage().getItem("filter");

  if (value.isNull()) return std::nullopt;

  return value.toString();
}

void ClipboardHistoryView::toggleMultiSelectMode() {
  m_multiSelectMode = !m_multiSelectMode;

  if (!m_multiSelectMode) {
    // Exiting multi-select mode - clear all selections
    m_selectedIds.clear();
    m_model->setMultiSelectedIds(m_selectedIds);
    // Refresh the list to remove checkmarks and restore normal item count display
    m_list->refreshAll();
    // Reload to refresh the status text with item count
    m_controller->reloadSearch();
  } else {
    updateMultiSelectStatusText();
    m_model->setMultiSelectedIds(m_selectedIds);
  }
}

void ClipboardHistoryView::toggleItemSelection(const QString &id) {
  auto it = std::find(m_selectedIds.begin(), m_selectedIds.end(), id);

  if (it != m_selectedIds.end()) {
    m_selectedIds.erase(it);
  } else {
    m_selectedIds.push_back(id);
  }

  m_model->setMultiSelectedIds(m_selectedIds);
}

bool ClipboardHistoryView::isItemSelected(const QString &id) const {
  return std::find(m_selectedIds.begin(), m_selectedIds.end(), id) != m_selectedIds.end();
}

void ClipboardHistoryView::clearMultiSelection() {
  m_selectedIds.clear();
  updateMultiSelectStatusText();
  m_model->setMultiSelectedIds(m_selectedIds);
}

void ClipboardHistoryView::updateMultiSelectStatusText() {
  if (m_multiSelectMode) {
    if (m_selectedIds.empty()) {
      m_statusToolbar->setLeftText("Multi-select: Press Space to select items");
    } else {
      m_statusToolbar->setLeftText(QString("Multi-select: %1 item(s) selected").arg(m_selectedIds.size()));
    }
  }
}

bool ClipboardHistoryView::inputFilter(QKeyEvent *event) {
  // Handle Space for toggling selection in multi-select mode
  if (m_multiSelectMode && event->key() == Qt::Key_Space && event->modifiers() == Qt::NoModifier) {
    if (auto idx = m_list->currentSelection()) {
      if (auto item = m_model->fromIndex(*idx)) {
        toggleItemSelection((*item)->id);
        updateMultiSelectStatusText();
        return true;
      }
    }
  }

  // Handle Shift+Enter to select current item and enter multi-select mode
  if (event->key() == Qt::Key_Return && event->modifiers() == Qt::ShiftModifier) {
    if (auto idx = m_list->currentSelection()) {
      if (auto item = m_model->fromIndex(*idx)) {
        if (!m_multiSelectMode) {
          m_multiSelectMode = true;
        }
        toggleItemSelection((*item)->id);
        updateMultiSelectStatusText();
        return true;
      }
    }
  }

  return TypedListView::inputFilter(event);
}

void ClipboardHistoryView::itemActivated(typename ClipboardHistoryModel::Index idx) {
  // In multi-select mode with items selected, paste all selected items
  if (m_multiSelectMode && !m_selectedIds.empty()) {
    auto clipman = context()->services->clipman();
    auto wm = context()->services->windowManager();
    auto selectedCount = m_selectedIds.size();

    if (clipman->copyMultipleSelections(m_selectedIds, {.concealed = true})) {
      clearMultiSelection();
      m_multiSelectMode = false;
      m_controller->reloadSearch();

      // Paste if window manager supports it
      if (wm->canPaste()) {
        context()->navigation->closeWindow();
        QTimer::singleShot(Environment::pasteDelay(), [wm]() { wm->provider()->pasteToWindow(nullptr, nullptr); });
      } else {
        context()->navigation->showHud(QString("%1 items copied to clipboard").arg(selectedCount));
      }
      return;
    }
  }

  // Default behavior - execute primary action
  TypedListView::itemActivated(idx);
}

#include "clipboard-history-view.moc"
