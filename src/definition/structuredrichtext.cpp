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

#include "definition/structuredrichtext.h"

#include "definition/structuredrichtext_p.h"

namespace
{

/**
 * @brief Return the immutable implementation shared by all QML parser calls.
 *
 * @return The private structured-content renderer.
 */
const StructuredRichTextPrivate::Renderer &renderer()
{
    static const StructuredRichTextPrivate::Renderer RENDERER;
    return RENDERER;
}

}

StructuredRichText::StructuredRichText(QObject *parent) : QObject(parent)
{

}

StructuredRichText::~StructuredRichText()
{

}

QString StructuredRichText::parse(
    const DictionaryInfo *info,
    const QJsonArray &content,
    Setting::GlossaryStyle style,
    const QQuickItem *item,
    const QFont &font,
    const QColor &color,
    const QColor &backgroundColor,
    const QVariantMap &detailStates) const
{
    return renderer().parse(
        info,
        content,
        style,
        item,
        font,
        color,
        backgroundColor,
        detailStates
    );
}
