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

#include <QList>
#include <QString>

namespace FuriganaUtils
{

/**
 * @brief Contains a surface-reading pair.
 */
struct Pair
{
    /* Text of the raw string */
    QString surface;

    /* Reading of the surface. Empty if the reading is the surface.*/
    QString reading;
};

/**
 * @brief Simply a surface-reading pair by matching all characters duplicated
 * in the reading to the surface string.
 *
 * If a surface string has no solution or multiple solutions, a single pair of
 * surface and reading is returned.
 *
 * @param surface The surface string.
 * @param reading The reading of the surface.
 * @return A list of surface-reading pairs.
 */
[[nodiscard]]
QList<FuriganaUtils::Pair> simplifySurface(
    const QString &surface, const QString &reading);

} // namespace FuriganaUtils
