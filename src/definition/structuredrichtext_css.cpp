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

#include "definition/structuredrichtext_p.h"

#include <array>

namespace StructuredRichTextPrivate
{

/* CSS matching and Qt rich-text compatibility. */

/**
 * @brief Split a CSS value into whitespace-delimited top-level tokens.
 *
 * Whitespace inside quoted strings and function parentheses is preserved.
 *
 * @param value The CSS value to tokenize.
 * @return The top-level CSS tokens in source order.
 */
QStringList splitCssTokens(const QString &value)
{
    QStringList tokens;
    QString current;
    QChar quote;
    bool inQuote = false;
    int parenthesisDepth = 0;

    for (const QChar ch : value.trimmed())
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
        else if (ch.isSpace() && parenthesisDepth == 0)
        {
            if (!current.isEmpty())
            {
                tokens.emplaceBack(std::move(current));
                current.clear();
            }
        }
        else
        {
            current += ch;
        }
    }
    if (!current.isEmpty())
    {
        tokens.emplaceBack(std::move(current));
    }
    return tokens;
}

void Renderer::addStructuredStyle(
    const QJsonObject &obj,
    Renderer::Context &ctx,
    Renderer::ElementRenderState &state) const
{
    /* Structured style key that changes the inherited font size */
    constexpr const char *KEY_FONT_SIZE = "fontSize";

    const auto addString = [&obj, &ctx, &state, this]
    (const char *jsonKey, const char *cssProperty)
    {
        if (obj[jsonKey].isString())
        {
            addResolvedCssDeclaration(
                cssProperty,
                obj[jsonKey].toString(),
                ctx,
                state.declarations
            );
        }
    };
    const auto addSpacing = [&obj, &ctx, &state, this]
    (const char *jsonKey, const char *cssProperty)
    {
        if (obj[jsonKey].isString())
        {
            addResolvedCssDeclaration(
                cssProperty,
                obj[jsonKey].toString(),
                ctx,
                state.declarations
            );
        }
        else if (obj[jsonKey].isDouble())
        {
            addResolvedCssDeclaration(
                cssProperty,
                QString::number(obj[jsonKey].toDouble()) + "em",
                ctx,
                state.declarations
            );
        }
    };

    addString("fontStyle", "font-style");
    addString("fontWeight", "font-weight");
    if (obj[KEY_FONT_SIZE].isString())
    {
        const QString fontSize =
            resolveCssValue(obj[KEY_FONT_SIZE].toString(), ctx);
        if (!fontSize.isEmpty())
        {
            const double pixels = cssFontSizeToPixels(
                fontSize,
                ctx.screen,
                ctx.parentFontPixelSize,
                ctx.rootFontPixelSize
            );
            if (pixels >= 0.0)
            {
                state.fontPixelSize = pixels;
                addCssDeclaration(
                    "font-size",
                    formatPixelSize(pixels) + "px",
                    state.declarations
                );
            }
            else
            {
                addCssDeclaration("font-size", fontSize, state.declarations);
            }
        }
    }

    addString("color", "color");
    addString("background", "background");
    addString("backgroundColor", "background-color");
    if (obj["textDecorationLine"].isArray())
    {
        QStringList lines;
        const QJsonArray values =
            obj["textDecorationLine"].toArray();
        lines.reserve(values.size());
        for (const QJsonValue &value : values)
        {
            if (value.isString())
            {
                lines.emplaceBack(value.toString());
            }
        }
        addResolvedCssDeclaration(
            "text-decoration-line",
            lines.join(' '),
            ctx,
            state.declarations
        );
    }
    else
    {
        addString("textDecorationLine", "text-decoration-line");
    }
    addString("textDecorationStyle", "text-decoration-style");
    addString("textDecorationColor", "text-decoration-color");
    addString("borderColor", "border-color");
    addString("borderStyle", "border-style");
    addString("borderRadius", "border-radius");
    addString("borderWidth", "border-width");
    addString("clipPath", "clip-path");
    addString("verticalAlign", "vertical-align");
    addString("textAlign", "text-align");
    addString("textEmphasis", "text-emphasis");
    addString("textShadow", "text-shadow");
    addSpacing("margin", "margin");
    addSpacing("marginTop", "margin-top");
    addSpacing("marginRight", "margin-right");
    addSpacing("marginBottom", "margin-bottom");
    addSpacing("marginLeft", "margin-left");
    addString("padding", "padding");
    addString("paddingTop", "padding-top");
    addString("paddingRight", "padding-right");
    addString("paddingBottom", "padding-bottom");
    addString("paddingLeft", "padding-left");
    addString("wordBreak", "word-break");
    addString("whiteSpace", "white-space");
    addString("cursor", "cursor");
    addString("listStyleType", "list-style-type");
}

void Renderer::resolveElementStyles(
    const QJsonObject &obj,
    Renderer::Context &ctx,
    Renderer::ElementRenderState &state) const
{
    /* Structured-content key containing direct style declarations */
    constexpr const char *KEY_STYLE = "style";

    if (state.tag == "table")
    {
        addCssDeclaration("border", "1px solid", state.declarations);
        addCssDeclaration(
            "border-collapse", "collapse", state.declarations
        );
    }
    else if (state.tag == "th" || state.tag == "td")
    {
        addCssDeclaration("border", "1px solid", state.declarations);
        addCssDeclaration(
            "border-collapse", "collapse", state.declarations
        );
        addCssDeclaration("padding", "5px", state.declarations);
    }

    addMatchingCssRules(
        ctx,
        state.declarations,
        &state.beforeContent,
        &state.afterContent
    );

    if (obj[KEY_STYLE].isObject())
    {
        addStructuredStyle(
            obj[KEY_STYLE].toObject(),
            ctx,
            state
        );
    }
    if (state.declarations.contains("font-size"))
    {
        const double pixelSize = cssFontSizeToPixels(
            state.declarations["font-size"],
            ctx.screen,
            ctx.parentFontPixelSize,
            ctx.rootFontPixelSize
        );
        if (pixelSize >= 0.0)
        {
            state.fontPixelSize = pixelSize;
        }
    }
    if (state.declarations.contains("color"))
    {
        state.textColor = state.declarations["color"];
    }
}

void Renderer::addCssDeclaration(
    const QString &property,
    const QString &value,
    Renderer::CssDeclarations &declarations) const
{
    const QString name = property.trimmed().toLower();
    QString cssValue = value.trimmed();

    if (name.isEmpty() || cssValue.isEmpty())
    {
        return;
    }

    cssValue.replace('"', '\'');

    if (name == "margin")
    {
        /* Maximum component count permitted by the CSS margin shorthand */
        constexpr qsizetype MAXIMUM_MARGIN_VALUES = 4;

        QStringList values;
        QString current;
        bool inQuote = false;
        QChar quote;
        int parenDepth = 0;

        for (const QChar ch : cssValue)
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
            else if (ch == '(')
            {
                ++parenDepth;
                current += ch;
            }
            else if (ch == ')')
            {
                --parenDepth;
                current += ch;
            }
            else if (ch.isSpace() && parenDepth == 0)
            {
                if (!current.isEmpty())
                {
                    values.emplaceBack(current);
                    current.clear();
                }
            }
            else
            {
                current += ch;
            }
        }
        if (!current.isEmpty())
        {
            values.emplaceBack(current);
        }
        if (values.isEmpty() ||
            values.size() > MAXIMUM_MARGIN_VALUES)
        {
            return;
        }

        const QString top = values[0];
        const QString right = values.size() > 1 ? values[1] : top;
        const QString bottom = values.size() > 2 ? values[2] : top;
        const QString left = values.size() > 3 ? values[3] : right;
        addCssDeclaration("margin-top", top, declarations);
        addCssDeclaration("margin-right", right, declarations);
        addCssDeclaration("margin-bottom", bottom, declarations);
        addCssDeclaration("margin-left", left, declarations);
        return;
    }

    if (name == "clip-path" &&
        cssValue.startsWith("circle(", Qt::CaseInsensitive))
    {
        declarations["border-radius"] = "50%";
        return;
    }

    /* Make sure the function is supported by Qt rich text before using it */
    QRegularExpressionMatchIterator functions =
        m_cssFunctionRegex.globalMatch(cssValue);
    while (functions.hasNext())
    {
        const QString functionName =
            functions.next().captured(1).toLower();
        if (!m_supportedCssFunctions.contains(functionName))
        {
            return;
        }
    }

    if (name == "text-decoration-line")
    {
        declarations["text-decoration"] = cssValue;
    }
    else if (name == "content")
    {
        /* Remove quotes before injecting text */
        if (cssValue.size() >= 2 &&
            ((cssValue.front() == '"' && cssValue.back() == '"') ||
             (cssValue.front() == '\'' && cssValue.back() == '\'')))
        {
            cssValue = cssValue.sliced(1, cssValue.size() - 2);
        }
        declarations[name] = cssValue;
    }
    else if (name == "background")
    {
        if (!cssValue.contains(' ') && !cssValue.contains("url("))
        {
            declarations["background-color"] = cssValue;
        }
    }
    else if (name == "list-style")
    {
        for (const QString &token : splitCssTokens(cssValue))
        {
            const QString marker = normalizeListMarker(token);
            if (marker == "none" || isBuiltInListMarker(marker))
            {
                declarations["list-style-type"] = marker;
                return;
            }
        }
    }
    else if (name == "word-break")
    {
        if (cssValue.compare("keep-all", Qt::CaseInsensitive) == 0)
        {
            declarations["white-space"] = "nowrap";
        }
        else if (cssValue.compare("normal", Qt::CaseInsensitive) == 0)
        {
            declarations["white-space"] = "normal";
        }
    }
    else if (m_supportedCssProperties.contains(name))
    {
        declarations[name] = cssValue;
    }
}

void Renderer::addResolvedCssDeclaration(
    const QString &property,
    const QString &value,
    Renderer::Context &ctx,
    Renderer::CssDeclarations &declarations) const
{
    const QString resolved = resolveCssValue(value, ctx);
    if (resolved.isEmpty())
    {
        return;
    }
    addCssDeclaration(property, resolved, declarations);
}

void Renderer::addCssDeclarations(
    const Renderer::CssDeclarations &declarations,
    QString &out) const
{
    for (const auto &[key, value] : declarations.asKeyValueRange())
    {
        /* These are stored as metadata, not handled directly by styles */
        if (key == "content" || key == "list-style-type")
        {
            continue;
        }

        out += key;
        out += ": ";
        out += value;
        out += ';';
    }
}

QString Renderer::resolveCssValue(
    const QString &value,
    Renderer::Context &ctx) const
{
    const auto cached = ctx.resolvedCssValues.constFind(value);
    if (cached != ctx.resolvedCssValues.cend())
    {
        return cached.value();
    }

    QString resolved = resolveCssVariables(value.trimmed(), ctx);
    if (resolved.isEmpty())
    {
        ctx.resolvedCssValues[value] = "";
        return "";
    }

    resolved = resolveCssCalculations(resolved);
    if (resolved.isEmpty())
    {
        ctx.resolvedCssValues[value] = "";
        return "";
    }

    if (resolved.startsWith("color-mix(", Qt::CaseInsensitive))
    {
        resolved = resolveColorMix(resolved);
    }
    else if (resolved.contains("gradient(", Qt::CaseInsensitive))
    {
        resolved = cssGradientFallback(resolved);
    }
    ctx.resolvedCssValues[value] = resolved;
    return resolved;
}

QString Renderer::resolveCssVariables(
    QString value,
    const Renderer::Context &ctx) const
{
    /* Recursion guard for nested or cyclic CSS variable substitution */
    constexpr int MAX_REPLACEMENTS = 32;

    /* Character count of the opening "var(" token */
    constexpr qsizetype CSS_VARIABLE_PREFIX_LENGTH = 4;

    for (int replacementCount = 0;
         replacementCount < MAX_REPLACEMENTS;
         ++replacementCount)
    {
        const qsizetype start = value.indexOf(
            "var(",
            0,
            Qt::CaseInsensitive
        );
        if (start < 0)
        {
            return value;
        }

        qsizetype end = -1;
        int depth = 1;
        for (qsizetype i = start + CSS_VARIABLE_PREFIX_LENGTH;
             i < value.size();
             ++i)
        {
            if (value[i] == '(')
            {
                ++depth;
            }
            else if (value[i] == ')' && --depth == 0)
            {
                end = i;
                break;
            }
        }
        if (end < 0)
        {
            return "";
        }

        const QStringList arguments = splitCssArguments(
            value.sliced(
                start + CSS_VARIABLE_PREFIX_LENGTH,
                end - start - CSS_VARIABLE_PREFIX_LENGTH
            )
        );
        if (arguments.isEmpty())
        {
            return "";
        }

        const QString name = arguments[0].trimmed().toLower();
        QString fallback;
        if (arguments.size() > 1)
        {
            fallback = arguments.sliced(1).join(',');
        }

        QString replacement;
        if (name == "--text-color" || name == "--fg")
        {
            replacement = ctx.glossaryTextColor.isEmpty() ?
                fallback :
                ctx.glossaryTextColor;
        }
        else if (name == "--font-size-no-units")
        {
            replacement = formatPixelSize(ctx.rootFontPixelSize);
        }
        else if (name == "--background-color" || name == "--canvas")
        {
            replacement = ctx.glossaryBackgroundColor.isEmpty() ?
                fallback :
                ctx.glossaryBackgroundColor;
        }
        else
        {
            replacement = fallback;
        }

        if (replacement.isEmpty())
        {
            return "";
        }
        replacement = resolveCssVariables(replacement, ctx);
        if (replacement.isEmpty())
        {
            return "";
        }
        value.replace(start, end - start + 1, replacement);
    }

    return "";
}

QString Renderer::resolveCssCalculations(QString value) const
{
    /* Recursion guard for nested CSS calc() substitutions */
    constexpr int MAX_REPLACEMENTS = 16;

    /* Character count of the opening "calc(" token */
    constexpr qsizetype CSS_CALC_PREFIX_LENGTH = 5;

    for (int replacementCount = 0;
         replacementCount < MAX_REPLACEMENTS;
         ++replacementCount)
    {
        const qsizetype start = value.indexOf(
            "calc(",
            0,
            Qt::CaseInsensitive
        );
        if (start < 0)
        {
            return value;
        }

        qsizetype end = -1;
        int depth = 1;
        for (qsizetype i = start + CSS_CALC_PREFIX_LENGTH;
             i < value.size();
             ++i)
        {
            if (value[i] == '(')
            {
                ++depth;
            }
            else if (value[i] == ')' && --depth == 0)
            {
                end = i;
                break;
            }
        }
        if (end < 0)
        {
            return "";
        }

        const QString replacement = resolveCssCalc(
            value.sliced(start, end - start + 1)
        );
        if (replacement.isEmpty())
        {
            return "";
        }
        value.replace(start, end - start + 1, replacement);
    }

    return "";
}

QString Renderer::resolveColorMix(const QString &value) const
{
    /* Number of colors supported by the compatibility color-mix parser */
    constexpr qsizetype COLOR_COUNT = 2;

    /* Character count of the opening "color-mix(" token */
    constexpr qsizetype COLOR_MIX_PREFIX_LENGTH = 10;

    /* Character count of a CSS function's closing parenthesis */
    constexpr qsizetype CSS_FUNCTION_SUFFIX_LENGTH = 1;

    /* Equal weighting used when neither mixed color specifies a percentage */
    constexpr double DEFAULT_COLOR_PERCENTAGE = 50.0;

    /* Percentage total used to infer one omitted color weight */
    constexpr double TOTAL_COLOR_PERCENTAGE = 100.0;

    /* Sentinel identifying a color weight omitted from color-mix() */
    constexpr double UNSPECIFIED_PERCENTAGE = -1.0;

    if (!value.endsWith(')'))
    {
        return "";
    }

    QStringList arguments = splitCssArguments(
        value.sliced(
            COLOR_MIX_PREFIX_LENGTH,
            value.size() -
                COLOR_MIX_PREFIX_LENGTH -
                CSS_FUNCTION_SUFFIX_LENGTH
        )
    );
    if (!arguments.isEmpty() &&
        arguments.front().startsWith("in ", Qt::CaseInsensitive))
    {
        arguments.removeFirst();
    }
    if (arguments.size() != COLOR_COUNT)
    {
        return "";
    }

    std::array<QColor, COLOR_COUNT> colors;
    std::array<double, COLOR_COUNT> percentages = {
        UNSPECIFIED_PERCENTAGE,
        UNSPECIFIED_PERCENTAGE
    };
    /* Trailing percentage attached to an individual color-mix argument */
    static const QRegularExpression PERCENTAGE{
        R"(\s+([0-9]+(?:\.[0-9]+)?)%\s*$)"
    };

    for (qsizetype i = 0; i < COLOR_COUNT; ++i)
    {
        QString colorValue = arguments[i].trimmed();
        const QRegularExpressionMatch match = PERCENTAGE.match(colorValue);
        if (match.hasMatch())
        {
            percentages[i] = match.captured(1).toDouble();
            colorValue = colorValue.first(match.capturedStart()).trimmed();
        }
        colors[i] = QColor::fromString(colorValue);
        if (!colors[i].isValid())
        {
            return "";
        }
    }

    if (percentages[0] < 0.0 && percentages[1] < 0.0)
    {
        percentages[0] = DEFAULT_COLOR_PERCENTAGE;
        percentages[1] = DEFAULT_COLOR_PERCENTAGE;
    }
    else if (percentages[0] < 0.0)
    {
        percentages[0] = TOTAL_COLOR_PERCENTAGE - percentages[1];
    }
    else if (percentages[1] < 0.0)
    {
        percentages[1] = TOTAL_COLOR_PERCENTAGE - percentages[0];
    }

    const double total = percentages[0] + percentages[1];
    if (total <= 0.0)
    {
        return "";
    }
    const double firstWeight = percentages[0] / total;
    const double secondWeight = percentages[1] / total;
    const double alpha =
        colors[0].alphaF() * firstWeight +
        colors[1].alphaF() * secondWeight;
    if (alpha <= 0.0)
    {
        return QColor(Qt::transparent).name(QColor::HexArgb);
    }

    return QColor::fromRgbF(
        (
            colors[0].redF() * colors[0].alphaF() * firstWeight +
            colors[1].redF() * colors[1].alphaF() * secondWeight
        ) / alpha,
        (
            colors[0].greenF() * colors[0].alphaF() * firstWeight +
            colors[1].greenF() * colors[1].alphaF() * secondWeight
        ) / alpha,
        (
            colors[0].blueF() * colors[0].alphaF() * firstWeight +
            colors[1].blueF() * colors[1].alphaF() * secondWeight
        ) / alpha,
        alpha
    ).name(QColor::HexArgb);
}

QString Renderer::resolveCssCalc(const QString &value) const
{
    /* Simple dimension-by-number division supported inside calc() */
    static const QRegularExpression DIVISION{
        R"(^calc\(\s*([+-]?(?:\d+(?:\.\d*)?|\.\d+))([a-z%]+)\s*/\s*)"
        R"(([+-]?(?:\d+(?:\.\d*)?|\.\d+))\s*\)$)",
        QRegularExpression::CaseInsensitiveOption
    };

    const QRegularExpressionMatch match = DIVISION.match(value);
    if (!match.hasMatch())
    {
        return "";
    }

    const double denominator = match.captured(3).toDouble();
    if (denominator == 0.0)
    {
        return "";
    }
    return formatPixelSize(
        match.captured(1).toDouble() / denominator
    ) + match.captured(2);
}

QString Renderer::cssGradientFallback(const QString &value) const
{
    const qsizetype open = value.indexOf('(');
    if (open < 0 || !value.endsWith(')'))
    {
        return "";
    }

    const QStringList arguments = splitCssArguments(
        value.sliced(open + 1, value.size() - open - 2)
    );
    if (arguments.isEmpty())
    {
        return "";
    }

    QString colorValue = arguments.front().trimmed();
    /* Trailing percentage position attached to a gradient color stop */
    static const QRegularExpression COLOR_STOP{
        R"(\s+[0-9]+(?:\.[0-9]+)?%\s*$)"
    };
    const QRegularExpressionMatch match = COLOR_STOP.match(colorValue);
    if (match.hasMatch())
    {
        colorValue = colorValue.first(match.capturedStart()).trimmed();
    }

    const QColor color = QColor::fromString(colorValue);
    return color.isValid() ? color.name(QColor::HexArgb) : "";
}

QStringList Renderer::splitCssArguments(
    const QString &arguments) const
{
    QStringList result;
    QString current;
    bool inQuote = false;
    QChar quote;
    int parenDepth = 0;

    for (const QChar ch : arguments)
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
        else if (ch == '(')
        {
            ++parenDepth;
            current += ch;
        }
        else if (ch == ')')
        {
            --parenDepth;
            current += ch;
        }
        else if (ch == ',' && parenDepth == 0)
        {
            result.emplaceBack(current.trimmed());
            current.clear();
        }
        else
        {
            current += ch;
        }
    }
    result.emplaceBack(current.trimmed());
    return result;
}

void Renderer::addMatchingCssRules(
    Renderer::Context &ctx,
    Renderer::CssDeclarations &declarations,
    QString *beforeContent,
    QString *afterContent) const
{
    if (ctx.dictionaryStyles == nullptr || ctx.elements.isEmpty())
    {
        return;
    }

    const ParsedStylesheet &stylesheet =
        ctx.dictionaryStyles->parsedStylesheet();
    const StructuredElement &element =
        ctx.selectorElements[ctx.elements.back()];
    const QList<qsizetype> &candidateIndexes =
        ctx.dictionaryStyles->candidateRuleIndexes(
            element.tag,
            element.childIndex
        );
    for (const qsizetype index : candidateIndexes)
    {
        const CssRule &rule = stylesheet.rules[index];
        if (!cssRuleMatches(rule, ctx))
        {
            continue;
        }
        if (rule.pseudoElement != DictionaryStyles::CssPseudoElement::None)
        {
            QString *content =
                rule.pseudoElement ==
                    DictionaryStyles::CssPseudoElement::After ?
                    afterContent :
                    beforeContent;
            if (content != nullptr)
            {
                for (const CssDeclaration &declaration : rule.declarations)
                {
                    if (declaration.property == "content")
                    {
                        *content = declaration.value;
                    }
                }
            }
            continue;
        }

        for (const CssDeclaration &declaration : rule.declarations)
        {
            QString value = resolveCssValue(declaration.value, ctx);
            if (value.isEmpty())
            {
                continue;
            }
            if (declaration.property == "font-size")
            {
                const double pixelSize = cssFontSizeToPixels(
                    value,
                    ctx.screen,
                    ctx.parentFontPixelSize,
                    ctx.rootFontPixelSize
                );
                if (pixelSize >= 0.0)
                {
                    value = formatPixelSize(pixelSize) + "px";
                }
            }
            addCssDeclaration(declaration.property, value, declarations);
        }
    }
}

bool Renderer::cssRuleMatches(
    const CssRule &rule,
    const Renderer::Context &ctx) const
{
    if (rule.selector.isEmpty() || ctx.elements.isEmpty())
    {
        return false;
    }

    const qsizetype stackIndex = ctx.elements.size() - 1;
    return cssRuleMatchesAt(
        rule,
        ctx,
        rule.selector.size() - 1,
        stackIndex,
        ctx.elements[stackIndex]
    );
}

bool Renderer::cssRuleMatchesAt(
    const CssRule &rule,
    const Renderer::Context &ctx,
    qsizetype partIndex,
    qsizetype stackIndex,
    qsizetype selectorElementIndex) const
{
    const StructuredElement &element =
        ctx.selectorElements[selectorElementIndex];
    if (partIndex < 0 ||
        !cssSelectorPartMatches(rule.selector[partIndex], element))
    {
        return false;
    }
    if (partIndex == 0)
    {
        return true;
    }

    switch (rule.selector[partIndex].combinator)
    {
    case DictionaryStyles::CssCombinator::Child:
        if (stackIndex <= 0)
        {
            return false;
        }
        return cssRuleMatchesAt(
            rule,
            ctx,
            partIndex - 1,
            stackIndex - 1,
            ctx.elements[stackIndex - 1]
        );

    case DictionaryStyles::CssCombinator::AdjacentSibling:
        if (element.previousSibling < 0)
        {
            return false;
        }
        return cssRuleMatchesAt(
            rule,
            ctx,
            partIndex - 1,
            stackIndex,
            element.previousSibling
        );

    case DictionaryStyles::CssCombinator::GeneralSibling:
        for (qsizetype sibling = element.previousSibling;
             sibling >= 0;
             sibling = ctx.selectorElements[sibling].previousSibling)
        {
            if (cssRuleMatchesAt(
                    rule,
                    ctx,
                    partIndex - 1,
                    stackIndex,
                    sibling
                ))
            {
                return true;
            }
        }
        return false;

    case DictionaryStyles::CssCombinator::Descendant:
        for (qsizetype i = stackIndex - 1; i >= 0; --i)
        {
            if (cssRuleMatchesAt(
                    rule,
                    ctx,
                    partIndex - 1,
                    i,
                    ctx.elements[i]
                ))
            {
                return true;
            }
        }
        return false;
    }

    return false;
}

bool Renderer::cssSelectorPartMatches(
    const CssSelectorPart &part,
    const StructuredElement &element) const
{
    if (!part.tag.isEmpty() && part.tag != element.tag)
    {
        return false;
    }
    for (const QString &className : part.classNames)
    {
        if (!element.classes.contains(className))
        {
            return false;
        }
    }
    for (const DictionaryStyles::CssAttributeSelector &attribute :
         part.attributes)
    {
        const auto it = element.attributes.constFind(attribute.name);
        if (it == element.attributes.cend())
        {
            return false;
        }
        switch (attribute.op)
        {
        case DictionaryStyles::CssAttributeOperator::Exists:
            break;

        case DictionaryStyles::CssAttributeOperator::Equals:
            if (it.value() != attribute.value)
            {
                return false;
            }
            break;

        case DictionaryStyles::CssAttributeOperator::StartsWith:
            if (!it.value().startsWith(attribute.value))
            {
                return false;
            }
            break;
        }
    }
    if (!part.pseudoClasses.isEmpty() && element.childIndex <= 0)
    {
        return false;
    }
    for (const DictionaryStyles::CssPseudoClassSelector &pseudo :
         part.pseudoClasses)
    {
        bool matched = false;
        switch (pseudo.type)
        {
        case DictionaryStyles::CssPseudoClass::FirstChild:
            matched = element.childIndex == 1;
            break;

        case DictionaryStyles::CssPseudoClass::LastChild:
            if (element.childCount <= 0)
            {
                return false;
            }
            matched = element.childIndex == element.childCount;
            break;

        case DictionaryStyles::CssPseudoClass::NthChild:
            matched = element.childIndex == pseudo.childIndex;
            break;
        }

        if (pseudo.negated ? matched : !matched)
        {
            return false;
        }
    }
    return true;
}

qsizetype Renderer::structuredElement(
    const QJsonObject &obj,
    Renderer::Context &ctx,
    bool detailsOpen) const
{
    /* Structured-content keys recorded for CSS selector matching */
    constexpr const char *KEY_DATA = "data";
    constexpr const char *KEY_HREF = "href";
    constexpr const char *KEY_LANG = "lang";
    constexpr const char *KEY_TAG = "tag";
    constexpr const char *KEY_TITLE = "title";

    StructuredElement element;
    element.tag = obj[KEY_TAG].toString().toLower();
    element.classes.insert("gloss-sc-" + element.tag);
    if (element.tag == "details" && detailsOpen)
    {
        element.attributes["open"] = "";
    }
    if (element.tag == "img")
    {
        element.classes.insert("gloss-image");
        element.classes.insert("gloss-image-container");
        element.classes.insert("gloss-image-link");
    }

    if (obj[KEY_DATA].isObject())
    {
        const QJsonObject data = obj[KEY_DATA].toObject();
        for (const QString &key : data.keys())
        {
            if (data[key].isString())
            {
                element.attributes[structuredDataAttributeName(key)] =
                    data[key].toString();
            }
        }
    }

    /* Standard attributes copied into the selector-node attribute map */
    constexpr std::array<const char *, 3> STANDARD_ATTRIBUTES = {
        KEY_HREF,
        KEY_LANG,
        KEY_TITLE
    };
    for (const char *key : STANDARD_ATTRIBUTES)
    {
        if (obj[key].isString())
        {
            element.attributes[key] = obj[key].toString();
        }
    }

    if (!ctx.siblings.isEmpty())
    {
        SiblingState &siblings = ctx.siblings.back();
        element.previousSibling = siblings.previousElement;
        element.childIndex = ++siblings.visitedElementCount;
        element.childCount = siblings.elementCount;
    }

    const qsizetype index = ctx.selectorElements.size();
    ctx.selectorElements.emplaceBack(std::move(element));
    if (!ctx.siblings.isEmpty())
    {
        ctx.siblings.back().previousElement = index;
    }
    return index;
}

QString Renderer::structuredDataAttributeName(
    const QString &key) const
{
    QString out = "data-sc-";
    for (const QChar ch : key)
    {
        if (ch.isUpper())
        {
            out += '-';
            out += ch.toLower();
        }
        else
        {
            out += ch;
        }
    }
    return out;
}

bool Renderer::isSupportedStructuredTag(const QString &tag) const
{
    return m_supportedTags.contains(tag);
}

}
