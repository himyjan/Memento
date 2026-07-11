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

#include "dict/data/dictionarystyles_p.h"

#include <algorithm>
#include <functional>

namespace DictionaryStylesPrivate
{

/* Dictionary CSS parsing, nesting expansion, and rule-index construction. */

namespace
{
/* Initial nesting depth after an opening CSS brace. */
constexpr qsizetype INITIAL_BRACE_DEPTH = 1;

/* Sentinel used when a rule has no positive target :nth-child() selector. */
constexpr qsizetype NO_NTH_CHILD_INDEX = 0;

/**
 * @brief Remove CSS block comments without changing the surrounding text.
 *
 * @param css The unparsed stylesheet text.
 * @return The stylesheet with block comments removed.
 */
QString withoutCssComments(const QString &css)
{
    QString source;
    bool inComment = false;
    bool inQuote = false;
    bool escaped = false;
    QChar quote;
    for (qsizetype i = 0; i < css.size(); ++i)
    {
        const QChar ch = css[i];
        if (!inComment && !inQuote && i + 1 < css.size() &&
            ch == '/' && css[i + 1] == '*')
        {
            inComment = true;
            ++i;
            continue;
        }
        if (inComment && i + 1 < css.size() &&
            ch == '*' && css[i + 1] == '/')
        {
            inComment = false;
            ++i;
            continue;
        }
        if (inComment)
        {
            continue;
        }
        source += ch;
        if (inQuote)
        {
            if (ch == '\\' && !escaped)
            {
                escaped = true;
                continue;
            }
            if (ch == quote && !escaped)
            {
                inQuote = false;
            }
            escaped = false;
        }
        else if (ch == '"' || ch == '\'')
        {
            inQuote = true;
            quote = ch;
        }
    }
    return source;
}

/**
 * @brief Find the closing brace paired with a CSS opening brace.
 *
 * @param source The CSS text to search.
 * @param open The position of an opening brace.
 * @return The matching closing-brace index, or -1 when it is missing.
 */
qsizetype matchingCssBrace(const QString &source, qsizetype open)
{
    qsizetype depth = INITIAL_BRACE_DEPTH;
    bool inQuote = false;
    bool escaped = false;
    QChar quote;

    for (qsizetype i = open + 1; i < source.size(); ++i)
    {
        const QChar ch = source[i];
        if (inQuote)
        {
            if (ch == '\\' && !escaped)
            {
                escaped = true;
                continue;
            }
            if (ch == quote && !escaped)
            {
                inQuote = false;
            }
            escaped = false;
            continue;
        }
        if (ch == '"' || ch == '\'')
        {
            inQuote = true;
            quote = ch;
        }
        else if (ch == '{')
        {
            ++depth;
        }
        else if (ch == '}' && --depth == 0)
        {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Skip a complete top-level CSS at-rule.
 *
 * @param source The CSS text to search.
 * @param start The index of the at-rule marker.
 * @return The index of the at-rule terminator or the text size.
 */
qsizetype skipCssAtRule(const QString &source, qsizetype start)
{
    const qsizetype semicolon = source.indexOf(';', start);
    const qsizetype brace = source.indexOf('{', start);
    if (brace >= 0 && (semicolon < 0 || brace < semicolon))
    {
        const qsizetype close = matchingCssBrace(source, brace);
        return close < 0 ? source.size() : close;
    }
    if (semicolon >= 0)
    {
        return semicolon;
    }
    return source.size();
}

/**
 * @brief Split a selector list on top-level commas.
 *
 * @param selector The selector list to split.
 * @return Individual selector strings in source order.
 */
QStringList splitCssSelectors(const QString &selector)
{
    QStringList selectors;
    QString current;
    bool inQuote = false;
    QChar quote;
    int bracketDepth = 0;
    int parenthesisDepth = 0;

    for (const QChar ch : selector)
    {
        if (inQuote)
        {
            current += ch;
            if (ch == quote)
            {
                inQuote = false;
            }
        }
        else if (ch == '"' || ch == '\'')
        {
            inQuote = true;
            quote = ch;
            current += ch;
        }
        else if (ch == '[')
        {
            ++bracketDepth;
            current += ch;
        }
        else if (ch == ']')
        {
            --bracketDepth;
            current += ch;
        }
        else if (ch == '(')
        {
            ++parenthesisDepth;
            current += ch;
        }
        else if (ch == ')')
        {
            --parenthesisDepth;
            current += ch;
        }
        else if (ch == ',' && bracketDepth == 0 && parenthesisDepth == 0)
        {
            if (!current.trimmed().isEmpty())
            {
                selectors.emplaceBack(current.trimmed());
            }
            current.clear();
        }
        else
        {
            current += ch;
        }
    }

    if (!current.trimmed().isEmpty())
    {
        selectors.emplaceBack(current.trimmed());
    }
    return selectors;
}

/**
 * @brief Expand a nested selector list against each parent selector.
 *
 * @param parents The selectors surrounding the nested CSS block.
 * @param nestedSelector The selector list found inside the CSS block.
 * @return Flattened selectors suitable for normal CSS rule parsing.
 */
QStringList expandNestedCssSelectors(
    const QStringList &parents,
    const QString &nestedSelector)
{
    QStringList combined;
    const QStringList nestedSelectors = splitCssSelectors(nestedSelector);
    combined.reserve(parents.size() * nestedSelectors.size());
    for (const QString &parent : parents)
    {
        for (const QString &nested : nestedSelectors)
        {
            if (nested.contains('&'))
            {
                QString expanded = nested.trimmed();
                expanded.replace("&", parent);
                combined.emplaceBack(std::move(expanded));
            }
            else
            {
                combined.emplaceBack(parent + ' ' + nested);
            }
        }
    }
    return combined;
}

/**
 * @brief Find a positive exact :nth-child() selector on a rule target.
 *
 * @param rule The parsed stylesheet rule to inspect.
 * @return The requested child index, or NO_NTH_CHILD_INDEX when unrestricted.
 */
qsizetype exactTargetNthChildIndex(const DictionaryStyles::CssRule &rule)
{
    if (rule.selector.isEmpty())
    {
        return NO_NTH_CHILD_INDEX;
    }

    for (const DictionaryStyles::CssPseudoClassSelector &pseudo :
         rule.selector.back().pseudoClasses)
    {
        if (pseudo.type == DictionaryStyles::CssPseudoClass::NthChild &&
            !pseudo.negated && pseudo.childIndex > 0)
        {
            return pseudo.childIndex;
        }
    }
    return NO_NTH_CHILD_INDEX;
}

}

DictionaryStyles::ParsedStylesheet Parser::parseStylesheet(
    const QString &css)
{
    const QString source = withoutCssComments(css);

    QList<CssRule> rules;
    QStringList diagnostics;
    int order = 0;

    /* Expand declarations and nested selectors into flat CSS rules. */
    std::function<void(const QStringList &, const QString &)> parseBody =
    [&] (const QStringList &selectors, const QString &body)
    {
        QString declarationsText;
        qsizetype last = 0;

        for (qsizetype i = 0; i < body.size(); ++i)
        {
            if (body[i] != '{')
            {
                continue;
            }

            qsizetype selectorStart = body.lastIndexOf(';', i);
            selectorStart = selectorStart < last ? last : selectorStart + 1;
            declarationsText += body.sliced(last, selectorStart - last);

            const QString nestedSelector =
                body.sliced(selectorStart, i - selectorStart).trimmed();
            const qsizetype close = matchingCssBrace(body, i);
            if (close < 0)
            {
                break;
            }

            const QStringList combined =
                expandNestedCssSelectors(selectors, nestedSelector);

            parseBody(combined, body.sliced(i + 1, close - i - 1));
            i = close;
            last = close + 1;
        }

        declarationsText += body.sliced(last);
        const QList<CssDeclaration> declarations =
            parseCssDeclarations(declarationsText, &diagnostics);
        if (declarations.isEmpty())
        {
            return;
        }

        for (const QString &selector : selectors)
        {
            CssPseudoElement pseudoElement = CssPseudoElement::None;
            int specificity = 0;
            QString diagnostic;
            QList<CssSelectorPart> parts = parseCssSelector(
                selector,
                pseudoElement,
                specificity,
                &diagnostic
            );
            if (parts.isEmpty())
            {
                if (!diagnostic.isEmpty())
                {
                    diagnostics.emplaceBack(
                        QStringLiteral(
                            "Ignoring unsupported dictionary CSS selector '"
                        ) +
                        selector + "': " + diagnostic
                    );
                }
                continue;
            }

            rules.emplaceBack(CssRule{
                parts,
                declarations,
                pseudoElement,
                specificity,
                order++
            });
        }
    };

    for (qsizetype i = 0; i < source.size(); ++i)
    {
        while (i < source.size() && source[i].isSpace())
        {
            ++i;
        }
        if (i >= source.size())
        {
            break;
        }
        if (source[i] == '@')
        {
            i = skipCssAtRule(source, i);
            continue;
        }

        const qsizetype open = source.indexOf('{', i);
        if (open < 0)
        {
            break;
        }

        const qsizetype close = matchingCssBrace(source, open);
        if (close < 0)
        {
            break;
        }

        parseBody(
            splitCssSelectors(source.sliced(i, open - i).trimmed()),
            source.sliced(open + 1, close - open - 1)
        );
        i = close;
    }

    std::sort(
        std::begin(rules), std::end(rules),
        [] (const CssRule &lhs, const CssRule &rhs) -> bool
        {
            return lhs.specificity < rhs.specificity ||
                (
                    lhs.specificity == rhs.specificity &&
                    lhs.order < rhs.order
                );
        }
    );

    ParsedStylesheet parsed;
    parsed.rules = std::move(rules);

    QSet<QString> targetTags;
    for (qsizetype i = 0; i < parsed.rules.size(); ++i)
    {
        const CssRule &rule = parsed.rules[i];
        for (const CssSelectorPart &part : rule.selector)
        {
            for (const CssPseudoClassSelector &pseudo :
                 part.pseudoClasses)
            {
                parsed.usesElementChildCount =
                    parsed.usesElementChildCount ||
                    pseudo.type == CssPseudoClass::LastChild;
                if (pseudo.type == CssPseudoClass::LastChild)
                {
                    parsed.elementChildCountTags.insert(part.tag);
                }
            }
        }
        const QString targetTag =
            rule.selector.isEmpty() ? "" : rule.selector.back().tag;
        if (targetTag.isEmpty())
        {
            parsed.universalRuleIndexes.emplaceBack(i);
        }
        else
        {
            targetTags.insert(targetTag);
        }
    }

    QStringList candidateTags;
    candidateTags.emplaceBack("");
    for (const QString &tag : targetTags)
    {
        QList<qsizetype> &indexes =
            parsed.ruleIndexesByTargetTag[tag];
        indexes.reserve(parsed.rules.size());
        for (qsizetype i = 0; i < parsed.rules.size(); ++i)
        {
            const CssRule &rule = parsed.rules[i];
            const QString targetTag =
                rule.selector.isEmpty() ? "" : rule.selector.back().tag;
            if (targetTag.isEmpty() || targetTag == tag)
            {
                indexes.emplaceBack(i);
            }
        }
        candidateTags.emplaceBack(tag);
    }

    for (const QString &tag : candidateTags)
    {
        const QList<qsizetype> &allIndexes = tag.isEmpty() ?
            parsed.universalRuleIndexes :
            parsed.ruleIndexesByTargetTag[tag];
        QList<qsizetype> &nonNthIndexes =
            parsed.nonNthRuleIndexesByTargetTag[tag];
        nonNthIndexes.reserve(allIndexes.size());

        QSet<qsizetype> childIndexes;
        for (const qsizetype index : allIndexes)
        {
            const qsizetype childIndex =
                exactTargetNthChildIndex(parsed.rules[index]);
            if (childIndex == NO_NTH_CHILD_INDEX)
            {
                nonNthIndexes.emplaceBack(index);
            }
            else
            {
                childIndexes.insert(childIndex);
            }
        }

        for (const qsizetype childIndex : childIndexes)
        {
            QList<qsizetype> &indexes =
                parsed.ruleIndexesByTargetTagAndChildIndex[tag][childIndex];
            indexes.reserve(allIndexes.size());
            for (const qsizetype index : allIndexes)
            {
                const qsizetype ruleChildIndex =
                    exactTargetNthChildIndex(parsed.rules[index]);
                if (ruleChildIndex == NO_NTH_CHILD_INDEX ||
                    ruleChildIndex == childIndex)
                {
                    indexes.emplaceBack(index);
                }
            }
        }
    }

    parsed.diagnostics = std::move(diagnostics);
    return parsed;
}

QList<DictionaryStyles::CssSelectorPart> Parser::parseCssSelector(
    const QString &selector,
    CssPseudoElement &pseudoElement,
    int &specificity,
    QString *diagnostic)
{
    /* Specificity weight for one class, attribute, or pseudo-class */
    constexpr int CLASS_SPECIFICITY = 10;

    /* Specificity weight for one tag or pseudo-element */
    constexpr int TAG_SPECIFICITY = 1;

    /* Character count for the attribute prefix-match operator */
    constexpr qsizetype PREFIX_ATTRIBUTE_OPERATOR_LENGTH = 2;

    /* Character count for the attribute equality operator */
    constexpr qsizetype EQUAL_ATTRIBUTE_OPERATOR_LENGTH = 1;

    /* Character count for a CSS pseudo-element prefix */
    constexpr qsizetype PSEUDO_ELEMENT_PREFIX_LENGTH = 2;

    /* Prefix that begins a supported exact numeric :nth-child() selector */
    const QString nthChildPrefix = ":nth-child(";

    /* Prefix that begins a supported simple :not() wrapper */
    const QString notPseudoPrefix = ":not(";

    pseudoElement = CssPseudoElement::None;
    specificity = 0;
    if (diagnostic != nullptr)
    {
        diagnostic->clear();
    }

    /* Return an empty selector while recording a useful compatibility note. */
    const auto reject =
    [diagnostic] (const QString &reason) -> QList<CssSelectorPart>
    {
        if (diagnostic != nullptr)
        {
            *diagnostic = reason;
        }
        return {};
    };

    QString normalized = selector.trimmed();
    if (normalized.contains("::"))
    {
        const qsizetype pseudoStart = normalized.indexOf("::");
        qsizetype pseudoEnd =
            pseudoStart + PSEUDO_ELEMENT_PREFIX_LENGTH;
        while (pseudoEnd < normalized.size() &&
               (normalized[pseudoEnd].isLetter() ||
                normalized[pseudoEnd] == '-'))
        {
            ++pseudoEnd;
        }

        const QString pseudo =
            normalized.sliced(pseudoStart, pseudoEnd - pseudoStart);
        if (pseudo == "::before")
        {
            pseudoElement = CssPseudoElement::Before;
        }
        else if (pseudo == "::after")
        {
            pseudoElement = CssPseudoElement::After;
        }
        else
        {
            return reject("unsupported pseudo-element " + pseudo);
        }
        normalized.remove(pseudoStart, pseudo.size());
        if (normalized.contains("::"))
        {
            return reject("multiple pseudo-elements");
        }
        specificity += TAG_SPECIFICITY;
    }

    QList<QString> rawParts;
    QList<CssCombinator> combinators;
    QString current;
    CssCombinator combinator = CssCombinator::Descendant;
    bool inQuote = false;
    QChar quote;
    int bracketDepth = 0;

    const auto flushPart = [&] ()
    {
        if (current.trimmed().isEmpty())
        {
            return;
        }
        rawParts.emplaceBack(current.trimmed());
        combinators.emplaceBack(combinator);
        current.clear();
        combinator = CssCombinator::Descendant;
    };

    for (const QChar ch : normalized)
    {
        if (inQuote)
        {
            current += ch;
            if (ch == quote)
            {
                inQuote = false;
            }
        }
        else if (ch == '"' || ch == '\'')
        {
            inQuote = true;
            quote = ch;
            current += ch;
        }
        else if (ch == '[')
        {
            ++bracketDepth;
            current += ch;
        }
        else if (ch == ']')
        {
            --bracketDepth;
            current += ch;
        }
        else if (ch == '>' && bracketDepth == 0)
        {
            flushPart();
            combinator = CssCombinator::Child;
        }
        else if (ch == '+' && bracketDepth == 0)
        {
            flushPart();
            combinator = CssCombinator::AdjacentSibling;
        }
        else if (ch == '~' && bracketDepth == 0)
        {
            flushPart();
            combinator = CssCombinator::GeneralSibling;
        }
        else if (ch.isSpace() && bracketDepth == 0)
        {
            if (!current.trimmed().isEmpty())
            {
                flushPart();
            }
        }
        else
        {
            current += ch;
        }
    }

    flushPart();

    if (rawParts.isEmpty())
    {
        return reject("empty selector");
    }

    QList<CssSelectorPart> parts;
    for (qsizetype i = 0; i < rawParts.size(); ++i)
    {
        CssSelectorPart part;
        part.combinator = combinators[i];
        QString raw = rawParts[i];

        qsizetype attrIndex = raw.indexOf('[');
        while (attrIndex >= 0)
        {
            const qsizetype attrEnd = raw.indexOf(']', attrIndex);
            if (attrEnd < 0)
            {
                return reject("unterminated attribute selector");
            }

            QString attr = raw.sliced(
                attrIndex + 1,
                attrEnd - attrIndex - 1
            ).trimmed();
            CssAttributeSelector selector;
            qsizetype opIndex = attr.indexOf("^=");
            if (opIndex >= 0)
            {
                selector.op = CssAttributeOperator::StartsWith;
            }
            else
            {
                opIndex = attr.indexOf('=');
                selector.op = opIndex < 0 ?
                    CssAttributeOperator::Exists :
                    CssAttributeOperator::Equals;
            }

            if (opIndex < 0)
            {
                selector.name = attr.trimmed();
            }
            else
            {
                selector.name = attr.first(opIndex).trimmed();
                const qsizetype valueOffset =
                    selector.op == CssAttributeOperator::StartsWith ?
                        PREFIX_ATTRIBUTE_OPERATOR_LENGTH :
                        EQUAL_ATTRIBUTE_OPERATOR_LENGTH;
                selector.value = attr.sliced(opIndex + valueOffset).trimmed();
                if (selector.value.size() >= 2 &&
                    ((selector.value.front() == '"' &&
                      selector.value.back() == '"') ||
                     (selector.value.front() == '\'' &&
                      selector.value.back() == '\'')))
                {
                    selector.value =
                        selector.value.sliced(1, selector.value.size() - 2);
                }
            }
            if (selector.name.isEmpty())
            {
                return reject("attribute selector without a name");
            }
            part.attributes.emplaceBack(std::move(selector));
            raw.remove(attrIndex, attrEnd - attrIndex + 1);
            attrIndex = raw.indexOf('[', attrIndex);
            specificity += CLASS_SPECIFICITY;
        }

        /* Parse one supported pseudo-class into the current selector part. */
        const auto parsePseudoClass =
        [&part, &specificity, nthChildPrefix]
        (const QString &rawPseudo, bool negated) -> bool
        {
            CssPseudoClassSelector selector;
            selector.negated = negated;
            if (rawPseudo == ":first-child")
            {
                selector.type = CssPseudoClass::FirstChild;
            }
            else if (rawPseudo == ":last-child")
            {
                selector.type = CssPseudoClass::LastChild;
            }
            else if (rawPseudo.startsWith(nthChildPrefix))
            {
                const qsizetype valueStart = nthChildPrefix.size();
                const qsizetype valueEnd =
                    rawPseudo.indexOf(')', valueStart);
                if (valueEnd != rawPseudo.size() - 1)
                {
                    return false;
                }

                const QString value = rawPseudo.sliced(
                    valueStart,
                    valueEnd - valueStart
                ).trimmed();
                if (value.isEmpty())
                {
                    return false;
                }
                for (const QChar ch : value)
                {
                    if (!ch.isDigit())
                    {
                        return false;
                    }
                }

                bool ok = false;
                const int childIndex = value.toInt(&ok);
                if (!ok || childIndex <= 0)
                {
                    return false;
                }
                selector.type = CssPseudoClass::NthChild;
                selector.childIndex = childIndex;
            }
            else
            {
                return false;
            }

            part.pseudoClasses.emplaceBack(std::move(selector));
            specificity += CLASS_SPECIFICITY;
            return true;
        };
        /* Find the endpoint of a supported or unsupported pseudo-class. */
        const auto pseudoClassTokenEnd =
        [&nthChildPrefix] (const QString &text, qsizetype start) -> qsizetype
        {
            if (text.sliced(start).startsWith(nthChildPrefix))
            {
                const qsizetype valueStart =
                    start + nthChildPrefix.size();
                const qsizetype valueEnd = text.indexOf(')', valueStart);
                return valueEnd < 0 ? -1 : valueEnd + 1;
            }

            qsizetype end = start + 1;
            while (end < text.size() &&
                   (text[end].isLetter() || text[end] == '-'))
            {
                ++end;
            }
            return end == start + 1 ? -1 : end;
        };

        qsizetype notIndex = raw.indexOf(notPseudoPrefix);
        while (notIndex >= 0)
        {
            const qsizetype pseudoStart =
                notIndex + notPseudoPrefix.size();
            const qsizetype pseudoEnd =
                pseudoClassTokenEnd(raw, pseudoStart);
            if (pseudoEnd < 0 ||
                pseudoEnd >= raw.size() ||
                raw[pseudoEnd] != ')')
            {
                return reject("unsupported :not() argument");
            }

            const QString rawPseudo =
                raw.sliced(pseudoStart, pseudoEnd - pseudoStart);
            if (!parsePseudoClass(rawPseudo.trimmed(), true))
            {
                return reject("unsupported pseudo-class " + rawPseudo);
            }
            raw.remove(notIndex, pseudoEnd - notIndex + 1);
            notIndex = raw.indexOf(notPseudoPrefix);
        }

        qsizetype pseudoIndex = raw.indexOf(':');
        while (pseudoIndex >= 0)
        {
            const qsizetype pseudoEnd =
                pseudoClassTokenEnd(raw, pseudoIndex);
            if (pseudoEnd < 0)
            {
                return reject("malformed pseudo-class selector");
            }
            const qsizetype length = pseudoEnd - pseudoIndex;
            const QString rawPseudo = raw.sliced(pseudoIndex, length);
            if (!parsePseudoClass(rawPseudo.trimmed(), false))
            {
                return reject("unsupported pseudo-class " + rawPseudo);
            }
            raw.remove(pseudoIndex, length);
            pseudoIndex = raw.indexOf(':');
        }

        qsizetype classIndex = raw.indexOf('.');
        while (classIndex >= 0)
        {
            qsizetype end = classIndex + 1;
            while (end < raw.size() &&
                   (raw[end].isLetterOrNumber() || raw[end] == '-' ||
                    raw[end] == '_'))
            {
                ++end;
            }
            if (end == classIndex + 1)
            {
                return reject("class selector without a name");
            }
            part.classNames.insert(
                raw.sliced(classIndex + 1, end - classIndex - 1)
            );
            raw.remove(classIndex, end - classIndex);
            specificity += CLASS_SPECIFICITY;
            classIndex = raw.indexOf('.', classIndex);
        }
        if (raw.contains(':'))
        {
            return reject("malformed pseudo-class selector");
        }
        if (raw.contains('#'))
        {
            return reject("unsupported ID selector");
        }

        raw = raw.trimmed();
        if (!raw.isEmpty() && raw != "*")
        {
            part.tag = raw.toLower();
            specificity += TAG_SPECIFICITY;
        }
        parts.emplaceBack(std::move(part));
    }

    return parts;
}

QList<DictionaryStyles::CssDeclaration> Parser::parseCssDeclarations(
    const QString &body,
    QStringList *diagnostics)
{
    /* CSS priority suffix not supported by the compatibility cascade. */
    const QString importantSuffix = QStringLiteral("!important");

    QList<CssDeclaration> declarations;
    QString property;
    QString value;
    bool inProperty = true;
    bool inQuote = false;
    QChar quote;
    int parenDepth = 0;

    /* Finalize one declaration while applying safe renderer fallbacks. */
    const auto flush = [&] ()
    {
        const QString name = property.trimmed().toLower();
        QString cssValue = value.trimmed();
        if (name.isEmpty() || cssValue.isEmpty())
        {
            property.clear();
            value.clear();
            inProperty = true;
            return;
        }

        if (cssValue.endsWith(importantSuffix, Qt::CaseInsensitive))
        {
            cssValue.chop(importantSuffix.size());
            cssValue = cssValue.trimmed();
            if (diagnostics != nullptr)
            {
                diagnostics->emplaceBack(
                    QStringLiteral(
                        "Ignoring !important priority for dictionary CSS "
                        "property '"
                    ) +
                    name + "'"
                );
            }
        }
        if (cssValue.isEmpty())
        {
            property.clear();
            value.clear();
            inProperty = true;
            return;
        }

        cssValue.replace('"', '\'');
        if (cssValue.size() >= 2 &&
            ((cssValue.front() == '\'' && cssValue.back() == '\'') ||
             (cssValue.front() == '"' && cssValue.back() == '"')))
        {
            cssValue = cssValue.sliced(1, cssValue.size() - 2);
        }

        if (name == "word-break")
        {
            if (cssValue.compare("keep-all", Qt::CaseInsensitive) == 0)
            {
                declarations.emplaceBack(
                    CssDeclaration{"white-space", "nowrap"}
                );
            }
            else if (cssValue.compare("normal", Qt::CaseInsensitive) == 0)
            {
                declarations.emplaceBack(
                    CssDeclaration{"white-space", "normal"}
                );
            }
            else if (diagnostics != nullptr)
            {
                diagnostics->emplaceBack(
                    QStringLiteral(
                        "Ignoring unsupported dictionary CSS word-break "
                        "value '"
                    ) +
                    cssValue + "'"
                );
            }
        }
        else
        {
            declarations.emplaceBack(CssDeclaration{name, cssValue});
        }
        property.clear();
        value.clear();
        inProperty = true;
    };

    for (const QChar ch : body)
    {
        if (inQuote)
        {
            (inProperty ? property : value) += ch;
            if (ch == quote)
            {
                inQuote = false;
            }
        }
        else if (ch == '"' || ch == '\'')
        {
            inQuote = true;
            quote = ch;
            (inProperty ? property : value) += ch;
        }
        else if (ch == '(')
        {
            ++parenDepth;
            (inProperty ? property : value) += ch;
        }
        else if (ch == ')')
        {
            --parenDepth;
            (inProperty ? property : value) += ch;
        }
        else if (ch == ':' && inProperty)
        {
            inProperty = false;
        }
        else if (ch == ';' && parenDepth == 0)
        {
            flush();
        }
        else
        {
            (inProperty ? property : value) += ch;
        }
    }
    flush();

    return declarations;
}

}
