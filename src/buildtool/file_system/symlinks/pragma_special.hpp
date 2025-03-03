// Copyright 2023 Huawei Cloud Computing Technology Co., Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_SYMLINKS_PRAGMA_SPECIAL_HPP
#define INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_SYMLINKS_PRAGMA_SPECIAL_HPP

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>

/* Enum used by the resolve_symlinks_map */

/// \brief Pragma "special" value enum
enum class PragmaSpecial : std::uint8_t {
    Ignore,
    ResolvePartially,
    ResolveCompletely
};

/// \brief Pragma "special" value map, from string to enum
inline std::unordered_map<std::string, PragmaSpecial> const kPragmaSpecialMap =
    {{"ignore", PragmaSpecial::Ignore},
     {"resolve-partially", PragmaSpecial::ResolvePartially},
     {"resolve-completely", PragmaSpecial::ResolveCompletely}};

/// \brief Pragma "special" value inverse map, from enum to string
inline std::unordered_map<PragmaSpecial, std::string> const
    kPragmaSpecialInverseMap = {
        {PragmaSpecial::Ignore, "ignore"},
        {PragmaSpecial::ResolvePartially, "resolve-partially"},
        {PragmaSpecial::ResolveCompletely, "resolve-completely"}};

#endif  // INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_SYMLINKS_PRAGMA_SPECIAL_HPP
