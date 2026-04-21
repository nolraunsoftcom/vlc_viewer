#pragma once

#include <QString>

namespace Style {

inline const QString MENU = QStringLiteral(
    "QMenu { background-color: #2a2a2a; color: #ccc; border: 1px solid #444; font-size: 12px; }"
    "QMenu::item { padding: 6px 20px; }"
    "QMenu::item:selected { background-color: #335; }");

} // namespace Style
