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

#ifndef INCLUDED_SRC_OTHER_TOOLS_ROOT_MAPS_TREE_ID_GIT_MAP_HPP
#define INCLUDED_SRC_OTHER_TOOLS_ROOT_MAPS_TREE_ID_GIT_MAP_HPP

#include <string>
#include <utility>

#include "nlohmann/json.hpp"
#include "src/other_tools/ops_maps/critical_git_op_map.hpp"

struct TreeIdInfo {
    std::string hash{}; /* key */
    std::map<std::string, std::string> env_vars{};
    std::vector<std::string> command{};
    // name of repository for which work is done; used in progress reporting
    std::string origin{};

    [[nodiscard]] auto operator==(const TreeIdInfo& other) const -> bool {
        return hash == other.hash;
    }
};

namespace std {
template <>
struct hash<TreeIdInfo> {
    [[nodiscard]] auto operator()(const TreeIdInfo& ti) const noexcept
        -> std::size_t {
        return std::hash<std::string>{}(ti.hash);
    }
};
}  // namespace std

/// \brief Maps a known tree provided through a generic command to its
/// workspace root and the information whether it was a cache it.
using TreeIdGitMap =
    AsyncMapConsumer<TreeIdInfo, std::pair<nlohmann::json, bool>>;

[[nodiscard]] auto CreateTreeIdGitMap(
    gsl::not_null<CriticalGitOpMap*> const& critical_git_op_map,
    std::string const& git_bin,
    std::vector<std::string> const& launcher,
    std::size_t jobs) -> TreeIdGitMap;

#endif  // INCLUDED_SRC_OTHER_TOOLS_ROOT_MAPS_TREE_ID_GIT_MAP_HPP
