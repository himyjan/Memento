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

#include <QList>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QQmlListProperty>

#include "audio/audiofile.h"
#include "dict/data/term.h"
#include "setting/settings.h"

/**
 * @brief Contains audio files from an audio source resolver.
 */
class AudioFiles : public QObject
{
    Q_OBJECT

    Q_PROPERTY(
        const Settings *settings
        READ settings
        WRITE setSettings
        NOTIFY settingsChanged
    )

    Q_PROPERTY(
        const Term *term
        READ term
        WRITE setTerm
        NOTIFY termChanged
    )

    Q_PROPERTY(
        LoadState loadState
        READ loadState
        NOTIFY loadStateChanged
    )

    Q_PROPERTY(
        QQmlListProperty<AudioFile> files
        READ files
        NOTIFY filesChanged
    )

public:
    AudioFiles(QObject *parent = nullptr);
    virtual ~AudioFiles();

    /**
     * @brief Possible audio source load states.
     */
    enum LoadState
    {
        /* Audio files haven't yet been loaded */
        Unloaded,

        /* Audio files are currently loading */
        Loading,

        /* Audio files are currently loaded */
        Loaded,
    };
    Q_ENUM(LoadState)

    /**
     * @brief Get the application settings.
     *
     * @return The application settings.
     */
    [[nodiscard]]
    const Settings *settings() const noexcept;

    /**
     * @brief Set the application settings object. Does not take ownership.
     *
     * @param value The new application settings object.
     */
    void setSettings(const Settings *value);

    /**
     * @brief Get the current term.
     *
     * @return The current term.
     */
    [[nodiscard]]
    const Term *term() const noexcept;

    /**
     * @brief Set the current term.
     *
     * @param value The current term.
     */
    void setTerm(const Term *value);

    /**
     * @brief Get the current load state.
     *
     * @param value The current load state.
     */
    [[nodiscard]]
    LoadState loadState() const noexcept;

    /**
     * @brief Get the files for this audio source.
     * Each element contains the fields:
     *  * name: Name of the audio source
     *  * url: URL of the audio source
     *  * skipHash: Skip hash of the audio source
     *  * exists: true if this audio source exists, false otherwise
     *
     * @return A read-only list of audio source files.
     */
    [[nodiscard]]
    QQmlListProperty<AudioFile> files();

signals:
    /**
     * @brief Emitted when the application settings are changed.
     *
     * @param settings The new value.
     */
    void settingsChanged(const Settings *settings);

    /**
     * @brief Emitted when the term is changed.
     *
     * @param value The new value.
     */
    void termChanged(const Term *value);

    /**
     * @brief Emitted when the load state is changed.
     *
     * @param value The new value.
     */
    void loadStateChanged(LoadState value);

    /**
     * @brief Emitted when the files are changed.
     */
    void filesChanged();

private slots:
    /**
     * @brief Load files based on the current properties.
     */
    void loadFiles();

private:
    /**
     * @brief Set the current load state.
     *
     * @param value The new load state.
     */
    void setLoadState(LoadState value);

    /**
     * @brief Load the current audio source.
     *
     * @param generation The generation of the load being continued.
     */
    void loadCurrentSource(quint64 generation);

    /**
     * @brief Load the current JSON audio source.
     *
     * @param generation The generation of the load being continued.
     */
    void loadJsonAudioSource(quint64 generation);

    /* The network access manager */
    QNetworkAccessManager m_manager{this};

    /* Application settings */
    QPointer<const Settings> m_settings{nullptr};

    /* Watches for the application settings object being destroyed */
    QMetaObject::Connection m_settingsDestroyedConnection;

    /* The term to get audio files for */
    QPointer<const Term> m_term{nullptr};

    /* Watches for the term object being destroyed */
    QMetaObject::Connection m_termDestroyedConnection;

    /* Incremented whenever outstanding asynchronous loads become stale */
    quint64 m_loadGeneration{0};

    /* The current load state */
    LoadState m_loadState{Unloaded};

    /* List of audio files.
     * Each element contains the fields:
     *  * name: Name of the audio source
     *  * url: URL of the audio source
     *  * skipHash: Skip hash of the audio source
     *  * exists: true if this audio source exists, false otherwise
     */
    QList<AudioFile *> m_files;

    /* Saved list of audio sources */
    QList<AudioSource> m_sources;

    /* Index of the current audio source being loaded */
    qsizetype m_sourceCurrent{0};
};
