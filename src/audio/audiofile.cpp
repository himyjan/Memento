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

#include "audio/audiofile.h"

AudioFile::AudioFile(QObject *parent) : QObject(parent)
{

}

const QString &AudioFile::name() const noexcept
{
    return m_name;
}

void AudioFile::setName(const QString &value)
{
    if (m_name == value)
    {
        return;
    }
    m_name = value;
    emit nameChanged(m_name);
}

const QString &AudioFile::url() const noexcept
{
    return m_url;
}

void AudioFile::setUrl(const QString &value)
{
    if (m_url == value)
    {
        return;
    }
    m_url = value;
    emit urlChanged(m_url);
}

const QString &AudioFile::skipHash() const noexcept
{
    return m_skipHash;
}

void AudioFile::setSkipHash(const QString &value)
{
    if (m_skipHash == value)
    {
        return;
    }
    m_skipHash = value;
    emit skipHashChanged(m_skipHash);
}

bool AudioFile::exists() const noexcept
{
    return m_exists;
}

void AudioFile::setExists(bool value)
{
    if (m_exists == value)
    {
        return;
    }
    m_exists = value;
    emit existsChanged(m_exists);
}
