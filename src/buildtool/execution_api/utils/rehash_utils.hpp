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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_UTILS_REHASH_UTILS_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_UTILS_REHASH_UTILS_HPP

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "gsl/gsl"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/execution_api/common/api_bundle.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/utils/cpp/expected.hpp"

namespace RehashUtils {

/// \brief Get a corresponding known object from a different local CAS, as
/// stored in a mapping file, if exists.
/// \param digest Source digest.
/// \param source_config Storage config corresponding to source digest.
/// \param target_config Storage config corresponding to target digest.
/// \param from_git  Specify if source digest comes from a Git location instead
/// of CAS.
/// \returns The target artifact info on successfully reading an existing
/// mapping file, nullopt if no mapping file exists, or the error message on
/// failure.
[[nodiscard]] auto ReadRehashedDigest(ArtifactDigest const& digest,
                                      StorageConfig const& source_config,
                                      StorageConfig const& target_config,
                                      bool from_git = false) noexcept
    -> expected<std::optional<Artifact::ObjectInfo>, std::string>;

/// \brief Write the mapping file linking two digests hashing the same content.
/// \param source_digest Source digest.
/// \param target_digest Target digest.
/// \param obj_type Object type of the content represented by the two digests.
/// \param source_config Storage config corresponding to source digest.
/// \param target_config Storage config corresponding to target digest.
/// \param from_git  Specify if source digest comes from a Git location instead
/// of CAS.
/// \returns nullopt on success, error message on failure.
[[nodiscard]] auto StoreRehashedDigest(ArtifactDigest const& source_digest,
                                       ArtifactDigest const& target_digest,
                                       ObjectType obj_type,
                                       StorageConfig const& source_config,
                                       StorageConfig const& target_config,
                                       bool from_git = false) noexcept
    -> std::optional<std::string>;

[[nodiscard]] auto RehashDigest(
    std::vector<Artifact::ObjectInfo> const& digests,
    StorageConfig const& source_config,
    StorageConfig const& target_config,
    std::optional<gsl::not_null<ApiBundle const*>> apis)
    -> expected<std::vector<Artifact::ObjectInfo>, std::string>;

[[nodiscard]] auto RehashGitDigest(
    std::vector<Artifact::ObjectInfo> const& digests,
    StorageConfig const& source_config,
    StorageConfig const& target_config,
    RepositoryConfig const& repo_config)
    -> expected<std::vector<Artifact::ObjectInfo>, std::string>;

class Rehasher {
  public:
    Rehasher(StorageConfig source_config,
             StorageConfig target_config,
             std::optional<gsl::not_null<ApiBundle const*>> apis)
        : source_{std::move(source_config)},
          target_{std::move(target_config)},
          apis_{std::move(apis)} {}

    [[nodiscard]] auto Rehash(Artifact::ObjectInfo const& info) const
        -> expected<Artifact::ObjectInfo, std::string>;

  private:
    StorageConfig const source_;
    StorageConfig const target_;
    std::optional<gsl::not_null<ApiBundle const*>> const apis_;
};

}  // namespace RehashUtils

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_UTILS_REHASH_UTILS_HPP
