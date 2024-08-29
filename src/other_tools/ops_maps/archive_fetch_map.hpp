// Copyright 2022 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_OTHER_TOOLS_OPS_MAPS_ARCHIVE_FETCH_MAP_HPP
#define INCLUDED_SRC_OTHER_TOOLS_OPS_MAPS_ARCHIVE_FETCH_MAP_HPP

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

#include "gsl/gsl"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/other_tools/just_mr/progress_reporting/statistics.hpp"
#include "src/other_tools/ops_maps/content_cas_map.hpp"

/// \brief Maps an archive content hash to a status flag.
using ArchiveFetchMap = AsyncMapConsumer<ArchiveContent, bool>;

[[nodiscard]] auto CreateArchiveFetchMap(
    gsl::not_null<ContentCASMap*> const& content_cas_map,
    std::filesystem::path const& fetch_dir,  // should exist!
    gsl::not_null<Storage const*> const& storage,
    gsl::not_null<IExecutionApi const*> const& local_api,
    IExecutionApi const* remote_api,
    gsl::not_null<JustMRStatistics*> const& stats,
    std::size_t jobs) -> ArchiveFetchMap;

// use explicit cast to std::function to allow template deduction when used
static const std::function<std::string(ArchiveContent const&)>
    kArchiveContentPrinter =
        [](ArchiveContent const& x) -> std::string { return x.content; };

#endif  // INCLUDED_SRC_OTHER_TOOLS_OPS_MAPS_ARCHIVE_FETCH_MAP_HPP
