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

#include <QString>

/**
 * @brief Describes an audio file exposed to QML.
 */
class AudioFile : public QObject
{
    Q_OBJECT

    Q_PROPERTY(
        QString name
        READ name
        WRITE setName
        NOTIFY nameChanged
    )

    Q_PROPERTY(
        QString url
        READ url
        WRITE setUrl
        NOTIFY urlChanged
    )

    Q_PROPERTY(
        QString skipHash
        READ skipHash
        WRITE setSkipHash
        NOTIFY skipHashChanged
    )

    Q_PROPERTY(
        bool exists
        READ exists
        WRITE setExists
        NOTIFY existsChanged
    )

public:
    explicit AudioFile(QObject *parent = nullptr);
    virtual ~AudioFile() = default;

    /**
     * @brief Get the display name of the audio file.
     *
     * @return The display name.
     */
    [[nodiscard]]
    const QString &name() const noexcept;

    /**
     * @brief Set the display name of the audio file.
     *
     * @param value The new display name.
     */
    void setName(const QString &value);

    /**
     * @brief Get the URL of the audio file.
     *
     * @return The audio file URL.
     */
    [[nodiscard]]
    const QString &url() const noexcept;

    /**
     * @brief Set the URL of the audio file.
     *
     * @param value The new audio file URL.
     */
    void setUrl(const QString &value);

    /**
     * @brief Get the hash that should be skipped before playback.
     *
     * @return The hash to skip.
     */
    [[nodiscard]]
    const QString &skipHash() const noexcept;

    /**
     * @brief Set the hash that should be skipped before playback.
     *
     * @param value The new hash to skip.
     */
    void setSkipHash(const QString &value);

    /**
     * @brief Check whether the audio file exists.
     *
     * @return true if the audio file exists, false otherwise.
     */
    [[nodiscard]]
    bool exists() const noexcept;

    /**
     * @brief Set whether the audio file exists.
     *
     * @param value Whether the audio file exists.
     */
    void setExists(bool value);

signals:
    /**
     * @brief Emitted when the display name changes.
     *
     * @param value The new display name.
     */
    void nameChanged(const QString &value);

    /**
     * @brief Emitted when the URL changes.
     *
     * @param value The new URL.
     */
    void urlChanged(const QString &value);

    /**
     * @brief Emitted when the skip hash changes.
     *
     * @param value The new skip hash.
     */
    void skipHashChanged(const QString &value);

    /**
     * @brief Emitted when the existence state changes.
     *
     * @param value The new existence state.
     */
    void existsChanged(bool value);

private:
    /* Display name of the audio file */
    QString m_name;

    /* URL of the audio file */
    QString m_url;

    /* Hash to skip before playback */
    QString m_skipHash;

    /* Whether the audio file exists */
    bool m_exists{true};
};
