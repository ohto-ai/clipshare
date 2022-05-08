#include <QString>
#include "Utils.h"

void from_json(const nlohmann::json& j, QString& s)
{
    s = QString::fromStdString(j.get<std::string>());
}
void to_json(nlohmann::json& j, const QString& s)
{
    j = s.toStdString();
}

void from_json(const nlohmann::json& j, QByteArray& s)
{
    s = QByteArray::fromStdString(j.get<std::string>());
}
void to_json(nlohmann::json& j, const QByteArray& s)
{
    j = s.toStdString();
}
