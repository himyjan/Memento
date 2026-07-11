////////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2026 Ripose
//
// This file is part of Memento.
//
// Memento is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2 of the License.
//
// Memento is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Memento.  If not, see <https://www.gnu.org/licenses/>.
//
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <QObject>

#include <QColor>
#include <QFont>
#include <QJsonArray>
#include <QQuickItem>
#include <QVariantMap>

#include "dict/data/dictionaryinfo.h"
#include "setting/keys.h"

/**
 * @brief Parses structured content into Qt-friendly rich text.
 */
class StructuredRichText : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Create the QML-visible structured-content parser.
     *
     * @param parent Parent QObject that owns this parser.
     */
    StructuredRichText(QObject *parent = nullptr);

    /**
     * @brief Destroy the QML-visible structured-content parser.
     */
    ~StructuredRichText() override;

    /**
     * @brief Parse structured content into Qt rich text.
     *
     * @param info Information about the dictionary being rendered.
     * @param content The structured content to parse.
     * @param style The glossary style to display.
     * @param item The item this rich text will be displayed in.
     * @param font The font in use.
     * @param color The glossary text color.
     * @param backgroundColor The glossary canvas color.
     * @param detailStates Open-state overrides keyed by stable detail IDs.
     * @return A string containing the structured content as rich text.
     */
    [[nodiscard]]
    Q_INVOKABLE QString parse(
        const DictionaryInfo *info,
        const QJsonArray &content,
        Setting::GlossaryStyle style,
        const QQuickItem *item,
        const QFont &font,
        const QColor &color,
        const QColor &backgroundColor,
        const QVariantMap &detailStates) const;
};
