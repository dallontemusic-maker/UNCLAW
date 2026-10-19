#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <memory>

// Список папок браузера хранится в пользовательском файле настроек (общий для всех экземпляров плагина),
// поэтому добавленные папки не пропадают при закрытии проекта.
class BrowserSettings {
public:
    BrowserSettings() {
        juce::PropertiesFile::Options o;
        o.applicationName = "WavetableSynth";
        o.folderName = "WavetableSynth";
        o.filenameSuffix = ".settings";
        o.osxLibrarySubFolder = "Application Support";
        o.storageFormat = juce::PropertiesFile::storeAsXML;
        file = std::make_unique<juce::PropertiesFile>(o);
    }

    juce::StringArray getFolders() {
        file->reload();
        auto list = juce::StringArray::fromLines(file->getValue("folders"));
        list.removeEmptyStrings();
        return list;
    }

    void addFolder(const juce::File& folder) {
        auto list = getFolders();
        list.addIfNotAlreadyThere(folder.getFullPathName());
        store(list);
    }

    void removeFolder(int index) {
        auto list = getFolders();
        if (juce::isPositiveAndBelow(index, list.size())) {
            list.remove(index);
            store(list);
        }
    }

    int getLastFolderIndex() { return file->getIntValue("lastFolder", 0); }
    void setLastFolderIndex(int index) { file->setValue("lastFolder", index); file->saveIfNeeded(); }

private:
    void store(const juce::StringArray& list) {
        file->setValue("folders", list.joinIntoString("\n"));
        file->saveIfNeeded();
    }

    std::unique_ptr<juce::PropertiesFile> file;
};
