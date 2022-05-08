#pragma once
#include <nlohmann/json.hpp>
#include <spdlog/fmt/fmt.h>

class QString;
class QByteArray;

void from_json(const nlohmann::json& j, QString& s);
void to_json(nlohmann::json& j, const QString& s);

void from_json(const nlohmann::json& j, QByteArray& s);
void to_json(nlohmann::json& j, const QByteArray& s);

template <> struct fmt::formatter<QString> : formatter<std::string> {
    template <typename FormatContext>
    auto format(QString s, FormatContext& ctx) {
        return formatter<string_view>::format(s.toStdString(), ctx);
    }
};