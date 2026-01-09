#include <QClipboard>
#include "clipboard-service.hpp"
#include <filesystem>
#include <numeric>
#include <qapplication.h>
#include <QTimer>
#include "environment.hpp"
#include "services/app-service/abstract-app-db.hpp"
#include "x11/x11-clipboard-server.hpp"
#include <qclipboard.h>
#include <qimagereader.h>
#include <qlogging.h>
#include <qmimedata.h>
#include <qnamespace.h>
#include <qregularexpression.h>
#include <qsqlquery.h>
#include <qstringview.h>
#include <qt6keychain/keychain.h>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QBuffer>
#include <QImage>
#include "clipboard-server-factory.hpp"
#include "crypto.hpp"
#include "services/app-service/app-service.hpp"
#include "services/clipboard/clipboard-db.hpp"
#include "services/clipboard/clipboard-encrypter.hpp"
#include "services/clipboard/clipboard-server.hpp"
#include "services/clipboard/gnome/gnome-clipboard-server.hpp"
#include "utils.hpp"
#include "data-control/data-control-clipboard-server.hpp"
#include "services/window-manager/abstract-window-manager.hpp"
#include "services/window-manager/window-manager.hpp"

namespace fs = std::filesystem;

/**
 * If any of these is found in a selection, we ignore the entire selection.
 */
static const std::set<QString> IGNORED_MIME_TYPES = {
    Clipboard::CONCEALED_MIME_TYPE,
};

static const std::set<QString> PASSWORD_MIME_TYPES = {
    "x-kde-passwordManagerHint",
};

bool ClipboardService::setPinned(const QString id, bool pinned) {
  if (!ClipboardDatabase().setPinned(id, pinned)) { return false; }

  emit selectionPinStatusChanged(id, pinned);

  return true;
}

bool ClipboardService::clear() {
  QApplication::clipboard()->clear();
  return true;
}

bool ClipboardService::supportsMonitoring() const { return m_clipboardServer->id() != "dummy"; }

bool ClipboardService::copyContent(const Clipboard::Content &content, const Clipboard::CopyOptions options) {
  struct ContentVisitor {
    ClipboardService &service;
    const Clipboard::CopyOptions &options;

    bool operator()(const Clipboard::NoData &dummy) const {
      qWarning() << "attempt to copy NoData content";
      return false;
    }
    bool operator()(const Clipboard::Html &html) const { return service.copyHtml(html, options); }
    bool operator()(const Clipboard::File &file) const { return service.copyFile(file.path, options); }
    bool operator()(const Clipboard::Text &text) const { return service.copyText(text.text, options); }
    bool operator()(const ClipboardSelection &selection) const {
      return service.copySelection(selection, options);
    }
    bool operator()(const Clipboard::SelectionRecordHandle &handle) const {
      return service.copySelectionRecord(handle.id, options);
    }

    ContentVisitor(ClipboardService &service, const Clipboard::CopyOptions &options)
        : service(service), options(options) {}
  };

  ContentVisitor visitor(*this, options);

  return std::visit(visitor, content);
}

bool ClipboardService::pasteContent(const Clipboard::Content &content, const Clipboard::CopyOptions options) {
  if (!copyContent(content, options)) return false;

  if (!m_wm.provider()->supportsPaste()) {
    qWarning() << "pasteContent called but the current window manager cannot paste, ignoring...";
    return false;
  }

  QTimer::singleShot(Environment::pasteDelay(), [wm = &m_wm, appDb = &m_appDb]() {
    auto window = wm->getFocusedWindow();
    std::shared_ptr<AbstractApplication> app;
    if (window) { app = appDb->find(window->wmClass()); }
    wm->provider()->pasteToWindow(window.get(), app.get());
  });

  return true;
}

bool ClipboardService::copyFile(const std::filesystem::path &path, const Clipboard::CopyOptions &options) {
  QMimeData *data = new QMimeData;

  // copying files should normally copy a link to the file, not the file itself
  // This is what text/uri-list is used for. On Windows or other systems we might have
  // to do something else, I'm not sure.
  data->setData("text/uri-list", QString("file://%1").arg(path.c_str()).toUtf8());

  return copyQMimeData(data, options);
}

void ClipboardService::setRecordAllOffers(bool value) { m_recordAllOffers = value; }

void ClipboardService::setEncryption(bool value) {
  m_encrypter.reset();

  if (value) {
    m_encrypter = std::make_unique<ClipboardEncrypter>();
    m_encrypter->loadKey();
  }
}

bool ClipboardService::isEncryptionReady() const { return m_encrypter.get(); }

void ClipboardService::setIgnorePasswords(bool value) { m_ignorePasswords = value; }

void ClipboardService::setAutoPathToUri(bool value) { m_autoPathToUri = value; }

void ClipboardService::setMonitoring(bool value) {
  if (m_monitoring == value) return;

  if (value) {
    qInfo() << "Starting clipboard server" << m_clipboardServer->id();
    if (m_clipboardServer->start()) {
      qInfo() << "Clipboard server" << m_clipboardServer->id() << "started successfully.";
    } else {
      qWarning() << "Failed to start clipboard server" << m_clipboardServer->id();
    }
  } else {
    qInfo() << "Stopping clipboard server" << m_clipboardServer->id();
    if (m_clipboardServer->stop()) {
      qInfo() << "Clipboard server" << m_clipboardServer->id() << "stopped successfully.";
    } else {
      qWarning() << "Failed to stop clipboard server" << m_clipboardServer->id();
    }
  }

  m_monitoring = value;
  emit monitoringChanged(value);
}

bool ClipboardService::monitoring() const { return m_monitoring; }

bool ClipboardService::copyHtml(const Clipboard::Html &data, const Clipboard::CopyOptions &options) {
  auto mimeData = new QMimeData;

  mimeData->setData("text/html", data.html.toUtf8());

  if (auto text = data.text) mimeData->setData("text/plain", text->toUtf8());

  return copyQMimeData(mimeData, options);
}

bool ClipboardService::copyText(const QString &text, const Clipboard::CopyOptions &options) {
  auto mimeData = new QMimeData;

  mimeData->setData("text/plain", text.toUtf8());

  if (options.concealed) mimeData->setData(Clipboard::CONCEALED_MIME_TYPE, "1");

  return copyQMimeData(mimeData, options);
}

QFuture<PaginatedResponse<ClipboardHistoryEntry>>
ClipboardService::listAll(int limit, int offset, const ClipboardListSettings &opts) const {
  return QtConcurrent::run(
      [opts, limit, offset]() { return ClipboardDatabase().query(limit, offset, opts); });
}

ClipboardOfferKind ClipboardService::getKind(const ClipboardDataOffer &offer) {
  if (offer.mimeType == "text/uri-list") {
    QString text = offer.data;
    auto uris = text.split("\r\n", Qt::SkipEmptyParts);
    if (uris.size() == 1 && QUrl(uris.front()).isLocalFile()) return ClipboardOfferKind::File;
    return ClipboardOfferKind::Text;
  }

  if (offer.mimeType.startsWith("image/")) { return ClipboardOfferKind::Image; }
  if (offer.mimeType == "text/html") { return ClipboardOfferKind::Text; }

  if (Utils::isTextMimeType(offer.mimeType)) {
    auto url = QUrl::fromEncoded(offer.data, QUrl::StrictMode);

    if (url.isLocalFile()) { return ClipboardOfferKind::File; }
    if (!url.scheme().isEmpty()) { return ClipboardOfferKind::Link; }

    return ClipboardOfferKind::Text;
  }

  return ClipboardOfferKind::Unknown;
}

QString ClipboardService::getSelectionPreferredMimeType(const ClipboardSelection &selection) {
  static const std::vector<QString> plainTextMimeTypes = {
      "text/uri-list", "text/plain;charset=utf-8", "text/plain", "UTF8_STRING", "STRING", "TEXT",
      "COMPOUND_TEXT"};

  for (const auto &mime : plainTextMimeTypes) {
    auto it = std::ranges::find_if(
        selection.offers, [&](const auto &offer) { return offer.mimeType == mime && !offer.data.isEmpty(); });
    if (it != selection.offers.end()) return it->mimeType;
  }

  auto imageIt = std::ranges::find_if(selection.offers, [](const auto &offer) {
    return offer.mimeType.startsWith("image/") && !offer.data.isEmpty();
  });
  if (imageIt != selection.offers.end()) return imageIt->mimeType;

  auto htmlIt = std::ranges::find_if(selection.offers, [](const auto &offer) {
    return offer.mimeType == "text/html" && !offer.data.isEmpty();
  });
  if (htmlIt != selection.offers.end()) return htmlIt->mimeType;

  auto fallbackIt = std::ranges::find_if(selection.offers, [](const auto &offer) {
    return !offer.mimeType.startsWith("text/_moz_html") && !offer.data.isEmpty();
  });
  if (fallbackIt != selection.offers.end()) return fallbackIt->mimeType;

  if (!selection.offers.empty()) return selection.offers.front().mimeType;

  return {};
}

bool ClipboardService::removeSelection(const QString &selectionId) {
  ClipboardDatabase cdb;

  for (const auto &offer : cdb.removeSelection(selectionId)) {
    fs::remove(m_dataDir / offer.toStdString());
  }

  emit selectionRemoved(selectionId);

  return true;
}

std::expected<QByteArray, ClipboardService::OfferDecryptionError>
ClipboardService::decryptOffer(const QByteArray &data, ClipboardEncryptionType type) const {
  switch (type) {
  case ClipboardEncryptionType::Local: {
    if (!m_encrypter) { return std::unexpected(OfferDecryptionError::DecryptionRequired); }
    auto decryption = m_encrypter->decrypt(data);
    if (!decryption) { return std::unexpected(OfferDecryptionError::DecryptionFailed); }
    return decryption.value();
  }
  default:
    return data;
  }
}

std::expected<QByteArray, ClipboardService::OfferDecryptionError>
ClipboardService::getMainOfferData(const QString &selectionId) const {
  ClipboardDatabase cdb;

  auto offer = cdb.findPreferredOffer(selectionId);

  if (!offer) {
    qWarning() << "Can't find preferred offer for selection" << selectionId;
    return {};
  };

  fs::path path = m_dataDir / offer->id.toStdString();

  QFile file(path);

  if (!file.open(QIODevice::ReadOnly)) {
    qWarning() << "Failed to open file at" << path;
    return {};
  }

  return decryptOffer(file.readAll(), offer->encryption);
}

QByteArray ClipboardService::computeSelectionHash(const ClipboardSelection &selection) const {
  QCryptographicHash hash(QCryptographicHash::Md5);

  for (const auto &offer : selection.offers) {
    hash.addData(QCryptographicHash::hash(offer.data, QCryptographicHash::Md5));
  }

  return hash.result();
}

bool ClipboardService::isClearSelection(const ClipboardSelection &selection) const {
  return std::accumulate(selection.offers.begin(), selection.offers.end(), 0,
                         [](size_t acc, auto &&item) { return acc + item.data.size(); }) == 0;
}

QString ClipboardService::getOfferTextPreview(const ClipboardDataOffer &offer) {
  switch (getKind(offer)) {
  case ClipboardOfferKind::Text:
  case ClipboardOfferKind::Link:
  case ClipboardOfferKind::File:
    return offer.data.simplified().mid(0, 50);
  case ClipboardOfferKind::Image: {
    QBuffer buffer;
    QImageReader reader(&buffer);

    buffer.setData(offer.data);
    if (auto size = reader.size(); size.isValid()) {
      return QString("Image (%1x%2)").arg(size.width()).arg(size.height());
    }
    return "Image";
  }
  default:
    return "Unknown";
  }
}

std::optional<QString> ClipboardService::retrieveKeywords(const QString &id) {
  return ClipboardDatabase().retrieveKeywords(id);
}

bool ClipboardService::setKeywords(const QString &id, const QString &keywords) {
  return ClipboardDatabase().setKeywords(id, keywords);
}

bool ClipboardService::isConcealedSelection(const ClipboardSelection &selection) {
  return std::ranges::any_of(selection.offers,
                             [](auto &&offer) { return IGNORED_MIME_TYPES.contains(offer.mimeType); });
}

bool ClipboardService::isPasswordSelection(const ClipboardSelection &selection) {
  return std::ranges::any_of(selection.offers,
                             [](auto &&offer) { return PASSWORD_MIME_TYPES.contains(offer.mimeType); });
}

ClipboardSelection &ClipboardService::sanitizeSelection(ClipboardSelection &selection) {
  std::ranges::sort(selection.offers, [](auto &&a, auto &&b) {
    return std::ranges::lexicographical_compare(a.mimeType, b.mimeType);
  });
  std::ranges::unique(selection.offers, [](auto &&a, auto &&b) { return a.mimeType == b.mimeType; });

  // Skip path-to-URI conversion if disabled
  if (!m_autoPathToUri) return selection;

  // Check if there's already a text/uri-list with file:// URIs - if so, skip conversion
  bool alreadyHasFileUri = std::ranges::any_of(selection.offers, [](const auto &offer) {
    return offer.mimeType == "text/uri-list" &&
           QString::fromUtf8(offer.data).trimmed().startsWith("file://");
  });

  if (alreadyHasFileUri) {
    // Already has proper file:// URI, no conversion needed
    return selection;
  }

  // Check if we have a plain text offer that looks like an absolute file path
  // Some screenshot tools copy the file path as plain text instead of as a URI
  // First, find any text offer to check if it's a file path
  QString detectedFilePath;
  for (const auto &offer : selection.offers) {
    if (Utils::isTextMimeType(offer.mimeType) && !offer.data.isEmpty()) {
      QString text = QString::fromUtf8(offer.data).trimmed();
      // Check if it looks like an absolute file path (starts with /)
      // and doesn't already have a scheme (like file:// or http://)
      // This makes the conversion idempotent
      if (text.startsWith('/') && !text.contains("://")) {
        fs::path filePath = text.toStdString();
        std::error_code ec;
        if (fs::exists(filePath, ec) && fs::is_regular_file(filePath, ec)) {
          detectedFilePath = text;
          break;
        }
      }
    }
  }

  if (!detectedFilePath.isEmpty()) {
    QString fileUri = QString("file://%1").arg(detectedFilePath);

    // Update ALL text offers to contain the URI
    for (auto &offer : selection.offers) {
      if (Utils::isTextMimeType(offer.mimeType)) {
        offer.data = fileUri.toUtf8();
      }
    }

    // Add text/uri-list offer
    ClipboardDataOffer uriOffer;
    uriOffer.mimeType = "text/uri-list";
    uriOffer.data = fileUri.toUtf8();
    selection.offers.push_back(uriOffer);

    // Check if this is an image file and add actual image data
    // This allows apps like Notion to receive the image directly
    QMimeDatabase mimeDb;
    QMimeType mimeType = mimeDb.mimeTypeForFile(detectedFilePath);
    if (mimeType.name().startsWith("image/")) {
      // Check if we already have image data
      bool hasImageData = std::ranges::any_of(
          selection.offers, [](const auto &offer) { return offer.mimeType.startsWith("image/"); });

      if (!hasImageData) {
        QFile imageFile(detectedFilePath);
        if (imageFile.open(QIODevice::ReadOnly)) {
          ClipboardDataOffer imageOffer;
          imageOffer.mimeType = mimeType.name();
          imageOffer.data = imageFile.readAll();
          selection.offers.push_back(imageOffer);
          qInfo() << "Added image data from file:" << detectedFilePath << "mime:" << mimeType.name()
                  << "size:" << imageOffer.data.size();
        }
      }
    }

    qInfo() << "Converted plain file path to URI:" << fileUri;
  }

  return selection;
}

void ClipboardService::saveSelection(ClipboardSelection selection) {
  if (!m_monitoring) return;

  sanitizeSelection(selection);

  qInfo() << "Received new clipboard selection with" << selection.offers.size() << "offers";

  for (const auto &offer : selection.offers) {
    qInfo().nospace() << offer.mimeType << " (size=" << formatSize(offer.data.size())
                      << ", password=" << PASSWORD_MIME_TYPES.contains(offer.mimeType) << ")";
  }

  if (isConcealedSelection(selection)) {
    qInfo() << "Ignoring concealed selection";
    return;
  }

  if (m_ignorePasswords && isPasswordSelection(selection)) {
    qInfo() << "Ignored password clipboard selection";
    return;
  }

  if (isClearSelection(selection)) {
    qInfo() << "Ignored clipboard clear selection";
    return;
  }

  QString preferredMimeType = getSelectionPreferredMimeType(selection);
  ClipboardHistoryEntry insertedEntry;
  ClipboardDatabase cdb;
  auto preferredOfferIt =
      std::ranges::find_if(selection.offers, [&](auto &&o) { return o.mimeType == preferredMimeType; });

  if (preferredOfferIt == selection.offers.end()) {
    qCritical() << "preferredOfferIt is invalid, this should not be possible!";
    return;
  }

  auto preferredKind = getKind(*preferredOfferIt);

  if (preferredKind == ClipboardOfferKind::Unknown) {
    qWarning() << "Ignoring selection with primary offer of unknown kind" << preferredMimeType;
    return;
  }

  auto selectionHash = QCryptographicHash::hash(preferredOfferIt->data, QCryptographicHash::Md5).toHex();

  if (preferredKind == ClipboardOfferKind::Text && preferredOfferIt->data.trimmed().isEmpty()) {
    qInfo() << "Ignored text selection with empty text";
    return;
  }

  cdb.transaction([&](ClipboardDatabase &db) {
    if (db.tryBubbleUpSelection(selectionHash)) {
      qInfo() << "A similar clipboard selection is already indexed: moving it on top of the history";
      return true;
    }

    QString selectionId = Crypto::UUID::v4();

    if (!db.insertSelection({.id = selectionId,
                             .offerCount = static_cast<int>(selection.offers.size()),
                             .hash = selectionHash,
                             .preferredMimeType = preferredMimeType,
                             .kind = preferredKind,
                             .source = selection.sourceApp})) {
      qWarning() << "failed to insert selection";
      return false;
    }

    // Index all offers, including empty ones
    for (const auto &offer : selection.offers) {
      ClipboardOfferKind kind = getKind(offer);
      bool isIndexableText = kind == ClipboardOfferKind::Text || kind == ClipboardOfferKind::Link;
      QString textPreview = getOfferTextPreview(offer);

      if (isIndexableText && !offer.data.isEmpty()) {
        if (!db.indexSelectionContent(selectionId, offer.data)) {
          qWarning() << "Failed to index selection content for offer" << offer.mimeType;
          return false;
        }
      }

      auto md5sum = QCryptographicHash::hash(offer.data, QCryptographicHash::Md5).toHex();
      auto offerId = Crypto::UUID::v4();
      ClipboardEncryptionType encryption = ClipboardEncryptionType::None;

      if (m_encrypter) encryption = ClipboardEncryptionType::Local;

      InsertClipboardOfferPayload dto{
          .id = offerId,
          .selectionId = selectionId,
          .mimeType = offer.mimeType,
          .textPreview = textPreview,
          .md5sum = md5sum,
          .encryption = encryption,
          .size = static_cast<quint64>(offer.data.size()),
      };

      if (kind == ClipboardOfferKind::Link) {
        auto url = QUrl::fromEncoded(offer.data, QUrl::StrictMode);
        if (url.scheme().startsWith("http")) { dto.urlHost = url.host(); }
      }

      if (!db.insertOffer(dto)) {
        qWarning() << "Failed to insert offer" << offer.mimeType;
        return false;
      }

      fs::path targetPath = m_dataDir / offerId.toStdString();
      QFile targetFile(targetPath);

      if (!targetFile.open(QIODevice::WriteOnly)) { continue; }

      if (m_encrypter) {
        if (auto encrypted = m_encrypter->encrypt(offer.data)) {
          targetFile.write(encrypted.value());
        } else {
          qWarning() << "Failed to encrypt clipboard selection";
          return false;
        }
      } else {
        targetFile.write(offer.data);
      }

      // Set the insertedEntry for the preferred offer
      if (offer.mimeType == preferredMimeType) {
        insertedEntry.id = selectionId;
        insertedEntry.pinnedAt = 0;
        insertedEntry.updatedAt = {};
        insertedEntry.mimeType = offer.mimeType;
        insertedEntry.md5sum = md5sum;
        insertedEntry.textPreview = textPreview;
      }
    }

    return true;
  });

  emit itemInserted(insertedEntry);
}

std::optional<ClipboardSelection> ClipboardService::retrieveSelectionById(const QString &id) {
  ClipboardDatabase cdb;
  ClipboardSelection populatedSelection;
  const auto selection = cdb.findSelection(id);

  if (!selection) return std::nullopt;

  for (const auto &offer : selection->offers) {
    ClipboardDataOffer populatedOffer;
    fs::path path = m_dataDir / offer.id.toStdString();
    QFile file(path);

    if (!file.open(QIODevice::ReadOnly)) { continue; }

    auto data = decryptOffer(file.readAll(), offer.encryption);

    if (!data) return {};

    populatedOffer.data = data.value();
    populatedOffer.mimeType = offer.mimeType;
    populatedSelection.offers.emplace_back(populatedOffer);
  }

  return populatedSelection;
}

bool ClipboardService::copyQMimeData(QMimeData *data, const Clipboard::CopyOptions &options) {
  if (options.concealed) { data->setData(Clipboard::CONCEALED_MIME_TYPE, "1"); }

  return m_clipboardServer->setClipboardContent(data);
}

bool ClipboardService::copySelection(const ClipboardSelection &selection,
                                     const Clipboard::CopyOptions &options) {
  if (selection.offers.empty()) {
    qWarning() << "Not copying selection with no offers";
    return false;
  }

  QMimeData *mimeData = new QMimeData;

  for (const auto &offer : selection.offers) {
    if (offer.mimeType == "application/x-qt-image") continue; // we handle that ourselves
    if (offer.mimeType.startsWith("image/") && !mimeData->hasImage()) {
      auto img = QImage::fromData(offer.data);

      if (img.isNull()) {
        qWarning() << offer.mimeType << "could not be converted to valid image format";
        mimeData->setData(offer.mimeType, offer.data);
      } else {
        mimeData->setData(offer.mimeType, offer.data);
        mimeData->setImageData(img);
        qDebug() << "ClipboardService: Set image data with mime type" << offer.mimeType
                 << "size:" << offer.data.size();
      }
    } else if (offer.mimeType == "text/uri-list") {
      // Handle text/uri-list specially - set both raw data and URLs for Qt compatibility
      mimeData->setData(offer.mimeType, offer.data);
      QString uriData = QString::fromUtf8(offer.data).trimmed();
      QList<QUrl> urls;
      for (const auto &uri : uriData.split(QRegularExpression("[\r\n]+"), Qt::SkipEmptyParts)) {
        urls.append(QUrl(uri));
      }
      if (!urls.isEmpty()) {
        mimeData->setUrls(urls);
      }
    } else {
      if (Utils::isTextMimeType(offer.mimeType)) {
        mimeData->setText(QString::fromUtf8(offer.data));
      } else {
        mimeData->setData(offer.mimeType, offer.data);
      }
    }
  }

  return copyQMimeData(mimeData, options);
}

bool ClipboardService::copySelectionRecord(const QString &id, const Clipboard::CopyOptions &options) {
  auto selection = retrieveSelectionById(id);

  if (!selection) {
    qWarning() << "copySelectionRecord: could not get selection for ID" << id;
    return false;
  }

  ClipboardDatabase db;

  if (!db.tryBubbleUpSelection(id)) {
    qWarning() << "Failed to bubble up selection with id" << id;
    return false;
  }

  // we don't want subscribers to block before the actual copy happens
  QMetaObject::invokeMethod(this, [this]() { emit selectionUpdated(); }, Qt::QueuedConnection);

  return copySelection(*selection, options);
}

QString ClipboardService::readText() { return QApplication::clipboard()->text(); }

Clipboard::ReadContent ClipboardService::readContent() {
  Clipboard::ReadContent content;
  const QMimeData *mimeData = QApplication::clipboard()->mimeData();

  if (!mimeData) return content;

  if (mimeData->hasUrls()) {
    for (const auto &url : mimeData->urls()) {
      if (url.isLocalFile()) {
        content.file = url.toLocalFile();
        break;
      }
    }
  }

  if (mimeData->hasHtml()) { content.html = mimeData->html(); }
  if (mimeData->hasText()) { content.text = mimeData->text(); }

  return content;
}

bool ClipboardService::removeAllSelections() {
  ClipboardDatabase db;

  if (!db.removeAll()) {
    qWarning() << "Failed to remove all clipboard selections";
    return false;
  }

  fs::remove_all(m_dataDir);
  fs::create_directories(m_dataDir);

  emit allSelectionsRemoved();

  return true;
}

QMimeData *ClipboardService::buildCompositeSelection(const std::vector<ClipboardSelection> &selections) {
  QMimeData *composite = new QMimeData;
  QString combinedText;
  QString combinedHtml = "<div style=\"font-family: sans-serif;\">";
  QStringList fileUris;
  int imageCount = 0;
  QByteArray singleImageData;
  QString singleImageMime;

  for (const auto &sel : selections) {
    QString selectionText;
    QString selectionHtml;
    QByteArray selectionImageData;
    QString selectionImageMime;
    QString selectionFileUri;

    // First pass: collect text, HTML, image content, and file URIs
    for (const auto &offer : sel.offers) {
      if (offer.mimeType == "text/uri-list" && !offer.data.isEmpty()) {
        // Collect file URIs from text/uri-list
        QString uriData = QString::fromUtf8(offer.data).trimmed();
        for (const auto &uri : uriData.split(QRegularExpression("[\r\n]+"), Qt::SkipEmptyParts)) {
          if (uri.startsWith("file://")) {
            selectionFileUri = uri;
            fileUris.append(uri);
          }
        }
      } else if (Utils::isTextMimeType(offer.mimeType) && !offer.data.isEmpty()) {
        selectionText = QString::fromUtf8(offer.data);
      } else if (offer.mimeType == "text/html" && !offer.data.isEmpty()) {
        selectionHtml = QString::fromUtf8(offer.data);
      } else if (offer.mimeType.startsWith("image/") && !offer.data.isEmpty()) {
        selectionImageData = offer.data;
        selectionImageMime = offer.mimeType;
      }
    }

    // Add image content to HTML
    if (!selectionImageData.isEmpty()) {
      QString base64 = selectionImageData.toBase64();
      combinedHtml +=
          QString("<img src=\"data:%1;base64,%2\" style=\"max-width:100%;\"/>").arg(selectionImageMime, base64);

      // Track image count and store first image
      imageCount++;
      if (imageCount == 1) {
        singleImageData = selectionImageData;
        singleImageMime = selectionImageMime;
      }
    }

    // Add text content (skip file:// URIs if we have a proper file URI in uri-list)
    if (!selectionText.isEmpty() && !selectionText.startsWith("file://")) {
      if (!combinedText.isEmpty()) { combinedText += "\n"; }
      combinedText += selectionText;
      combinedHtml += QString("<p>%1</p>").arg(selectionText.toHtmlEscaped());
    } else if (!selectionHtml.isEmpty()) {
      // If no plain text but has HTML, use HTML and try to extract text
      combinedHtml += selectionHtml;
      // Strip HTML tags for plain text fallback
      QString strippedText = selectionHtml;
      strippedText.remove(QRegularExpression("<[^>]*>"));
      strippedText = strippedText.simplified();
      if (!strippedText.isEmpty()) {
        if (!combinedText.isEmpty()) { combinedText += "\n"; }
        combinedText += strippedText;
      }
    }
  }

  combinedHtml += "</div>";

  // Set text/uri-list with all file URIs (separated by \r\n as per RFC 2483)
  if (!fileUris.isEmpty()) {
    QString uriList = fileUris.join("\r\n");
    composite->setData("text/uri-list", uriList.toUtf8());
    // Also set URLs for Qt compatibility
    QList<QUrl> urls;
    for (const auto &uri : fileUris) {
      urls.append(QUrl(uri));
    }
    composite->setUrls(urls);
  }

  // Set text and HTML
  if (!combinedText.isEmpty()) {
    composite->setText(combinedText.trimmed());
  } else if (!fileUris.isEmpty()) {
    // If no other text, use the file URIs as text fallback
    composite->setText(fileUris.join("\n"));
  }
  composite->setHtml(combinedHtml);

  // Only set raw image data if there's exactly one image and no text and one selection
  // This forces apps like Notion to use HTML when there are multiple items
  if (imageCount == 1 && combinedText.isEmpty() && selections.size() == 1) {
    auto img = QImage::fromData(singleImageData);
    if (!img.isNull()) {
      composite->setImageData(img);
      composite->setData(singleImageMime, singleImageData);
    }
  }

  return composite;
}

bool ClipboardService::copyMultipleSelections(const std::vector<QString> &ids,
                                              const Clipboard::CopyOptions &options) {
  std::vector<ClipboardSelection> selections;

  for (const auto &id : ids) {
    if (auto sel = retrieveSelectionById(id)) {
      qDebug() << "copyMultipleSelections: Retrieved selection" << id << "with" << sel->offers.size() << "offers";
      for (const auto &offer : sel->offers) {
        qDebug() << "  - offer:" << offer.mimeType << "size:" << offer.data.size();
      }
      selections.push_back(*sel);
    } else {
      qWarning() << "copyMultipleSelections: Failed to retrieve selection" << id;
    }
  }

  if (selections.empty()) return false;

  return copyQMimeData(buildCompositeSelection(selections), options);
}

AbstractClipboardServer *ClipboardService::clipboardServer() const { return m_clipboardServer.get(); }

ClipboardService::ClipboardService(const std::filesystem::path &path, WindowManager &wm, AppService &appDb)
    : m_wm(wm), m_appDb(appDb) {
  m_dataDir = path.parent_path() / "clipboard-data";
  auto clip = QApplication::clipboard();

  {
    ClipboardServerFactory factory;

    factory.registerServer<GnomeClipboardServer>();
    factory.registerServer<DataControlClipboardServer>();
    factory.registerServer<X11ClipboardServer>();
    m_clipboardServer = factory.createFirstActivatable();
    qInfo() << "Activated clipboard server" << m_clipboardServer->id();
  }

  fs::create_directories(m_dataDir);
  ClipboardDatabase().runMigrations();

  connect(m_clipboardServer.get(), &AbstractClipboardServer::selectionAdded, this,
          &ClipboardService::saveSelection);

  // Health check timer to auto-recover from KDE crashes
  m_healthCheckTimer = new QTimer(this);
  connect(m_healthCheckTimer, &QTimer::timeout, this, &ClipboardService::checkServerHealth);
  m_healthCheckTimer->start(5000); // Check every 5 seconds
}

ClipboardService::~ClipboardService() {
  if (m_healthCheckTimer) {
    m_healthCheckTimer->stop();
  }
}

void ClipboardService::checkServerHealth() {
  if (!m_monitoring) return;

  if (!m_clipboardServer->isAlive()) {
    qWarning() << "Clipboard server" << m_clipboardServer->id() << "is not alive, attempting restart...";
    m_clipboardServer->stop();
    if (m_clipboardServer->start()) {
      qInfo() << "Clipboard server" << m_clipboardServer->id() << "restarted successfully.";
    } else {
      qWarning() << "Failed to restart clipboard server" << m_clipboardServer->id();
    }
  }
}
