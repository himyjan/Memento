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

#include "quick/furiganaparser.h"

namespace
{

constexpr const char *KEY_SURFACE = "surface";
[[maybe_unused]] constexpr const char *KEY_READING = "reading";

} // namespace

#ifdef MEMENTO_MECAB_SUPPORT

#include "util/utils.h"

/* Begin MeCab Version */

/* Begin Furigana Parser */

FuriganaParser::FuriganaParser(Settings *settings, QObject *parent) :
    QObject(parent),
    m_settings(settings)
{
    if (settings->searchSubtitleFurigana())
    {
        initializeTagger();
    }

    connect(
        m_settings, &Settings::searchSubtitleFuriganaChanged,
        this, &FuriganaParser::handleSearchSubtitleFuriganaChanged,
        Qt::QueuedConnection
    );
}

FuriganaParser::~FuriganaParser()
{

}

bool FuriganaParser::initializeTagger()
{
    if (m_tagger != nullptr)
    {
        return true;
    }

    m_tagger = MeCabUtils::makeTagger();
    if (m_tagger == nullptr)
    {
        qWarning("Could not create MeCab::Tagger: %s", MeCab::getTaggerError());
        return false;
    }
    return true;
}

void FuriganaParser::handleSearchSubtitleFuriganaChanged(bool value)
{
    if (!value)
    {
        m_tagger = nullptr;
        return;
    }
    initializeTagger();
}

QVariantList FuriganaParser::parse(const QString &text)
{
    if (m_tagger == nullptr &&
        (!m_settings->searchSubtitleFurigana() || !initializeTagger()))
    {
        return getFallbackResult(text);
    }

    std::unique_ptr<MeCab::Lattice> lattice{MeCab::createLattice()};
    if (lattice == nullptr)
    {
        qWarning("Could not create MeCab::Lattice: %s", MeCab::getLastError());
        return getFallbackResult(text);
    }

    QByteArray textArr = text.toUtf8();
    lattice->set_sentence(textArr);
    if (!m_tagger->parse(lattice.get()))
    {
        qWarning("Cannot access MeCab: %s", MeCab::getLastError());
        return getFallbackResult(text);
    }

    QVariantList result;
    mecab_node_t *curr = lattice->bos_node()->next;
    while (curr && curr->stat != MECAB_EOS_NODE)
    {
        if (curr->length < curr->rlength)
        {
            QVariantMap whitespaceElement;
            whitespaceElement[KEY_SURFACE] = getWhitespace(curr);
            result.emplaceBack(std::move(whitespaceElement));
        }

        QString surface = getSurface(curr);
        QString reading = getReading(curr);

        QString surfaceHiragana = katakanaToHiragana(surface);
        reading = katakanaToHiragana(reading);

        QVariantMap element;
        element[KEY_SURFACE] = std::move(surface);
        if (reading != "*" && surfaceHiragana != reading)
        {
            element[KEY_READING] = std::move(reading);
        }
        result.emplaceBack(std::move(element));

        curr = curr->next;
    }
    return result;
}

QString FuriganaParser::katakanaToHiragana(QString text)
{
    constexpr char16_t KATAKANA_LOW = 0x30A1;
    constexpr char16_t KATAKANA_HIGH = 0x30F6;
    constexpr char16_t HIRAGANA_KATAKANA_OFFSET = 0x60;

    text = text.normalized(QString::NormalizationForm_KC);

    for (qsizetype i = 0; i < text.size(); ++i)
    {
        const char16_t code = text[i].unicode();
        if (KATAKANA_LOW <= code && code <= KATAKANA_HIGH)
        {
            text[i] = QChar(code - HIRAGANA_KATAKANA_OFFSET);
        }
    }

    return text;
}

QString FuriganaParser::getReading(const MeCab::Node *node)
{
    constexpr qsizetype READING_INDEX{7};
    QStringList features = QString::fromUtf8(node->feature).split(',');
    if (features.size() <= READING_INDEX)
    {
        return "";
    }
    return features[READING_INDEX];
}

QString FuriganaParser::getSurface(const MeCab::Node *node)
{
    return QString::fromUtf8(node->surface, node->length);
}

QString FuriganaParser::getWhitespace(const MeCab::Node *node)
{
    int whitespaceLength = node->rlength - node->length;
    return QString::fromUtf8(
        node->surface - whitespaceLength, whitespaceLength
    );
}

QVariantList FuriganaParser::getFallbackResult(const QString &text)
{
    QVariantList result;
    QVariantMap element;
    element[KEY_SURFACE] = text;
    result.emplaceBack(std::move(element));
    return result;
}

/* End Furigana Parser */

/* End MeCab Version */

#else

/* Begin Non-MeCab Version */

FuriganaParser::FuriganaParser(Settings *, QObject *parent) : QObject(parent)
{

}

FuriganaParser::~FuriganaParser()
{

}

QVariantList FuriganaParser::parse(const QString &text)
{
    QVariantList result;
    QVariantMap element;
    element[KEY_SURFACE] = text;
    result.emplaceBack(std::move(element));
    return result;
}

/* End Non-MeCab Version */

#endif // MEMENTO_MECAB_SUPPORT
