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

#ifdef MEMENTO_MECAB_SUPPORT

#include <memory>

namespace MeCab
{
class Tagger;
} // namespace MeCab

namespace MeCabUtils
{

/**
 * @brief Make a MeCab::Tagger with the default Memento arguments.
 *
 * @return A pointer to the created tagger, nullptr on failure.
 */
[[nodiscard]]
std::unique_ptr<MeCab::Tagger> makeTagger();

} // namespace MeCabUtils

#endif // MEMENTO_MECAB_SUPPORT
