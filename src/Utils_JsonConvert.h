#pragma once
#include <nlohmann/json.hpp>

class QString;
class QByteArray;

void from_json(const nlohmann::json& j, QString& s);
void to_json(nlohmann::json& j, const QString& s);

void from_json(const nlohmann::json& j, QByteArray& s);
void to_json(nlohmann::json& j, const QByteArray& s);