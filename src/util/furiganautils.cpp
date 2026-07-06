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

#include "util/furiganautils.h"

#include <algorithm>

#include <QTextBoundaryFinder>

namespace
{

constexpr char16_t KATAKANA_LOW = 0x30A1;
constexpr char16_t KATAKANA_HIGH = 0x30F6;
constexpr char16_t HIRAGANA_KATAKANA_OFFSET = 0x60;
constexpr char16_t KATAKANA_ITERATION_MARK = 0x30FD;
constexpr char16_t KATAKANA_VOICED_ITERATION_MARK = 0x30FE;
constexpr char16_t HIRAGANA_ITERATION_MARK = 0x309D;
constexpr char16_t HIRAGANA_VOICED_ITERATION_MARK = 0x309E;
constexpr qsizetype NO_READING_INDEX = -1;
constexpr qsizetype NO_SOLUTION = 0;
constexpr qsizetype ONE_SOLUTION = 1;
constexpr qsizetype MANY_SOLUTIONS = 2;

/**
 * @brief A grapheme-sized string slice and its normalized comparison text.
 */
struct Unit
{
    /* Start offset in the original string. */
    qsizetype start{0};

    /* End offset in the original string. */
    qsizetype end{0};

    /* Normalized text used for matching. */
    QString normalized;
};

/**
 * @brief A contiguous surface span that is either matched text or furigana
 * text.
 */
struct Segment
{
    /* true if the segment should match the reading directly. */
    bool anchor{false};

    /* Start offset in the original surface string. */
    qsizetype start{0};

    /* End offset in the original surface string. */
    qsizetype end{0};

    /* Normalized surface text used for matching. */
    QString normalized;
};

/**
 * @brief Reading text split into units with offsets into its normalized form.
 */
struct ReadingData
{
    /* Grapheme-sized reading slices with original offsets. */
    QList<Unit> units;

    /* Normalized string offsets for each unit boundary. */
    QList<qsizetype> normalizedOffsets;

    /* Full normalized reading text used for matching. */
    QString normalized;
};

/**
 * @brief DP state for a segment and reading boundary.
 */
struct SolutionState
{
    /* Number of possible suffix solutions, capped at MANY_SOLUTIONS. */
    qsizetype count{NO_SOLUTION};

    /* Next reading boundary to use when count is ONE_SOLUTION. */
    qsizetype nextReadingUnitIndex{NO_READING_INDEX};
};

/**
 * @brief Normalize text for reading comparisons.
 *
 * @param text The text to normalize.
 * @return Text normalized with NFKC and katakana converted to hiragana.
 */
[[nodiscard]]
QString normalizeText(QString text)
{
    text = text.normalized(QString::NormalizationForm_KC);

    for (qsizetype i = 0; i < text.size(); ++i)
    {
        const char16_t code = text[i].unicode();
        if (KATAKANA_LOW <= code && code <= KATAKANA_HIGH)
        {
            text[i] = QChar(code - HIRAGANA_KATAKANA_OFFSET);
        }
        else if (code == KATAKANA_ITERATION_MARK)
        {
            text[i] = QChar(HIRAGANA_ITERATION_MARK);
        }
        else if (code == KATAKANA_VOICED_ITERATION_MARK)
        {
            text[i] = QChar(HIRAGANA_VOICED_ITERATION_MARK);
        }
    }

    return text;
}

/**
 * @brief Split text into grapheme units and normalize each unit.
 *
 * @param text The text to split.
 * @return Units containing original offsets and normalized text.
 */
[[nodiscard]]
QList<Unit> makeUnits(const QString &text)
{
    QList<Unit> units;

    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
    qsizetype start = 0;
    for (
        qsizetype end = finder.toNextBoundary();
        end != -1;
        start = end, end = finder.toNextBoundary()
    )
    {
        units.emplaceBack(Unit{
            start,
            end,
            normalizeText(text.mid(start, end - start)),
        });
    }

    return units;
}

/**
 * @brief Build all matching metadata for the reading string.
 *
 * @param reading The original reading text.
 * @return Reading units, normalized text, and normalized unit offsets.
 */
[[nodiscard]]
ReadingData makeReadingData(const QString &reading)
{
    ReadingData data;
    data.units = makeUnits(reading);
    data.normalizedOffsets.reserve(data.units.size() + 1);
    data.normalizedOffsets.emplaceBack(0);

    for (const Unit &unit : data.units)
    {
        data.normalized.append(unit.normalized);
        data.normalizedOffsets.emplaceBack(data.normalized.size());
    }

    return data;
}

/**
 * @brief Group surface units by whether they can anchor to the reading.
 *
 * @param surfaceUnits Surface text split into normalized units.
 * @param normalizedReading The normalized reading text.
 * @return Contiguous anchor and non-anchor surface segments.
 */
[[nodiscard]]
QList<Segment> makeSegments(
    const QList<Unit> &surfaceUnits,
    const QString &normalizedReading
)
{
    QList<Segment> segments;
    for (const Unit &unit : surfaceUnits)
    {
        const bool anchor =
            !unit.normalized.isEmpty() &&
            normalizedReading.contains(unit.normalized);

        if (!segments.isEmpty() && segments.back().anchor == anchor)
        {
            segments.back().end = unit.end;
            segments.back().normalized.append(unit.normalized);
            continue;
        }

        segments.emplaceBack(Segment{
            anchor,
            unit.start,
            unit.end,
            unit.normalized,
        });
    }

    return segments;
}

/**
 * @brief Convert a reading unit index to an offset in the original reading.
 *
 * @param reading The original reading text.
 * @param readingUnits Reading units with original offsets.
 * @param unitIndex The unit boundary to convert.
 * @return The original string offset for unitIndex.
 */
[[nodiscard]]
qsizetype readingOffset(
    const QString &reading,
    const QList<Unit> &readingUnits,
    qsizetype unitIndex
)
{
    if (unitIndex >= readingUnits.size())
    {
        return reading.size();
    }
    return readingUnits[unitIndex].start;
}

/**
 * @brief Check whether an anchor matches the reading at a unit boundary.
 *
 * @param reading The prepared reading metadata.
 * @param readingUnitIndex The reading unit boundary to test.
 * @param normalizedAnchor The normalized anchor text to match.
 * @return The ending unit boundary on match, or -1 on failure.
 */
[[nodiscard]]
qsizetype matchAnchorAt(
    const ReadingData &reading,
    qsizetype readingUnitIndex,
    const QString &normalizedAnchor
)
{
    if (
        normalizedAnchor.isEmpty() ||
        readingUnitIndex >= reading.normalizedOffsets.size()
    )
    {
        return -1;
    }

    const qsizetype normalizedStart =
        reading.normalizedOffsets[readingUnitIndex];
    if (
        normalizedStart + normalizedAnchor.size() >
        reading.normalized.size()
    )
    {
        return -1;
    }
    if (
        reading.normalized.mid(
            normalizedStart,
            normalizedAnchor.size()
        ) != normalizedAnchor
    )
    {
        return -1;
    }

    const qsizetype normalizedEnd =
        normalizedStart + normalizedAnchor.size();
    const auto begin = reading.normalizedOffsets.cbegin();
    const auto searchBegin = begin + readingUnitIndex;
    const auto iter = std::lower_bound(
        searchBegin,
        reading.normalizedOffsets.cend(),
        normalizedEnd
    );
    if (iter != reading.normalizedOffsets.cend() && *iter == normalizedEnd)
    {
        return std::distance(begin, iter);
    }

    return -1;
}

/**
 * @brief Append a pair using original surface and reading slices.
 *
 * @param pairs The output pair list to append to.
 * @param surface The original surface text.
 * @param surfaceStart Start offset of the surface slice.
 * @param surfaceEnd End offset of the surface slice.
 * @param reading The original reading text.
 * @param readingStart Start offset of the reading slice.
 * @param readingEnd End offset of the reading slice.
 */
void appendPair(
    QList<FuriganaUtils::Pair> &pairs,
    const QString &surface,
    qsizetype surfaceStart,
    qsizetype surfaceEnd,
    const QString &reading,
    qsizetype readingStart,
    qsizetype readingEnd
)
{
    pairs.emplaceBack(FuriganaUtils::Pair{
        surface.mid(surfaceStart, surfaceEnd - surfaceStart),
        reading.mid(readingStart, readingEnd - readingStart),
    });
}

/**
 * @brief Append a matched surface pair that does not need a reading.
 *
 * @param pairs The output pair list to append to.
 * @param surface The original surface text.
 * @param surfaceStart Start offset of the surface slice.
 * @param surfaceEnd End offset of the surface slice.
 */
void appendAnchorPair(
    QList<FuriganaUtils::Pair> &pairs,
    const QString &surface,
    qsizetype surfaceStart,
    qsizetype surfaceEnd
)
{
    pairs.emplaceBack(FuriganaUtils::Pair{
        surface.mid(surfaceStart, surfaceEnd - surfaceStart),
        "",
    });
}

/**
 * @brief Add solution counts while capping at MANY_SOLUTIONS.
 *
 * @param left The current count.
 * @param right The count to add.
 * @return The sum capped at MANY_SOLUTIONS.
 */
[[nodiscard]]
qsizetype cappedAdd(qsizetype left, qsizetype right)
{
    return std::min(left + right, MANY_SOLUTIONS);
}

/**
 * @brief Precompute anchor transitions for every reading boundary.
 *
 * @param segments Surface segments to align.
 * @param reading Prepared reading metadata.
 * @return Ending reading boundary for each anchor start, or NO_READING_INDEX.
 */
[[nodiscard]]
QList<QList<qsizetype>> makeAnchorEnds(
    const QList<Segment> &segments,
    const ReadingData &reading
)
{
    QList<QList<qsizetype>> anchorEnds(
        segments.size(),
        QList<qsizetype>(reading.units.size() + 1, NO_READING_INDEX)
    );

    for (qsizetype i = 0; i < segments.size(); ++i)
    {
        if (!segments[i].anchor)
        {
            continue;
        }

        for (qsizetype j = 0; j <= reading.units.size(); ++j)
        {
            anchorEnds[i][j] =
                matchAnchorAt(reading, j, segments[i].normalized);
        }
    }

    return anchorEnds;
}

/**
 * @brief Return the unsimplified fallback pair.
 *
 * @param surface The original surface text.
 * @param reading The original reading text.
 * @return A single pair containing surface and reading.
 */
[[nodiscard]]
QList<FuriganaUtils::Pair> fallbackResult(
    const QString &surface,
    const QString &reading
)
{
    return { FuriganaUtils::Pair{ surface, reading } };
}

/**
 * @brief Build the solution-count DP table for all segment suffixes.
 *
 * @param segments Surface segments to align.
 * @param reading Prepared reading metadata.
 * @param anchorEnds Precomputed anchor transitions.
 * @return DP state for every segment index and reading boundary.
 */
[[nodiscard]]
QList<QList<SolutionState>> makeSolutionTable(
    const QList<Segment> &segments,
    const ReadingData &reading,
    const QList<QList<qsizetype>> &anchorEnds
)
{
    QList<QList<SolutionState>> table(
        segments.size() + 1,
        QList<SolutionState>(reading.units.size() + 1)
    );
    QList<QList<qsizetype>> suffixCounts(
        segments.size() + 1,
        QList<qsizetype>(reading.units.size() + 2, NO_SOLUTION)
    );
    QList<QList<qsizetype>> suffixFirstIndex(
        segments.size() + 1,
        QList<qsizetype>(reading.units.size() + 2, NO_READING_INDEX)
    );

    table[segments.size()][reading.units.size()].count = ONE_SOLUTION;
    for (qsizetype j = reading.units.size(); j >= 0; --j)
    {
        suffixCounts[segments.size()][j] = ONE_SOLUTION;
        suffixFirstIndex[segments.size()][j] = reading.units.size();
    }

    for (qsizetype i = segments.size() - 1; i >= 0; --i)
    {
        const Segment &segment = segments[i];
        for (qsizetype j = reading.units.size(); j >= 0; --j)
        {
            if (segment.anchor)
            {
                const qsizetype endUnitIndex = anchorEnds[i][j];
                if (endUnitIndex != NO_READING_INDEX)
                {
                    table[i][j].count = table[i + 1][endUnitIndex].count;
                    if (table[i][j].count == ONE_SOLUTION)
                    {
                        table[i][j].nextReadingUnitIndex = endUnitIndex;
                    }
                }
                continue;
            }

            table[i][j].count = suffixCounts[i + 1][j + 1];
            if (table[i][j].count == ONE_SOLUTION)
            {
                table[i][j].nextReadingUnitIndex =
                    suffixFirstIndex[i + 1][j + 1];
            }
        }

        for (qsizetype j = reading.units.size(); j >= 0; --j)
        {
            suffixCounts[i][j] =
                cappedAdd(table[i][j].count, suffixCounts[i][j + 1]);
            suffixFirstIndex[i][j] =
                table[i][j].count > NO_SOLUTION ?
                    j :
                    suffixFirstIndex[i][j + 1];
        }
    }

    return table;
}

/**
 * @brief Reconstruct the unique alignment represented by the DP table.
 *
 * @param surface The original surface text.
 * @param readingText The original reading text.
 * @param segments Surface segments to align.
 * @param reading Prepared reading metadata.
 * @param table The solution-count DP table.
 * @return The unique furigana pair list.
 */
[[nodiscard]]
QList<FuriganaUtils::Pair> makeSolution(
    const QString &surface,
    const QString &readingText,
    const QList<Segment> &segments,
    const ReadingData &reading,
    const QList<QList<SolutionState>> &table
)
{
    QList<FuriganaUtils::Pair> solution;
    qsizetype readingUnitIndex = 0;
    for (qsizetype i = 0; i < segments.size(); ++i)
    {
        const SolutionState &state = table[i][readingUnitIndex];
        const Segment &segment = segments[i];
        if (segment.anchor)
        {
            appendAnchorPair(solution, surface, segment.start, segment.end);
        }
        else
        {
            appendPair(
                solution,
                surface,
                segment.start,
                segment.end,
                readingText,
                readingOffset(readingText, reading.units, readingUnitIndex),
                readingOffset(
                    readingText,
                    reading.units,
                    state.nextReadingUnitIndex
                )
            );
        }
        readingUnitIndex = state.nextReadingUnitIndex;
    }

    return solution;
}

} // namespace

/**
 * @brief Simplify a surface-reading pair into furigana spans.
 *
 * @param surface The original surface text.
 * @param reading The original reading text.
 * @return A unique simplified alignment, or one fallback pair.
 */
QList<FuriganaUtils::Pair> FuriganaUtils::simplifySurface(
    const QString &surface,
    const QString &reading
)
{
    if (normalizeText(surface) == normalizeText(reading))
    {
        return { Pair{ surface, "" } };
    }

    const ReadingData readingData = makeReadingData(reading);
    const QList<Segment> segments =
        makeSegments(makeUnits(surface), readingData.normalized);
    const QList<QList<qsizetype>> anchorEnds =
        makeAnchorEnds(segments, readingData);
    const QList<QList<SolutionState>> table =
        makeSolutionTable(segments, readingData, anchorEnds);

    if (table[0][0].count == ONE_SOLUTION)
    {
        return makeSolution(surface, reading, segments, readingData, table);
    }
    return fallbackResult(surface, reading);
}
