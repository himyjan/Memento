import QtQuick
import QtQuick.Controls
import Ripose.Memento

SearchableText {
    id: root

    enum TooltipType {
        None,
        Ruby,
        Title
    }

    required property TermDefinition definition
    required property color canvasColor

    /* State overrides for interactive structured <details> elements. */
    property var detailStates: ({})

    /* Overlay used to place the tooltip above the application content. */
    readonly property var tooltipOverlay: Overlay.overlay

    /* Tooltip parent, with this text item as a safe creation-time fallback. */
    readonly property Item tooltipParent: root.tooltipOverlay || root

    /* Internal link prefix emitted for structured dictionary anchors. */
    readonly property string glossaryLinkPrefix: "memento://glossary-link?"

    /* Internal link prefix emitted for structured <details> summaries. */
    readonly property string glossaryDetailsPrefix:
        "memento://glossary-details?"

    /* Delay before showing a title tooltip after a link hover. */
    readonly property int titleTooltipDelayMs: 500

    /* Delay used when a tooltip is shown by this component. */
    readonly property int tooltipImmediateDelayMs: 0

    /* Space between a tooltip and its text anchor. */
    readonly property real tooltipAnchorGap: 4

    /* Minimum distance between a tooltip and an Overlay edge. */
    readonly property real tooltipEdgeMargin: 4

    /* Qt's private rich-text sentinel range start code point. */
    readonly property int qtDocumentSentinelStartCode: 0xFDD0

    /* Qt's private rich-text sentinel range end code point. */
    readonly property int qtDocumentSentinelEndCode: 0xFDEF

    /* Qt's inline object replacement character code point. */
    readonly property int qtObjectReplacementCharacterCode: 0xFFFC

    /* Sentinel marking the start of an emitted list item. */
    readonly property string listItemSentinel: "\u2060"

    /* Sentinel pair surrounding an emitted list marker. */
    readonly property string listMarkerStartSentinel: "\u2061"
    readonly property string listMarkerEndSentinel: "\u2062"

    /* Sentinel pair surrounding generated disclosure controls. */
    readonly property string detailsControlStartSentinel: "\u2063"
    readonly property string detailsControlEndSentinel: "\u2064"

    /* Sentinel pair surrounding a synthetic inline table-cell boundary. */
    readonly property string inlineCellGapStartSentinel: "\u2065"
    readonly property string inlineCellGapEndSentinel: "\u206A"

    /* Characters normalized while exporting selected rich text. */
    readonly property string paragraphSeparator: "\u2029"
    readonly property string noBreakSpace: "\u00A0"
    readonly property string selectionLineBreak: "\n"

    readonly property string ankiSelectedText:
        root.cleanSelectedText(root.selectedText)

    signal searchRequested(query: string)

    /* Current type of tooltip */
    property int tooltipType: GlossaryText.TooltipType.None

    /* Raw link text of the tooltip */
    property string tooltipLink: ""

    /* Text of the current tooltip */
    property string tooltip: ""

    /* Text the current tooltip is attached to */
    property string tooltipText: ""

    /* Rectangle used for positioning ruby tool tips */
    property rect rubyTooltipRect: Qt.rect(0, 0, 0, 0)

    /* Position used for title tooltips */
    property point titleTooltipPosition: Qt.point(0, 0)

    /* true if the tooltip is visible, false otherwise */
    property bool tooltipVisible: false

    /**
     * Parse a query string from a link into a map.
     * @param link The link to parse.
     * @return The decoded link arguments.
     */
    function linkArgs(link) {
        const queryIndex = link.indexOf("?");
        if (queryIndex < 0)
        {
            return {};
        }

        let args = {};
        let pairs = link.substring(queryIndex + 1).split("&");
        for (let i = 0; i < pairs.length; ++i)
        {
            const pair = pairs[i];
            const separator = pair.indexOf("=");
            if (separator <= 0)
            {
                continue;
            }
            try
            {
                args[decodeURIComponent(pair.substring(0, separator))] =
                        decodeURIComponent(pair.substring(separator + 1));
            }
            catch (error)
            {
                console.warn("Could not decode glossary link argument:", error);
            }
        }
        return args;
    }

    /**
     * Toggle a structured <details> state without mutating the bound map.
     * @param args The decoded details link arguments.
     */
    function toggleDetailState(args) {
        const detailId = args.id ? args.id : "";
        if (detailId.length === 0)
        {
            return;
        }

        const states = {};
        for (const stateId in root.detailStates)
        {
            states[stateId] = root.detailStates[stateId];
        }
        states[detailId] = args.open !== "1";
        root.detailStates = states;
    }

    /**
     * Discard disclosure overrides before rendering a different definition.
     */
    function resetDetailStates() {
        root.detailStates = ({});
    }

    /**
     * Convert a tooltip type name to the enum value.
     * @param tooltipType The tooltip type name.
     * @return The tooltip type enum value.
     */
    function toTooltipType(tooltipType) {
        switch (tooltipType)
        {
        case "ruby":
            return GlossaryText.TooltipType.Ruby;

        case "title":
            return GlossaryText.TooltipType.Title;

        default:
            return GlossaryText.TooltipType.None;
        }
    }

    /**
     * Open a link from the glossary.
     * @param link The link to open.
     */
    function openLink(link) {
        if (link.startsWith(root.glossaryDetailsPrefix))
        {
            root.toggleDetailState(root.linkArgs(link));
            return;
        }

        if (link.startsWith(root.glossaryLinkPrefix))
        {
            const args = root.linkArgs(link);
            if (args.target)
            {
                root.openLink(args.target);
            }
            return;
        }

        if (link.startsWith("?"))
        {
            let query = "";

            const args = root.linkArgs(link);
            if (args.query)
            {
                query = args.query;
            }
            if (query.length > 0)
            {
                /* Hide the tooltip so it doesn't linger on transitions */
                root.tooltipVisible = false;

                root.searchRequested(query);
            }
        }
        else
        {
            Qt.openUrlExternally(link);
        }
    }

    /**
     * Update the ruby tooltip position to cover the full ruby base text.
     * @param text The ruby base text.
     */
    function updateRubyTooltipPosition(text) {
        if (root.hoverIndex < 0 || text.length <= 0)
        {
            root.rubyTooltipRect = Qt.rect(0, 0, 0, 0);
            return;
        }

        const start = root.plainText.lastIndexOf(
            text,
            Math.max(0, root.hoverIndex)
        );
        if (start < 0 || root.hoverIndex >= start + text.length)
        {
            root.rubyTooltipRect = root.positionToRectangle(root.hoverIndex);
            return;
        }

        const startRect = root.positionToRectangle(start);
        const endRect = root.positionToRectangle(start + text.length);
        if (Math.abs(startRect.y - endRect.y) > startRect.height)
        {
            root.rubyTooltipRect = startRect;
            return;
        }

        root.rubyTooltipRect = Qt.rect(
            startRect.x,
            startRect.y,
            Math.max(startRect.width, endRect.x - startRect.x),
            startRect.height
        );
    }

    /**
     * Map the active tooltip anchor into its current parent coordinates.
     * @return The anchor rectangle in tooltip-parent coordinates.
     */
    function tooltipAnchorRect() {
        if (root.tooltipType === GlossaryText.TooltipType.Ruby)
        {
            return root.mapToItem(root.tooltipParent, root.rubyTooltipRect);
        }

        const titleRect = Qt.rect(
            root.titleTooltipPosition.x,
            root.titleTooltipPosition.y,
            0,
            0
        );
        return root.mapToItem(root.tooltipParent, titleRect);
    }

    /**
     * Return the width available to a tooltip within its current parent.
     * @return The capped tooltip width.
     */
    function tooltipWidth() {
        const availableWidth = Math.max(
            0,
            root.tooltipParent.width - root.tooltipEdgeMargin * 2
        );
        return Math.min(glossaryToolTip.implicitWidth, availableWidth);
    }

    /**
     * Return the current tooltip height after its width is resolved.
     * @return The tooltip height.
     */
    function tooltipHeight() {
        return Math.max(
            glossaryToolTip.implicitHeight,
            glossaryToolTip.height
        );
    }

    /**
     * Return the x-coordinate for the current glossary tooltip.
     * @return The clamped tooltip x-coordinate in parent coordinates.
     */
    function tooltipX() {
        const anchor = root.tooltipAnchorRect();
        const width = root.tooltipWidth();
        const preferredX = anchor.x + (anchor.width - width) / 2;
        const maximumX = Math.max(
            root.tooltipEdgeMargin,
            root.tooltipParent.width - width - root.tooltipEdgeMargin
        );
        return Math.max(
            root.tooltipEdgeMargin,
            Math.min(preferredX, maximumX)
        );
    }

    /**
     * Return the y-coordinate for the current glossary tooltip.
     * @return The clamped tooltip y-coordinate in parent coordinates.
     */
    function tooltipY() {
        const anchor = root.tooltipAnchorRect();
        const height = root.tooltipHeight();
        const aboveY = anchor.y - height - root.tooltipAnchorGap;
        if (aboveY >= root.tooltipEdgeMargin)
        {
            return aboveY;
        }

        const belowY = anchor.y + anchor.height + root.tooltipAnchorGap;
        const maximumY = Math.max(
            root.tooltipEdgeMargin,
            root.tooltipParent.height - height - root.tooltipEdgeMargin
        );
        return Math.max(
            root.tooltipEdgeMargin,
            Math.min(belowY, maximumY)
        );
    }

    /**
     * Return true if a character is a Qt rich-text document sentinel.
     * @param ch The character to check.
     * @return true if the character should not be exported.
     */
    function isQtDocumentSentinel(ch) {
        const code = ch.charCodeAt(0);
        return (code >= root.qtDocumentSentinelStartCode &&
                code <= root.qtDocumentSentinelEndCode) ||
                code === root.qtObjectReplacementCharacterCode;
    }

    /**
     * Append one normalized line break unless the output already ends in one.
     * @param out The generated selection text characters.
     */
    function appendSelectionLineBreak(out) {
        if (out.length > 0 &&
            out[out.length - 1] !== root.selectionLineBreak)
        {
            out.push(root.selectionLineBreak);
        }
    }

    /**
     * Remove generated glossary controls and hidden rich-text sentinels.
     * @param text The raw selected text from TextEdit.
     * @return Text suitable for Anki marker output.
     */
    function cleanSelectedText(text) {
        let out = [];
        let boundary = 0;
        /* Rewind a selection that starts inside a disclosure control. */
        let detailsControlBoundary = 0;
        let inListMarker = false;
        let inDetailsControl = false;
        let inInlineCellGap = false;
        let inGeneratedListItem = false;
        let generatedListItemHasText = false;
        let discardSpacerAfterGeneratedListBoundary = false;

        for (let i = 0; i < text.length; ++i)
        {
            const ch = text.charAt(i);
            if (ch === root.inlineCellGapStartSentinel)
            {
                inInlineCellGap = true;
                continue;
            }
            if (ch === root.inlineCellGapEndSentinel)
            {
                inInlineCellGap = false;
                continue;
            }
            if (inInlineCellGap &&
                (root.isQtDocumentSentinel(ch) ||
                 ch === root.paragraphSeparator ||
                 ch === root.selectionLineBreak))
            {
                continue;
            }
            if (root.isQtDocumentSentinel(ch))
            {
                if (inGeneratedListItem && generatedListItemHasText)
                {
                    root.appendSelectionLineBreak(out);
                    boundary = out.length;
                    inGeneratedListItem = false;
                    generatedListItemHasText = false;
                    discardSpacerAfterGeneratedListBoundary = true;
                }
                continue;
            }
            if (ch === root.detailsControlStartSentinel)
            {
                detailsControlBoundary = out.length;
                inDetailsControl = true;
                continue;
            }
            if (ch === root.detailsControlEndSentinel)
            {
                if (inDetailsControl)
                {
                    inDetailsControl = false;
                }
                else
                {
                    out.length = detailsControlBoundary;
                }
                continue;
            }
            if (inDetailsControl)
            {
                continue;
            }
            if (discardSpacerAfterGeneratedListBoundary)
            {
                if (ch === root.noBreakSpace || ch === " " || ch === "\t")
                {
                    continue;
                }
                discardSpacerAfterGeneratedListBoundary = false;
            }
            if (ch === root.listMarkerStartSentinel)
            {
                inListMarker = true;
                continue;
            }
            if (ch === root.listMarkerEndSentinel)
            {
                if (inListMarker)
                {
                    inListMarker = false;
                }
                else
                {
                    out.length = boundary;
                }
                continue;
            }
            if (inListMarker)
            {
                continue;
            }
            if (ch === root.listItemSentinel)
            {
                root.appendSelectionLineBreak(out);
                boundary = out.length;
                inGeneratedListItem = true;
                generatedListItemHasText = false;
                continue;
            }
            out.push(
                ch === root.paragraphSeparator ? root.selectionLineBreak : ch
            );
            generatedListItemHasText = inGeneratedListItem;
        }

        return out.join("")
                  .replace(/[ \t]*\n[ \t]*/g, root.selectionLineBreak)
                  .replace(/\n\n+/g, root.selectionLineBreak)
                  .trim();
    }

    /**
     * Copy the cleaned Anki selection text to the clipboard.
     */
    function copyAnkiSelection() {
        if (root.ankiSelectedText.length > 0)
        {
            clipboard.setText(root.ankiSelectedText);
        }
    }

    cursorShape: Qt.IBeamCursor
    textFormat: TextEdit.RichText
    wrapMode: TextEdit.Wrap
    selectByMouse: true
    selectByKeyboard: true

    font.family: MementoSettings.interfaceSearchGlossaryFont.family
    font.italic: MementoSettings.interfaceSearchGlossaryFont.italic
    font.underline: MementoSettings.interfaceSearchGlossaryFont.underline
    font.pointSize: MementoSettings.interfaceSearchGlossaryFont.pointSize
    font.weight: MementoSettings.interfaceSearchGlossaryFont.weight
    font.overline: MementoSettings.interfaceSearchGlossaryFont.overline
    font.strikeout: MementoSettings.interfaceSearchGlossaryFont.strikeout
    font.letterSpacing:
        MementoSettings.interfaceSearchGlossaryFont.letterSpacing
    font.wordSpacing: MementoSettings.interfaceSearchGlossaryFont.wordSpacing
    font.kerning: MementoSettings.interfaceSearchGlossaryFont.kerning
    font.preferShaping:
        MementoSettings.interfaceSearchGlossaryFont.preferShaping
    font.hintingPreference:
        MementoSettings.interfaceSearchGlossaryFont.hintingPreference
    font.styleName: MementoSettings.interfaceSearchGlossaryFont.styleName

    text: StructuredRichText.parse(
              root.definition?.dictionaryInfo,
              root.definition?.glossary ?? [],
              MementoSettings.searchGlossaryStyle,
              root,
              root.font,
              root.color,
              root.canvasColor,
              root.detailStates)

    onDefinitionChanged: root.resetDetailStates()

    onLinkActivated: function(link) {
        root.openLink(link);
    }

    onLinkHovered: function(link) {
        if (link.startsWith(root.glossaryLinkPrefix))
        {
            const args = root.linkArgs(link);
            const linkChanged = root.tooltipLink !== link;

            root.tooltip = args.tooltip ? args.tooltip : "";
            root.tooltipText = args.tooltipText ? args.tooltipText : "";
            root.tooltipType = root.toTooltipType(args.tooltipType);

            if (root.tooltip.length > 0 &&
                root.tooltipType === GlossaryText.TooltipType.Title)
            {
                if (linkChanged)
                {
                    titleTooltipTimer.stop();
                    root.tooltipVisible = false;
                    titleTooltipTimer.start();
                }
                else if (!root.tooltipVisible &&
                         !titleTooltipTimer.running)
                {
                    titleTooltipTimer.start();
                }
            }
            else if (root.tooltip.length > 0 &&
                root.tooltipType === GlossaryText.TooltipType.Ruby)
            {
                titleTooltipTimer.stop();
                root.tooltipVisible = true;
                root.updateRubyTooltipPosition(root.tooltipText);
            }

            root.tooltipLink = link;
        }
        else
        {
            titleTooltipTimer.stop();
            root.tooltip = "";
            root.tooltipText = "";
            root.tooltipType = GlossaryText.TooltipType.None;
            root.tooltipVisible = false;
            root.tooltipLink = "";
        }
    }

    onHoverIndexChanged: {
        if (root.tooltip.length > 0 &&
            root.tooltipType === GlossaryText.TooltipType.Ruby)
        {
            root.updateRubyTooltipPosition(root.tooltipText);
        }
    }

    Keys.onPressed: function(event) {
        if (event.matches(StandardKey.Copy) && root.selectedText.length > 0)
        {
            root.copyAnkiSelection();
            event.accepted = true;
        }
    }

    Clipboard {
        id: clipboard
    }

    Timer {
        id: titleTooltipTimer

        interval: root.titleTooltipDelayMs
        repeat: false
        onTriggered: {
            if (root.tooltip.length > 0 &&
                root.tooltipType === GlossaryText.TooltipType.Title)
            {
                root.titleTooltipPosition = root.mousePosition;
                root.tooltipVisible = true;
            }
        }
    }

    ToolTip {
        id: glossaryToolTip

        parent: root.tooltipParent
        visible: root.visible &&
                 root.tooltip.length > 0 &&
                 root.tooltipVisible
        delay: root.tooltipImmediateDelayMs
        text: root.tooltip
        width: root.tooltipWidth()
        x: root.tooltipX()
        y: root.tooltipY()
    }
}
