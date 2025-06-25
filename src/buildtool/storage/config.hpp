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

#ifndef INCLUDED_SRC_BUILDTOOL_STORAGE_CONFIG_HPP
#define INCLUDED_SRC_BUILDTOOL_STORAGE_CONFIG_HPP

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

#include "fmt/core.h"
#include "gsl/gsl"
#include "src/buildtool/common/protocol_traits.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/storage/backend_description.hpp"
#include "src/utils/cpp/expected.hpp"
#include "src/utils/cpp/gsl.hpp"
#include "src/utils/cpp/tmp_dir.hpp"

struct StorageConfig;

struct GenerationConfig final {
    gsl::not_null<StorageConfig const*> const storage_config;
    std::filesystem::path const cas_f;
    std::filesystem::path const cas_x;
    std::filesystem::path const cas_t;
    std::filesystem::path const cas_large_f;
    std::filesystem::path const cas_large_t;
    std::filesystem::path const action_cache;
    std::filesystem::path const target_cache;
};

struct StorageConfig final {
    class Builder;

    static inline auto const kDefaultBuildRoot =
        FileSystemManager::GetUserHome() / ".cache" / "just";

    // Build root directory. All the storage dirs are subdirs of build_root.
    // By default, build_root is set to $HOME/.cache/just.
    // If the user uses --local-build-root PATH,
    // then build_root will be set to PATH.
    std::filesystem::path const build_root = kDefaultBuildRoot;

    // Number of total storage generations (default: two generations).
    std::size_t const num_generations = 2;

    HashFunction const hash_function{HashFunction::Type::GitSHA1};

    // Hash of the execution backend description
    BackendDescription const backend_description;

    /// \brief Root directory of all storage generations.
    [[nodiscard]] auto CacheRoot() const noexcept -> std::filesystem::path {
        return build_root / "protocol-dependent";
    }

    /// \brief Root directory of all repository generations.
    [[nodiscard]] auto RepositoryRoot() const noexcept
        -> std::filesystem::path {
        return build_root / "repositories";
    }

    /// \brief Directory for the given generation of stored repositories
    [[nodiscard]] auto RepositoryGenerationRoot(
        std::size_t index) const noexcept -> std::filesystem::path {
        ExpectsAudit(index < num_generations);
        auto generation = std::string{"generation-"} + std::to_string(index);
        return RepositoryRoot() / generation;
    }

    /// \brief Directory for the git repository of the given generation
    [[nodiscard]] auto GitGenerationRoot(std::size_t index) const noexcept
        -> std::filesystem::path {
        return RepositoryGenerationRoot(index) / "git";
    }

    /// \brief Directory for the git repository storing various roots
    [[nodiscard]] auto GitRoot() const noexcept -> std::filesystem::path {
        return GitGenerationRoot(0);
    }

    /// \brief Root directory of specific storage generation
    [[nodiscard]] auto GenerationCacheRoot(std::size_t index) const noexcept
        -> std::filesystem::path {
        ExpectsAudit(index < num_generations);
        auto generation = std::string{"generation-"} + std::to_string(index);
        return CacheRoot() / generation;
    }

    /// \brief Root directory for all ephemeral directories, i.e., directories
    /// that can (and should) be removed immediately by garbage collection.
    [[nodiscard]] auto EphemeralRoot() const noexcept -> std::filesystem::path {
        return GenerationCacheRoot(0) / "ephemeral";
    }

    /// \brief Root directory for local action executions; individual actions
    /// create a working directory below this root.
    [[nodiscard]] auto ExecutionRoot() const noexcept -> std::filesystem::path {
        return EphemeralRoot() / "exec_root";
    }

    /// \brief Create a tmp directory with controlled lifetime for specific
    /// operations (archive, zip, file, distdir checkouts; fetch; update).
    [[nodiscard]] auto CreateTypedTmpDir(std::string const& type) const noexcept
        -> TmpDir::Ptr {
        // try to create parent dir
        auto parent_path = EphemeralRoot() / "tmp-workspaces" / type;
        return TmpDir::Create(parent_path);
    }

    [[nodiscard]] auto CreateGenerationConfig(
        std::size_t generation) const noexcept -> GenerationConfig {
        bool const native = ProtocolTraits::IsNative(hash_function.GetType());
        auto const cache_root = GenerationCacheRoot(generation);
        auto const cache_dir = UpdatePathForCompatibility(cache_root, native);

        return GenerationConfig{
            .storage_config = this,
            .cas_f = cache_dir / "casf",
            .cas_x = cache_dir / "casx",
            .cas_t = cache_dir / (native ? "cast" : "casf"),
            .cas_large_f = cache_dir / "cas-large-f",
            .cas_large_t = cache_dir / (native ? "cas-large-t" : "cas-large-f"),
            .action_cache = cache_dir / "ac",
            .target_cache = cache_dir / "tc"};
    };

  private:
    // different folder for different caching protocol
    [[nodiscard]] static auto UpdatePathForCompatibility(
        std::filesystem::path const& dir,
        bool is_native) -> std::filesystem::path {
        return dir / (is_native ? "git-sha1" : "compatible-sha256");
    };
};

class StorageConfig::Builder final {
  public:
    explicit Builder() = default;

    /// \brief Create a configurable builder from an existing config.
    /// Useful, for example, to make a copy of an existing config and change
    /// the hash type.
    [[nodiscard]] static auto Rebuild(StorageConfig const& config) noexcept
        -> Builder {
        return Builder{}
            .SetBuildRoot(config.build_root)
            .SetNumGenerations(config.num_generations)
            .SetHashType(config.hash_function.GetType())
            .SetBackendDescription(config.backend_description);
    }

    auto SetBuildRoot(std::filesystem::path value) noexcept -> Builder& {
        build_root_ = std::move(value);
        return *this;
    }

    /// \brief Specifies the number of storage generations.
    auto SetNumGenerations(std::size_t value) noexcept -> Builder& {
        num_generations_ = value;
        return *this;
    }

    /// \brief Specify the type of the hash function
    auto SetHashType(HashFunction::Type value) noexcept -> Builder& {
        hash_type_ = value;
        return *this;
    }

    auto SetBackendDescription(BackendDescription value) noexcept -> Builder& {
        backend_description_ = std::move(value);
        return *this;
    }

    [[nodiscard]] auto Build() const noexcept
        -> expected<StorageConfig, std::string> {
        // To not duplicate default arguments of StorageConfig in builder,
        // create a default config and copy default arguments from there.
        StorageConfig const default_config;

        auto build_root = default_config.build_root;
        if (build_root_.has_value()) {
            build_root = *build_root_;
            if (FileSystemManager::IsRelativePath(build_root)) {
                return unexpected(fmt::format(
                    "Build root must be absolute path but got '{}'.",
                    build_root.string()));
            }
        }

        auto num_generations = default_config.num_generations;
        if (num_generations_.has_value()) {
            num_generations = *num_generations_;
            if (num_generations == 0) {
                return unexpected(std::string{
                    "The number of generations must be greater than 0."});
            }
        }

        auto const hash_function = hash_type_.has_value()
                                       ? HashFunction{*hash_type_}
                                       : default_config.hash_function;

        // Hash the execution backend description
        auto backend_description = default_config.backend_description;
        if (backend_description_) {
            backend_description = *backend_description_;
        }

        return StorageConfig{
            .build_root = std::move(build_root),
            .num_generations = num_generations,
            .hash_function = hash_function,
            .backend_description = std::move(backend_description)};
    }

  private:
    std::optional<std::filesystem::path> build_root_;
    std::optional<std::size_t> num_generations_;
    std::optional<HashFunction::Type> hash_type_;
    std::optional<BackendDescription> backend_description_;
};

#endif  // INCLUDED_SRC_BUILDTOOL_STORAGE_CONFIG_HPP
