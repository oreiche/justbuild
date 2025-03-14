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

#ifndef INCLUDED_SRC_BUILDTOOL_STORAGE_TARGET_CACHE_HPP
#define INCLUDED_SRC_BUILDTOOL_STORAGE_TARGET_CACHE_HPP

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "gsl/gsl"
#include "src/buildtool/build_engine/base_maps/entity_name_data.hpp"
#include "src/buildtool/build_engine/expression/configuration.hpp"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/file_system/file_storage.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/storage/backend_description.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/local_cas.hpp"
#include "src/buildtool/storage/target_cache_entry.hpp"
#include "src/buildtool/storage/target_cache_key.hpp"
#include "src/buildtool/storage/uplinker.hpp"

/// \brief The high-level target cache for storing export target's data.
/// Supports global uplinking across all generations. The uplink is
/// automatically performed for every entry that is read and already exists in
/// an older generation.
/// \tparam kDoGlobalUplink     Enable global uplinking.
template <bool kDoGlobalUplink>
class TargetCache {
  public:
    /// Local target cache generation used by GC without global uplink.
    using LocalGenerationTC = TargetCache</*kDoGlobalUplink=*/false>;

    /// Callback type for downloading known artifacts to local CAS.
    using ArtifactDownloader =
        std::function<bool(std::vector<Artifact::ObjectInfo> const&)>;

    explicit TargetCache(
        gsl::not_null<LocalCAS<kDoGlobalUplink> const*> const& cas,
        GenerationConfig const& config,
        gsl::not_null<Uplinker<kDoGlobalUplink> const*> const& uplinker)
        : TargetCache(cas,
                      config.target_cache,
                      uplinker,
                      config.storage_config->backend_description) {}

    /// \brief Returns a new TargetCache backed by the same CAS, but the
    /// FileStorage uses the given \p backend_description 's hash. This is
    /// particularly useful for the just-serve server implementation, since the
    /// sharding must be performed according to the client's request and not
    /// following the server configuration.
    [[nodiscard]] auto WithShard(BackendDescription backend_description) const
        -> TargetCache {
        if (backend_description_ == backend_description) {
            return *this;
        }

        return TargetCache(&cas_,
                           file_store_.StorageRoot().parent_path(),
                           &uplinker_,
                           std::move(backend_description));
    }

    TargetCache(TargetCache const&) = default;
    TargetCache(TargetCache&&) noexcept = default;
    auto operator=(TargetCache const&) -> TargetCache& = default;
    auto operator=(TargetCache&&) noexcept -> TargetCache& = default;
    ~TargetCache() noexcept = default;

    /// \brief Store new key-entry pair in the target cache.
    /// \param key          The target-cache key.
    /// \param value        The target-cache value to store.
    /// \param downloader   Callback for obtaining known artifacts to local CAS.
    /// \returns true on success.
    [[nodiscard]] auto Store(
        TargetCacheKey const& key,
        TargetCacheEntry const& value,
        ArtifactDownloader const& downloader) const noexcept -> bool;

    /// \brief Calculate TargetCacheKey based on auxiliary information.
    /// Doesn't create a TargetCacheEntry in the TargetCache.
    /// \return TargetCacheKey on success.
    [[nodiscard]] auto ComputeKey(
        ArtifactDigest const& repo_key,
        BuildMaps::Base::NamedTarget const& target_name,
        Configuration const& effective_config) const noexcept
        -> std::optional<TargetCacheKey>;

    /// \brief Read existing entry and object info from the target cache.
    /// \param key  The target-cache key to read the entry from.
    /// \param shard Optional explicit shard, if the default is not intended.
    /// \returns Pair of cache entry and its object info on success or nullopt.
    [[nodiscard]] auto Read(TargetCacheKey const& key) const noexcept
        -> std::optional<std::pair<TargetCacheEntry, Artifact::ObjectInfo>>;

    /// \brief Uplink entry from this to latest target cache generation.
    /// This function is only available for instances that are used as local GC
    /// generations (i.e., disabled global uplink).
    /// \tparam kIsLocalGeneration  True if this instance is a local generation.
    /// \param latest   The latest target cache generation.
    /// \param key      The target-cache key for the entry to uplink.
    /// \returns True if entry was successfully uplinked.
    template <bool kIsLocalGeneration = not kDoGlobalUplink>
        requires(kIsLocalGeneration)
    [[nodiscard]] auto LocalUplinkEntry(
        LocalGenerationTC const& latest,
        TargetCacheKey const& key) const noexcept -> bool;

  private:
    // By default, overwrite existing entries. Unless this is a generation
    // (disabled global uplink), then we never want to overwrite any entries.
    static constexpr auto kStoreMode =
        kDoGlobalUplink ? StoreMode::LastWins : StoreMode::FirstWins;

    std::shared_ptr<Logger> logger_{std::make_shared<Logger>("TargetCache")};
    LocalCAS<kDoGlobalUplink> const& cas_;
    FileStorage<ObjectType::File,
                kStoreMode,
                /*kSetEpochTime=*/false>
        file_store_;
    Uplinker<kDoGlobalUplink> const& uplinker_;
    BackendDescription const backend_description_;

    explicit TargetCache(
        gsl::not_null<LocalCAS<kDoGlobalUplink> const*> const& cas,
        std::filesystem::path const& root,
        gsl::not_null<Uplinker<kDoGlobalUplink> const*> const& uplinker,
        BackendDescription backend_description)
        : cas_{*cas},
          file_store_{
              root /
              backend_description.HashContent(cas->GetHashFunction()).hash()},
          uplinker_{*uplinker},
          backend_description_{std::move(backend_description)} {
        if constexpr (kDoGlobalUplink) {
            // Write backend description (which determines the target cache
            // shard) to CAS. It needs to be added for informational purposes
            // only, so it is not an error if insertion fails or returns an
            // unexpected result.
            auto const id = cas_.StoreBlob(
                backend_description_.GetDescription(), /*is_executable=*/false);

            auto const expected_hash =
                file_store_.StorageRoot().filename().string();
            if (not id) {
                logger_->Emit(LogLevel::Debug,
                              "TargetCache: Failed to add backend description "
                              "{} to the storage:\n{}",
                              expected_hash,
                              backend_description_.GetDescription());
            }
            else if (id->hash() != expected_hash) {
                logger_->Emit(LogLevel::Debug,
                              "TargetCache: backend description was added to "
                              "the storage with an unexpected hash. Expected "
                              "{}, added with {}. Content:\n{}",
                              expected_hash,
                              id->hash(),
                              backend_description_.GetDescription());
            }
        }
    }

    template <bool kIsLocalGeneration = not kDoGlobalUplink>
        requires(kIsLocalGeneration)
    [[nodiscard]] auto LocalUplinkEntry(
        LocalGenerationTC const& latest,
        std::string const& key_digest) const noexcept -> bool;

    [[nodiscard]] auto DownloadKnownArtifacts(
        TargetCacheEntry const& value,
        ArtifactDownloader const& downloader) const noexcept -> bool;
};

#ifdef BOOTSTRAP_BUILD_TOOL
using ActiveTargetCache = TargetCache<false>;
#else
// TargetCache type aware of bootstrapping
using ActiveTargetCache = TargetCache<true>;
#endif  // BOOTSTRAP_BUILD_TOOL

// NOLINTNEXTLINE(misc-header-include-cycle)
#include "src/buildtool/storage/target_cache.tpp"  // IWYU pragma: export

#endif  // INCLUDED_SRC_BUILDTOOL_STORAGE_TARGET_CACHE_HPP
