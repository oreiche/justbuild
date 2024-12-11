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

#ifndef INCLUDED_SRC_OTHER_TOOLS_JUST_MR_UPDATE_HPP
#define INCLUDED_SRC_OTHER_TOOLS_JUST_MR_UPDATE_HPP

#include <memory>
#include <string>

#include "src/buildtool/build_engine/expression/configuration.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/other_tools/just_mr/cli.hpp"

/// \brief Update of Git repos commit information for a multi-repository build.
[[nodiscard]] auto MultiRepoUpdate(std::shared_ptr<Configuration> const& config,
                                   MultiRepoCommonArguments const& common_args,
                                   MultiRepoUpdateArguments const& update_args,
                                   StorageConfig const& native_storage_config,
                                   std::string const& multi_repo_tool_name)
    -> int;

#endif  // INCLUDED_SRC_OTHER_TOOLS_JUST_MR_UPDATE_HPP
