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

#ifndef INCLUDED_SRC_BUILDTOOL_MAIN_INSTALL_CAS_HPP
#define INCLUDED_SRC_BUILDTOOL_MAIN_INSTALL_CAS_HPP

#include <optional>
#include <string>

#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#ifndef BOOTSTRAP_BUILD_TOOL
#include "src/buildtool/common/cli.hpp"
#include "src/buildtool/execution_api/common/api_bundle.hpp"
#include "src/buildtool/execution_api/remote/context.hpp"
#endif

/// \note Method is public for use also in tests.
[[nodiscard]] auto ObjectInfoFromLiberalString(HashFunction::Type hash_type,
                                               std::string const& s,
                                               bool has_remote) noexcept
    -> std::optional<Artifact::ObjectInfo>;

#ifndef BOOTSTRAP_BUILD_TOOL
[[nodiscard]] auto FetchAndInstallArtifacts(ApiBundle const& apis,
                                            FetchArguments const& clargs,
                                            RemoteContext const& remote_context)
    -> bool;
#endif

#endif  // INCLUDED_SRC_BUILDTOOL_MAIN_INSTALL_CAS_HPP
