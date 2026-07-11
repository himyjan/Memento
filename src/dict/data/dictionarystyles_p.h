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

#include "dict/data/dictionarystyles.h"

namespace DictionaryStylesPrivate
{

/**
 * @brief Parses dictionary CSS into renderer-facing stylesheet data.
 */
class Parser
{
public:
    /**
     * @brief Parse the supported dictionary stylesheet subset.
     *
     * @param css The dictionary stylesheet text.
     * @return Parsed rules, indexes, and compatibility diagnostics.
     */
    [[nodiscard]]
    static DictionaryStyles::ParsedStylesheet parseStylesheet(
        const QString &css);

private:
    using CssAttributeOperator = DictionaryStyles::CssAttributeOperator;
    using CssAttributeSelector = DictionaryStyles::CssAttributeSelector;
    using CssCombinator = DictionaryStyles::CssCombinator;
    using CssDeclaration = DictionaryStyles::CssDeclaration;
    using CssPseudoClass = DictionaryStyles::CssPseudoClass;
    using CssPseudoClassSelector = DictionaryStyles::CssPseudoClassSelector;
    using CssPseudoElement = DictionaryStyles::CssPseudoElement;
    using CssRule = DictionaryStyles::CssRule;
    using CssSelectorPart = DictionaryStyles::CssSelectorPart;
    using ParsedStylesheet = DictionaryStyles::ParsedStylesheet;

    /**
     * @brief Parse one selector into renderer-supported selector parts.
     *
     * @param selector The selector to parse.
     * @param[out] pseudoElement The selector's generated-content target.
     * @param[out] specificity The selector specificity.
     * @param[out] diagnostic Unsupported-selector explanation, when needed.
     * @return Parsed selector parts, or an empty list when unsupported.
     */
    [[nodiscard]]
    static QList<CssSelectorPart> parseCssSelector(
        const QString &selector,
        CssPseudoElement &pseudoElement,
        int &specificity,
        QString *diagnostic);

    /**
     * @brief Parse CSS declarations and record safe compatibility fallbacks.
     *
     * @param body The CSS declaration body.
     * @param[out] diagnostics Compatibility notes recorded while parsing.
     * @return Parsed declarations in source order.
     */
    [[nodiscard]]
    static QList<CssDeclaration> parseCssDeclarations(
        const QString &body,
        QStringList *diagnostics);
};

}
