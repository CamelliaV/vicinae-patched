#pragma once
#include "ui/vlist/common/vertical-list-model.hpp"
#include "utils.hpp"
#include <cstdint>

enum class FileSearchModelSection : std::uint8_t { Results };

class FileSearchModel : public vicinae::ui::VerticalListModel<std::filesystem::path, FileSearchModelSection> {
public:
  void setSectionName(std::string_view name) { m_sectionName = name; }

  void setFiles(const std::vector<std::filesystem::path> &files) {
    m_paths = files;
    emit dataChanged();
  }

  int sectionCount() const override { return 1; }

  FileSearchModelSection sectionIdFromIndex(int idx) const override {
    return FileSearchModelSection::Results;
  }

  int sectionItemCount(FileSearchModelSection id) const override {
    switch (id) {
    case FileSearchModelSection::Results:
      return m_paths.size();
    }
    return 0;
  }

  std::string_view sectionName(FileSearchModelSection id) const override { return m_sectionName; }

  std::filesystem::path sectionItemAt(FileSearchModelSection id, int itemIdx) const override {
    switch (id) {
    case FileSearchModelSection::Results:
      return m_paths[itemIdx];
    }
    return {};
  }
  int sectionItemHeight(FileSearchModelSection id) const override { return 41; }

  StableID stableId(const std::filesystem::path &item) const override {
    return std::hash<std::filesystem::path>{}(item);
  }

  WidgetTag widgetTag(const std::filesystem::path &item) const override { return 1; }
  WidgetType *createItemWidget(const std::filesystem::path &type) const override {
    return new DefaultListItemWidget;
  }
  void refreshItemWidget(const std::filesystem::path &path, WidgetType *widget, int index) const override {
    auto w = static_cast<DefaultListItemWidget *>(widget);
    w->setIconUrl(ImageURL::fileIcon(path));
    w->setName(getLastPathComponent(path).c_str());
    w->setActive(false);
  }

private:
  std::vector<std::filesystem::path> m_paths;
  std::string_view m_sectionName;
};
