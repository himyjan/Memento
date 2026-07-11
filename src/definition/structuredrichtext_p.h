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

#include <array>
#include <memory>

#include <QColor>
#include <QFont>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QQuickItem>
#include <QRegularExpression>
#include <QScreen>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include "definition/structuredrichtext.h"
#include "dict/data/dictionaryinfo.h"

namespace StructuredRichTextPrivate
{

/* Number of physical sides represented by CSS box arrays */
inline constexpr std::size_t BOX_SIDE_COUNT = 4;

/* Alpha channel value representing a fully opaque QColor */
inline constexpr int OPAQUE_ALPHA = 255;

/* Smallest pixel value treated as visible by compatibility rendering */
inline constexpr double MINIMUM_VISIBLE_PIXELS = 0.001;

/* Pixel width used for the CSS thin border keyword */
inline constexpr double THIN_BORDER_WIDTH_PIXELS = 1.0;

/* Pixel width used for an unspecified solid border and CSS medium */
inline constexpr double DEFAULT_BORDER_WIDTH_PIXELS = 3.0;

/* Pixel width used for the CSS thick border keyword */
inline constexpr double THICK_BORDER_WIDTH_PIXELS = 5.0;

/* Initial selector-element capacity for a structured-content render */
inline constexpr qsizetype SELECTOR_ELEMENT_RESERVE = 128;

/* Initial ancestor-stack capacity for selector matching */
inline constexpr qsizetype ELEMENT_STACK_RESERVE = 32;

/* Initial sibling-state stack capacity for margin collapsing */
inline constexpr qsizetype SIBLING_STACK_RESERVE = 32;

/* Initial nested-list stack capacity */
inline constexpr qsizetype LIST_STACK_RESERVE = 8;

/* Initial capacity for resolved CSS values cached during a render */
inline constexpr qsizetype CSS_VALUE_CACHE_RESERVE = 64;

/* Initial character capacity for generated rich-text HTML */
inline constexpr qsizetype OUTPUT_RESERVE = 4096;

/* Zero-width marker used to indicate a list item boundary in selected text */
inline constexpr char16_t LIST_ITEM_SENTINEL = u'\u2060';

/* Zero-width marker used to indicate the start of generated list marker text */
inline constexpr char16_t LIST_MARKER_START_SENTINEL = u'\u2061';

/* Zero-width marker used to indicate the end of generated list marker text */
inline constexpr char16_t LIST_MARKER_END_SENTINEL = u'\u2062';

/* Zero-width marker used before generated disclosure control text */
inline constexpr char16_t DETAILS_CONTROL_START_SENTINEL = u'\u2063';

/* Zero-width marker used after generated disclosure control text */
inline constexpr char16_t DETAILS_CONTROL_END_SENTINEL = u'\u2064';

/* Zero-width marker placed before a synthetic inline cell boundary */
inline constexpr char16_t INLINE_CELL_GAP_START_SENTINEL = u'\u2065';

/* Zero-width marker placed after a synthetic inline cell boundary */
inline constexpr char16_t INLINE_CELL_GAP_END_SENTINEL = u'\u206a';

/* Right-pointing chevron displayed for collapsed dictionary details */
inline constexpr char16_t CLOSED_DETAILS_CHEVRON = u'\u25b8';

/* Down-pointing chevron displayed for expanded dictionary details */
inline constexpr char16_t OPEN_DETAILS_CHEVRON = u'\u25be';

/**
 * @brief Split a CSS value into whitespace-delimited top-level tokens.
 *
 * Whitespace inside quoted strings and function parentheses is preserved.
 *
 * @param value The CSS value to tokenize.
 * @return The top-level CSS tokens in source order.
 */
[[nodiscard]]
QStringList splitCssTokens(const QString &value);

/**
 * @brief Renders one glossary entry without exposing implementation details.
 */
class Renderer
{
public:
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
    QString parse(
        const DictionaryInfo *info,
        const QJsonArray &content,
        Setting::GlossaryStyle style,
        const QQuickItem *item,
        const QFont &font,
        const QColor &color,
        const QColor &backgroundColor,
        const QVariantMap &detailStates) const;

private:
    /**
     * @brief A structured content element used for CSS selector matching.
     */
    struct StructuredElement
    {
        /* The element tag */
        QString tag;

        /* Attributes used by stylesheet selectors */
        QHash<QString, QString> attributes;

        /* Generated classes used by stylesheet selectors */
        QSet<QString> classes;

        /* Index of the previous element sibling in selectorElements, or -1 */
        qsizetype previousSibling{-1};

        /* One-based element-child index among siblings */
        qsizetype childIndex{0};

        /* Total element-child count in this element's parent */
        qsizetype childCount{0};
    };

    /**
     * @brief Element sibling state for the current content container.
     */
    struct SiblingState
    {
        /* Index of the most recently rendered element child in
         * selectorElements */
        qsizetype previousElement{-1};

        /* Number of element children encountered in this sibling group */
        qsizetype visitedElementCount{0};

        /* Total number of element children in this sibling group */
        qsizetype elementCount{0};

        /* Largest positive margin waiting to be emitted */
        double pendingPositiveMarginPixels{0.0};

        /* Smallest negative margin waiting to be emitted */
        double pendingNegativeMarginPixels{0.0};
    };

    /**
     * @brief List rendering state.
     */
    struct StructuredList
    {
        /* Parent list tag */
        QString tag;

        /* Parent marker type */
        QString marker;

        /* Current item index */
        qsizetype item{0};
    };

    /**
     * @brief State assigned to one interactive structured details element.
     */
    struct DetailsState
    {
        /* Stable ID shared with the QML disclosure state map */
        QString id;

        /* True when the details body should be emitted */
        bool open{false};
    };

    using CssRule = DictionaryStyles::CssRule;
    using CssDeclaration = DictionaryStyles::CssDeclaration;
    using CssSelectorPart = DictionaryStyles::CssSelectorPart;
    using ParsedStylesheet = DictionaryStyles::ParsedStylesheet;
    using CssDeclarations = QMap<QString, QString>;

    /**
     * @brief Physical side of a CSS box in clockwise order.
     */
    enum class BoxSide : std::size_t
    {
        Top,
        Right,
        Bottom,
        Left,
        Count /* Only used to determine the number of sides */
    };

    /**
     * @brief Qt-compatible layout selected for a structured element.
     */
    enum class ElementLayout
    {
        Inline,
        Block,
        List,
        MarkedListItem,
        MarkerlessListItem,
        Box
    };

    /**
     * @brief Width behavior used by an emitted compatibility box.
     */
    enum class WidthPolicy
    {
        Natural,
        Fill,
        FitContent,
        Explicit
    };

    /**
     * @brief Vertical margin behavior of a rendered element.
     */
    enum class MarginFlow
    {
        Inline,
        Collapsible,
        Contained,
        PropagateLastChild
    };

    /**
     * @brief Resolved border data for one side of a CSS box.
     */
    struct BorderSide
    {
        /* Resolved border color */
        QString color;

        /* Resolved border style */
        QString style;

        /* Border width converted to pixels */
        double widthPixels{0.0};

        /* True when a compatibility frame paints this border */
        bool painted{false};
    };

    /**
     * @brief Normalized CSS box used by the Qt compatibility renderer.
     */
    struct BoxStyle
    {
        /* Margins in top, right, bottom, left order */
        std::array<QString, static_cast<std::size_t>(BoxSide::Count)> margins{};

        /* Padding widths in top, right, bottom, left order, in pixels */
        std::array<double, static_cast<std::size_t>(BoxSide::Count)> padding{};

        /* Border definitions in top, right, bottom, left order */
        std::array<
            BorderSide,
            static_cast<std::size_t>(BoxSide::Count)
        > borders{};

        /* Opaque background color painted by the box frame */
        QString backgroundColor;

        /* True when the element needs a table-backed compatibility box */
        bool enabled{false};

        /* True when all painted borders can share one compact frame */
        bool compactBorderFrame{false};
    };

    /**
     * @brief Current context of the private renderer.
     */
    struct Context
    {
        /* The current DictionaryInfo */
        const DictionaryInfo *info{nullptr};

        /* The style to render v1 style terms in */
        Setting::GlossaryStyle style{
            Setting::GlossaryStyle::GlossaryStyleBullet
        };

        /* The current screen */
        const QScreen *screen{nullptr};

        /* The current font */
        QFont font;

        /* Font size in pixels of the root element */
        double rootFontPixelSize{12.0};

        /* Font size in pixels of the parent element */
        double parentFontPixelSize{12.0};

        /* Text color of the parent element */
        QString textColor;

        /* Global glossary text color used by palette CSS variables */
        QString glossaryTextColor;

        /* Background color of the glossary canvas */
        QString backgroundColor;

        /* Global glossary background color used by palette CSS variables */
        QString glossaryBackgroundColor;

        /* Nearest opaque background painted around the current content */
        QString paintedBackgroundColor;

        /* Base path of resources for this dictionary */
        QString basepath;

        /* CSS rules parsed from the dictionary stylesheet */
        std::shared_ptr<const DictionaryStyles> dictionaryStyles;

        /* True when selectors need total element-child counts */
        bool needsElementChildCount{false};

        /* Storage containing selector elements for the current render */
        QList<StructuredElement> selectorElements;

        /* Stack of indexes into selectorElements */
        QList<qsizetype> elements;

        /* Stack of sibling state for structured content containers */
        QList<SiblingState> siblings;

        /* Stack of lists being rendered */
        QList<StructuredList> lists;

        /* Open-state overrides received from the QML glossary item */
        QVariantMap detailStates;

        /* Stack of details elements enclosing the current content */
        QList<DetailsState> details;

        /* Next stable depth-first details ID for this render */
        qsizetype nextDetailId{0};

        /* CSS values resolved during this render, keyed by source value */
        QHash<QString, QString> resolvedCssValues;

        /* Inherited text layout direction */
        Qt::LayoutDirection textDirection{Qt::LeftToRight};

        /* The href inherited from a parent anchor */
        QString linkHref;

        /* The title tooltip inherited from a parent element */
        QString titleTooltip;

        /* True while a raised link fragment must not inherit an underline */
        bool suppressLinkDecoration{false};

        /* True while an inline label needs a table-backed CSS box */
        bool forceInlineBox{false};

        /* True when a wrapper cell owns an inline label's side spacing */
        bool suppressInlineBoxSpacing{false};
    };

    /**
     * @brief Resolved state used to render a structured element.
     */
    struct ElementRenderState
    {
        /* Original structured content tag used for selector matching */
        QString tag;

        /* Qt-compatible HTML tag emitted for the element */
        QString outputTag;

        /* HTML attributes emitted on the outer element */
        QString attributes;

        /* CSS declarations emitted on the outer element */
        CssDeclarations declarations;

        /* CSS declarations moved to a table-backed content cell */
        CssDeclarations cellDeclarations;

        /* Normalized CSS box used by the compatibility emitter */
        BoxStyle box;

        /* Qt-compatible layout selected for the element */
        ElementLayout layout{ElementLayout::Inline};

        /* Width behavior of the element or compatibility box */
        WidthPolicy widthPolicy{WidthPolicy::Natural};

        /* Vertical margin behavior selected for the element */
        MarginFlow marginFlow{MarginFlow::Inline};

        /* Resolved font size inherited by child content */
        double fontPixelSize{0.0};

        /* Resolved text color inherited by child content */
        QString textColor;

        /* Resolved title tooltip inherited by child content */
        QString titleTooltip;

        /* Background color inherited by child content */
        QString paintedBackgroundColor;

        /* Generated ::before content resolved with the element styles */
        QString beforeContent;

        /* Generated ::after content resolved with the element styles */
        QString afterContent;

        /* Visible marker emitted for the current list item */
        QString listMarker;

        /* Marker type inherited by child list items */
        QString listMarkerType;

        /* Resolved layout direction inherited by child content */
        Qt::LayoutDirection textDirection{Qt::LeftToRight};

        /* True when a Jitendex label needs non-breaking side padding */
        bool paddedSpan{false};

        /* True when the element contains only a text string */
        bool textOnlyContent{false};

        /* Details state when this element represents a disclosure */
        DetailsState details;

        /* True when this summary toggles its direct details parent */
        bool disclosureSummary{false};

        /* True when the element uses CSS super or sub vertical alignment */
        bool raisedText{false};

        /* Inline spacing before text-only block content */
        QString contentInlineSpacingBefore;

        /* Inline spacing after text-only block content */
        QString contentInlineSpacingAfter;

        /* Inline spacing emitted before the element */
        QString inlineSpacingBefore;

        /* Inline spacing emitted after the element */
        QString inlineSpacingAfter;
    };

    /**
     * @brief Add structured data attributes to the string.
     *
     * @param obj The structured data attributes to parse.
     * @param[out] out The string to append the data attributes to.
     */
    void addStructuredData(const QJsonObject &obj, QString &out) const;

    /**
     * @brief Add structured style objects to an element render state.
     *
     * @param obj The structured style object.
     * @param ctx The renderer context.
     * @param[out] state The element render state to update.
     */
    void addStructuredStyle(
        const QJsonObject &obj,
        Renderer::Context &ctx,
        ElementRenderState &state) const;

    /**
     * @brief Add string structured content.
     *
     * @param str The string to add.
     * @param[out] out The string this string will be appended to.
     */
    void addStructuredContentHelper(const QString &str, QString &out) const;

    /**
     * @brief Add string structured content, inheriting a parent link if
     * present.
     *
     * @param str The string to add.
     * @param ctx The renderer context.
     * @param[out] out The string this string will be appended to.
     */
    void addStructuredContentHelper(
        const QString &str,
        Renderer::Context &ctx,
        QString &out) const;

    /**
     * @brief Add an array of structured content.
     *
     * @param arr The array of structured content.
     * @param ctx The renderer context.
     * @param[out] out The string this content will be appended to.
     */
    void addStructuredContentHelper(
        const QJsonArray &arr,
        Renderer::Context &ctx,
        QString &out) const;

    /**
     * @brief Add an object of structured content.
     *
     * @param obj The object of structured content.
     * @param ctx The renderer context.
     * @param[out] out The string this content will be appended to.
     */
    void addStructuredContentHelper(
        const QJsonObject &obj,
        Renderer::Context &ctx,
        QString &out) const;

    /**
     * @brief Add children with a new element-sibling context.
     *
     * @param val The child structured content.
     * @param ctx The renderer context.
     * @param[out] out The string this content will be appended to.
     */
    void addStructuredChildren(
        const QJsonValue &val,
        Renderer::Context &ctx,
        QString &out) const;

    /**
     * @brief Count direct element children used by CSS sibling selectors.
     *
     * @param val The structured content value to inspect.
     * @return Number of supported structured elements in this child group.
     */
    [[nodiscard]]
    qsizetype structuredElementChildCount(const QJsonValue &val) const;

    /**
     * @brief Check whether direct children need CSS :last-child() counts.
     *
     * @param val The structured content value to inspect.
     * @param ctx The renderer context.
     * @return True when a direct child tag needs a total sibling count.
     */
    [[nodiscard]]
    bool needsStructuredElementChildCount(
        const QJsonValue &val,
        const Renderer::Context &ctx) const;

    /**
     * @brief Render direct summary children while a details body is closed.
     *
     * Detail IDs inside skipped content are still consumed to keep later
     * disclosures stable when a section is opened.
     *
     * @param val The details child content to inspect.
     * @param ctx The renderer context.
     * @param[out] out The string this content will be appended to.
     */
    void addClosedDetailsContent(
        const QJsonValue &val,
        Renderer::Context &ctx,
        QString &out) const;

    /**
     * @brief Consume stable details IDs in content not rendered while closed.
     *
     * @param val The hidden structured content to inspect.
     * @param ctx The renderer context.
     */
    void skipClosedDetailsContent(
        const QJsonValue &val,
        Renderer::Context &ctx) const;

    /**
     * @brief Render a normal structured element.
     *
     * @param obj The structured content object.
     * @param tag The original structured content tag.
     * @param ctx The renderer context.
     * @param[out] out The string this content will be appended to.
     */
    void addStructuredElement(
        const QJsonObject &obj,
        const QString &tag,
        Renderer::Context &ctx,
        QString &out) const;

    /**
     * @brief Render children with inline-label compatibility fallbacks.
     *
     * @param obj The structured parent object.
     * @param tag The original structured parent tag.
     * @param ctx The renderer context with a child sibling state active.
     * @param[out] out The string this content will be appended to.
     */
    void addCompatibleElementContent(
        const QJsonObject &obj,
        const QString &tag,
        Renderer::Context &ctx,
        QString &out) const;

    /**
     * @brief Render a leading label through Qt-compatible inline boxes.
     *
     * @param content The original children beginning with a label span.
     * @param ctx The renderer context with a child sibling state active.
     * @param[out] out The string this content will be appended to.
     */
    void addInlineLabelContent(
        const QJsonValue &content,
        Renderer::Context &ctx,
        QString &out) const;

    /**
     * @brief Resolve the attributes, styles, and layout for an element.
     *
     * @param obj The structured content object.
     * @param tag The original structured content tag.
     * @param ctx The renderer context.
     * @return The resolved element render state.
     */
    [[nodiscard]]
    ElementRenderState elementRenderState(
        const QJsonObject &obj,
        const QString &tag,
        Renderer::Context &ctx) const;

    /**
     * @brief Allocate a stable open state for one structured details element.
     *
     * @param obj The structured details object.
     * @param ctx The renderer context.
     * @return The details ID and resolved open state.
     */
    [[nodiscard]]
    DetailsState detailsState(
        const QJsonObject &obj,
        Renderer::Context &ctx) const;

    /**
     * @brief Build the internal URL used to toggle a details element.
     *
     * @param details The details state to toggle.
     * @return A QML-handled glossary disclosure URL.
     */
    [[nodiscard]]
    QString detailsLinkHref(const DetailsState &details) const;

    /**
     * @brief Add HTML attributes from a structured content object.
     *
     * @param obj The structured content object.
     * @param[out] state The element render state to update.
     */
    void addElementAttributes(
        const QJsonObject &obj,
        ElementRenderState &state) const;

    /**
     * @brief Resolve matched and inline styles for an element.
     *
     * @param obj The structured content object.
     * @param ctx The renderer context.
     * @param[out] state The element render state to update.
     */
    void resolveElementStyles(
        const QJsonObject &obj,
        Renderer::Context &ctx,
        ElementRenderState &state) const;

    /**
     * @brief Resolve list state and the Qt-compatible output tag.
     *
     * @param ctx The renderer context.
     * @param[out] state The element render state to update.
     */
    void resolveElementLayout(
        Renderer::Context &ctx,
        ElementRenderState &state) const;

    /**
     * @brief Apply Qt rich text compatibility rewrites to an element.
     *
     * @param ctx The renderer context.
     * @param[out] state The element render state to update.
     */
    void applyElementCompatibility(
        const Renderer::Context &ctx,
        ElementRenderState &state) const;

    /**
     * @brief Move CSS box declarations into normalized render state.
     *
     * @param ctx The renderer context.
     * @param[out] state The element render state to update.
     */
    void resolveBoxStyle(
        const Renderer::Context &ctx,
        ElementRenderState &state) const;

    /**
     * @brief Select vertical margin behavior after box normalization.
     *
     * @param state The resolved element render state.
     * @return The margin flow for the element.
     */
    [[nodiscard]]
    MarginFlow marginFlow(const ElementRenderState &state) const;

    /**
     * @brief Add the opening HTML for a structured element.
     *
     * @param state The element render state.
     * @param ctx The renderer context.
     * @param[out] out The string this HTML will be appended to.
     */
    void addStructuredElementStart(
        const ElementRenderState &state,
        const Renderer::Context &ctx,
        QString &out) const;

    /**
     * @brief Add the closing HTML for a structured element.
     *
     * @param state The element render state.
     * @param ctx The renderer context.
     * @param[out] out The string this HTML will be appended to.
     */
    void addStructuredElementEnd(
        const ElementRenderState &state,
        const Renderer::Context &ctx,
        QString &out) const;

    /**
     * @brief Add a zero-width selection sentinel to generated rich text.
     *
     * @param sentinel The sentinel character to add.
     * @param[out] out The string this HTML will be appended to.
     */
    void addSelectionSentinel(char16_t sentinel, QString &out) const;

    /**
     * @brief Add the opening table frame for a normalized CSS box.
     *
     * @param state The resolved element render state.
     * @param ctx The renderer context.
     * @param[out] out The string this HTML will be appended to.
     */
    void addBoxStart(
        const ElementRenderState &state,
        const Renderer::Context &ctx,
        QString &out) const;

    /**
     * @brief Close the table frame for a normalized CSS box.
     *
     * @param state The resolved element render state.
     * @param ctx The renderer context.
     * @param[out] out The string this HTML will be appended to.
     */
    void addBoxEnd(
        const ElementRenderState &state,
        const Renderer::Context &ctx,
        QString &out) const;

    /**
     * @brief Add an inline spacer supported by Qt rich text.
     *
     * @param spacing The CSS spacing value.
     * @param fontPixelSize The element font size used for relative spacing.
     * @param ctx The renderer context.
     * @param[out] out The string this spacer will be appended to.
     */
    void addInlineSpacer(
        const QString &spacing,
        double fontPixelSize,
        const Renderer::Context &ctx,
        QString &out) const;

    /**
     * @brief Add a vertical spacer supported by Qt rich text.
     *
     * @param spacing The CSS spacing value.
     * @param fontPixelSize The element font size used for relative spacing.
     * @param ctx The renderer context.
     * @param[out] out The string this spacer will be appended to.
     */
    void addVerticalSpacer(
        const QString &spacing,
        double fontPixelSize,
        const Renderer::Context &ctx,
        QString &out) const;

    /**
     * @brief Emit a vertical spacer with a deterministic rendered height.
     *
     * @param pixelSize The signed spacer height in pixels.
     * @param[out] out The string this spacer will be appended to.
     */
    void addVerticalPixelSpacer(double pixelSize, QString &out) const;

    /**
     * @brief Add a margin to a pending CSS margin-collapse group.
     *
     * @param spacing The CSS margin value.
     * @param fontPixelSize The element font size used for relative spacing.
     * @param ctx The renderer context.
     * @param[out] siblings The sibling state receiving the margin.
     */
    void addPendingVerticalMargin(
        const QString &spacing,
        double fontPixelSize,
        const Renderer::Context &ctx,
        SiblingState &siblings) const;

    /**
     * @brief Merge one pending CSS margin-collapse group into another.
     *
     * @param source The pending margins to merge.
     * @param[out] destination The sibling state receiving the margins.
     */
    void mergePendingVerticalMargins(
        const SiblingState &source,
        SiblingState &destination) const;

    /**
     * @brief Emit and clear the current pending collapsed margin.
     *
     * @param ctx The renderer context.
     * @param[out] out The string this spacer will be appended to.
     */
    void flushPendingVerticalMargin(
        Renderer::Context &ctx,
        QString &out) const;

    /**
     * @brief Clear pending collapsed margins from a sibling state.
     *
     * @param[out] siblings The sibling state to clear.
     */
    void clearPendingVerticalMargins(SiblingState &siblings) const;

    /**
     * @brief Get one side from a one-to-four-value CSS box shorthand.
     *
     * @param value The CSS shorthand value.
     * @param side The side index in top, right, bottom, left order.
     * @return The value for the requested side, or an empty string.
     */
    [[nodiscard]]
    QString cssBoxSideValue(const QString &value, qsizetype side) const;

    /**
     * @brief Check whether a CSS spacing value is zero.
     *
     * @param spacing The CSS spacing value.
     * @return true if the value represents zero spacing.
     */
    [[nodiscard]]
    bool isZeroSpacing(const QString &spacing) const;

    /**
     * @brief Add a ruby structured content object.
     *
     * @param obj The ruby object to add.
     * @param ctx The renderer context.
     * @param[out] out The string this content will be appended to.
     */
    void addRuby(
        const QJsonObject &obj,
        Renderer::Context &ctx,
        QString &out) const;

    /**
     * @brief Split ruby content into visible base text and hidden reading.
     *
     * @param val The ruby content value.
     * @param[out] base The visible ruby base content.
     * @param[out] reading The hidden ruby reading.
     */
    void splitRubyContent(
        const QJsonValue &val,
        QJsonArray &base,
        QString &reading) const;

    /**
     * @brief Convert structured content to plain text.
     *
     * @param val The structured content value.
     * @return The plain text content.
     */
    [[nodiscard]]
    QString structuredContentText(const QJsonValue &val) const;

    /**
     * @brief Check if structured content contains a ruby tag.
     *
     * @param val The structured content value.
     * @return true if ruby content exists.
     */
    [[nodiscard]]
    bool containsRuby(const QJsonValue &val) const;

    /**
     * @brief Check if structured content contains raised inline text.
     *
     * @param val The structured content value.
     * @return true if a descendant uses super or sub vertical alignment.
     */
    [[nodiscard]]
    bool containsRaisedContent(const QJsonValue &val) const;

    /**
     * @brief Check if an anchor needs custom fragment rendering.
     *
     * @param obj The structured content anchor object.
     * @param ctx The renderer context.
     * @return true if the anchor should be split into generated fragments.
     */
    [[nodiscard]]
    bool anchorNeedsCustomHandling(
        const QJsonObject &obj,
        const Renderer::Context &ctx) const;

    /**
     * @brief Build an internal href carrying optional target and tooltip data.
     *
     * @param target The normal link target.
     * @param tooltip The tooltip text.
     * @param tooltipText The visible text covered by the tooltip.
     * @param tooltipType The type of tooltip to display.
     * @return The internal href.
     */
    [[nodiscard]]
    QString internalLinkHref(
        const QString &target,
        const QString &tooltip,
        const QString &tooltipText,
        const QString &tooltipType) const;

    /**
     * @brief Add an anchor tag.
     *
     * @param href The link href.
     * @param color The concrete text color used to suppress default link
     * styling.
     * @param suppressDecoration True to omit the fragment underline.
     * @param[out] out The string this anchor will be appended to.
     */
    void addAnchorStart(
        const QString &href,
        const QString &color,
        bool suppressDecoration,
        QString &out) const;

    /**
     * @brief Select the rendered source URL for a structured image.
     *
     * Monochrome dictionary images use the image provider so their glyph color
     * follows the surrounding glossary text. Other resources keep file URLs.
     *
     * @param obj The image structured-content object.
     * @param ctx The renderer context.
     * @return The unescaped source URL for the image.
     */
    [[nodiscard]]
    QString structuredImageSource(
        const QJsonObject &obj,
        const Renderer::Context &ctx) const;

    /**
     * @brief Parses and outputs structured content to HTML.
     *
     * @param val The JSON value of the structured content.
     * @param ctx The renderer context.
     * @param[out] out The string this content will be appended to.
     */
    void addStructuredContent(
        const QJsonValue &val,
        Renderer::Context &ctx,
        QString &out) const;

    /**
     * @brief Add an image type object.
     *
     * @param obj The image object.
     * @param ctx The renderer context.
     * @param[out] out The string this image will be appended to.
     */
    void addImage(
        const QJsonObject &obj,
        Renderer::Context &ctx,
        QString &out) const;

    /**
     * @brief Add a text object to the HTML document.
     *
     * @param obj The text object.
     * @param[out] out The string the formatted text will be appended to.
     */
    void addText(const QJsonObject &obj, QString &out) const;

    /**
     * @brief Check whether an array contains structured content.
     *
     * @param content The array of content.
     * @return True if the array contains structured content, false otherwise.
     */
    [[nodiscard]]
    bool containsStructuredContent(const QJsonArray &content) const;

    /**
     * @brief Escape a string for use in HTML.
     *
     * @param str The string to escape.
     * @return An HTML escaped string.
     */
    [[nodiscard]]
    QString escapeHtml(const QString &str) const;

    /**
     * @brief Add a CSS declaration to a declaration map.
     *
     * @param property The CSS property.
     * @param value The CSS value.
     * @param[out] declarations The declaration map to append to.
     */
    void addCssDeclaration(
        const QString &property,
        const QString &value,
        CssDeclarations &declarations) const;

    /**
     * @brief Resolve a CSS value before adding it to a declaration map.
     *
     * @param property The CSS property.
     * @param value The unresolved CSS value.
     * @param ctx The renderer context.
     * @param[out] declarations The declaration map to append to.
     */
    void addResolvedCssDeclaration(
        const QString &property,
        const QString &value,
        Renderer::Context &ctx,
        CssDeclarations &declarations) const;

    /**
     * @brief Add declarations to a CSS style attribute.
     *
     * @param declarations The declarations to output.
     * @param[out] out The string to append CSS to.
     */
    void addCssDeclarations(
        const CssDeclarations &declarations, QString &out) const;

    /**
     * @brief Resolve browser CSS values to values supported by Qt rich text.
     *
     * @param value The browser CSS value.
     * @param ctx The renderer context.
     * @return A Qt-compatible value, or an empty string if unsupported.
     */
    [[nodiscard]]
    QString resolveCssValue(
        const QString &value,
        Renderer::Context &ctx) const;

    /**
     * @brief Resolve CSS variable references in a declaration value.
     *
     * @param value The declaration value.
     * @param ctx The renderer context.
     * @return The value with supported variables replaced.
     */
    [[nodiscard]]
    QString resolveCssVariables(
        QString value,
        const Renderer::Context &ctx) const;

    /**
     * @brief Resolve supported calc functions embedded in a CSS value.
     *
     * @param value The declaration value.
     * @return The value with calculations replaced, or an empty string if
     * unsupported.
     */
    [[nodiscard]]
    QString resolveCssCalculations(QString value) const;

    /**
     * @brief Resolve a color-mix function to a concrete color.
     *
     * @param value The color-mix value.
     * @return The mixed color, or an empty string if invalid.
     */
    [[nodiscard]]
    QString resolveColorMix(const QString &value) const;

    /**
     * @brief Resolve a simple calc division to a concrete length.
     *
     * @param value The calc value.
     * @return The calculated length, or an empty string if unsupported.
     */
    [[nodiscard]]
    QString resolveCssCalc(const QString &value) const;

    /**
     * @brief Select a solid-color fallback from a CSS gradient.
     *
     * @param value The gradient value.
     * @return The fallback color, or an empty string if unsupported.
     */
    [[nodiscard]]
    QString cssGradientFallback(const QString &value) const;

    /**
     * @brief Split a CSS function argument list at top-level commas.
     *
     * @param arguments The function argument text.
     * @return The trimmed function arguments.
     */
    [[nodiscard]]
    QStringList splitCssArguments(const QString &arguments) const;

    /**
     * @brief Normalize a list marker value.
     *
     * @param marker The CSS list marker value.
     * @return The normalized marker.
     */
    [[nodiscard]]
    QString normalizeListMarker(QString marker) const;

    /**
     * @brief Get the default marker for a list tag.
     *
     * @param tag The list tag.
     * @return The default marker type.
     */
    [[nodiscard]]
    QString defaultListMarker(const QString &tag) const;

    /**
     * @brief Get a rendered marker for a list item.
     *
     * @param list The list state.
     * @param marker The normalized item marker override.
     * @return The marker text.
     */
    [[nodiscard]]
    QString listMarker(const StructuredList &list, const QString &marker) const;

    /**
     * @brief Check if the list marker is a built-in marker.
     *
     * @param marker The list marker type.
     * @return true if the marker should be formatted from the item index.
     */
    [[nodiscard]]
    bool isBuiltInListMarker(const QString &marker) const;

    /**
     * @brief Apply matching stylesheet rules to a declaration map.
     *
     * @param ctx The renderer context.
     * @param[out] declarations The declaration map to update.
     * @param[out] beforeContent Generated ::before content, when requested.
     */
    void addMatchingCssRules(
        Renderer::Context &ctx,
        CssDeclarations &declarations,
        QString *beforeContent = nullptr,
        QString *afterContent = nullptr) const;

    /**
     * @brief Check if a rule matches the current element stack.
     *
     * @param rule The CSS rule to check.
     * @param ctx The current render context and selector storage.
     * @return true if the rule matches, false otherwise.
     */
    [[nodiscard]]
    bool cssRuleMatches(
        const CssRule &rule,
        const Context &ctx) const;

    /**
     * @brief Match a selector recursively from a candidate element.
     *
     * @param rule The CSS rule being matched.
     * @param ctx The current render context and selector storage.
     * @param partIndex The selector part being matched.
     * @param stackIndex The candidate element's depth in the stack.
     * @param selectorElementIndex The candidate element's index in
     * selectorElements.
     * @return true if this part and all preceding parts match.
     */
    [[nodiscard]]
    bool cssRuleMatchesAt(
        const CssRule &rule,
        const Context &ctx,
        qsizetype partIndex,
        qsizetype stackIndex,
        qsizetype selectorElementIndex) const;

    /**
     * @brief Check if a selector part matches an element.
     *
     * @param part The selector part to check.
     * @param element The element to check.
     * @return true if the selector part matches, false otherwise.
     */
    [[nodiscard]]
    bool cssSelectorPartMatches(
        const CssSelectorPart &part,
        const StructuredElement &element) const;

    /**
     * @brief Convert structured data to element attributes.
     *
     * @param obj The structured content object.
     * @param ctx The renderer context.
     * @return The index of the structured element in selectorElements.
     */
    [[nodiscard]]
    qsizetype structuredElement(
        const QJsonObject &obj,
        Renderer::Context &ctx,
        bool detailsOpen = false) const;

    /**
     * @brief Convert a structured data key to an HTML data attribute name.
     *
     * @param key The structured data key.
     * @return The HTML data attribute name.
     */
    [[nodiscard]]
    QString structuredDataAttributeName(const QString &key) const;

    /**
     * @brief Check if a structured content tag is supported.
     *
     * @param tag The structured content tag to check.
     * @return true if the tag is supported, false otherwise.
     */
    [[nodiscard]]
    bool isSupportedStructuredTag(const QString &tag) const;

    /**
     * @brief Convert a CSS font-size value to pixels.
     *
     * @param size The CSS font-size value.
     * @param screen The current screen.
     * @param font The font that relative CSS values are based on.
     * @return The converted size in pixels, or a negative value if unsupported.
     */
    [[nodiscard]]
    double cssFontSizeToPixels(
        const QString &size, const QScreen *screen, const QFont &font) const;

    /**
     * @brief Convert a CSS font-size value to pixels.
     *
     * @param size The CSS font-size value.
     * @param screen The current screen.
     * @param parentFontPixelSize The inherited font size in pixels.
     * @param rootFontPixelSize The root font size in pixels.
     * @return The converted size in pixels, or a negative value if unsupported.
     */
    [[nodiscard]]
    double cssFontSizeToPixels(
        const QString &size,
        const QScreen *screen,
        double parentFontPixelSize,
        double rootFontPixelSize) const;

    /**
     * @brief Get the pixel size for a QFont.
     *
     * @param font The font to measure.
     * @param screen The current screen.
     * @return The font size in pixels.
     */
    [[nodiscard]]
    double fontPixelSize(const QFont &font, const QScreen *screen) const;

    /**
     * @brief Get the current screen DPI used for font conversion.
     *
     * @return The current logical screen DPI, or 96 if unavailable.
     */
    [[nodiscard]]
    double screenDpi(const QScreen *screen) const;

    /**
     * @brief Format a pixel size for CSS output.
     *
     * @param size The pixel size.
     * @return The formatted size.
     */
    [[nodiscard]]
    QString formatPixelSize(double size) const;

    /* Set of supported structured content tags */
    const QSet<QString> m_supportedTags = {
        "br", "ruby", "rp", "rt", "table", "thead", "tbody", "tfoot", "tr",
        "td", "th", "span", "div", "ol", "ul", "li", "details", "summary",
        "img", "a"
    };

    /* Set of supported CSS properties */
    const QSet<QString> m_supportedCssProperties = {
        "background-color", "border", "border-bottom",
        "border-bottom-color", "border-bottom-style",
        "border-bottom-width", "border-collapse", "border-color",
        "border-left", "border-left-color", "border-left-style",
        "border-left-width", "border-radius", "border-right",
        "border-right-color", "border-right-style",
        "border-right-width", "border-style", "border-top", "display",
        "border-top-color", "border-top-style", "border-top-width",
        "border-width", "color", "cursor", "float", "font",
        "font-family", "font-kerning", "font-size", "font-style",
        "font-variant", "font-weight", "image-rendering",
        "line-height", "list-style-type", "margin-bottom",
        "margin-left", "margin-right", "margin-top", "padding",
        "padding-bottom", "padding-left", "padding-right",
        "padding-top", "text-align", "text-decoration",
        "text-indent", "text-transform", "vertical-align",
        "white-space", "width", "word-break", "word-spacing"
    };

    /* Matches CSS function names */
    const QRegularExpression m_cssFunctionRegex{
        "\\b([A-Za-z-]+)\\s*\\("
    };

    /* CSS functions that are supported by Qt rich text */
    const QSet<QString> m_supportedCssFunctions = {
        "hsl", "hsla", "rgb", "rgba"
    };
};

}
