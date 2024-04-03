// Copyright 2024 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_BUILDTOOL_MAIN_ARCHIVE_HPP
#define INCLUDED_SRC_BUILDTOOL_MAIN_ARCHIVE_HPP

#ifndef BOOTSTRAP_BUILD_TOOL

#include <filesystem>
#include <optional>

#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"

[[nodiscard]] auto GenerateArchive(
    gsl::not_null<IExecutionApi*> const& api,
    const Artifact::ObjectInfo& artifact,
    const std::optional<std::filesystem::path>& output_path) -> bool;

#endif

#endif
