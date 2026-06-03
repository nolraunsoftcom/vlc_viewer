#pragma once

#include <QString>

namespace Style {

inline const QString MENU = QStringLiteral(
    "QMenu { background-color: #ffffff; color: #1f1f1f; border: 1px solid #c8c8c8; font-size: 12px; }"
    "QMenu::item { padding: 6px 20px; }"
    "QMenu::item:selected { background-color: #dbeafe; }"
    "QMenu::item:disabled { color: #9a9a9a; background-color: #f4f4f4; }"
    "QMenu::item:disabled:selected { color: #9a9a9a; background-color: #f4f4f4; }");

// info bar 내 툴 버튼 (📷, ⏺)
inline const QString TOOL_BUTTON = QStringLiteral(
    "QPushButton { color: #3a3a3a; background: transparent; border: none; "
    "padding: 0; margin: 0 2px; font-size: 12px; min-width: 24px; min-height: 20px; }"
    "QPushButton:hover { color: #0f62fe; }"
    "QPushButton:disabled { color: #b0b0b0; }");

// 녹화 토글 활성 상태 (Active)
inline const QString TOOL_BUTTON_REC = QStringLiteral(
    "QPushButton { color: #ff4040; background: transparent; border: none; "
    "padding: 0; margin: 0 2px; font-size: 12px; min-width: 24px; min-height: 20px; }"
    "QPushButton:hover { color: #ff6666; }"
    "QPushButton:disabled { color: #b0b0b0; }");

// REC 배지 — Active (빨강)
inline const QString REC_BADGE_ACTIVE = QStringLiteral(
    "QLabel { color: #ff4040; font-size: 10px; font-weight: bold; "
    "background: transparent; padding: 0 4px; }");

// REC 배지 — Starting (노랑)
inline const QString REC_BADGE_STARTING = QStringLiteral(
    "QLabel { color: #e8a838; font-size: 10px; font-weight: bold; "
    "background: transparent; padding: 0 4px; }");

} // namespace Style
