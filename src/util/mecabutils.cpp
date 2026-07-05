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

#ifdef MEMENTO_MECAB_SUPPORT

#include "util/mecabutils.h"

#include <QByteArray>
#include <QDir>
#include <QString>

#include <mecab.h>

#include "util/utils.h"

#if defined(Q_OS_WIN)
/**
 * This whole section is necessary on Windows because MeCab has a bug that
 * prevents it from loading dictionaries if there are spaces in the path on
 * Windows. If Memento is to be install in "Program Files", this quickly
 * becomes an issue. This workaround turns all long paths into space-less
 * short paths.
 */
#ifndef NOMINMAX
#define NOMINMAX 1
#endif // NOMINMAX
#include <Windows.h>

#include <fileapi.h>

namespace
{

/**
 * Takes a Windows long path and returns an 8.3/short path.
 * @param path The Window long path to convert.
 * @return A Windows short path, or the empty string on error.
 */
[[nodiscard]]
QByteArray toWindowsShortPath(const QString &path)
{
    QByteArray pathArr = QDir::toNativeSeparators(path).toUtf8();
    DWORD length = 0;

    length = GetShortPathNameA(pathArr.constData(), NULL, 0);
    if (length == 0)
    {
        return "";
    }

    QByteArray buf(length, '\0');
    length = GetShortPathNameA(pathArr, buf.data(), length);
    if (length == 0)
    {
        return "";
    }
    buf.chop(1);
    return buf;
}

/**
 * Generates the MeCab argument on Windows.
 * @return An argument to pass MeCab so it uses the install's ipadic.
 */
[[nodiscard]]
QByteArray genMecabArg()
{
    QString ipadicPath = DirectoryUtils::getMecabDictionary() + "ipadic";
    QString dicrcPath = ipadicPath + "/dicrc";

    QByteArray arg;
    arg += " -d ";
    arg += toWindowsShortPath(ipadicPath);
    arg += " -r ";
    arg += toWindowsShortPath(dicrcPath);
    return arg;
}

}

#endif // defined(Q_OS_WIN)

std::unique_ptr<MeCab::Tagger> MeCabUtils::makeTagger()
{
#if defined(Q_OS_WIN)
    QByteArray mecabArg = genMecabArg();
#elif defined(MEMENTO_BUNDLE)
    QByteArray mecabArg = ( \
        "-r " + DirectoryUtils::getMecabDictionary() + "ipadic/dicrc " \
        "-d " + DirectoryUtils::getMecabDictionary() + "ipadic" \
    ).toUtf8();
#else
    QByteArray mecabArg = "";
#endif
    return std::unique_ptr<MeCab::Tagger>{MeCab::createTagger(mecabArg)};
}

#endif // MEMENTO_MECAB_SUPPORT
