#pragma once
# pragma execution_character_set("utf-8") 
#include <nlohmann/json.hpp>
#include <spdlog/fmt/fmt.h>

#ifdef _WIN32
#include <QTextCodec>	
#endif

class QString;
class QByteArray;

void from_json(const nlohmann::json& j, QString& s);
void to_json(nlohmann::json& j, const QString& s);

void from_json(const nlohmann::json& j, QByteArray& s);
void to_json(nlohmann::json& j, const QByteArray& s);

template <> struct fmt::formatter<QString> : formatter<std::string> {
    template <typename FormatContext>
    auto format(QString s, FormatContext& ctx) {
#ifdef _WIN32
        return formatter<string_view>::format(QTextCodec::codecForName("GBK")->fromUnicode(s).toStdString(), ctx);
#else
        return formatter<string_view>::format(s.toStdString(), ctx);		
#endif
    }
};
