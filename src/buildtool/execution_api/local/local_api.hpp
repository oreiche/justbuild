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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_LOCAL_API_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_LOCAL_API_HPP

#include <cstddef>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>  // std::move
#include <variant>
#include <vector>

#include "fmt/core.h"
#include "grpcpp/support/status.h"
#include "gsl/gsl"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/compatibility/compatibility.hpp"
#include "src/buildtool/compatibility/native_support.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_blob.hpp"
#include "src/buildtool/execution_api/bazel_msg/blob_tree.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/execution_api/execution_service/cas_utils.hpp"
#include "src/buildtool/execution_api/git/git_api.hpp"
#include "src/buildtool/execution_api/local/local_action.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/storage/storage.hpp"

/// \brief API for local execution.
class LocalApi final : public IExecutionApi {
  public:
    explicit LocalApi(std::optional<gsl::not_null<RepositoryConfig*>>
                          repo_config = std::nullopt)
        : repo_config_{std::move(repo_config)} {}

    auto CreateAction(
        ArtifactDigest const& root_digest,
        std::vector<std::string> const& command,
        std::vector<std::string> const& output_files,
        std::vector<std::string> const& output_dirs,
        std::map<std::string, std::string> const& env_vars,
        std::map<std::string, std::string> const& properties) noexcept
        -> IExecutionAction::Ptr final {
        return IExecutionAction::Ptr{new LocalAction{storage_,
                                                     root_digest,
                                                     command,
                                                     output_files,
                                                     output_dirs,
                                                     env_vars,
                                                     properties}};
    }

    // NOLINTNEXTLINE(misc-no-recursion,google-default-arguments)
    [[nodiscard]] auto RetrieveToPaths(
        std::vector<Artifact::ObjectInfo> const& artifacts_info,
        std::vector<std::filesystem::path> const& output_paths,
        std::optional<gsl::not_null<IExecutionApi*>> const& /*alternative*/ =
            std::nullopt) noexcept -> bool final {
        if (artifacts_info.size() != output_paths.size()) {
            Logger::Log(LogLevel::Error,
                        "different number of digests and output paths.");
            return false;
        }

        for (std::size_t i{}; i < artifacts_info.size(); ++i) {
            auto const& info = artifacts_info[i];
            if (IsTreeObject(info.type)) {
                // read object infos from sub tree and call retrieve recursively
                auto const infos = storage_->CAS().RecursivelyReadTreeLeafs(
                    info.digest, output_paths[i]);
                if (not infos) {
                    if (Compatibility::IsCompatible()) {
                        // infos not available, and in compatible mode cannot
                        // fall back to git
                        return false;
                    }
                    if (repo_config_ and
                        not GitApi(repo_config_.value())
                                .RetrieveToPaths({info}, {output_paths[i]})) {
                        return false;
                    }
                }
                else if (not RetrieveToPaths(infos->second, infos->first)) {
                    return false;
                }
            }
            else {
                auto const blob_path = storage_->CAS().BlobPath(
                    info.digest, IsExecutableObject(info.type));
                if (not blob_path) {
                    if (Compatibility::IsCompatible()) {
                        // infos not available, and in compatible mode cannot
                        // fall back to git
                        return false;
                    }
                    if (repo_config_ and
                        not GitApi(repo_config_.value())
                                .RetrieveToPaths({info}, {output_paths[i]})) {
                        return false;
                    }
                }
                else if (not FileSystemManager::CreateDirectory(
                             output_paths[i].parent_path()) or
                         not FileSystemManager::CopyFileAs<
                             /*kSetEpochTime=*/true,
                             /*kSetWritable=*/true>(
                             *blob_path, output_paths[i], info.type)) {
                    Logger::Log(LogLevel::Error,
                                "staging to output path {} failed.",
                                output_paths[i].string());
                    return false;
                }
            }
        }
        return true;
    }

    [[nodiscard]] auto RetrieveToFds(
        std::vector<Artifact::ObjectInfo> const& artifacts_info,
        std::vector<int> const& fds,
        bool raw_tree) noexcept -> bool final {
        if (artifacts_info.size() != fds.size()) {
            Logger::Log(LogLevel::Error,
                        "different number of digests and file descriptors.");
            return false;
        }

        for (std::size_t i{}; i < artifacts_info.size(); ++i) {
            auto fd = fds[i];
            auto const& info = artifacts_info[i];

            if (gsl::owner<FILE*> out = fdopen(dup(fd), "wb")) {  // NOLINT
                auto const success =
                    storage_->CAS().DumpToStream(info, out, raw_tree);
                std::fclose(out);
                if (not success) {
                    Logger::Log(
                        LogLevel::Debug,
                        "dumping {} {} from CAS to file descriptor {} failed.",
                        IsTreeObject(info.type) ? "tree" : "blob",
                        info.ToString(),
                        fd);
                    if (Compatibility::IsCompatible()) {
                        // infos not available, and in compatible mode cannot
                        // fall back to git
                        return false;
                    }
                    if (repo_config_ and
                        not GitApi(repo_config_.value())
                                .RetrieveToFds({info}, {fd}, raw_tree)) {
                        return false;
                    }
                }
            }
            else {
                Logger::Log(LogLevel::Error,
                            "dumping to file descriptor {} failed.",
                            fd);
                return false;
            }
        }
        return true;
    }

    // NOLINTNEXTLINE(misc-no-recursion)
    [[nodiscard]] auto RetrieveToCas(
        std::vector<Artifact::ObjectInfo> const& artifacts_info,
        gsl::not_null<IExecutionApi*> const& api) noexcept -> bool final {

        // Return immediately if target CAS is this CAS
        if (this == api) {
            return true;
        }

        // Determine missing artifacts in other CAS.
        std::vector<ArtifactDigest> digests;
        digests.reserve(artifacts_info.size());
        std::unordered_map<ArtifactDigest, Artifact::ObjectInfo> info_map;
        for (auto const& info : artifacts_info) {
            digests.push_back(info.digest);
            info_map[info.digest] = info;
        }
        auto const& missing_digests = api->IsAvailable(digests);
        std::vector<Artifact::ObjectInfo> missing_artifacts_info;
        missing_artifacts_info.reserve(missing_digests.size());
        for (auto const& digest : missing_digests) {
            missing_artifacts_info.push_back(info_map[digest]);
        }

        // Collect blobs of missing artifacts from local CAS. Trees are
        // processed recursively before any blob is uploaded.
        BlobContainer container{};
        for (auto const& info : missing_artifacts_info) {
            // Recursively process trees.
            if (IsTreeObject(info.type)) {
                auto const& infos = storage_->CAS().ReadDirectTreeEntries(
                    info.digest, std::filesystem::path{});
                if (not infos or not RetrieveToCas(infos->second, api)) {
                    return false;
                }
            }

            // Determine artifact path.
            auto const& path =
                IsTreeObject(info.type)
                    ? storage_->CAS().TreePath(info.digest)
                    : storage_->CAS().BlobPath(info.digest,
                                               IsExecutableObject(info.type));
            if (not path) {
                return false;
            }

            // Read artifact content (file or symlink).
            auto const& content = FileSystemManager::ReadFile(*path);
            if (not content) {
                return false;
            }

            // Regenerate digest since object infos read by
            // storage_->ReadTreeInfos() will contain 0 as size.
            ArtifactDigest digest;
            if (IsTreeObject(info.type)) {
                digest = ArtifactDigest::Create<ObjectType::Tree>(*content);
            }
            else {
                digest = ArtifactDigest::Create<ObjectType::File>(*content);
            }

            // Collect blob.
            try {
                container.Emplace(
                    BazelBlob{digest, *content, IsExecutableObject(info.type)});
            } catch (std::exception const& ex) {
                Logger::Log(
                    LogLevel::Error, "failed to emplace blob: ", ex.what());
                return false;
            }
        }

        // Upload blobs to remote CAS.
        return api->Upload(container, /*skip_find_missing=*/true);
    }

    [[nodiscard]] auto RetrieveToMemory(
        Artifact::ObjectInfo const& artifact_info) noexcept
        -> std::optional<std::string> override {
        std::optional<std::filesystem::path> location{};
        if (IsTreeObject(artifact_info.type)) {
            location = storage_->CAS().TreePath(artifact_info.digest);
        }
        else {
            location = storage_->CAS().BlobPath(
                artifact_info.digest, IsExecutableObject(artifact_info.type));
        }
        std::optional<std::string> content = std::nullopt;
        if (location) {
            content = FileSystemManager::ReadFile(*location);
        }
        if ((not content) and repo_config_) {
            content =
                GitApi(repo_config_.value()).RetrieveToMemory(artifact_info);
        }
        return content;
    }

    [[nodiscard]] auto Upload(BlobContainer const& blobs,
                              bool /*skip_find_missing*/) noexcept
        -> bool final {
        for (auto const& blob : blobs) {
            auto const is_tree = NativeSupport::IsTree(blob.digest.hash());
            auto cas_digest =
                is_tree ? storage_->CAS().StoreTree(blob.data)
                        : storage_->CAS().StoreBlob(blob.data, blob.is_exec);
            if (not cas_digest or not std::equal_to<bazel_re::Digest>{}(
                                      *cas_digest, blob.digest)) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] auto UploadFile(std::filesystem::path const& file_path,
                                  ObjectType type) noexcept -> bool override {
        Logger::Log(LogLevel::Trace, [&file_path, &type]() {
            return fmt::format("Storing {} of type {} directly to CAS.",
                               file_path.string(),
                               ToChar(type));
        });
        switch (type) {
            case ObjectType::Tree:
                return storage_->CAS()
                    .StoreTree</* kOwner= */ true>(file_path)
                    .has_value();
            case ObjectType::Symlink:
            case ObjectType::File:
                return storage_->CAS()
                    .StoreBlob</* kOwner= */ true>(file_path,
                                                   /* is_executable= */ false)
                    .has_value();
            case ObjectType::Executable:
                return storage_->CAS()
                    .StoreBlob</* kOwner= */ true>(file_path,
                                                   /* is_executable= */ true)
                    .has_value();
        }
        Ensures(false);  // unreachable
        return false;
    }

    /// NOLINTNEXTLINE(misc-no-recursion)
    [[nodiscard]] auto UploadBlobTree(BlobTreePtr const& blob_tree) noexcept
        -> bool {

        // Create digest list from blobs for batch availability check.
        std::vector<ArtifactDigest> digests;
        digests.reserve(blob_tree->size());
        std::unordered_map<ArtifactDigest, BlobTreePtr> tree_map;
        for (auto const& node : *blob_tree) {
            auto digest = ArtifactDigest{node->Blob().digest};
            digests.emplace_back(digest);
            try {
                tree_map.emplace(std::move(digest), node);
            } catch (...) {
                return false;
            }
        }

        // Find missing digests.
        auto missing_digests = IsAvailable(digests);

        // Process missing blobs.
        BlobContainer container;
        for (auto const& digest : missing_digests) {
            if (auto it = tree_map.find(digest); it != tree_map.end()) {
                auto const& node = it->second;
                // Process trees.
                if (node->IsTree()) {
                    if (not UploadBlobTree(node)) {
                        return false;
                    }
                }
                // Store blob.
                try {
                    container.Emplace(node->Blob());
                } catch (...) {
                    return false;
                }
            }
        }

        return Upload(container, /*skip_find_missing=*/true);
    }

    [[nodiscard]] auto UploadTree(
        std::vector<DependencyGraph::NamedArtifactNodePtr> const&
            artifacts) noexcept -> std::optional<ArtifactDigest> final {
        auto build_root = DirectoryTree::FromNamedArtifacts(artifacts);
        if (not build_root) {
            Logger::Log(LogLevel::Debug,
                        "failed to create build root from artifacts.");
            return std::nullopt;
        }

        if (Compatibility::IsCompatible()) {
            BlobContainer blobs{};
            auto const& cas = storage_->CAS();
            auto digest = BazelMsgFactory::CreateDirectoryDigestFromTree(
                *build_root,
                [&cas](std::vector<bazel_re::Digest> const& digests,
                       std::vector<std::string>* targets) {
                    targets->reserve(digests.size());
                    for (auto const& digest : digests) {
                        auto p = cas.BlobPath(digest, /*is_executable=*/false);
                        auto content = FileSystemManager::ReadFile(*p);
                        targets->emplace_back(*content);
                    }
                },
                [&blobs](BazelBlob&& blob) { blobs.Emplace(std::move(blob)); });
            if (not digest) {
                Logger::Log(LogLevel::Debug,
                            "failed to create digest for build root.");
                return std::nullopt;
            }
            Logger::Log(LogLevel::Trace, [&digest]() {
                std::ostringstream oss{};
                oss << "upload root directory" << std::endl;
                oss << fmt::format(" - root digest: {}", digest->hash())
                    << std::endl;
                return oss.str();
            });
            if (not Upload(blobs, /*skip_find_missing=*/false)) {
                Logger::Log(LogLevel::Debug,
                            "failed to upload blobs for build root.");
                return std::nullopt;
            }
            return ArtifactDigest{*digest};
        }

        auto blob_tree = BlobTree::FromDirectoryTree(*build_root);
        if (not blob_tree) {
            Logger::Log(LogLevel::Debug,
                        "failed to create blob tree for build root.");
            return std::nullopt;
        }
        auto tree_blob = (*blob_tree)->Blob();
        // Upload blob tree if tree is not available at the remote side (content
        // first).
        if (not IsAvailable(ArtifactDigest{tree_blob.digest})) {
            if (not UploadBlobTree(*blob_tree)) {
                Logger::Log(LogLevel::Debug,
                            "failed to upload blob tree for build root.");
                return std::nullopt;
            }
            if (not Upload(BlobContainer{{tree_blob}},
                           /*skip_find_missing=*/true)) {
                Logger::Log(LogLevel::Debug,
                            "failed to upload tree blob for build root.");
                return std::nullopt;
            }
        }
        return ArtifactDigest{tree_blob.digest};
    }

    [[nodiscard]] auto IsAvailable(ArtifactDigest const& digest) const noexcept
        -> bool final {
        return static_cast<bool>(
            NativeSupport::IsTree(static_cast<bazel_re::Digest>(digest).hash())
                ? storage_->CAS().TreePath(digest)
                : storage_->CAS().BlobPath(digest, false));
    }

    [[nodiscard]] auto IsAvailable(std::vector<ArtifactDigest> const& digests)
        const noexcept -> std::vector<ArtifactDigest> final {
        std::vector<ArtifactDigest> result;
        for (auto const& digest : digests) {
            auto const& path = NativeSupport::IsTree(
                                   static_cast<bazel_re::Digest>(digest).hash())
                                   ? storage_->CAS().TreePath(digest)
                                   : storage_->CAS().BlobPath(digest, false);
            if (not path) {
                result.push_back(digest);
            }
        }
        return result;
    }

    [[nodiscard]] auto SplitBlob(ArtifactDigest const& blob_digest)
        const noexcept -> std::optional<std::vector<ArtifactDigest>> final {
        Logger::Log(LogLevel::Debug, "SplitBlob({})", blob_digest.hash());
        auto split_result = CASUtils::SplitBlobFastCDC(
            static_cast<bazel_re::Digest>(blob_digest), *storage_);
        if (std::holds_alternative<grpc::Status>(split_result)) {
            auto* status = std::get_if<grpc::Status>(&split_result);
            Logger::Log(LogLevel::Error, status->error_message());
            return std::nullopt;
        }
        auto* chunk_digests =
            std::get_if<std::vector<bazel_re::Digest>>(&split_result);
        Logger::Log(LogLevel::Debug, [&blob_digest, &chunk_digests]() {
            std::stringstream ss{};
            ss << "Split blob " << blob_digest.hash() << ":"
               << blob_digest.size() << " into " << chunk_digests->size()
               << " chunks: [ ";
            for (auto const& chunk_digest : *chunk_digests) {
                ss << chunk_digest.hash() << ":" << chunk_digest.size_bytes()
                   << " ";
            }
            ss << "]";
            return ss.str();
        });
        auto artifact_digests = std::vector<ArtifactDigest>{};
        artifact_digests.reserve(chunk_digests->size());
        std::transform(
            chunk_digests->cbegin(),
            chunk_digests->cend(),
            std::back_inserter(artifact_digests),
            [](auto const& digest) { return ArtifactDigest{digest}; });
        return artifact_digests;
    }

    [[nodiscard]] auto BlobSplitSupport() const noexcept -> bool final {
        return true;
    }

    [[nodiscard]] auto SpliceBlob(
        ArtifactDigest const& blob_digest,
        std::vector<ArtifactDigest> const& chunk_digests) const noexcept
        -> std::optional<ArtifactDigest> final {
        Logger::Log(LogLevel::Debug,
                    "SpliceBlob({}, {} chunks)",
                    blob_digest.hash(),
                    chunk_digests.size());
        auto digests = std::vector<bazel_re::Digest>{};
        digests.reserve(chunk_digests.size());
        std::transform(
            chunk_digests.cbegin(),
            chunk_digests.cend(),
            std::back_inserter(digests),
            [](auto const& artifact_digest) {
                return static_cast<bazel_re::Digest>(artifact_digest);
            });
        auto splice_result = CASUtils::SpliceBlob(
            static_cast<bazel_re::Digest>(blob_digest), digests, *storage_);
        if (std::holds_alternative<grpc::Status>(splice_result)) {
            auto* status = std::get_if<grpc::Status>(&splice_result);
            Logger::Log(LogLevel::Error, status->error_message());
            return std::nullopt;
        }
        auto* digest = std::get_if<bazel_re::Digest>(&splice_result);
        return ArtifactDigest{*digest};
    }

    [[nodiscard]] auto BlobSpliceSupport() const noexcept -> bool final {
        return true;
    }

  private:
    std::optional<gsl::not_null<RepositoryConfig*>> repo_config_{};
    gsl::not_null<Storage const*> storage_ = &Storage::Instance();
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_LOCAL_API_HPP
