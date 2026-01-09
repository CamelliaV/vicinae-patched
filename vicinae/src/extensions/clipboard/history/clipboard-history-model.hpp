#pragma once
#include "common.hpp"
#include "layout.hpp"
#include "services/clipboard/clipboard-db.hpp"
#include "services/clipboard/clipboard-service.hpp"
#include "ui/vlist/common/section-model.hpp"
#include "ui/vlist/vlist.hpp"
#include "utils.hpp"
#include <algorithm>
#include <qevent.h>

class ClipboardHistoryItemWidget : public SelectableOmniListWidget {
  Q_OBJECT

signals:
  void shiftClicked(int index);
  void ctrlClicked(int index);

public:
  void setEntry(const ClipboardHistoryEntry &entry, bool isMultiSelected = false, int index = -1) {
    auto createdAt = QDateTime::fromSecsSinceEpoch(entry.updatedAt);
    m_title->setText(entry.textPreview);
    m_pinIcon->setVisible(entry.pinnedAt);
    m_description->setText(QString("%1").arg(getRelativeTimeString(createdAt)));
    m_icon->setFixedSize(25, 25);
    m_icon->setUrl(iconForMime(entry));
    m_checkIcon->setVisible(isMultiSelected);
    m_entryId = entry.id;
    m_index = index;
  }

  QString entryId() const { return m_entryId; }

  ClipboardHistoryItemWidget() { setupUI(); }

protected:
  void mousePressEvent(QMouseEvent *event) override {
    if (event->button() == Qt::LeftButton && event->modifiers() & Qt::ShiftModifier) {
      emit shiftClicked(m_index);
      event->accept();
      return;
    }
    if (event->button() == Qt::LeftButton && event->modifiers() & Qt::ControlModifier) {
      emit ctrlClicked(m_index);
      event->accept();
      return;
    }
    SelectableOmniListWidget::mousePressEvent(event);
  }

private:
  TypographyWidget *m_title = new TypographyWidget;
  TypographyWidget *m_description = new TypographyWidget;
  ImageWidget *m_icon = new ImageWidget;
  ImageWidget *m_pinIcon = new ImageWidget;
  ImageWidget *m_checkIcon = new ImageWidget;
  QString m_entryId;
  int m_index = -1;

  ImageURL getLinkIcon(const std::optional<QString> &urlHost) const {
    auto dflt = ImageURL::builtin("link");

    if (urlHost) return ImageURL::favicon(*urlHost).withFallback(dflt);

    return dflt;
  }

  ImageURL iconForMime(const ClipboardHistoryEntry &entry) const {
    switch (entry.kind) {
    case ClipboardOfferKind::Image:
      return ImageURL::builtin("image");
    case ClipboardOfferKind::Link:
      return getLinkIcon(entry.urlHost);
    case ClipboardOfferKind::Text:
      return ImageURL::builtin("text");
    case ClipboardOfferKind::File:
      return ImageURL::builtin("folder");
    default:
      break;
    }
    return ImageURL::builtin("question-mark-circle");
  }

  void setupUI() {
    m_pinIcon->setUrl(ImageURL::builtin("pin").setFill(SemanticColor::Red));
    m_pinIcon->setFixedSize(16, 16);
    m_checkIcon->setUrl(ImageURL::builtin("checkmark").setFill(SemanticColor::Green));
    m_checkIcon->setFixedSize(20, 20);
    m_checkIcon->setVisible(false);
    m_description->setColor(SemanticColor::TextMuted);
    m_description->setSize(TextSize::TextSmaller);

    auto layout = HStack().margins(5).spacing(10).add(m_checkIcon).add(m_icon).add(
        VStack().add(m_title).add(HStack().add(m_pinIcon).add(m_description).spacing(5)));

    setLayout(layout.buildLayout());
  }
};

enum class ClipboardHistorySection { Main };

class ClipboardHistoryModel
    : public vicinae::ui::SectionListModel<const ClipboardHistoryEntry *, ClipboardHistorySection> {
  Q_OBJECT

signals:
  void itemShiftClicked(const QString &id, int index) const;
  void itemCtrlClicked(const QString &id, int index) const;
  void multiSelectionChanged() const;

public:
  ClipboardHistoryModel(QObject *parent = nullptr) { setParent(parent); }

  void setData(const PaginatedResponse<ClipboardHistoryEntry> &data) {
    m_res = data;
    emit dataChanged();
  }

  void setMultiSelectedIds(const std::vector<QString> &ids) {
    m_multiSelectedIds = ids;
    // Don't emit dataChanged() as it causes the list to recalculate and scroll
    // Instead emit a custom signal that the view can handle
    emit multiSelectionChanged();
  }

  bool isMultiSelected(const QString &id) const {
    return std::find(m_multiSelectedIds.begin(), m_multiSelectedIds.end(), id) != m_multiSelectedIds.end();
  }

protected:
  int sectionCount() const override { return 1; }
  ClipboardHistorySection sectionIdFromIndex(int idx) const override { return ClipboardHistorySection::Main; }
  int sectionItemCount(ClipboardHistorySection id) const override { return m_res.data.size(); }
  std::string_view sectionName(ClipboardHistorySection id) const override { return ""; }
  const ClipboardHistoryEntry *sectionItemAt(ClipboardHistorySection id, int itemIdx) const override {
    return &m_res.data[itemIdx];
  }
  StableID stableId(const ClipboardHistoryEntry *const &item) const override {
    static std::hash<QString> hasher = {};
    return hasher(item->id);
  }
  int sectionItemHeight(ClipboardHistorySection id) const override { return 50; }

  WidgetTag widgetTag(const ClipboardHistoryEntry *const &item) const override { return 1; }

  WidgetType *createItemWidget(const ClipboardHistoryEntry *const &type) const override {
    auto widget = new ClipboardHistoryItemWidget;
    connect(widget, &ClipboardHistoryItemWidget::shiftClicked, this, [this, widget](int index) {
      emit itemShiftClicked(widget->entryId(), index);
    });
    connect(widget, &ClipboardHistoryItemWidget::ctrlClicked, this, [this, widget](int index) {
      emit itemCtrlClicked(widget->entryId(), index);
    });
    return widget;
  }

  void refreshItemWidget(const ClipboardHistoryEntry *const &entry, WidgetType *widget, int index) const override {
    static_cast<ClipboardHistoryItemWidget *>(widget)->setEntry(*entry, isMultiSelected(entry->id), index);
  }

private:
  PaginatedResponse<ClipboardHistoryEntry> m_res;
  std::vector<QString> m_multiSelectedIds;
};
