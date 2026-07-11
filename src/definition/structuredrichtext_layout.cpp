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

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include <QGuiApplication>

namespace StructuredRichTextPrivate
{

/* Qt rich-text layout and compatibility-box rendering. */

namespace
{
/* Array index for the top side of a CSS box */
constexpr std::size_t TOP_SIDE = 0;

/* Array index for the right side of a CSS box */
constexpr std::size_t RIGHT_SIDE = 1;

/* Array index for the bottom side of a CSS box */
constexpr std::size_t BOTTOM_SIDE = 2;

/* Array index for the left side of a CSS box */
constexpr std::size_t LEFT_SIDE = 3;

/* CSS side names in top, right, bottom, left order */
constexpr std::array<const char *, BOX_SIDE_COUNT> SIDE_NAMES = {
    "top",
    "right",
    "bottom",
    "left"
};

/* Longhand padding properties in CSS box-side order */
constexpr std::array<const char *, BOX_SIDE_COUNT> PADDING_PROPERTIES = {
    "padding-top",
    "padding-right",
    "padding-bottom",
    "padding-left"
};

/* Longhand margin properties in CSS box-side order */
constexpr std::array<const char *, BOX_SIDE_COUNT> MARGIN_PROPERTIES = {
    "margin-top",
    "margin-right",
    "margin-bottom",
    "margin-left"
};

/**
 * @brief Components parsed from a CSS border shorthand.
 */
struct ParsedBorder
{
    /* Border width token */
    QString width;

    /* Border style token */
    QString style;

    /* Border color token */
    QString color;
};
/**
 * @brief Parse supported components from a CSS border shorthand.
 *
 * @param value The border shorthand value.
 * @return The discovered width, style, and color tokens.
 */
ParsedBorder parseBorderShorthand(const QString &value)
{
    /* CSS border styles recognized while classifying shorthand tokens */
    static const QSet<QString> BORDER_STYLES = {
        "none",
        "hidden",
        "dotted",
        "dashed",
        "solid",
        "double",
        "groove",
        "ridge",
        "inset",
        "outset"
    };

    ParsedBorder border;
    QStringList colorTokens;
    for (const QString &token : splitCssTokens(value))
    {
        const QString normalized = token.toLower();
        if (BORDER_STYLES.contains(normalized))
        {
            border.style = normalized;
        }
        else if (normalized == "thin" ||
                 normalized == "medium" ||
                 normalized == "thick" ||
                 normalized.front().isDigit() ||
                 normalized.front() == '.' ||
                 normalized.front() == '+' ||
                 normalized.front() == '-')
        {
            border.width = token;
        }
        else
        {
            colorTokens.emplaceBack(token);
        }
    }
    border.color = colorTokens.join(' ');
    return border;
}

/**
 * @brief Composite a foreground color over a background color.
 *
 * @param foreground The color painted over the background.
 * @param background The existing painted color.
 * @return The resulting premultiplied color, or an invalid color if clear.
 */
QColor compositeColor(const QColor &foreground, const QColor &background)
{
    const double foregroundAlpha = foreground.alphaF();
    const double backgroundAlpha = background.alphaF();
    const double outputAlpha =
        foregroundAlpha + backgroundAlpha * (1.0 - foregroundAlpha);
    if (outputAlpha <= 0.0)
    {
        return {};
    }

    return QColor::fromRgbF(
        (
            foreground.redF() * foregroundAlpha +
            background.redF() * backgroundAlpha *
                (1.0 - foregroundAlpha)
        ) / outputAlpha,
        (
            foreground.greenF() * foregroundAlpha +
            background.greenF() * backgroundAlpha *
                (1.0 - foregroundAlpha)
        ) / outputAlpha,
        (
            foreground.blueF() * foregroundAlpha +
            background.blueF() * backgroundAlpha *
                (1.0 - foregroundAlpha)
        ) / outputAlpha,
        outputAlpha
    );
}

/**
 * @brief Parse a CSS color supported by the compatibility renderer.
 *
 * QColor handles named and hexadecimal colors. This helper additionally
 * accepts comma-separated rgb() and rgba() values used by dictionaries.
 *
 * @param value The CSS color value.
 * @return The parsed color, or an invalid color when unsupported.
 */
QColor parseCssColor(const QString &value)
{
    QColor color = QColor::fromString(value.trimmed());
    if (color.isValid())
    {
        return color;
    }

    /* Pattern for comma-separated integer RGB channels and decimal alpha */
    static const QRegularExpression RGB_COLOR{
        R"(^rgba?\(\s*([+-]?\d+(?:\.\d+)?)\s*,\s*)"
        R"(([+-]?\d+(?:\.\d+)?)\s*,\s*)"
        R"(([+-]?\d+(?:\.\d+)?)\s*)"
        R"((?:,\s*([+-]?\d+(?:\.\d+)?)\s*)?\)$)",
        QRegularExpression::CaseInsensitiveOption
    };
    const QRegularExpressionMatch match =
        RGB_COLOR.match(value.trimmed());
    if (!match.hasMatch())
    {
        return {};
    }

    /* Maximum numeric value of an RGB color channel */
    constexpr double COLOR_CHANNEL_MAXIMUM = 255.0;
    const double red = std::clamp(
        match.captured(1).toDouble(),
        0.0,
        COLOR_CHANNEL_MAXIMUM
    );
    const double green = std::clamp(
        match.captured(2).toDouble(),
        0.0,
        COLOR_CHANNEL_MAXIMUM
    );
    const double blue = std::clamp(
        match.captured(3).toDouble(),
        0.0,
        COLOR_CHANNEL_MAXIMUM
    );
    const double alpha = match.captured(4).isEmpty() ?
        1.0 :
        std::clamp(match.captured(4).toDouble(), 0.0, 1.0);
    return QColor::fromRgbF(
        red / COLOR_CHANNEL_MAXIMUM,
        green / COLOR_CHANNEL_MAXIMUM,
        blue / COLOR_CHANNEL_MAXIMUM,
        alpha
    );
}

}

void Renderer::resolveElementLayout(
    Renderer::Context &ctx,
    Renderer::ElementRenderState &state) const
{
    const QString display =
        state.declarations.take("display").trimmed().toLower();
    const QString listType =
        state.declarations.value("list-style-type");
    state.listMarkerType = normalizeListMarker(listType.isEmpty() ?
        defaultListMarker(state.tag) :
        listType
    );
    const bool isList = state.tag == "ul" || state.tag == "ol";
    const bool isListItem = state.tag == "li" && !ctx.lists.isEmpty();
    const bool inlineListItem =
        isListItem && display.compare("inline", Qt::CaseInsensitive) == 0;
    const QString verticalAlign =
        state.declarations.value("vertical-align").trimmed();
    state.raisedText =
        verticalAlign.compare("super", Qt::CaseInsensitive) == 0 ||
        verticalAlign.compare("sub", Qt::CaseInsensitive) == 0;
    if (isListItem)
    {
        StructuredList &list = ctx.lists.back();
        ++list.item;
        state.listMarker = listMarker(list, listType);
    }

    const auto isBoxDeclaration = [] (const QString &property) -> bool
    {
        return property == "background-color" ||
            property.startsWith("border") ||
            property.startsWith("padding");
    };
    const bool styledBlock =
        (
            state.tag == "div" ||
            state.tag == "details" ||
            state.tag == "summary"
        ) &&
        std::any_of(
            state.declarations.keyBegin(),
            state.declarations.keyEnd(),
            isBoxDeclaration
        );
    state.paddedSpan =
        state.tag == "span" &&
        (
            ctx.selectorElements[ctx.elements.back()].attributes.value(
                "data-sc-class"
            ) == "tag" ||
            ctx.selectorElements[ctx.elements.back()].attributes.value(
                "data-sc-content"
            ) == "tag" ||
            ctx.selectorElements[ctx.elements.back()].attributes.value(
                "data-sc-content"
            ) == "forms-label"
        );

    if (isList)
    {
        state.layout = ElementLayout::List;
        state.outputTag = "div";
    }
    else if (inlineListItem)
    {
        state.layout = ElementLayout::Inline;
        state.outputTag = "span";
    }
    else if (isListItem && !state.listMarker.isEmpty())
    {
        state.layout = ElementLayout::MarkedListItem;
        state.outputTag = "table";
        state.widthPolicy = WidthPolicy::Fill;
    }
    else if (isListItem)
    {
        state.layout = ElementLayout::MarkerlessListItem;
        state.outputTag = "div";
    }
    else if (state.tag == "details" || styledBlock)
    {
        state.layout = ElementLayout::Box;
        state.outputTag = "table";
        state.widthPolicy = WidthPolicy::Fill;
    }
    else if (state.tag == "div" || state.tag == "summary")
    {
        state.layout = ElementLayout::Block;
        state.outputTag = "div";
    }

    if (isList || isListItem)
    {
        state.declarations.remove("list-style-type");
    }
    if (state.listMarkerType == "none")
    {
        state.listMarkerType.clear();
    }

    const QString width = state.declarations.value("width");
    if (state.layout == ElementLayout::Box &&
        (width.compare("fit-content", Qt::CaseInsensitive) == 0 ||
         width.compare("max-content", Qt::CaseInsensitive) == 0))
    {
        state.widthPolicy = WidthPolicy::FitContent;
        state.declarations.remove("width");
    }
    else if (state.layout == ElementLayout::Box && !width.isEmpty())
    {
        state.widthPolicy = WidthPolicy::Explicit;
    }
}

void Renderer::applyElementCompatibility(
    const Renderer::Context &ctx,
    Renderer::ElementRenderState &state) const
{
    if (state.tag == "span")
    {
        const QString left = state.declarations.take("margin-left");
        const QString right = state.declarations.take("margin-right");
        if (state.textDirection == Qt::RightToLeft)
        {
            state.inlineSpacingBefore = right;
            state.inlineSpacingAfter = left;
        }
        else
        {
            state.inlineSpacingBefore = left;
            state.inlineSpacingAfter = right;
        }
        if (ctx.forceInlineBox && ctx.suppressInlineBoxSpacing)
        {
            state.inlineSpacingBefore.clear();
            state.inlineSpacingAfter.clear();
            state.declarations.remove("vertical-align");
        }
    }

    if (state.layout == ElementLayout::Inline && !ctx.forceInlineBox)
    {
        state.marginFlow = MarginFlow::Inline;
        return;
    }

    if (state.layout == ElementLayout::Inline)
    {
        state.layout = ElementLayout::Box;
        state.outputTag = "table";
        state.widthPolicy = WidthPolicy::FitContent;
    }

    resolveBoxStyle(ctx, state);
    state.marginFlow = marginFlow(state);
}

void Renderer::resolveBoxStyle(
    const Renderer::Context &ctx,
    Renderer::ElementRenderState &state) const
{
    const bool hasPadding =
        state.declarations.contains("padding") ||
        std::any_of(
            PADDING_PROPERTIES.begin(),
            PADDING_PROPERTIES.end(),
            [&state] (const char *property)
            {
                return state.declarations.contains(property);
            }
        );
    if (hasPadding)
    {
        const QString paddingShorthand =
            state.declarations.take("padding");
        for (std::size_t side = 0; side < BOX_SIDE_COUNT; ++side)
        {
            QString value =
                state.declarations.take(PADDING_PROPERTIES[side]);
            if (value.isEmpty())
            {
                value = cssBoxSideValue(
                    paddingShorthand,
                    static_cast<qsizetype>(side)
                );
            }
            const double pixels = cssFontSizeToPixels(
                value,
                ctx.screen,
                state.fontPixelSize,
                ctx.rootFontPixelSize
            );
            if (pixels > MINIMUM_VISIBLE_PIXELS)
            {
                state.box.padding[side] = pixels;
            }
        }
    }

    for (std::size_t side = 0; side < BOX_SIDE_COUNT; ++side)
    {
        state.box.margins[side] =
            state.declarations.take(MARGIN_PROPERTIES[side]);
    }

    if (state.layout == ElementLayout::Block &&
        state.textOnlyContent)
    {
        QString &left = state.box.margins[LEFT_SIDE];
        QString &right = state.box.margins[RIGHT_SIDE];
        if (state.textDirection == Qt::RightToLeft)
        {
            state.contentInlineSpacingBefore = std::move(right);
            state.contentInlineSpacingAfter = std::move(left);
        }
        else
        {
            state.contentInlineSpacingBefore = std::move(left);
            state.contentInlineSpacingAfter = std::move(right);
        }
    }

    if (state.declarations.contains("background-color"))
    {
        const QString background =
            state.declarations.take("background-color");
        QColor resolvedBackground = parseCssColor(background);
        if (resolvedBackground.isValid() &&
            resolvedBackground.alpha() < OPAQUE_ALPHA)
        {
            const QColor parent =
                parseCssColor(ctx.paintedBackgroundColor);
            if (parent.isValid())
            {
                resolvedBackground =
                    compositeColor(resolvedBackground, parent);
            }
        }
        if (resolvedBackground.isValid())
        {
            state.box.backgroundColor = resolvedBackground.name(
                resolvedBackground.alpha() == OPAQUE_ALPHA ?
                    QColor::HexRgb :
                    QColor::HexArgb
            );
            state.paintedBackgroundColor = state.box.backgroundColor;
        }
        else
        {
            state.box.backgroundColor = background;
        }
    }

    const bool hasBorder = std::any_of(
        state.declarations.keyBegin(),
        state.declarations.keyEnd(),
        [] (const QString &property)
        {
            return property == "border" ||
                property.startsWith("border-");
        }
    );
    if (!hasBorder)
    {
        state.declarations.remove("border-radius");
        state.box.enabled =
            state.layout == ElementLayout::Box ||
            !state.box.backgroundColor.isEmpty() ||
            hasPadding;
        const bool hasHorizontalMargins =
            !isZeroSpacing(state.box.margins[LEFT_SIDE]) ||
            !isZeroSpacing(state.box.margins[RIGHT_SIDE]);
        if (state.layout == ElementLayout::Block &&
            !state.textOnlyContent &&
            hasHorizontalMargins)
        {
            state.layout = ElementLayout::Box;
            state.outputTag = "table";
            state.widthPolicy = WidthPolicy::Fill;
            state.box.enabled = true;
        }
        return;
    }

    const ParsedBorder commonBorder =
        parseBorderShorthand(state.declarations.take("border"));
    const QString borderWidths =
        state.declarations.take("border-width");
    const QString borderStyles =
        state.declarations.take("border-style");
    const QString borderColors =
        state.declarations.take("border-color");

    std::size_t paintedBorderCount = 0;
    QString sharedBorderColor;
    bool sharedBorderColorMatches = true;
    for (std::size_t side = 0; side < BOX_SIDE_COUNT; ++side)
    {
        ParsedBorder border = commonBorder;
        const QString sideName = SIDE_NAMES[side];
        const QString boxWidth = cssBoxSideValue(
            borderWidths,
            static_cast<qsizetype>(side)
        );
        const QString boxStyle = cssBoxSideValue(
            borderStyles,
            static_cast<qsizetype>(side)
        );
        const QString boxColor = cssBoxSideValue(
            borderColors,
            static_cast<qsizetype>(side)
        );
        if (!boxWidth.isEmpty())
        {
            border.width = boxWidth;
        }
        if (!boxStyle.isEmpty())
        {
            border.style = boxStyle;
        }
        if (!boxColor.isEmpty())
        {
            border.color = boxColor;
        }

        const ParsedBorder sideBorder = parseBorderShorthand(
            state.declarations.take("border-" + sideName)
        );
        if (!sideBorder.width.isEmpty())
        {
            border.width = sideBorder.width;
        }
        if (!sideBorder.style.isEmpty())
        {
            border.style = sideBorder.style;
        }
        if (!sideBorder.color.isEmpty())
        {
            border.color = sideBorder.color;
        }

        const QString widthProperty = "border-" + sideName + "-width";
        const QString styleProperty = "border-" + sideName + "-style";
        const QString colorProperty = "border-" + sideName + "-color";
        if (state.declarations.contains(widthProperty))
        {
            border.width = state.declarations.take(widthProperty);
        }
        if (state.declarations.contains(styleProperty))
        {
            border.style = state.declarations.take(styleProperty);
        }
        if (state.declarations.contains(colorProperty))
        {
            border.color = state.declarations.take(colorProperty);
        }

        if (border.width.isEmpty() &&
            border.style.compare("solid", Qt::CaseInsensitive) == 0)
        {
            border.width =
                formatPixelSize(DEFAULT_BORDER_WIDTH_PIXELS) + "px";
        }
        double widthPixels = cssFontSizeToPixels(
            border.width,
            ctx.screen,
            state.fontPixelSize,
            ctx.rootFontPixelSize
        );
        if (border.width.compare("thin", Qt::CaseInsensitive) == 0)
        {
            widthPixels = THIN_BORDER_WIDTH_PIXELS;
        }
        else if (border.width.compare("medium", Qt::CaseInsensitive) == 0)
        {
            widthPixels = DEFAULT_BORDER_WIDTH_PIXELS;
        }
        else if (border.width.compare("thick", Qt::CaseInsensitive) == 0)
        {
            widthPixels = THICK_BORDER_WIDTH_PIXELS;
        }

        if (border.color.isEmpty() ||
            border.color.compare("currentcolor", Qt::CaseInsensitive) == 0)
        {
            border.color = state.textColor;
        }

        BorderSide &resolved = state.box.borders[side];
        resolved.color = border.color;
        resolved.style = border.style;
        resolved.widthPixels = std::max(0.0, widthPixels);
        const QColor color = parseCssColor(resolved.color);
        resolved.painted =
            resolved.style.compare("solid", Qt::CaseInsensitive) == 0 &&
            resolved.widthPixels > MINIMUM_VISIBLE_PIXELS &&
            color.isValid();
        if (resolved.painted)
        {
            ++paintedBorderCount;
            if (sharedBorderColor.isEmpty())
            {
                sharedBorderColor = resolved.color;
            }
            else if (sharedBorderColor != resolved.color)
            {
                sharedBorderColorMatches = false;
            }
        }
        else if (!resolved.style.isEmpty() &&
                 resolved.style.compare("none", Qt::CaseInsensitive) != 0 &&
                 resolved.widthPixels > MINIMUM_VISIBLE_PIXELS)
        {
            state.cellDeclarations["border-" + sideName] =
                formatPixelSize(resolved.widthPixels) + "px " +
                resolved.style + ' ' + resolved.color;
        }
    }
    state.declarations.remove("border-radius");

    state.box.compactBorderFrame =
        paintedBorderCount > 0 && sharedBorderColorMatches;
    state.box.enabled =
        state.layout == ElementLayout::Box ||
        paintedBorderCount > 0 ||
        !state.box.backgroundColor.isEmpty() ||
        std::any_of(
            state.box.padding.begin(),
            state.box.padding.end(),
            [] (double value)
            {
                return value > MINIMUM_VISIBLE_PIXELS;
            }
        );

    const bool hasHorizontalMargins =
        !isZeroSpacing(state.box.margins[LEFT_SIDE]) ||
        !isZeroSpacing(state.box.margins[RIGHT_SIDE]);
    if (state.layout == ElementLayout::Block &&
        !state.textOnlyContent &&
        hasHorizontalMargins)
    {
        state.layout = ElementLayout::Box;
        state.outputTag = "table";
        state.widthPolicy = WidthPolicy::Fill;
        state.box.enabled = true;
    }
}

Renderer::MarginFlow Renderer::marginFlow(
    const Renderer::ElementRenderState &state) const
{
    if (state.layout == ElementLayout::Inline)
    {
        return MarginFlow::Inline;
    }

    const bool hasVerticalPadding =
        state.box.padding[TOP_SIDE] > MINIMUM_VISIBLE_PIXELS ||
        state.box.padding[BOTTOM_SIDE] > MINIMUM_VISIBLE_PIXELS;
    const bool hasPaintedBorder = std::any_of(
        state.box.borders.begin(),
        state.box.borders.end(),
        [] (const BorderSide &border)
        {
            return border.painted;
        }
    );
    if (state.box.enabled &&
        (!state.box.backgroundColor.isEmpty() ||
         hasVerticalPadding ||
         hasPaintedBorder))
    {
        return MarginFlow::Contained;
    }
    if (state.layout == ElementLayout::MarkedListItem)
    {
        return MarginFlow::PropagateLastChild;
    }
    return MarginFlow::Collapsible;
}

void Renderer::addStructuredElementStart(
    const Renderer::ElementRenderState &state,
    const Renderer::Context &ctx,
    QString &out) const
{
    addInlineSpacer(
        state.inlineSpacingBefore,
        state.fontPixelSize,
        ctx,
        out
    );

    if (state.layout == ElementLayout::Box)
    {
        addBoxStart(state, ctx, out);
    }
    else
    {
        out += '<';
        out += state.outputTag;
        if (state.layout == ElementLayout::MarkedListItem)
        {
            out += " cellspacing=\"0\""
                " width=\"100%\"";
        }
        out += state.attributes;
        if (!state.declarations.isEmpty())
        {
            out += " style=\"";
            addCssDeclarations(state.declarations, out);
            out += '"';
        }
        out += '>';
    }

    if (state.layout == ElementLayout::MarkerlessListItem)
    {
        addVerticalPixelSpacer(state.box.padding[TOP_SIDE], out);
        addSelectionSentinel(LIST_ITEM_SENTINEL, out);
    }

    if (state.layout == ElementLayout::MarkedListItem)
    {
        /* Marker-to-content gap relative to the item's font size */
        constexpr double RELATIVE_INDENT_FACTOR = 0.35;
        const char *paddingProperty =
            state.textDirection == Qt::RightToLeft ?
                "padding-left" :
                "padding-right";

        out += "<tr><td style=\"vertical-align: top; white-space: nowrap; ";
        out += paddingProperty;
        out += ": ";
        out += formatPixelSize(
            state.fontPixelSize * RELATIVE_INDENT_FACTOR
        );
        out += "px;\">";
        addSelectionSentinel(LIST_ITEM_SENTINEL, out);
        addSelectionSentinel(LIST_MARKER_START_SENTINEL, out);
        out += escapeHtml(state.listMarker);
        addSelectionSentinel(LIST_MARKER_END_SENTINEL, out);
        out += "</td><td width=\"100%\" style=\"vertical-align: top;\">";
    }

    /* Keep disclosure controls inside the summary's rendered content cell. */
    if (state.disclosureSummary)
    {
        addAnchorStart(
            detailsLinkHref(state.details),
            state.textColor,
            false,
            out
        );
        addSelectionSentinel(DETAILS_CONTROL_START_SENTINEL, out);
        out += QChar(
            state.details.open ?
                OPEN_DETAILS_CHEVRON :
                CLOSED_DETAILS_CHEVRON
        );
        out += ' ';
        addSelectionSentinel(DETAILS_CONTROL_END_SENTINEL, out);
    }

    if (state.paddedSpan)
    {
        out += "&nbsp;";
    }

    addInlineSpacer(
        state.contentInlineSpacingBefore,
        state.fontPixelSize,
        ctx,
        out
    );

    if (!state.beforeContent.isEmpty())
    {
        out += escapeHtml(state.beforeContent);
    }
}

void Renderer::addStructuredElementEnd(
    const Renderer::ElementRenderState &state,
    const Renderer::Context &ctx,
    QString &out) const
{
    if (!state.afterContent.isEmpty())
    {
        out += escapeHtml(state.afterContent);
    }
    if (state.paddedSpan)
    {
        out += "&nbsp;";
    }
    addInlineSpacer(
        state.contentInlineSpacingAfter,
        state.fontPixelSize,
        ctx,
        out
    );
    if (state.layout == ElementLayout::MarkerlessListItem)
    {
        addVerticalPixelSpacer(state.box.padding[BOTTOM_SIDE], out);
    }
    if (state.layout == ElementLayout::Box)
    {
        if (state.disclosureSummary)
        {
            out += "</a>";
        }
        addBoxEnd(state, ctx, out);
    }
    else
    {
        if (state.disclosureSummary)
        {
            out += "</a>";
        }
        if (state.layout == ElementLayout::MarkedListItem)
        {
            out += "</td></tr>";
        }

        out += "</";
        out += state.outputTag;
        out += '>';
    }

    addInlineSpacer(
        state.inlineSpacingAfter,
        state.fontPixelSize,
        ctx,
        out
    );

    if (state.paddedSpan)
    {
        out += ' ';
    }
}

void Renderer::addSelectionSentinel(
    char16_t sentinel, QString &out) const
{
    out += QChar(sentinel);
}

void Renderer::addBoxStart(
    const Renderer::ElementRenderState &state,
    const Renderer::Context &ctx,
    QString &out) const
{
    const auto marginPixels =
        [&state, &ctx, this] (std::size_t side) -> double
    {
        return std::max(
            0.0,
            cssFontSizeToPixels(
                state.box.margins[side],
                ctx.screen,
                state.fontPixelSize,
                ctx.rootFontPixelSize
            )
        );
    };
    const double leftMargin = marginPixels(LEFT_SIDE);
    const double rightMargin = marginPixels(RIGHT_SIDE);
    const bool hasMarginGrid =
        leftMargin > MINIMUM_VISIBLE_PIXELS ||
        rightMargin > MINIMUM_VISIBLE_PIXELS;

    if (hasMarginGrid)
    {
        out += "<table cellspacing=\"0\""
            " width=\"100%\"><tr>";
        if (leftMargin > MINIMUM_VISIBLE_PIXELS)
        {
            out += "<td width=\"";
            out += formatPixelSize(leftMargin);
            out += "\"></td>";
        }
        out += "<td width=\"100%\" style=\"vertical-align: top;\">";
    }
    if (!state.box.enabled)
    {
        return;
    }

    const auto addTableStart =
        [&state, &out, this] (
            const QString &background,
            bool outerTable)
    {
        out += "<table cellspacing=\"0\"";
        if (state.widthPolicy == WidthPolicy::Fill)
        {
            out += " width=\"100%\"";
        }
        if (!background.isEmpty())
        {
            out += " bgcolor=\"";
            out += background;
            out += '"';
        }
        if (outerTable)
        {
            out += state.attributes;
            if (!state.declarations.isEmpty())
            {
                out += " style=\"";
                addCssDeclarations(state.declarations, out);
                out += '"';
            }
        }
        out += "><tr><td style=\"vertical-align: top;";
    };

    const auto addBorderPadding =
        [&state, &out, this] (std::size_t side)
    {
        const BorderSide &border = state.box.borders[side];
        if (!border.painted)
        {
            return;
        }
        out += "padding-";
        out += SIDE_NAMES[side];
        out += ": ";
        out += formatPixelSize(border.widthPixels);
        out += "px;";
    };

    const auto closeOpeningCell = [&out] ()
    {
        out += "\">";
    };

    const auto firstPaintedBorder =
        std::find_if(
            state.box.borders.begin(),
            state.box.borders.end(),
            [] (const BorderSide &border)
            {
                return border.painted;
            }
        );
    const bool hasPaintedBorder =
        firstPaintedBorder != state.box.borders.end();
    const QString contentBackground =
        state.paintedBackgroundColor;

    bool outerTable = true;
    if (hasPaintedBorder && state.box.compactBorderFrame)
    {
        addTableStart(firstPaintedBorder->color, outerTable);
        for (std::size_t side = 0; side < BOX_SIDE_COUNT; ++side)
        {
            addBorderPadding(side);
        }
        closeOpeningCell();
        outerTable = false;
    }
    else if (hasPaintedBorder)
    {
        /* Stable nesting order for independently colored border layers */
        constexpr std::array<std::size_t, BOX_SIDE_COUNT> BORDER_LAYER_ORDER = {
            TOP_SIDE,
            RIGHT_SIDE,
            BOTTOM_SIDE,
            LEFT_SIDE
        };
        for (const std::size_t side : BORDER_LAYER_ORDER)
        {
            const BorderSide &border = state.box.borders[side];
            if (!border.painted)
            {
                continue;
            }
            addTableStart(border.color, outerTable);
            addBorderPadding(side);
            closeOpeningCell();
            outerTable = false;
        }
    }

    addTableStart(contentBackground, outerTable);
    /* Cell-padding emission order used by the Qt HTML table workaround */
    constexpr std::array<std::size_t, BOX_SIDE_COUNT> PADDING_ORDER = {
        BOTTOM_SIDE,
        LEFT_SIDE,
        RIGHT_SIDE,
        TOP_SIDE
    };
    for (const std::size_t side : PADDING_ORDER)
    {
        if (state.box.padding[side] <= MINIMUM_VISIBLE_PIXELS)
        {
            continue;
        }
        out += "padding-";
        out += SIDE_NAMES[side];
        out += ": ";
        out += formatPixelSize(state.box.padding[side]);
        out += "px;";
    }
    addCssDeclarations(state.cellDeclarations, out);
    closeOpeningCell();
}

void Renderer::addBoxEnd(
    const Renderer::ElementRenderState &state,
    const Renderer::Context &ctx,
    QString &out) const
{
    if (state.box.enabled)
    {
        out += "</td></tr></table>";
        const std::size_t paintedBorderCount =
            static_cast<std::size_t>(std::count_if(
                state.box.borders.begin(),
                state.box.borders.end(),
                [] (const BorderSide &border)
                {
                    return border.painted;
                }
            ));
        const std::size_t borderLayerCount =
            state.box.compactBorderFrame && paintedBorderCount > 0 ?
                1 :
                paintedBorderCount;
        for (std::size_t layer = 0;
             layer < borderLayerCount;
             ++layer)
        {
            out += "</td></tr></table>";
        }
    }

    const auto marginPixels =
    [&state, &ctx, this] (std::size_t side) -> double
    {
        return std::max(
            0.0,
            cssFontSizeToPixels(
                state.box.margins[side],
                ctx.screen,
                state.fontPixelSize,
                ctx.rootFontPixelSize
            )
        );
    };
    const double leftMargin = marginPixels(LEFT_SIDE);
    const double rightMargin = marginPixels(RIGHT_SIDE);
    if (leftMargin > MINIMUM_VISIBLE_PIXELS ||
        rightMargin > MINIMUM_VISIBLE_PIXELS)
    {
        out += "</td>";
        if (rightMargin > MINIMUM_VISIBLE_PIXELS)
        {
            out += "<td width=\"";
            out += formatPixelSize(rightMargin);
            out += "\"></td>";
        }
        out += "</tr></table>";
    }
}

void Renderer::addInlineSpacer(
    const QString &spacing,
    double fontPixelSize,
    const Renderer::Context &ctx,
    QString &out) const
{
    const double pixelSize = cssFontSizeToPixels(
        spacing,
        ctx.screen,
        fontPixelSize,
        ctx.rootFontPixelSize
    );
    if (pixelSize <= 0.0)
    {
        return;
    }

    out += "<span style=\"font-size: 1px;letter-spacing: ";
    out += formatPixelSize(pixelSize);
    out += "px;\">&nbsp;</span>";
}

void Renderer::addVerticalSpacer(
    const QString &spacing,
    double fontPixelSize,
    const Renderer::Context &ctx,
    QString &out) const
{
    const QString value = spacing.trimmed();
    if (isZeroSpacing(value))
    {
        return;
    }

    const double pixelSize = cssFontSizeToPixels(
        value,
        ctx.screen,
        fontPixelSize,
        ctx.rootFontPixelSize
    );
    if (pixelSize < 0.0)
    {
        return;
    }

    addVerticalPixelSpacer(pixelSize, out);
}

void Renderer::addVerticalPixelSpacer(
    double pixelSize, QString &out) const
{
    /* Threshold below which a spacer is omitted from generated HTML */
    constexpr double MINIMUM_VISIBLE_SPACING_PIXELS = 0.001;

    /* Line box height used to represent an arbitrary vertical spacer */
    constexpr double SPACER_LINE_HEIGHT_PIXELS = 1.0;

    if (std::abs(pixelSize) < MINIMUM_VISIBLE_SPACING_PIXELS)
    {
        return;
    }

    out += "<div style=\"font-size: ";
    out += formatPixelSize(SPACER_LINE_HEIGHT_PIXELS);
    out += "px;line-height: ";
    out += formatPixelSize(SPACER_LINE_HEIGHT_PIXELS);
    out += "px;margin-top: ";
    out += formatPixelSize(pixelSize - SPACER_LINE_HEIGHT_PIXELS);
    out += "px;\">&nbsp;</div>";
}

void Renderer::addPendingVerticalMargin(
    const QString &spacing,
    double fontPixelSize,
    const Renderer::Context &ctx,
    Renderer::SiblingState &siblings) const
{
    /* CSS length syntax accepted by the margin-collapsing compatibility path */
    static const QRegularExpression CSS_LENGTH{
        R"(^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:px|%|em|rem|ex|rex|ch|rch|)"
        R"(cap|rcap|ic|ric|lh|rlh|vw|svw|lvw|dvw|vi|svi|lvi|dvi|vh|)"
        R"(svh|lvh|dvh|vb|svb|lvb|dvb|vmin|svmin|lvmin|dvmin|vmax|)"
        R"(svmax|lvmax|dvmax|pt|pc|in|cm|mm|q)$)",
        QRegularExpression::CaseInsensitiveOption
    };

    const QString value = spacing.trimmed();
    if (isZeroSpacing(value) || !CSS_LENGTH.match(value).hasMatch())
    {
        return;
    }

    const double pixelSize = cssFontSizeToPixels(
        value,
        ctx.screen,
        fontPixelSize,
        ctx.rootFontPixelSize
    );
    if (pixelSize > 0.0)
    {
        siblings.pendingPositiveMarginPixels = std::max(
            siblings.pendingPositiveMarginPixels,
            pixelSize
        );
    }
    else if (pixelSize < 0.0)
    {
        siblings.pendingNegativeMarginPixels = std::min(
            siblings.pendingNegativeMarginPixels,
            pixelSize
        );
    }
}

void Renderer::mergePendingVerticalMargins(
    const Renderer::SiblingState &source,
    Renderer::SiblingState &destination) const
{
    destination.pendingPositiveMarginPixels = std::max(
        destination.pendingPositiveMarginPixels,
        source.pendingPositiveMarginPixels
    );
    destination.pendingNegativeMarginPixels = std::min(
        destination.pendingNegativeMarginPixels,
        source.pendingNegativeMarginPixels
    );
}

void Renderer::flushPendingVerticalMargin(
    Renderer::Context &ctx,
    QString &out) const
{
    if (ctx.siblings.isEmpty())
    {
        return;
    }

    SiblingState &siblings = ctx.siblings.back();
    const double margin =
        siblings.pendingPositiveMarginPixels +
        siblings.pendingNegativeMarginPixels;
    clearPendingVerticalMargins(siblings);
    addVerticalPixelSpacer(margin, out);
}

void Renderer::clearPendingVerticalMargins(
    Renderer::SiblingState &siblings) const
{
    siblings.pendingPositiveMarginPixels = 0.0;
    siblings.pendingNegativeMarginPixels = 0.0;
}

QString Renderer::cssBoxSideValue(
    const QString &value, qsizetype side) const
{
    /* Side count and indexes used to expand CSS box shorthands */
    constexpr qsizetype BOX_SIDE_COUNT = 4;
    constexpr qsizetype TOP_SIDE = 0;
    constexpr qsizetype RIGHT_SIDE = 1;
    constexpr qsizetype BOTTOM_SIDE = 2;

    if (side < 0 || side >= BOX_SIDE_COUNT)
    {
        return "";
    }

    QStringList values;
    QString current;
    int parenthesisDepth = 0;
    for (const QChar ch : value.trimmed())
    {
        if (ch == '(')
        {
            ++parenthesisDepth;
        }
        else if (ch == ')')
        {
            --parenthesisDepth;
        }

        if (ch.isSpace() && parenthesisDepth == 0)
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

    switch (values.size())
    {
    case 1:
        return values[0];

    case 2:
        return side == TOP_SIDE || side == BOTTOM_SIDE ?
            values[0] :
            values[1];

    case 3:
        if (side == TOP_SIDE)
        {
            return values[0];
        }
        if (side == RIGHT_SIDE)
        {
            return values[1];
        }
        return side == BOTTOM_SIDE ? values[2] : values[1];

    case BOX_SIDE_COUNT:
        return values[side];

    default:
        return "";
    }
}

bool Renderer::isZeroSpacing(const QString &spacing) const
{
    /* CSS numeric zero with an optional unit */
    static const QRegularExpression ZERO_SPACING{
        R"(^[+-]?(?:0+(?:\.0*)?|\.0+)(?:[a-z%]+)?$)",
        QRegularExpression::CaseInsensitiveOption
    };

    const QString value = spacing.trimmed();
    return value.isEmpty() || ZERO_SPACING.match(value).hasMatch();
}

QString Renderer::normalizeListMarker(QString marker) const
{
    marker = marker.trimmed();
    marker.replace('"', '\'');
    if (marker.size() >= 2 &&
        ((marker.front() == '\'' && marker.back() == '\'') ||
         (marker.front() == '"' && marker.back() == '"')))
    {
        marker = marker.sliced(1, marker.size() - 2);
    }
    return marker;
}

QString Renderer::defaultListMarker(const QString &tag) const
{
    if (tag == "ol")
    {
        return "1";
    }
    if (tag == "ul")
    {
        return "disc";
    }
    return "";
}

QString Renderer::listMarker(
    const StructuredList &list, const QString &marker) const
{
    QString normalized = normalizeListMarker(marker);
    if (normalized.isEmpty())
    {
        normalized = list.marker;
    }
    if (normalized == "none")
    {
        return "";
    }
    if (!isBuiltInListMarker(normalized))
    {
        return normalized;
    }
    if (normalized == "disc")
    {
        return QString{QChar(0x2022)};
    }
    if (normalized == "circle")
    {
        return QString{QChar(0x25E6)};
    }
    if (normalized == "square")
    {
        return QString{QChar(0x25AA)};
    }
    if (normalized == "1")
    {
        return QString::number(list.item) + '.';
    }
    if (normalized == "a" || normalized == "A")
    {
        /* Number of symbols in the Latin alphabetic list-marker sequence */
        constexpr int LETTERS_IN_ALPHABET = 26;

        QString alpha;
        qsizetype value = list.item;
        while (value > 0)
        {
            --value;
            const QChar ch{
                'a' + static_cast<char>(value % LETTERS_IN_ALPHABET)
            };
            alpha.prepend(normalized == "A" ? ch.toUpper() : ch);
            value /= LETTERS_IN_ALPHABET;
        }
        return alpha + '.';
    }
    return list.marker;
}

bool Renderer::isBuiltInListMarker(const QString &marker) const
{
    const QString normalized = normalizeListMarker(marker);
    return normalized.isEmpty() ||
        normalized == "disc" ||
        normalized == "circle" ||
        normalized == "square" ||
        normalized == "1" ||
        normalized == "a" ||
        normalized == "A";
}

double Renderer::cssFontSizeToPixels(
    const QString &size,
    const QScreen *screen,
    const QFont &font) const
{
    const double fontSize = fontPixelSize(font, screen);
    return cssFontSizeToPixels(size, screen, fontSize, fontSize);
}


double Renderer::cssFontSizeToPixels(
    const QString &size,
    const QScreen *screen,
    double parentFontPixelSize,
    double rootFontPixelSize) const
{
    const QString normalized = size.trimmed().toLower();
    if (normalized.isEmpty())
    {
        return -1.0;
    }

    if (normalized == "inherit" || normalized == "unset" ||
        normalized == "revert" || normalized == "revert-layer")
    {
        return parentFontPixelSize;
    }
    if (normalized == "initial" || normalized == "medium")
    {
        return rootFontPixelSize;
    }
    if (normalized == "xx-small")
    {
        return rootFontPixelSize * 3.0 / 5.0;
    }
    if (normalized == "x-small")
    {
        return rootFontPixelSize * 3.0 / 4.0;
    }
    if (normalized == "small")
    {
        return rootFontPixelSize * 8.0 / 9.0;
    }
    if (normalized == "large")
    {
        return rootFontPixelSize * 6.0 / 5.0;
    }
    if (normalized == "x-large")
    {
        return rootFontPixelSize * 3.0 / 2.0;
    }
    if (normalized == "xx-large")
    {
        return rootFontPixelSize * 2.0;
    }
    if (normalized == "xxx-large")
    {
        return rootFontPixelSize * 3.0;
    }
    if (normalized == "smaller")
    {
        return parentFontPixelSize / 1.2;
    }
    if (normalized == "larger")
    {
        return parentFontPixelSize * 1.2;
    }

    qsizetype index = 0;
    bool ok = false;
    normalized.toDouble(&ok);
    if (ok)
    {
        index = normalized.size();
    }
    else
    {
        while (index < normalized.size())
        {
            const QChar ch = normalized[index];
            if (!ch.isDigit() && ch != '-' && ch != '+' && ch != '.')
            {
                break;
            }
            ++index;
        }
        if (index == 0)
        {
            return -1.0;
        }
    }

    const QString number = normalized.first(index);
    ok = false;
    const double numericValue = number.toDouble(&ok);
    if (!ok || !std::isfinite(numericValue))
    {
        return -1.0;
    }

    const QString unit = normalized.sliced(index).trimmed();
    if (unit.isEmpty())
    {
        return numericValue == 0.0 ? 0.0 : -1.0;
    }
    if (unit == "px")
    {
        return numericValue;
    }
    if (unit == "%")
    {
        return parentFontPixelSize * numericValue / 100.0;
    }
    if (unit == "em")
    {
        return parentFontPixelSize * numericValue;
    }
    if (unit == "rem")
    {
        return rootFontPixelSize * numericValue;
    }
    if (unit == "ex")
    {
        return parentFontPixelSize * numericValue / 2.0;
    }
    if (unit == "rex")
    {
        return rootFontPixelSize * numericValue / 2.0;
    }
    if (unit == "ch")
    {
        return parentFontPixelSize * numericValue / 2.0;
    }
    if (unit == "rch")
    {
        return rootFontPixelSize * numericValue / 2.0;
    }
    if (unit == "cap")
    {
        return parentFontPixelSize * numericValue * 0.7;
    }
    if (unit == "rcap")
    {
        return rootFontPixelSize * numericValue * 0.7;
    }
    if (unit == "ic")
    {
        return parentFontPixelSize * numericValue;
    }
    if (unit == "ric")
    {
        return rootFontPixelSize * numericValue;
    }
    if (unit == "lh")
    {
        return parentFontPixelSize * numericValue * 1.2;
    }
    if (unit == "rlh")
    {
        return rootFontPixelSize * numericValue * 1.2;
    }

    if (screen != nullptr)
    {
        const QSize screenSize = screen->availableGeometry().size();
        if (unit == "vw" || unit == "svw" || unit == "lvw" || unit == "dvw" ||
            unit == "vi" || unit == "svi" || unit == "lvi" || unit == "dvi")
        {
            return screenSize.width() * numericValue / 100.0;
        }
        if (unit == "vh" || unit == "svh" || unit == "lvh" || unit == "dvh" ||
            unit == "vb" || unit == "svb" || unit == "lvb" || unit == "dvb")
        {
            return screenSize.height() * numericValue / 100.0;
        }
        if (unit == "vmin" || unit == "svmin" || unit == "lvmin" ||
            unit == "dvmin")
        {
            return std::min(screenSize.width(), screenSize.height()) *
                numericValue / 100.0;
        }
        if (unit == "vmax" || unit == "svmax" || unit == "lvmax" ||
            unit == "dvmax")
        {
            return std::max(screenSize.width(), screenSize.height()) *
                numericValue / 100.0;
        }
    }

    const double dpi = screenDpi(screen);
    if (unit == "pt")
    {
        return numericValue * dpi / 72.0;
    }
    if (unit == "pc")
    {
        return numericValue * dpi / 6.0;
    }
    if (unit == "in")
    {
        return numericValue * dpi;
    }
    if (unit == "cm")
    {
        return numericValue * dpi / 2.54;
    }
    if (unit == "mm")
    {
        return numericValue * dpi / 25.4;
    }
    if (unit == "q")
    {
        return numericValue * dpi / 101.6;
    }

    return -1.0;
}

double Renderer::fontPixelSize(
    const QFont &font, const QScreen *screen) const
{
    /* Typographic points per physical inch */
    constexpr double POINTS_IN_INCH = 72.0;

    /* Browser-compatible font size used when the QFont has no valid size */
    constexpr double DEFAULT_PIXEL_SIZE = 16.0;

    if (font.pixelSize() > 0)
    {
        return font.pixelSize();
    }
    if (font.pointSizeF() > 0)
    {
        return font.pointSizeF() * screenDpi(screen) / POINTS_IN_INCH;
    }
    return DEFAULT_PIXEL_SIZE;
}

double Renderer::screenDpi(const QScreen *screen) const
{
    /* CSS reference pixel density used when no screen DPI is available */
    constexpr double DEFAULT_SCREEN_DPI = 96.0;

    if (screen == nullptr)
    {
        screen = QGuiApplication::primaryScreen();
        if (screen == nullptr)
        {
            return DEFAULT_SCREEN_DPI;
        }
    }
    const double dpi = screen->logicalDotsPerInch();
    return dpi > 0.0 ? dpi : DEFAULT_SCREEN_DPI;
}

QString Renderer::formatPixelSize(double size) const
{
    QString formatted = QString::number(size, 'f', 3);
    while (formatted.endsWith('0'))
    {
        formatted.chop(1);
    }
    if (formatted.endsWith('.'))
    {
        formatted.chop(1);
    }
    return formatted;
}

}
