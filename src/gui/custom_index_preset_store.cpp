#include "custom_index_preset_store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

namespace {

QString trimmedQString(const std::string& value) {
    return QString::fromUtf8(value).trimmed();
}

std::string sanitizePresetName(const QString& name) {
    std::string result;
    const QByteArray utf8 = name.toUtf8();
    result.reserve(static_cast<size_t>(utf8.size()));
    for (const char ch : utf8) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch)) {
            result.push_back(static_cast<char>(std::tolower(uch)));
        } else if (ch == ' ' || ch == '-' || ch == '_') {
            if (result.empty() || result.back() == '_') {
                continue;
            }
            result.push_back('_');
        }
    }

    while (!result.empty() && result.back() == '_') {
        result.pop_back();
    }
    return result;
}

QString resolvePresetFilePath() {
    const QString overridePath = qEnvironmentVariable("GIS_CUSTOM_INDEX_PRESET_FILE");
    if (!overridePath.trimmed().isEmpty()) {
        return overridePath;
    }

    QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (baseDir.trimmed().isEmpty()) {
        baseDir = QDir::currentPath();
    }
    return QDir(baseDir).filePath(QStringLiteral("spindex_custom_index_presets.json"));
}

QJsonObject presetToJson(const gis::gui::CustomIndexUserPreset& preset) {
    QJsonObject object;
    object.insert(QStringLiteral("key"), QString::fromUtf8(preset.key));
    object.insert(QStringLiteral("name"), QString::fromUtf8(preset.name));
    object.insert(QStringLiteral("expression"), QString::fromUtf8(preset.expression));
    return object;
}

std::vector<gis::gui::CustomIndexUserPreset> parsePresets(const QJsonDocument& document) {
    std::vector<gis::gui::CustomIndexUserPreset> presets;
    if (!document.isObject()) {
        return presets;
    }

    const QJsonArray items = document.object().value(QStringLiteral("custom_index_presets")).toArray();
    presets.reserve(static_cast<size_t>(items.size()));
    for (const QJsonValue& item : items) {
        const QJsonObject object = item.toObject();
        const QString key = object.value(QStringLiteral("key")).toString().trimmed();
        const QString name = object.value(QStringLiteral("name")).toString().trimmed();
        const QString expression = object.value(QStringLiteral("expression")).toString().trimmed();
        if (key.isEmpty() || name.isEmpty() || expression.isEmpty()) {
            continue;
        }
        presets.push_back({
            key.toUtf8().constData(),
            name.toUtf8().constData(),
            expression.toUtf8().constData()
        });
    }
    return presets;
}

bool writePresets(const std::vector<gis::gui::CustomIndexUserPreset>& presets,
                  std::string* errorMessage) {
    const QString filePath = resolvePresetFilePath();
    const QFileInfo fileInfo(filePath);
    QDir().mkpath(fileInfo.dir().absolutePath());

    QJsonArray items;
    for (const auto& preset : presets) {
        items.append(presetToJson(preset));
    }

    QJsonObject root;
    root.insert(QStringLiteral("custom_index_presets"), items);

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = std::string("无法写入预设文件: ") + filePath.toUtf8().constData();
        }
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (errorMessage) {
            *errorMessage = std::string("保存预设文件失败: ") + filePath.toUtf8().constData();
        }
        return false;
    }
    return true;
}

} // namespace

namespace gis::gui {

std::string customIndexUserPresetFilePath() {
    return resolvePresetFilePath().toUtf8().constData();
}

std::vector<CustomIndexUserPreset> loadCustomIndexUserPresets() {
    QFile file(resolvePresetFilePath());
    if (!file.exists()) {
        return {};
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    return parsePresets(document);
}

std::string findCustomIndexUserPresetExpression(const std::string& presetKey) {
    for (const auto& preset : loadCustomIndexUserPresets()) {
        if (preset.key == presetKey) {
            return preset.expression;
        }
    }
    return {};
}

std::string findCustomIndexUserPresetName(const std::string& presetKey) {
    for (const auto& preset : loadCustomIndexUserPresets()) {
        if (preset.key == presetKey) {
            return preset.name;
        }
    }
    return {};
}

bool isCustomIndexUserPresetKey(const std::string& presetKey) {
    return presetKey.rfind("user:", 0) == 0;
}

std::string saveCustomIndexUserPreset(const std::string& name,
                                      const std::string& expression,
                                      std::string* errorMessage) {
    const QString trimmedName = trimmedQString(name);
    const QString trimmedExpression = trimmedQString(expression);
    if (trimmedName.isEmpty()) {
        if (errorMessage) {
            *errorMessage = "预设名称不能为空";
        }
        return {};
    }
    if (trimmedExpression.isEmpty()) {
        if (errorMessage) {
            *errorMessage = "表达式不能为空";
        }
        return {};
    }

    auto presets = loadCustomIndexUserPresets();
    for (auto& preset : presets) {
        if (QString::fromUtf8(preset.name).trimmed() == trimmedName) {
            preset.expression = trimmedExpression.toUtf8().constData();
            if (writePresets(presets, errorMessage)) {
                return preset.key;
            }
            return {};
        }
    }

    std::string baseKey = sanitizePresetName(trimmedName);
    if (baseKey.empty()) {
        baseKey = "preset";
    }

    std::string candidateKey = "user:" + baseKey;
    int suffix = 2;
    const auto keyExists = [&](const std::string& key) {
        return std::any_of(presets.begin(), presets.end(), [&](const auto& preset) {
            return preset.key == key;
        });
    };
    while (keyExists(candidateKey)) {
        candidateKey = "user:" + baseKey + "_" + std::to_string(suffix++);
    }

    presets.push_back({
        candidateKey,
        trimmedName.toUtf8().constData(),
        trimmedExpression.toUtf8().constData()
    });
    if (!writePresets(presets, errorMessage)) {
        return {};
    }
    return candidateKey;
}

bool removeCustomIndexUserPreset(const std::string& presetKey,
                                 std::string* errorMessage) {
    if (!isCustomIndexUserPresetKey(presetKey)) {
        if (errorMessage) {
            *errorMessage = "只能删除自定义预设";
        }
        return false;
    }

    auto presets = loadCustomIndexUserPresets();
    const auto it = std::remove_if(presets.begin(), presets.end(), [&](const auto& preset) {
        return preset.key == presetKey;
    });
    if (it == presets.end()) {
        if (errorMessage) {
            *errorMessage = "未找到要删除的预设";
        }
        return false;
    }
    presets.erase(it, presets.end());
    return writePresets(presets, errorMessage);
}

} // namespace gis::gui
