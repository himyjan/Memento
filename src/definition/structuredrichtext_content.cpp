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
#include <utility>

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonObject>
#include <QLocale>
#include <QQuickWindow>
#include <QUrl>

#include "util/utils.h"

namespace StructuredRichTextPrivate
{

namespace
{

/* File suffix used by the SVG image provider path. */
constexpr const char *SVG_SUFFIX = ".svg";

/**
 * @brief Checks whether an explicit monochrome image can be colorized.
 *
 * This mirrors the dictionary image provider's SVG and raster format support,
 * so unsupported image forms retain their normal file URL.
 *
 * @param path The dictionary resource path to inspect.
 * @return True when the color image provider can render the resource.
 */
[[nodiscard]]
bool canTintMonochromeImage(const QString &path)
{
    if (path.endsWith(SVG_SUFFIX, Qt::CaseInsensitive))
    {
        return true;
    }

    const QByteArray suffix = QFileInfo(path).suffix().toLatin1();
    if (suffix.isEmpty())
    {
        return false;
    }

    /* Cache formats because a definition can contain many image objects. */
    static const QList<QByteArray> formats =
        QImageReader::supportedImageFormats();
    return std::any_of(
        formats.cbegin(),
        formats.cend(),
        [&suffix] (const QByteArray &format) -> bool
        {
            return format.compare(suffix, Qt::CaseInsensitive) == 0;
        }
    );
}

/**
 * @brief Flatten transparent structured-content arrays in source order.
 *
 * @param value The content value to flatten.
 * @param[out] values The direct renderable values discovered in the value.
 */
void appendDirectContentValues(
    const QJsonValue &value,
    QJsonArray &values)
{
    if (value.isArray())
    {
        for (const QJsonValue &child : value.toArray())
        {
            appendDirectContentValues(child, values);
        }
        return;
    }
    if (!value.isNull() && !value.isUndefined())
    {
        values.append(value);
    }
}

/**
 * @brief Check whether an inline style requests a visible CSS box.
 *
 * @param style The structured-content style object.
 * @return True when the style contains a box-painting property.
 */
[[nodiscard]]
bool hasInlineBoxStyle(const QJsonObject &style)
{
    for (const QString &key : style.keys())
    {
        const QString normalized = key.toLower();
        if (normalized == "backgroundcolor" ||
            normalized.startsWith("border") ||
            normalized.startsWith("padding"))
        {
            return true;
        }
    }
    return false;
}

/**
 * @brief Check whether a value is a text label needing an inline box.
 *
 * @param value The structured-content value to inspect.
 * @return True for a text-only span with box-painting inline styles.
 */
[[nodiscard]]
bool isInlineBoxLabel(const QJsonValue &value)
{
    if (!value.isObject())
    {
        return false;
    }

    const QJsonObject obj = value.toObject();
    return obj["tag"].toString().compare(
        "span",
        Qt::CaseInsensitive
    ) == 0 &&
        obj["content"].isString() &&
        obj["style"].isObject() &&
        hasInlineBoxStyle(obj["style"].toObject());
}

/**
 * @brief Check whether content fits one inline compatibility row.
 *
 * @param value The structured-content value to inspect.
 * @return True when the content has no block-level source elements.
 */
[[nodiscard]]
bool isInlineStructuredContent(const QJsonValue &value)
{
    if (value.isString())
    {
        return true;
    }
    if (value.isArray())
    {
        const QJsonArray values = value.toArray();
        return std::all_of(
            values.cbegin(),
            values.cend(),
            [] (const QJsonValue &child)
            {
                return isInlineStructuredContent(child);
            }
        );
    }
    if (!value.isObject())
    {
        return true;
    }

    const QString tag = value.toObject()["tag"].toString().toLower();
    return tag == "a" || tag == "br" || tag == "img" ||
        tag == "rp" || tag == "rt" || tag == "ruby" ||
        tag == "span";
}

} // namespace

/* Structured-content traversal and rich-text assembly. */

QString Renderer::parse(
    const DictionaryInfo *info,
    const QJsonArray &content,
    Setting::GlossaryStyle style,
    const QQuickItem *item,
    const QFont &font,
    const QColor &color,
    const QColor &backgroundColor,
    const QVariantMap &detailStates) const
{
    if (info == nullptr)
    {
        return "";
    }

    /* JSON keys and discriminator values for top-level glossary entries */
    constexpr const char *KEY_TYPE = "type";
    constexpr const char *KEY_CONTENT = "content";
    constexpr const char *VALUE_TYPE_IMAGE = "image";
    constexpr const char *VALUE_TYPE_STRUCTURED_CONTENT = "structured-content";
    constexpr const char *VALUE_TYPE_TEXT = "text";

    Renderer::Context ctx;
    ctx.info = info;
    ctx.style = style;
    ctx.detailStates = detailStates;
    if (item && item->window())
    {
        ctx.screen = item->window()->screen();
    }
    ctx.font = font;
    ctx.dictionaryStyles = info->styles();
    if (ctx.dictionaryStyles != nullptr)
    {
        ctx.needsElementChildCount =
            ctx.dictionaryStyles->parsedStylesheet().usesElementChildCount;
    }

    ctx.rootFontPixelSize = fontPixelSize(font, ctx.screen);
    ctx.parentFontPixelSize = ctx.rootFontPixelSize;
    if (color.isValid())
    {
        ctx.glossaryTextColor = color.name(
            color.alpha() == OPAQUE_ALPHA ?
                QColor::HexRgb :
                QColor::HexArgb
        );
        ctx.textColor = ctx.glossaryTextColor;
    }
    if (backgroundColor.isValid())
    {
        ctx.glossaryBackgroundColor = backgroundColor.name(
            backgroundColor.alpha() == OPAQUE_ALPHA ?
                QColor::HexRgb :
                QColor::HexArgb
        );
        ctx.backgroundColor = ctx.glossaryBackgroundColor;
    }
    ctx.paintedBackgroundColor = ctx.backgroundColor;
    ctx.selectorElements.reserve(SELECTOR_ELEMENT_RESERVE);
    ctx.elements.reserve(ELEMENT_STACK_RESERVE);
    ctx.siblings.reserve(SIBLING_STACK_RESERVE);
    ctx.lists.reserve(LIST_STACK_RESERVE);
    ctx.details.reserve(ELEMENT_STACK_RESERVE);
    ctx.resolvedCssValues.reserve(CSS_VALUE_CACHE_RESERVE);
    ctx.basepath = DirectoryUtils::getDictionaryResourceDir();
#if defined(Q_OS_WIN)
    ctx.basepath.prepend('/');
    ctx.basepath.replace('\\', '/');
#endif
    ctx.basepath.prepend("file://");
    ctx.basepath += '/';
    ctx.basepath += info->name();
    ctx.basepath += '/';

    const bool containsSc = containsStructuredContent(content);
    const bool shouldUseBullets =
        style == Setting::GlossaryStyleBullet && !containsSc;

    QString glossary;
    glossary.reserve(OUTPUT_RESERVE);
    glossary = "<html><head></head><body>";

    StructuredList fallbackList{"ul", "disc"};

    for (qsizetype i = 0; i < content.size(); ++i)
    {
        const QJsonValue &val = content[i];

        if (shouldUseBullets)
        {
            /* Marker-to-content gap relative to the inherited font size */
            constexpr double RELATIVE_INDENT_FACTOR = 0.35;

            ++fallbackList.item;
            glossary += "<table "
                "cellspacing=\"0\" "
                "width=\"100%\">"
                    "<tr><td "
                    "style=\""
                        "vertical-align: top; "
                        "white-space: nowrap; "
                        "padding-right: ";
            glossary += formatPixelSize(
                ctx.parentFontPixelSize * RELATIVE_INDENT_FACTOR
            );
            glossary += "px;\">";
            addSelectionSentinel(LIST_ITEM_SENTINEL, glossary);
            addSelectionSentinel(LIST_MARKER_START_SENTINEL, glossary);
            glossary += escapeHtml(listMarker(fallbackList, ""));
            addSelectionSentinel(LIST_MARKER_END_SENTINEL, glossary);
            glossary += "</td>"
                "<td width=\"100%\" style=\"vertical-align: top;\">";
        }

        switch (val.type())
        {
            case QJsonValue::Type::String:
                glossary += escapeHtml(val.toString()).replace('\n', "<br>");
                break;

            case QJsonValue::Type::Object:
            {
                QJsonObject obj = val.toObject();
                if (obj[KEY_TYPE] == VALUE_TYPE_STRUCTURED_CONTENT)
                {
                    addStructuredChildren(obj[KEY_CONTENT], ctx, glossary);
                }
                else if (obj[KEY_TYPE] == VALUE_TYPE_IMAGE)
                {
                    addImage(obj, ctx, glossary);
                }
                else if (obj[KEY_TYPE] == VALUE_TYPE_TEXT)
                {
                    addText(obj, glossary);
                }
                break;
            }

            default:
                break;
        }

        if (containsSc)
        {
            /* Ignore this check if structured content is detected */
        }
        else if (shouldUseBullets)
        {
            glossary += "</td></tr></table>";
        }
        else if (i >= content.size() - 1)
        {
            /* Avoid putting <br> or | after the fine line */
        }
        else if (style == Setting::GlossaryStyleLineBreak)
        {
            glossary += "<br>";
        }
        else if (style == Setting::GlossaryStylePipe)
        {
            glossary += " | ";
        }
    }

    glossary += "</body></html>";

    return glossary;
}

/* Structured content traversal. */
void Renderer::addStructuredData(
    const QJsonObject &obj, QString &out) const
{
    for (const QString &key : obj.keys())
    {
        const QJsonValue &val = obj[key];
        if (!val.isString())
        {
            continue;
        }
        out += ' ';
        out += structuredDataAttributeName(key);
        out += "=\"";
        out += escapeHtml(val.toString());
        out += '"';
    }
}

void Renderer::addStructuredContentHelper(
    const QString &str, QString &out) const
{
    out += escapeHtml(str).replace('\n', "<br>");
}

void Renderer::addStructuredContentHelper(
    const QString &str,
    Renderer::Context &ctx,
    QString &out) const
{
    flushPendingVerticalMargin(ctx, out);

    if (ctx.linkHref.isEmpty() && ctx.titleTooltip.isEmpty())
    {
        addStructuredContentHelper(str, out);
        return;
    }

    const bool titleOnly =
        ctx.linkHref.isEmpty() && !ctx.titleTooltip.isEmpty();
    const QString href = ctx.titleTooltip.isEmpty() ?
        ctx.linkHref :
        internalLinkHref(ctx.linkHref, ctx.titleTooltip, "", "title");
    addAnchorStart(
        href,
        titleOnly ? ctx.textColor : "",
        ctx.suppressLinkDecoration,
        out
    );
    addStructuredContentHelper(str, out);
    out += "</a>";
}

void Renderer::addStructuredContentHelper(
    const QJsonArray &arr,
    Renderer::Context &ctx,
    QString &out) const
{
    for (const QJsonValue &val : arr)
    {
        addStructuredContent(val, ctx, out);
    }
}

void Renderer::addStructuredContentHelper(
    const QJsonObject &obj,
    Renderer::Context &ctx,
    QString &out) const
{
    /* Structured-content keys consumed by special element renderers */
    constexpr const char *KEY_ALT = "alt";
    constexpr const char *KEY_BACKGROUND = "background";
    constexpr const char *KEY_BORDER = "border";
    constexpr const char *KEY_BORDER_RADIUS = "borderRadius";
    constexpr const char *KEY_CONTENT = "content";
    constexpr const char *KEY_DATA = "data";
    constexpr const char *KEY_DESCRIPTION = "description";
    constexpr const char *KEY_HEIGHT = "height";
    constexpr const char *KEY_HREF = "href";
    constexpr const char *KEY_PIXELATED = "pixelated";
    constexpr const char *KEY_RENDERING = "imageRendering";
    constexpr const char *KEY_TAG = "tag";
    constexpr const char *KEY_TITLE = "title";
    constexpr const char *KEY_UNITS = "sizeUnits";
    constexpr const char *KEY_VERT_ALIGN = "verticalAlign";
    constexpr const char *KEY_WIDTH = "width";

    QString tag = obj[KEY_TAG].toString();
    if (!isSupportedStructuredTag(tag))
    {
        return;
    }
    else if (tag == "ruby")
    {
        flushPendingVerticalMargin(ctx, out);
        ctx.elements.emplaceBack(structuredElement(obj, ctx));
        addRuby(obj, ctx, out);
        ctx.elements.removeLast();
    }
    else if (obj[KEY_TAG].toString() == "a" &&
        anchorNeedsCustomHandling(obj, ctx))
    {
        flushPendingVerticalMargin(ctx, out);
        ctx.elements.emplaceBack(structuredElement(obj, ctx));

        const QString oldLinkHref = ctx.linkHref;
        const QString oldTitleTooltip = ctx.titleTooltip;
        if (obj[KEY_HREF].isString())
        {
            ctx.linkHref = obj[KEY_HREF].toString();
        }
        if (obj[KEY_TITLE].isString())
        {
            ctx.titleTooltip = obj[KEY_TITLE].toString();
        }
        addStructuredChildren(obj[KEY_CONTENT], ctx, out);
        ctx.titleTooltip = oldTitleTooltip;
        ctx.linkHref = oldLinkHref;

        ctx.elements.removeLast();
    }
    else if (tag == "br")
    {
        flushPendingVerticalMargin(ctx, out);
        ctx.elements.emplaceBack(structuredElement(obj, ctx));
        out += '<';
        out += tag;
        if (obj[KEY_DATA].isObject())
        {
            addStructuredData(obj[KEY_DATA].toObject(), out);
        }
        out += '>';
        ctx.elements.removeLast();
    }
    else if (tag == "img")
    {
        flushPendingVerticalMargin(ctx, out);
        ctx.elements.emplaceBack(structuredElement(obj, ctx));

        const QString filename = escapeHtml(structuredImageSource(obj, ctx));

        const QString imageTitle = obj[KEY_TITLE].isString() ?
            obj[KEY_TITLE].toString() :
            ctx.titleTooltip;

        const bool imageTitleOnly = ctx.linkHref.isEmpty() &&
            !imageTitle.isEmpty();

        if (!imageTitle.isEmpty())
        {
            addAnchorStart(
                internalLinkHref(ctx.linkHref, imageTitle, "", "title"),
                imageTitleOnly ? ctx.textColor : "",
                ctx.suppressLinkDecoration,
                out
            );
        }

        out += "<img src=\"";
        out += filename;
        out += '"';

        if (obj[KEY_DATA].isObject())
        {
            addStructuredData(obj[KEY_DATA].toObject(), out);
        }

        if (obj[KEY_ALT].isString())
        {
            out += " alt=\"";
            out += escapeHtml(obj[KEY_ALT].toString());
            out += '"';
        }

        if (obj[KEY_DESCRIPTION].isString())
        {
            out += " description=\"";
            out += escapeHtml(obj[KEY_DESCRIPTION].toString());
            out += '"';
        }

        QString units = obj[KEY_UNITS].toString("px");
        if (obj[KEY_WIDTH].isDouble())
        {
            QString cssSize = QString("%1%2")
                .arg(obj[KEY_WIDTH].toDouble())
                .arg(units);
            const double pixelSize = cssFontSizeToPixels(
                cssSize,
                ctx.screen,
                ctx.parentFontPixelSize,
                ctx.rootFontPixelSize
            );

            out += " width=\"";
            out += formatPixelSize(pixelSize);
            out += '"';
        }
        if (obj[KEY_HEIGHT].isDouble())
        {
            QString cssSize = QString("%1%2")
                .arg(obj[KEY_HEIGHT].toDouble())
                .arg(units);
            const double pixelSize = cssFontSizeToPixels(
                cssSize,
                ctx.screen,
                ctx.parentFontPixelSize,
                ctx.rootFontPixelSize
            );

            out += " height=\"";
            out += formatPixelSize(pixelSize);
            out += '"';
        }

        CssDeclarations declarations;
        addMatchingCssRules(ctx, declarations);

        if (obj[KEY_RENDERING].isString())
        {
            addCssDeclaration(
                "image-rendering",
                obj[KEY_RENDERING].toString("auto"),
                declarations
            );
        }
        else if (obj[KEY_PIXELATED].toBool(false))
        {
            addCssDeclaration("image-rendering", "pixelated", declarations);
        }

        if (obj[KEY_BACKGROUND].toBool(true))
        {
            addCssDeclaration(
                "background-color", "currentColor", declarations
            );
        }

        if (obj[KEY_VERT_ALIGN].isString())
        {
            addCssDeclaration(
                "vertical-align",
                obj[KEY_VERT_ALIGN].toString(),
                declarations
            );
        }

        if (obj[KEY_BORDER].isString())
        {
            addCssDeclaration(
                "border",
                obj[KEY_BORDER].toString(),
                declarations
            );
        }

        if (obj[KEY_BORDER_RADIUS].isString())
        {
            addCssDeclaration(
                "border-radius",
                obj[KEY_BORDER_RADIUS].toString(),
                declarations
            );
        }

        if (!declarations.isEmpty())
        {
            out += " style=\"";
            addCssDeclarations(declarations, out);
            out += '"';
        }
        out += '>';

        if (!imageTitle.isEmpty())
        {
            out += "</a>";
        }

        ctx.elements.removeLast();
    }
    else
    {
        addStructuredElement(obj, tag, ctx, out);
    }
}

void Renderer::addStructuredChildren(
    const QJsonValue &val,
    Renderer::Context &ctx,
    QString &out) const
{
    SiblingState siblings;
    if (ctx.needsElementChildCount &&
        needsStructuredElementChildCount(val, ctx))
    {
        siblings.elementCount = structuredElementChildCount(val);
    }
    ctx.siblings.emplaceBack(std::move(siblings));
    addStructuredContent(val, ctx, out);
    flushPendingVerticalMargin(ctx, out);
    ctx.siblings.removeLast();
}

qsizetype Renderer::structuredElementChildCount(
    const QJsonValue &val) const
{
    if (val.isArray())
    {
        qsizetype count = 0;
        const QJsonArray arr = val.toArray();
        for (const QJsonValue &child : arr)
        {
            count += structuredElementChildCount(child);
        }
        return count;
    }
    if (!val.isObject())
    {
        return 0;
    }

    const QJsonObject obj = val.toObject();
    const QString tag = obj["tag"].toString().toLower();
    return isSupportedStructuredTag(tag) ? 1 : 0;
}

bool Renderer::needsStructuredElementChildCount(
    const QJsonValue &val,
    const Renderer::Context &ctx) const
{
    if (ctx.dictionaryStyles == nullptr)
    {
        return false;
    }
    if (val.isArray())
    {
        for (const QJsonValue &child : val.toArray())
        {
            if (needsStructuredElementChildCount(child, ctx))
            {
                return true;
            }
        }
        return false;
    }
    if (!val.isObject())
    {
        return false;
    }

    const QString tag = val.toObject()["tag"].toString().toLower();
    return isSupportedStructuredTag(tag) &&
        ctx.dictionaryStyles->needsElementChildCountForTag(tag);
}

void Renderer::addClosedDetailsContent(
    const QJsonValue &val,
    Renderer::Context &ctx,
    QString &out) const
{
    if (val.isArray())
    {
        for (const QJsonValue &child : val.toArray())
        {
            addClosedDetailsContent(child, ctx, out);
        }
        return;
    }
    if (!val.isObject())
    {
        return;
    }

    const QJsonObject obj = val.toObject();
    const QString tag = obj["tag"].toString();
    if (tag.compare("summary", Qt::CaseInsensitive) == 0)
    {
        addStructuredContent(val, ctx, out);
        return;
    }

    skipClosedDetailsContent(val, ctx);
}

void Renderer::skipClosedDetailsContent(
    const QJsonValue &val,
    Renderer::Context &ctx) const
{
    if (val.isArray())
    {
        for (const QJsonValue &child : val.toArray())
        {
            skipClosedDetailsContent(child, ctx);
        }
        return;
    }
    if (!val.isObject())
    {
        return;
    }

    const QJsonObject obj = val.toObject();
    if (obj["tag"].toString().compare("details", Qt::CaseInsensitive) == 0)
    {
        ++ctx.nextDetailId;
    }
    skipClosedDetailsContent(obj["content"], ctx);
}

void Renderer::addInlineLabelContent(
    const QJsonValue &content,
    Renderer::Context &ctx,
    QString &out) const
{
    QJsonArray values;
    appendDirectContentValues(content, values);
    if (values.isEmpty())
    {
        return;
    }

    const bool inlineRemainder = std::all_of(
        values.cbegin() + 1,
        values.cend(),
        [] (const QJsonValue &value)
        {
            return isInlineStructuredContent(value);
        }
    );
    const bool hasRemainder = values.size() > 1;
    QString labelSpacing;
    double labelFontPixelSize = ctx.parentFontPixelSize;

    if (inlineRemainder && hasRemainder)
    {
        const QJsonObject label = values.at(0).toObject();
        const QJsonObject style = label["style"].toObject();
        const QString fontSize = style["fontSize"].toString();
        if (!fontSize.isEmpty())
        {
            labelFontPixelSize = cssFontSizeToPixels(
                fontSize,
                ctx.screen,
                ctx.parentFontPixelSize,
                ctx.rootFontPixelSize
            );
        }
        labelSpacing = style[
            ctx.textDirection == Qt::RightToLeft ?
                "marginLeft" :
                "marginRight"
        ].toString();
        out += "<table cellspacing=\"0\" width=\"100%\"><tr>"
            "<td style=\"vertical-align: middle; "
            "white-space: nowrap;\">";
    }

    const bool oldForceInlineBox = ctx.forceInlineBox;
    const bool oldSuppressInlineBoxSpacing =
        ctx.suppressInlineBoxSpacing;
    ctx.forceInlineBox = true;
    ctx.suppressInlineBoxSpacing = inlineRemainder && hasRemainder;
    addStructuredContent(values.at(0), ctx, out);
    ctx.suppressInlineBoxSpacing = oldSuppressInlineBoxSpacing;
    ctx.forceInlineBox = oldForceInlineBox;

    if (inlineRemainder && hasRemainder)
    {
        addSelectionSentinel(INLINE_CELL_GAP_START_SENTINEL, out);
        out += "</td><td width=\"100%\" "
            "style=\"vertical-align: top;\">";
        addSelectionSentinel(INLINE_CELL_GAP_END_SENTINEL, out);
        addInlineSpacer(
            labelSpacing,
            labelFontPixelSize,
            ctx,
            out
        );
    }

    for (qsizetype i = 1; i < values.size(); ++i)
    {
        addStructuredContent(values[i], ctx, out);
    }

    if (inlineRemainder && hasRemainder)
    {
        out += "</td></tr></table>";
    }
}

void Renderer::addCompatibleElementContent(
    const QJsonObject &obj,
    const QString &tag,
    Renderer::Context &ctx,
    QString &out) const
{
    const QJsonValue content = obj["content"];
    const bool supportsInlineLabel =
        tag == "div" || tag == "td" || tag == "th";
    if (!supportsInlineLabel)
    {
        addStructuredContent(content, ctx, out);
        return;
    }

    QJsonArray values;
    appendDirectContentValues(content, values);
    if (values.isEmpty() || !isInlineBoxLabel(values.at(0)))
    {
        addStructuredContent(content, ctx, out);
        return;
    }

    const bool labelledBlock = tag == "div";
    const bool labelledCell =
        (tag == "td" || tag == "th") && values.size() == 1;
    if (labelledBlock || labelledCell)
    {
        addInlineLabelContent(content, ctx, out);
        return;
    }

    addStructuredContent(content, ctx, out);
}

void Renderer::addStructuredElement(
    const QJsonObject &obj,
    const QString &tag,
    Renderer::Context &ctx,
    QString &out) const
{
    /* Structured-content key containing an element's children */
    constexpr const char *KEY_CONTENT = "content";

    const bool isDetails = tag == "details";
    const DetailsState details = isDetails ?
        detailsState(obj, ctx) :
        DetailsState{};
    ctx.elements.emplaceBack(structuredElement(obj, ctx, details.open));

    const QString oldTitleTooltip = ctx.titleTooltip;
    ElementRenderState state = elementRenderState(obj, tag, ctx);
    if (isDetails)
    {
        state.details = details;
    }
    const bool blockElement = state.layout != ElementLayout::Inline;
    const bool collapsibleBlock = state.marginFlow == MarginFlow::Collapsible;
    const bool propagatesTrailingChildMargin =
        state.marginFlow == MarginFlow::PropagateLastChild;

    if (blockElement)
    {
        addPendingVerticalMargin(
            state.box.margins[static_cast<std::size_t>(BoxSide::Top)],
            state.fontPixelSize,
            ctx,
            ctx.siblings.back()
        );
        if (!collapsibleBlock)
        {
            flushPendingVerticalMargin(ctx, out);
        }
    }
    else
    {
        flushPendingVerticalMargin(ctx, out);
    }
    addStructuredElementStart(state, ctx, out);

    if (state.layout == ElementLayout::List)
    {
        ctx.lists.emplaceBack(StructuredList{tag, state.listMarkerType});
    }
    if (isDetails)
    {
        ctx.details.emplaceBack(state.details);
    }

    std::swap(ctx.parentFontPixelSize, state.fontPixelSize);
    std::swap(ctx.textColor, state.textColor);
    std::swap(ctx.titleTooltip, state.titleTooltip);
    std::swap(ctx.textDirection, state.textDirection);
    std::swap(ctx.paintedBackgroundColor, state.paintedBackgroundColor);
    const bool oldSuppressLinkDecoration =
        ctx.suppressLinkDecoration;
    ctx.suppressLinkDecoration =
        oldSuppressLinkDecoration || state.raisedText;

    SiblingState completedChildren;
    if (blockElement)
    {
        SiblingState childSiblings;
        if (ctx.needsElementChildCount &&
            needsStructuredElementChildCount(obj[KEY_CONTENT], ctx))
        {
            childSiblings.elementCount =
                structuredElementChildCount(obj[KEY_CONTENT]);
        }
        if (collapsibleBlock)
        {
            mergePendingVerticalMargins(
                ctx.siblings.back(),
                childSiblings
            );
            clearPendingVerticalMargins(ctx.siblings.back());
        }

        ctx.siblings.emplaceBack(std::move(childSiblings));
        if (isDetails && !state.details.open)
        {
            addClosedDetailsContent(obj[KEY_CONTENT], ctx, out);
        }
        else
        {
            addCompatibleElementContent(obj, tag, ctx, out);
        }
        if (!collapsibleBlock && !propagatesTrailingChildMargin)
        {
            flushPendingVerticalMargin(ctx, out);
        }
        completedChildren = ctx.siblings.takeLast();
    }
    else
    {
        SiblingState childSiblings;
        if (ctx.needsElementChildCount &&
            needsStructuredElementChildCount(obj[KEY_CONTENT], ctx))
        {
            childSiblings.elementCount =
                structuredElementChildCount(obj[KEY_CONTENT]);
        }
        ctx.siblings.emplaceBack(std::move(childSiblings));
        addCompatibleElementContent(obj, tag, ctx, out);
        flushPendingVerticalMargin(ctx, out);
        ctx.siblings.removeLast();
    }

    ctx.suppressLinkDecoration = oldSuppressLinkDecoration;
    std::swap(ctx.paintedBackgroundColor, state.paintedBackgroundColor);
    std::swap(ctx.textDirection, state.textDirection);
    std::swap(ctx.titleTooltip, state.titleTooltip);
    std::swap(ctx.textColor, state.textColor);
    std::swap(ctx.parentFontPixelSize, state.fontPixelSize);
    ctx.titleTooltip = oldTitleTooltip;

    if (state.layout == ElementLayout::List)
    {
        ctx.lists.removeLast();
    }
    if (isDetails)
    {
        ctx.details.removeLast();
    }

    addStructuredElementEnd(state, ctx, out);
    if (blockElement)
    {
        if (collapsibleBlock || propagatesTrailingChildMargin)
        {
            mergePendingVerticalMargins(
                completedChildren,
                ctx.siblings.back()
            );
        }
        addPendingVerticalMargin(
            state.box.margins[static_cast<std::size_t>(BoxSide::Bottom)],
            state.fontPixelSize,
            ctx,
            ctx.siblings.back()
        );
    }
    ctx.elements.removeLast();
}

Renderer::ElementRenderState
Renderer::elementRenderState(
    const QJsonObject &obj,
    const QString &tag,
    Renderer::Context &ctx) const
{
    ElementRenderState state;
    state.tag = tag;
    state.outputTag = tag;
    state.fontPixelSize = ctx.parentFontPixelSize;
    state.textColor = ctx.textColor;
    state.titleTooltip = ctx.titleTooltip;
    state.textDirection = ctx.textDirection;
    state.paintedBackgroundColor = ctx.paintedBackgroundColor;
    state.textOnlyContent = obj["content"].isString();
    if (tag == "summary" && ctx.elements.size() > 1 &&
        !ctx.details.isEmpty())
    {
        const qsizetype parentIndex = ctx.elements.size() - 2;
        const StructuredElement &parent =
            ctx.selectorElements[ctx.elements[parentIndex]];
        if (parent.tag == "details")
        {
            state.disclosureSummary = true;
            state.details = ctx.details.back();
        }
    }
    if (obj["lang"].isString())
    {
        state.textDirection =
            QLocale(obj["lang"].toString()).textDirection();
    }

    addElementAttributes(obj, state);
    resolveElementStyles(obj, ctx, state);
    resolveElementLayout(ctx, state);
    applyElementCompatibility(ctx, state);

    return state;
}

Renderer::DetailsState Renderer::detailsState(
    const QJsonObject &obj,
    Renderer::Context &ctx) const
{
    /* Structured-content key carrying the source disclosure state */
    constexpr const char *KEY_OPEN = "open";

    DetailsState state;
    state.id = QString::number(ctx.nextDetailId++);
    state.open = obj[KEY_OPEN].isBool() && obj[KEY_OPEN].toBool();

    const auto stateOverride = ctx.detailStates.constFind(state.id);
    if (stateOverride != ctx.detailStates.cend())
    {
        state.open = stateOverride->toBool();
    }
    return state;
}

QString Renderer::detailsLinkHref(
    const Renderer::DetailsState &details) const
{
    QString href = "memento://glossary-details?id=";
    href += details.id;
    href += "&open=";
    href += details.open ? '1' : '0';
    return href;
}

void Renderer::addElementAttributes(
    const QJsonObject &obj,
    Renderer::ElementRenderState &state) const
{
    /* Structured-content keys emitted as HTML element attributes */
    constexpr const char *KEY_COLSPAN = "colSpan";
    constexpr const char *KEY_DATA = "data";
    constexpr const char *KEY_HREF = "href";
    constexpr const char *KEY_LANG = "lang";
    constexpr const char *KEY_ROWSPAN = "rowSpan";
    constexpr const char *KEY_TITLE = "title";

    if (obj[KEY_HREF].isString())
    {
        state.attributes += " href=\"";
        state.attributes += escapeHtml(obj[KEY_HREF].toString());
        state.attributes += '"';
    }

    if (obj[KEY_DATA].isObject())
    {
        addStructuredData(obj[KEY_DATA].toObject(), state.attributes);
    }

    if (obj[KEY_COLSPAN].isDouble())
    {
        state.attributes += " colspan=\"";
        state.attributes += QString::number(
            static_cast<int>(obj[KEY_COLSPAN].toDouble())
        );
        state.attributes += '"';
    }

    if (obj[KEY_ROWSPAN].isDouble())
    {
        state.attributes += " rowspan=\"";
        state.attributes += QString::number(
            static_cast<int>(obj[KEY_ROWSPAN].toDouble())
        );
        state.attributes += '"';
    }

    if (obj[KEY_TITLE].isString())
    {
        state.titleTooltip = obj[KEY_TITLE].toString();
        state.attributes += " title=\"";
        state.attributes += escapeHtml(obj[KEY_TITLE].toString());
        state.attributes += '"';
    }

    if (obj[KEY_LANG].isString())
    {
        state.attributes += " lang=\"";
        state.attributes += escapeHtml(obj[KEY_LANG].toString());
        state.attributes += '"';
    }
}

void Renderer::addStructuredContent(
    const QJsonValue &val,
    Renderer::Context &ctx,
    QString &out) const
{
    switch (val.type())
    {
    case QJsonValue::Type::String:
        addStructuredContentHelper(val.toString(), ctx, out);
        break;

    case QJsonValue::Type::Array:
        addStructuredContentHelper(val.toArray(), ctx, out);
        break;

    case QJsonValue::Type::Object:
        addStructuredContentHelper(val.toObject(), ctx, out);
        break;

    default:
        break;
    }
}

/* Legacy glossary object rendering. */

void Renderer::addImage(
    const QJsonObject &obj,
    Renderer::Context &ctx,
    QString &out) const
{
    /* Legacy glossary image object keys */
    constexpr const char *KEY_WIDTH = "width";
    constexpr const char *KEY_HEIGHT = "height";
    constexpr const char *KEY_TITLE = "title";
    constexpr const char *KEY_RENDERING = "imageRendering";
    constexpr const char *KEY_DESCRIPTION = "description";

    const QString filename = escapeHtml(structuredImageSource(obj, ctx));

    if (obj[KEY_TITLE].isString())
    {
        addAnchorStart(
            internalLinkHref("", obj[KEY_TITLE].toString(), "", "title"),
            ctx.textColor,
            false,
            out
        );
    }

    out += "<img src=\"";
    out += filename;
    out += '"';

    if (obj[KEY_WIDTH].isDouble())
    {
        out += " width=\"";
        out += QString::number(obj[KEY_WIDTH].toDouble(1.0));
        out += '"';
    }
    if (obj[KEY_HEIGHT].isDouble())
    {
        out += " height=\"";
        out += QString::number(obj[KEY_HEIGHT].toDouble(1.0));
        out += '"';
    }

    out += " style=\"display: inline-table; vertical-align: top;";
    if (obj[KEY_RENDERING].isString())
    {
        out += "image-rendering: ";
        out += obj[KEY_RENDERING].toString("auto");
        out += ";";
    }
    out += '"';

    out += '>';

    if (obj[KEY_TITLE].isString())
    {
        out += "</a>";
    }

    if (obj[KEY_DESCRIPTION].isString())
    {
        out += "<br>";
        out += escapeHtml(obj[KEY_DESCRIPTION].toString())
            .replace('\n', "<br>");
    }
}

void Renderer::addText(const QJsonObject &obj, QString &out) const
{
    /* Legacy glossary text object key */
    constexpr const char *KEY_TEXT = "text";
    out += escapeHtml(obj[KEY_TEXT].toString()).replace('\n', "<br>");
}

/* Rich-text helpers. */

bool Renderer::containsStructuredContent(
    const QJsonArray &content) const
{
    return std::any_of(
        std::begin(content), std::end(content),
        [] (const QJsonValue &value) -> bool
        {
            /* Top-level discriminator for structured glossary entries */
            constexpr const char *KEY_TYPE = "type";
            constexpr const char *VALUE_STRUCTURED_CONTENT =
                "structured-content";

            return value.type() == QJsonValue::Object &&
                value.toObject()[KEY_TYPE] == VALUE_STRUCTURED_CONTENT;
        }
    );
}

QString Renderer::escapeHtml(const QString &str) const
{
    return str.toHtmlEscaped();
}

void Renderer::addRuby(
    const QJsonObject &obj,
    Renderer::Context &ctx,
    QString &out) const
{
    /* Structured-content key containing ruby base and reading children */
    constexpr const char *KEY_CONTENT = "content";

    QJsonArray base;
    QString reading;
    splitRubyContent(obj[KEY_CONTENT], base, reading);

    if (reading.isEmpty())
    {
        addStructuredChildren(base, ctx, out);
        return;
    }

    addAnchorStart(
        internalLinkHref(
            ctx.linkHref,
            reading,
            structuredContentText(base),
            "ruby"
        ),
        ctx.linkHref.isEmpty() ? ctx.textColor : "",
        ctx.suppressLinkDecoration,
        out
    );

    const QString oldLinkHref = ctx.linkHref;
    const QString oldTitleTooltip = ctx.titleTooltip;
    ctx.linkHref.clear();
    ctx.titleTooltip.clear();
    addStructuredChildren(base, ctx, out);
    ctx.titleTooltip = oldTitleTooltip;
    ctx.linkHref = oldLinkHref;

    out += "</a>";
}

void Renderer::splitRubyContent(
    const QJsonValue &val,
    QJsonArray &base,
    QString &reading) const
{
    /* Structured-content keys used to separate ruby base and reading nodes */
    constexpr const char *KEY_CONTENT = "content";
    constexpr const char *KEY_TAG = "tag";

    switch (val.type())
    {
    case QJsonValue::Type::Array:
    {
        const QJsonArray arr = val.toArray();
        for (const QJsonValue &child : arr)
        {
            if (child.isObject())
            {
                const QJsonObject obj = child.toObject();
                const QString tag = obj[KEY_TAG].toString();
                if (tag == "rt")
                {
                    reading += structuredContentText(obj[KEY_CONTENT]);
                    continue;
                }
                if (tag == "rp")
                {
                    continue;
                }
            }
            base.append(child);
        }
        break;
    }

    case QJsonValue::Type::Object:
    {
        const QJsonObject obj = val.toObject();
        const QString tag = obj[KEY_TAG].toString();
        if (tag == "rt")
        {
            reading += structuredContentText(obj[KEY_CONTENT]);
        }
        else if (tag != "rp")
        {
            base.append(val);
        }
        break;
    }

    default:
        base.append(val);
        break;
    }
}

QString Renderer::structuredContentText(const QJsonValue &val) const
{
    /* Structured-content key recursively traversed for plain text */
    constexpr const char *KEY_CONTENT = "content";

    switch (val.type())
    {
    case QJsonValue::Type::String:
        return val.toString();

    case QJsonValue::Type::Array:
    {
        QString text;
        const QJsonArray arr = val.toArray();
        for (const QJsonValue &child : arr)
        {
            text += structuredContentText(child);
        }
        return text;
    }

    case QJsonValue::Type::Object:
        return structuredContentText(val.toObject()[KEY_CONTENT]);

    default:
        return "";
    }
}

bool Renderer::containsRuby(const QJsonValue &val) const
{
    /* Structured-content keys used while searching for ruby descendants */
    constexpr const char *KEY_CONTENT = "content";
    constexpr const char *KEY_TAG = "tag";

    switch (val.type())
    {
    case QJsonValue::Type::Array:
    {
        const QJsonArray arr = val.toArray();
        return std::any_of(
            std::begin(arr), std::end(arr),
            [this] (const QJsonValue &child) -> bool
            {
                return containsRuby(child);
            }
        );
    }

    case QJsonValue::Type::Object:
    {
        const QJsonObject obj = val.toObject();
        if (obj[KEY_TAG].toString() == "ruby")
        {
            return true;
        }
        return containsRuby(obj[KEY_CONTENT]);
    }

    default:
        return false;
    }
}

bool Renderer::containsRaisedContent(const QJsonValue &val) const
{
    /* Structured-content keys used while searching for raised descendants */
    constexpr const char *KEY_CONTENT = "content";
    constexpr const char *KEY_STYLE = "style";
    constexpr const char *KEY_VERTICAL_ALIGN = "verticalAlign";

    if (val.isArray())
    {
        const QJsonArray values = val.toArray();
        return std::any_of(
            values.cbegin(),
            values.cend(),
            [this] (const QJsonValue &child)
            {
                return containsRaisedContent(child);
            }
        );
    }
    if (!val.isObject())
    {
        return false;
    }

    const QJsonObject obj = val.toObject();
    const QString alignment =
        obj[KEY_STYLE].toObject()[KEY_VERTICAL_ALIGN].toString();
    if (alignment.compare("super", Qt::CaseInsensitive) == 0 ||
        alignment.compare("sub", Qt::CaseInsensitive) == 0)
    {
        return true;
    }
    return containsRaisedContent(obj[KEY_CONTENT]);
}

bool Renderer::anchorNeedsCustomHandling(
    const QJsonObject &obj,
    const Renderer::Context &ctx) const
{
    /* Keys that require custom anchor fragment handling */
    constexpr const char *KEY_CONTENT = "content";
    constexpr const char *KEY_TITLE = "title";

    return containsRuby(obj[KEY_CONTENT]) ||
        containsRaisedContent(obj[KEY_CONTENT]) ||
        obj[KEY_TITLE].isString() ||
        !ctx.titleTooltip.isEmpty();
}

QString Renderer::internalLinkHref(
    const QString &target,
    const QString &tooltip,
    const QString &tooltipText,
    const QString &tooltipType) const
{
    QString out = "memento://glossary-link?";
    bool hasParam = false;

    if (!target.isEmpty())
    {
        out += "target=";
        out += QString::fromUtf8(QUrl::toPercentEncoding(target));
        hasParam = true;
    }

    if (!tooltip.isEmpty())
    {
        if (hasParam)
        {
            out += '&';
        }
        out += "tooltip=";
        out += QString::fromUtf8(QUrl::toPercentEncoding(tooltip));
        hasParam = true;
    }

    if (!tooltipText.isEmpty())
    {
        if (hasParam)
        {
            out += '&';
        }
        out += "tooltipText=";
        out += QString::fromUtf8(QUrl::toPercentEncoding(tooltipText));
        hasParam = true;
    }

    if (!tooltipType.isEmpty())
    {
        if (hasParam)
        {
            out += '&';
        }
        out += "tooltipType=";
        out += QString::fromUtf8(QUrl::toPercentEncoding(tooltipType));
    }

    return out;
}

void Renderer::addAnchorStart(
    const QString &href,
    const QString &color,
    bool suppressDecoration,
    QString &out) const
{
    /* Quote substitution used to keep generated style attributes valid */
    constexpr const char QUOTE_SEARCH = '"';
    constexpr const char QUOTE_ESCAPE = '\'';

    out += "<a href=\"";
    out += escapeHtml(href);
    out += '"';
    if (!color.isEmpty() || suppressDecoration)
    {
        out += " style=\"";
        if (!color.isEmpty())
        {
            out += "color: ";
            out += escapeHtml(
                QString(color).replace(QUOTE_SEARCH, QUOTE_ESCAPE)
            );
            out += "; ";
        }
        out += "text-decoration: none;\"";
    }
    out += '>';
}

QString Renderer::structuredImageSource(
    const QJsonObject &obj,
    const Renderer::Context &ctx) const
{
    /* Structured-image keys used to select a monochrome image source */
    constexpr const char *KEY_APPEARANCE = "appearance";
    constexpr const char *KEY_PATH = "path";
    constexpr const char *VALUE_MONOCHROME = "monochrome";

    const QString path = obj[KEY_PATH].toString();
    const bool isMonochromeImage =
        obj[KEY_APPEARANCE].toString().compare(
            VALUE_MONOCHROME,
            Qt::CaseInsensitive
        ) == 0;
    const QColor color = QColor::fromString(ctx.textColor);
    if (!isMonochromeImage || !canTintMonochromeImage(path) ||
        !color.isValid() || ctx.info == nullptr)
    {
        return ctx.basepath + path;
    }

    const QString basePath = QUrl(ctx.basepath).toLocalFile();
    const QString sourcePath = QDir(basePath).filePath(path);
    if (basePath.isEmpty() || sourcePath.isEmpty())
    {
        return ctx.basepath + path;
    }

    QString source = "image://colored-image/file/";
    source += QString::fromLatin1(QUrl::toPercentEncoding(sourcePath));
    source += '/';
    source += color.name(QColor::HexArgb).mid(1);
    return source;
}

}
