#pragma once

#include "ModelTypes.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <utility>

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

private:
    std::filesystem::path m_root;
};
