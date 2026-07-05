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

#include <QObject>

#include <QVariantList>

#ifdef MEMENTO_MECAB_SUPPORT
#include <mecab.h>
#endif // MEMENTO_MECAB_SUPPORT

#include "setting/settings.h"

/**
 * @brief Object that allows QML to request furigana from text and receive it
 * in a structured form.
 */
class FuriganaParser : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Create a new furigana parser.
     *
     * @param settings The settings object to track.
     * @param parent The parent of this object.
     */
    FuriganaParser(Settings *settings, QObject *parent = nullptr);
    virtual ~FuriganaParser();

    /**
     * @brief Parse text and return a segmented text including text and reading.
     *
     * @param text The text to prase.
     * @return A list of QVariantMaps containing properties:
     *      surface: The original text.
     *      reading: The reading of the text unless the surface is the reading.
     */
    [[nodiscard]]
    Q_INVOKABLE QVariantList parse(const QString &text);

#ifdef MEMENTO_MECAB_SUPPORT
private slots:
    /**
     * @brief Handle updates to the subtitle furigana settings change.
     *
     * @param value true to enable furigana, false to disable it.
     */
    void handleSearchSubtitleFuriganaChanged(bool value);

private:
    /**
     * @brief Initializes the tagger object if it hasn't already been
     * initialized.
     * @return true on success,
     * @return false if the tagger is still nullptr.
     */
    bool initializeTagger();

    /**
     * @brief Convert all katakana in a string to hiragana.
     *
     * @param text The text to convert.
     * @return The text with all katakana replaced with hiragana.
     */
    [[nodiscard]]
    static QString katakanaToHiragana(QString text);

    /**
     * @brief Get the reading of the current node.
     *
     * @param node The node to get the reading from.
     * @return The reading from the node.
     */
    [[nodiscard]]
    static QString getReading(const MeCab::Node *node);

    /**
     * @brief Gets the surface string including whitespace from a MeCab node.
     *
     * @param node The MeCab node to get the surface from.
     * @return The surface string including whitespace.
     */
    [[nodiscard]]
    static QString getSurface(const MeCab::Node *node);

    /**
     * @brief Get the whitespace before the surface.
     *
     * @param node The node to get the whitespace from.
     * @return The whitespace from before the surface.
     */
    [[nodiscard]]
    static QString getWhitespace(const MeCab::Node *node);

    /**
     * @brief Get a fallback result for parse.
     *
     * @param text The text to package in the fallback.
     * @return A QVariantList with a single item that contains a single surface
     * containing text.
     */
    [[nodiscard]]
    static QVariantList getFallbackResult(const QString &text);

    /* The settings object to track for changes */
    Settings *m_settings{nullptr};

    /* The object used for interacting with MeCab */
    std::unique_ptr<MeCab::Tagger> m_tagger{nullptr};
#endif // MEMENTO_MECAB_SUPPORT
};
