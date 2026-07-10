#pragma once

#include "ModelTypes.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct AuditExportResult {
    bool success = false;
    std::size_t eventsExported = 0;
    std::size_t invalidEvents = 0;
};

class AuditLog {
public:
    explicit AuditLog(std::filesystem::path root) : m_root(std::move(root)) {}

    bool recordMetadataChange(const ModelData& model,
                              std::string_view property,
                              std::string_view before,
                              std::string_view after) const {
        if (before == after)
            return true;

        const QDateTime now = QDateTime::currentDateTimeUtc();
        const QString timestamp = now.toString(Qt::ISODateWithMs);
        const QString actor = [&] {
            const auto env = QProcessEnvironment::systemEnvironment();
            const QString user = env.value("USER", env.value("USERNAME"));
            return user.isEmpty() ? QStringLiteral("unknown") : user;
        }();

        std::error_code ec;
        const auto day = now.toString("yyyy-MM-dd").toStdString();
        const auto dayDir = m_root / day;
        std::filesystem::create_directories(dayDir, ec);
        if (ec)
            return false;

        std::random_device random;
        const auto suffix = static_cast<unsigned long long>(random());
        const auto stem = "event-" + now.toString("yyyyMMddTHHmmsszzz").toStdString()
                        + "-" + std::to_string(suffix);
        const auto finalPath = dayDir / (stem + ".json");
        const auto tempPath = dayDir / (stem + ".tmp");

        QJsonObject event;
        event["schema_version"] = 1;
        event["event_type"] = QStringLiteral("metadata_changed");
        event["timestamp"] = timestamp;
        event["actor"] = actor;
        event["model_id"] = model.id;
        event["short_name"] = QString::fromStdString(model.short_name);
        event["file_path"] = QString::fromStdString(model.file_path);
        event["property"] = QString::fromStdString(std::string(property));
        event["before"] = QString::fromStdString(std::string(before));
        event["after"] = QString::fromStdString(std::string(after));

        std::ofstream output(tempPath, std::ios::out | std::ios::trunc);
        if (!output)
            return false;

        const QByteArray payload = QJsonDocument(event).toJson(QJsonDocument::Compact);
        output.write(payload.constData(), payload.size());
        output << '\n';
        output.close();
        if (!output)
            return false;

        std::filesystem::rename(tempPath, finalPath, ec);
        if (ec) {
            std::filesystem::remove(tempPath, ec);
            return false;
        }

        return true;
    }

    AuditExportResult exportJsonLines(const std::filesystem::path& outputPath) const {
        AuditExportResult result;
        std::error_code ec;
        std::vector<std::filesystem::path> events;

        if (std::filesystem::exists(m_root, ec)) {
            for (std::filesystem::recursive_directory_iterator it(m_root, ec), end;
                 !ec && it != end;
                 it.increment(ec)) {
                if (it->is_regular_file(ec) && it->path().extension() == ".json")
                    events.push_back(it->path());
            }
        }
        if (ec)
            return result;

        std::sort(events.begin(), events.end());
        if (!outputPath.parent_path().empty()) {
            std::filesystem::create_directories(outputPath.parent_path(), ec);
            if (ec)
                return result;
        }

        const auto tempPath = outputPath.string() + ".tmp";
        std::ofstream output(tempPath, std::ios::out | std::ios::trunc);
        if (!output)
            return result;

        for (const auto& eventPath : events) {
            std::ifstream input(eventPath);
            const std::string contents((std::istreambuf_iterator<char>(input)), {});
            const QJsonDocument event = QJsonDocument::fromJson(
                QByteArray::fromStdString(contents));
            if (!event.isObject()) {
                ++result.invalidEvents;
                continue;
            }

            const QByteArray line = event.toJson(QJsonDocument::Compact);
            output.write(line.constData(), line.size());
            output << '\n';
            ++result.eventsExported;
        }
        output.close();
        if (!output)
            return AuditExportResult{};

        std::filesystem::rename(tempPath, outputPath, ec);
        if (ec) {
            ec.clear();
            std::filesystem::remove(outputPath, ec);
            if (!ec)
                std::filesystem::rename(tempPath, outputPath, ec);
            if (ec) {
                std::filesystem::remove(tempPath, ec);
                return AuditExportResult{};
            }
        }

        result.success = true;
        return result;
    }

private:
    std::filesystem::path m_root;
};
