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

#include "dict/data/dictionarystyles.h"

#include <QDebug>

#include "dict/data/dictionarystyles_p.h"

namespace
{

/**
 * @brief Return the index key for a tag or the shared universal-rule key.
 *
 * @param tag The structured element tag requested by the renderer.
 * @param stylesheet Parsed data containing target-tag indexes.
 * @return The tag-specific key, or an empty key for universal rules.
 */
QString candidateTagKey(
    const QString &tag,
    const DictionaryStyles::ParsedStylesheet &stylesheet)
{
    return stylesheet.ruleIndexesByTargetTag.contains(tag) ? tag : "";
}

}

DictionaryStyles::DictionaryStyles(const QString &stylesheet) :
    m_stylesheet(stylesheet),
    m_parsedStylesheet(
        DictionaryStylesPrivate::Parser::parseStylesheet(m_stylesheet)
    )
{
#ifdef MEMENTO_WARN_UNSUPPORTED_CSS
    for (const QString &diagnostic : m_parsedStylesheet.diagnostics)
    {
        qWarning().noquote() << diagnostic;
    }
#endif
}

const QString &DictionaryStyles::stylesheet() const noexcept
{
    return m_stylesheet;
}

const DictionaryStyles::ParsedStylesheet &
DictionaryStyles::parsedStylesheet() const noexcept
{
    return m_parsedStylesheet;
}

const QList<qsizetype> &DictionaryStyles::candidateRuleIndexes(
    const QString &tag) const noexcept
{
    const auto rules =
        m_parsedStylesheet.ruleIndexesByTargetTag.constFind(tag);
    return rules == m_parsedStylesheet.ruleIndexesByTargetTag.cend() ?
        m_parsedStylesheet.universalRuleIndexes :
        rules.value();
}

const QList<qsizetype> &DictionaryStyles::candidateRuleIndexes(
    const QString &tag,
    qsizetype childIndex) const noexcept
{
    const QString key = candidateTagKey(tag, m_parsedStylesheet);
    const auto byTag =
        m_parsedStylesheet.ruleIndexesByTargetTagAndChildIndex.constFind(key);
    if (byTag !=
        m_parsedStylesheet.ruleIndexesByTargetTagAndChildIndex.cend())
    {
        const auto byChild = byTag->constFind(childIndex);
        if (byChild != byTag->cend())
        {
            return byChild.value();
        }
    }

    const auto nonNth =
        m_parsedStylesheet.nonNthRuleIndexesByTargetTag.constFind(key);
    return nonNth ==
        m_parsedStylesheet.nonNthRuleIndexesByTargetTag.cend() ?
        m_parsedStylesheet.universalRuleIndexes :
        nonNth.value();
}

bool DictionaryStyles::needsElementChildCountForTag(
    const QString &tag) const noexcept
{
    return m_parsedStylesheet.usesElementChildCount &&
        (m_parsedStylesheet.elementChildCountTags.contains("") ||
         m_parsedStylesheet.elementChildCountTags.contains(tag));
}
