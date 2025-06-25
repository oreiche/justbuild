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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_ENGINE_EXECUTOR_EXECUTOR_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_ENGINE_EXECUTOR_EXECUTOR_HPP

#include <algorithm>
#include <chrono>
#include <cmath>
#include <compare>
#include <exception>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "fmt/core.h"
#include "gsl/gsl"
#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/expression/evaluator.hpp"
#include "src/buildtool/build_engine/target_map/configured_target.hpp"
#include "src/buildtool/common/action.hpp"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_blob.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/common/git_hashes_converter.hpp"
#include "src/buildtool/common/protocol_traits.hpp"
#include "src/buildtool/common/remote/remote_common.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/common/statistics.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/bazel_msg/execution_config.hpp"
#include "src/buildtool/execution_api/common/api_bundle.hpp"
#include "src/buildtool/execution_api/common/common_api.hpp"
#include "src/buildtool/execution_api/common/execution_action.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/execution_api/common/execution_response.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_api.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/execution_api/remote/context.hpp"
#include "src/buildtool/execution_engine/dag/dag.hpp"
#include "src/buildtool/execution_engine/executor/context.hpp"
#include "src/buildtool/execution_engine/tree_operations/tree_operations_utils.hpp"
#include "src/buildtool/file_system/file_root.hpp"
#include "src/buildtool/file_system/git_tree.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/profile/profile.hpp"
#include "src/buildtool/progress_reporting/progress.hpp"
#include "src/buildtool/progress_reporting/task_tracker.hpp"
#include "src/utils/cpp/back_map.hpp"
#include "src/utils/cpp/expected.hpp"
#include "src/utils/cpp/hex_string.hpp"
#include "src/utils/cpp/path_rebase.hpp"
#include "src/utils/cpp/prefix.hpp"
#include "src/utils/cpp/tmp_dir.hpp"

/// \brief Implementations for executing actions and uploading artifacts.
class ExecutorImpl {
  public:
    /// \brief Computes the result of a tree-overlay action.
    /// \returns std::nullopt on success, nullptr on error.
    [[nodiscard]] static auto ExecuteTreeOverlayAction(
        Logger const& logger,
        gsl::not_null<DependencyGraph::ActionNode const*> const& action,
        IExecutionApi const& api,
        gsl::not_null<Progress*> const& progress)
        -> std::optional<IExecutionResponse::Ptr> {
        progress->TaskTracker().Start(action->Content().Id());
        std::vector<DependencyGraph::NamedArtifactNodePtr> inputs =
            action->Dependencies();
        std::sort(inputs.begin(), inputs.end(), [](auto a, auto b) {
            return a.path < b.path;
        });
        logger.Emit(LogLevel::Debug, [&]() {
            std::ostringstream oss{};
            oss << "Requested tree-overlay action is " << action->Content().Id()
                << "\n";
            oss << "Trees to overlay:";
            for (auto const& input : inputs) {
                oss << "\n - " << input.node->Content().Info()->digest.hash()
                    << ":" << input.node->Content().Info()->digest.size() << ":"
                    << ToChar(input.node->Content().Info()->type);
            }
            return oss.str();
        });
        auto tree = *inputs[0].node->Content().Info();
        for (auto const& overlay : inputs) {
            auto computed_overlay = TreeOperationsUtils::ComputeTreeOverlay(
                api,
                tree,
                *overlay.node->Content().Info(),
                action->Content().IsOverlayDisjoint());
            if (not computed_overlay) {
                logger.Emit(LogLevel::Error,
                            "Tree-overlay computation failed:\n{}",
                            computed_overlay.error());
                return nullptr;
            }
            tree = computed_overlay.value();
        }
        auto const& tree_artifact = action->OutputDirs()[0].node->Content();
        bool failed_inputs = false;
        for (auto const& [local_path, artifact] : inputs) {
            failed_inputs |= artifact->Content().Info()->failed;
        }
        tree_artifact.SetObjectInfo(
            tree.digest, ObjectType::Tree, failed_inputs);
        progress->TaskTracker().Stop(action->Content().Id());
        return std::nullopt;
    }

    /// \brief Execute action and obtain response.
    /// \returns std::nullopt for actions without response (e.g., tree actions).
    /// \returns nullptr on error.
    [[nodiscard]] static auto ExecuteAction(
        Logger const& logger,
        gsl::not_null<DependencyGraph::ActionNode const*> const& action,
        IExecutionApi const& api,
        ExecutionProperties const& merged_properties,
        gsl::not_null<RemoteContext const*> const& remote_context,
        std::chrono::milliseconds const& timeout,
        IExecutionAction::CacheFlag cache_flag,
        gsl::not_null<Statistics*> const& stats,
        gsl::not_null<Progress*> const& progress) noexcept
        -> std::optional<IExecutionResponse::Ptr> {
        try {
            if (action->Content().IsTreeOverlayAction()) {
                return ExecuteTreeOverlayAction(logger, action, api, progress);
            }
            auto const& inputs = action->Dependencies();
            auto const tree_action = action->Content().IsTreeAction();

            logger.Emit(LogLevel::Trace, [&inputs, tree_action]() {
                std::ostringstream oss{};
                oss << "execute " << (tree_action ? "tree " : "") << "action"
                    << std::endl;
                for (auto const& [local_path, artifact] : inputs) {
                    auto const& info = artifact->Content().Info();
                    oss << fmt::format(
                               " - needs {} {}",
                               local_path,
                               info ? info->ToString() : std::string{"[???]"})
                        << std::endl;
                }
                return oss.str();
            });

            auto const root_digest = CreateRootDigest(api, inputs);
            if (not root_digest) {
                logger.Emit(
                    LogLevel::Error,
                    "failed to create root digest for input artifacts.");
                return nullptr;
            }

            if (tree_action) {
                auto const& tree_artifact =
                    action->OutputDirs()[0].node->Content();
                bool failed_inputs = false;
                for (auto const& [local_path, artifact] : inputs) {
                    failed_inputs |= artifact->Content().Info()->failed;
                }
                tree_artifact.SetObjectInfo(
                    *root_digest, ObjectType::Tree, failed_inputs);
                return std::nullopt;
            }

            // do not count statistics for rebuilder fetching from cache
            if (cache_flag != IExecutionAction::CacheFlag::FromCacheOnly) {
                progress->TaskTracker().Start(action->Content().Id());
                stats->IncrementActionsQueuedCounter();
            }

            // get the alternative endpoint
            auto alternative_api = GetAlternativeEndpoint(merged_properties,
                                                          remote_context,
                                                          api.GetHashType(),
                                                          api.GetTempSpace());
            if (alternative_api) {
                if (not api.ParallelRetrieveToCas(
                        std::vector<Artifact::ObjectInfo>{
                            Artifact::ObjectInfo{*root_digest,
                                                 ObjectType::Tree,
                                                 /* failed= */ false}},
                        *alternative_api,
                        /* jobs= */ 1,
                        /* use_blob_splitting= */ true)) {
                    logger.Emit(LogLevel::Error,
                                "Failed to sync tree {} to dispatch endpoint",
                                root_digest->hash());
                    return nullptr;
                }
            }

            auto base = action->Content().Cwd();
            auto cwd_relative_output_files =
                RebasePathStringsRelativeTo(base, action->OutputFilePaths());
            auto cwd_relative_output_dirs =
                RebasePathStringsRelativeTo(base, action->OutputDirPaths());
            auto remote_action = (alternative_api ? *alternative_api : api)
                                     .CreateAction(*root_digest,
                                                   action->Command(),
                                                   base,
                                                   cwd_relative_output_files,
                                                   cwd_relative_output_dirs,
                                                   action->Env(),
                                                   merged_properties);

            if (remote_action == nullptr) {
                logger.Emit(LogLevel::Error,
                            "failed to create action for execution.");
                return nullptr;
            }

            // set action options
            remote_action->SetCacheFlag(cache_flag);
            remote_action->SetTimeout(timeout);

            // execute action
            auto result = remote_action->Execute(&logger);

            // process result
            if (result) {
                // in compatible mode, check that all artifacts are valid
                if (not ProtocolTraits::IsNative(api.GetHashType())) {
                    auto upwards_symlinks_check = result->HasUpwardsSymlinks();
                    if (not upwards_symlinks_check) {
                        logger.Emit(LogLevel::Error,
                                    upwards_symlinks_check.error());
                        return nullptr;
                    }
                    if (upwards_symlinks_check.value()) {
                        logger.Emit(
                            LogLevel::Error,
                            "Executed action produced invalid outputs -- "
                            "upwards symlinks");
                        return nullptr;
                    }
                }
                // if alternative endpoint used, transfer any missing blobs
                if (alternative_api) {
                    auto const artifacts = result->Artifacts();
                    if (not artifacts) {
                        logger.Emit(LogLevel::Error, artifacts.error());
                        return nullptr;
                    }
                    std::vector<Artifact::ObjectInfo> object_infos{};
                    object_infos.reserve(artifacts.value()->size());
                    for (auto const& [path, info] : *artifacts.value()) {
                        object_infos.emplace_back(info);
                    }
                    if (not alternative_api->RetrieveToCas(object_infos, api)) {
                        logger.Emit(LogLevel::Warning,
                                    "Failed to retrieve back artifacts from "
                                    "dispatch endpoint");
                    }
                }
            }
            return result;
        } catch (std::exception const& ex) {
            logger.Emit(LogLevel::Error,
                        "Unexpectedly failed to execute action with:\n{}",
                        ex.what());
            return nullptr;
        }
    }

    /// \brief Ensures the artifact is available to the CAS, either checking
    /// that its existing digest corresponds to that of an object already
    /// available or by uploading it if there is no digest in the artifact. In
    /// the later case, the new digest is saved in the artifact
    /// \param[in] artifact The artifact to process.
    /// \returns True if artifact is available at the point of return, false
    /// otherwise
    [[nodiscard]] static auto VerifyOrUploadArtifact(
        Logger const& logger,
        gsl::not_null<DependencyGraph::ArtifactNode const*> const& artifact,
        gsl::not_null<const RepositoryConfig*> const& repo_config,
        ApiBundle const& apis) noexcept -> bool {
        auto const object_info_opt = artifact->Content().Info();
        auto const file_path_opt = artifact->Content().FilePath();
        // If there is no object info and no file path, the artifact can not be
        // processed: it means its definition is ill-formed or that it is the
        // output of an action, in which case it shouldn't have reached here
        if (not object_info_opt and not file_path_opt) {
            logger.Emit(LogLevel::Error,
                        "artifact {} can not be processed.",
                        ToHexString(artifact->Content().Id()));
            return false;
        }
        // If the artifact has digest, we check that an object with this digest
        // is available to the execution API
        if (object_info_opt) {
            logger.Emit(LogLevel::Trace, [&object_info_opt]() {
                std::ostringstream oss{};
                oss << fmt::format("upload KNOWN artifact: {}",
                                   object_info_opt->ToString())
                    << std::endl;
                return oss.str();
            });
            if (not apis.remote->IsAvailable(object_info_opt->digest)) {
                // Check if requested artifact is available in local CAS and
                // upload to remote CAS in case it is.
                if (apis.local->IsAvailable(object_info_opt->digest) and
                    apis.local->RetrieveToCas({*object_info_opt},
                                              *apis.remote)) {
                    return true;
                }

                if (not VerifyOrUploadKnownArtifact(
                        logger,
                        *apis.remote,
                        artifact->Content().Repository(),
                        repo_config,
                        *object_info_opt)) {
                    logger.Emit(
                        LogLevel::Error,
                        "artifact {} should be present in CAS but is missing.",
                        ToHexString(artifact->Content().Id()));
                    return false;
                }
            }
            return true;
        }

        // Otherwise, we upload the new file to make it available to the
        // execution API
        // Note that we can be sure now that file_path_opt has a value and
        // that the path stored is relative to the workspace dir, so we need to
        // prepend it
        logger.Emit(LogLevel::Trace, [&file_path_opt]() {
            std::ostringstream oss{};
            oss << fmt::format("upload LOCAL artifact: {}",
                               file_path_opt->string())
                << std::endl;
            return oss.str();
        });
        auto repo = artifact->Content().Repository();
        auto new_info =
            UploadFile(*apis.remote, repo, repo_config, *file_path_opt);
        if (not new_info) {
            logger.Emit(LogLevel::Error,
                        "artifact in {} could not be uploaded to CAS.",
                        file_path_opt->string());
            return false;
        }

        // And we save the digest object type in the artifact
        artifact->Content().SetObjectInfo(*new_info, false);
        return true;
    }

    /// \brief Uploads the content of a git tree recursively to the CAS. It is
    /// first checked which elements of a directory are not available in the
    /// CAS and the missing elements are uploaded accordingly. This ensures the
    /// invariant that if a git tree is known to the CAS all its content is also
    /// existing in the CAS.
    /// \param[in] api      The remote execution API of the CAS.
    /// \param[in] tree     The git tree to be uploaded.
    /// \returns True if the upload was successful, False in case of any error.
    [[nodiscard]] static auto VerifyOrUploadTree(Logger const& logger,
                                                 IExecutionApi const& api,
                                                 GitTree const& tree) noexcept
        -> bool {
        // create list of digests for batch check of CAS availability
        using ElementType = typename GitTree::entries_t::value_type;
        auto const back_map = BackMap<ArtifactDigest, ElementType>::Make(
            &tree, [](ElementType const& entry) {
                return ArtifactDigestFactory::Create(
                    HashFunction::Type::GitSHA1,
                    entry.second->Hash(),
                    *entry.second->Size(),
                    entry.second->IsTree());
            });
        if (back_map == nullptr) {
            return false;
        }

        logger.Emit(LogLevel::Trace, [&tree]() {
            std::ostringstream oss{};
            oss << "upload directory content of " << tree.FileRootHash()
                << std::endl;
            for (auto const& [path, entry] : tree) {
                oss << fmt::format(" - {}: {}", path, entry->Hash())
                    << std::endl;
            }
            return oss.str();
        });

        // find missing digests
        auto const missing_digests = api.GetMissingDigests(back_map->GetKeys());
        auto const missing_entries =
            back_map->IterateReferences(&missing_digests);

        // process missing trees
        for (auto const& [_, value] : missing_entries) {
            auto const entry = value->second;
            if (entry->IsTree()) {
                auto const& tree = entry->Tree();
                if (not tree or not VerifyOrUploadTree(logger, api, *tree)) {
                    return false;
                }
            }
        }

        // upload missing entries (blobs or trees)
        HashFunction const hash_function{api.GetHashType()};
        std::unordered_set<ArtifactBlob> container;
        for (auto const& [digest, value] : missing_entries) {
            auto const entry = value->second;
            auto content = entry->RawData();
            if (not content.has_value()) {
                return false;
            }
            auto blob = ArtifactBlob::FromMemory(
                hash_function, entry->Type(), *std::move(content));
            if (not blob.has_value()) {
                return false;
            }
            // store and/or upload blob, taking into account the maximum
            // transfer size
            if (not UpdateContainerAndUpload(
                    &container,
                    *std::move(blob),
                    /*exception_is_fatal=*/true,
                    [&api](std::unordered_set<ArtifactBlob>&& blobs) {
                        return api.Upload(std::move(blobs),
                                          /*skip_find_missing=*/true);
                    })) {
                return false;
            }
        }
        // upload remaining blobs
        return api.Upload(std::move(container), /*skip_find_missing=*/true);
    }

    /// \brief Lookup blob via digest in local git repositories and upload.
    /// \param api          The endpoint used for uploading
    /// \param repo         The global repository name, the artifact belongs to
    /// \param info         The info of the object
    /// \param hash         The git-sha1 hash of the object
    /// \returns true on success
    [[nodiscard]] static auto VerifyOrUploadGitArtifact(
        Logger const& logger,
        IExecutionApi const& api,
        std::string const& repo,
        gsl::not_null<const RepositoryConfig*> const& repo_config,
        Artifact::ObjectInfo const& info,
        std::string const& hash) noexcept -> bool {
        std::optional<std::string> content;
        if (info.digest.IsTree()) {
            // if known tree is not available, recursively upload its content
            auto tree = ReadGitTree(repo, repo_config, hash);
            if (not tree) {
                logger.Emit(
                    LogLevel::Error, "failed to read git tree {}", hash);
                return false;
            }
            if (not VerifyOrUploadTree(logger, api, *tree)) {
                logger.Emit(LogLevel::Error,
                            "failed to verifyorupload git tree {} [{}]",
                            tree->FileRootHash(),
                            hash);
                return false;
            }
            content = tree->RawData();
        }
        else {
            // if known blob is not available, read and upload it
            content = ReadGitBlob(
                repo, repo_config, hash, IsSymlinkObject(info.type));
        }
        if (not content) {
            logger.Emit(LogLevel::Error, "failed to get content");
            return false;
        }

        auto blob = ArtifactBlob::FromMemory(
            HashFunction{api.GetHashType()}, info.type, *std::move(content));
        if (not blob.has_value()) {
            logger.Emit(LogLevel::Error, "failed to create ArtifactBlob");
            return false;
        }

        return api.Upload({*std::move(blob)},
                          /*skip_find_missing=*/true);
    }

    [[nodiscard]] static auto ReadGitBlob(
        std::string const& repo,
        gsl::not_null<const RepositoryConfig*> const& repo_config,
        std::string const& hash,
        bool is_symlink) noexcept -> std::optional<std::string> {
        std::optional<std::string> blob{};
        if (auto const* ws_root = repo_config->WorkspaceRoot(repo)) {
            // try to obtain blob from local workspace's Git CAS, if any
            blob = ws_root->ReadBlob(hash, is_symlink);
        }
        if (not blob) {
            // try to obtain blob from global Git CAS, if any
            blob = repo_config->ReadBlobFromGitCAS(hash, is_symlink);
        }
        return blob;
    }

    [[nodiscard]] static auto ReadGitTree(
        std::string const& repo,
        gsl::not_null<const RepositoryConfig*> const& repo_config,
        std::string const& hash) noexcept -> std::optional<GitTree> {
        std::optional<GitTree> tree{};
        if (auto const* ws_root = repo_config->WorkspaceRoot(repo)) {
            // try to obtain tree from local workspace's Git CAS, if any
            tree = ws_root->ReadTree(hash);
        }
        if (not tree) {
            // try to obtain tree from global Git CAS, if any
            tree = repo_config->ReadTreeFromGitCAS(hash);
        }
        return tree;
    }

    /// \brief Lookup blob via digest in local git repositories and upload.
    /// \param api          The endpoint used for uploading
    /// \param repo         The global repository name, the artifact belongs to
    /// \param repo_config  Configuration specifying the workspace root
    /// \param info         The info of the object
    /// \returns true on success
    [[nodiscard]] static auto VerifyOrUploadKnownArtifact(
        Logger const& logger,
        IExecutionApi const& api,
        std::string const& repo,
        gsl::not_null<const RepositoryConfig*> const& repo_config,
        Artifact::ObjectInfo const& info) noexcept -> bool {
        if (not ProtocolTraits::IsNative(api.GetHashType())) {
            auto opt =
                GitHashesConverter::Instance().GetGitEntry(info.digest.hash());
            if (opt) {
                auto const& [git_sha1_hash, comp_repo] = *opt;
                return VerifyOrUploadGitArtifact(
                    logger, api, comp_repo, repo_config, info, git_sha1_hash);
            }
            return false;
        }
        return VerifyOrUploadGitArtifact(
            logger, api, repo, repo_config, info, info.digest.hash());
    }

    /// \brief Lookup file via path in local workspace root and upload.
    /// \param api          The endpoint used for uploading
    /// \param repo         The global repository name, the artifact belongs to
    /// \param repo_config  Configuration specifying the workspace root
    /// \param file_path    The path of the file to be read
    /// \returns The computed object info on success
    [[nodiscard]] static auto UploadFile(
        IExecutionApi const& api,
        std::string const& repo,
        gsl::not_null<const RepositoryConfig*> const& repo_config,
        std::filesystem::path const& file_path) noexcept
        -> std::optional<Artifact::ObjectInfo> {
        auto const* ws_root = repo_config->WorkspaceRoot(repo);
        if (ws_root == nullptr) {
            return std::nullopt;
        }
        auto const object_type = ws_root->BlobType(file_path);
        if (not object_type) {
            return std::nullopt;
        }
        auto content = ws_root->ReadContent(file_path);
        if (not content.has_value()) {
            return std::nullopt;
        }

        auto blob = ArtifactBlob::FromMemory(
            HashFunction{api.GetHashType()}, *object_type, *std::move(content));
        if (not blob.has_value()) {
            return std::nullopt;
        }
        auto digest = blob->GetDigest();
        if (not api.Upload({*std::move(blob)})) {
            return std::nullopt;
        }
        return Artifact::ObjectInfo{.digest = std::move(digest),
                                    .type = *object_type};
    }

    /// \brief Add digests and object type to artifact nodes for all outputs of
    /// the action that was run
    void static SaveObjectInfo(
        IExecutionResponse::ArtifactInfos const& artifacts,
        gsl::not_null<DependencyGraph::ActionNode const*> const& action,
        bool fail_artifacts) noexcept {
        auto base = action->Content().Cwd();
        for (auto const& [name, node] : action->OutputFiles()) {
            node->Content().SetObjectInfo(
                artifacts.at(RebasePathStringRelativeTo(base, name)),
                fail_artifacts);
        }
        for (auto const& [name, node] : action->OutputDirs()) {
            node->Content().SetObjectInfo(
                artifacts.at(RebasePathStringRelativeTo(base, name)),
                fail_artifacts);
        }
    }

    /// \brief Create root tree digest for input artifacts.
    /// \param api          The endpoint required for uploading
    /// \param artifacts    The artifacts to create the root tree digest from
    [[nodiscard]] static auto CreateRootDigest(
        IExecutionApi const& api,
        std::vector<DependencyGraph::NamedArtifactNodePtr> const& artifacts)
        -> std::optional<ArtifactDigest> {
        if (artifacts.size() == 1 and
            (artifacts[0].path == "." or artifacts[0].path.empty())) {
            auto const& info = artifacts[0].node->Content().Info();
            if (info and IsTreeObject(info->type)) {
                // Artifact list contains single tree with path "." or "". Reuse
                // the existing tree artifact by returning its digest.
                return info->digest;
            }
        }
        return api.UploadTree(artifacts);
    }
    /// \brief Check that all outputs expected from the action description
    /// are present in the artifacts map and of the expected type
    template <bool kIsTree = false>
    [[nodiscard]] static auto CheckOutputsExist(
        IExecutionResponse::ArtifactInfos const& artifacts,
        std::vector<Action::LocalPath> const& outputs,
        std::string base) -> bool {
        return std::all_of(
            outputs.begin(),
            outputs.end(),
            [&artifacts, &base](Action::LocalPath const& output) {
                auto it =
                    artifacts.find(RebasePathStringRelativeTo(base, output));
                if (it != artifacts.end()) {
                    auto const& type = it->second.type;
                    if constexpr (kIsTree) {
                        return IsTreeObject(type) or IsSymlinkObject(type);
                    }
                    else {
                        return IsFileObject(type) or IsSymlinkObject(type);
                    }
                }
                return false;
            });
    }

    /// \brief Parse response and write object info to DAG's artifact nodes.
    /// \returns false on non-zero exit code or if output artifacts are missing
    [[nodiscard]] static auto ParseResponse(
        Logger const& logger,
        IExecutionResponse::Ptr const& response,
        gsl::not_null<DependencyGraph::ActionNode const*> const& action,
        gsl::not_null<Statistics*> const& stats,
        gsl::not_null<Progress*> const& progress,
        bool count_as_executed = false) -> bool {
        logger.Emit(LogLevel::Trace, "finished execution");

        if (not response) {
            logger.Emit(LogLevel::Trace, "response is empty");
            PrintError(logger, action, progress);
            return false;
        }

        if (not count_as_executed and response->IsCached()) {
            logger.Emit(LogLevel::Trace, " - served from cache");
            stats->IncrementActionsCachedCounter();
        }
        else {
            stats->IncrementActionsExecutedCounter();
        }
        progress->TaskTracker().Stop(action->Content().Id());

        PrintInfo(logger, action, response);
        bool action_failed = false;
        bool should_fail_outputs = false;
        for (auto const& [local_path, node] : action->Dependencies()) {
            should_fail_outputs |= node->Content().Info()->failed;
        }
        if (response->ExitCode() != 0) {
            if (action->MayFail()) {
                should_fail_outputs = true;
                action_failed = true;
            }
            else {
                logger.Emit(LogLevel::Error,
                            "action returned non-zero exit code {}",
                            response->ExitCode());
                PrintError(logger, action, progress);
                return false;
            }
        }

        auto const artifacts = response->Artifacts();
        if (not artifacts) {
            logger.Emit(LogLevel::Error, artifacts.error());
            return false;
        }

        auto output_files = action->OutputFilePaths();
        auto output_dirs = action->OutputDirPaths();
        std::sort(output_files.begin(), output_files.end());
        std::sort(output_dirs.begin(), output_dirs.end());

        if (artifacts.value()->empty() or
            not CheckOutputsExist</*kIsTree=*/false>(
                *artifacts.value(), output_files, action->Content().Cwd()) or
            not CheckOutputsExist</*kIsTree=*/true>(
                *artifacts.value(), output_dirs, action->Content().Cwd())) {
            logger.Emit(LogLevel::Error, [&]() {
                std::ostringstream message{};
                if (action_failed) {
                    message << *(action->MayFail()) << " (exit code "
                            << response->ExitCode() << ")\nMoreover ";
                }
                message << "action executed with missing outputs.\nAction "
                           "outputs should be the following artifacts:";
                for (auto const& output : output_files) {
                    message << "\n  - file: " << output;
                }
                for (auto const& output : output_dirs) {
                    message << "\n  - dir: " << output;
                }
                return message.str();
            });
            PrintError(logger, action, progress);
            return false;
        }

        SaveObjectInfo(*artifacts.value(), action, should_fail_outputs);
        if (action_failed) {
            logger.Emit(LogLevel::Warning, [&]() {
                std::ostringstream message{};
                auto base = action->Content().Cwd();
                message << *(action->MayFail()) << " (exit code "
                        << response->ExitCode() << "); outputs:";
                for (auto const& name : output_files) {
                    message << "\n - " << nlohmann::json(name).dump() << " "
                            << artifacts.value()
                                   ->at(RebasePathStringRelativeTo(base, name))
                                   .ToString();
                }
                for (auto const& name : output_dirs) {
                    message << "\n - " << nlohmann::json(name).dump() << " "
                            << artifacts.value()
                                   ->at(RebasePathStringRelativeTo(base, name))
                                   .ToString();
                }
                return message.str();
            });
        }

        return true;
    }

    /// \brief Write out if response is empty and otherwise, write out
    /// standard error/output if they are present
    void static PrintInfo(
        Logger const& logger,
        gsl::not_null<DependencyGraph::ActionNode const*> const& action,
        IExecutionResponse::Ptr const& response) {
        if (not response) {
            logger.Emit(LogLevel::Error, "response is empty");
            return;
        }
        auto const has_err = response->HasStdErr();
        auto const has_out = response->HasStdOut();
        auto build_message = [has_err, has_out, &action, &response]() {
            using namespace std::string_literals;
            auto message = ""s;
            bool has_both = has_err and has_out;
            if (has_err or has_out) {
                if (has_both) {
                    message += "Output"s;
                }
                else {
                    message += has_out ? "Stdout"s : "Stderr"s;
                }
                message += " of command ";
            }
            message += nlohmann::json(action->Command()).dump() +
                       " in environment " +
                       nlohmann::json(action->Env()).dump() + "\n";
            if (response->HasStdOut()) {
                if (has_both) {
                    message += "Stdout:\n";
                }
                message += PrefixLines(response->StdOut());
            }
            if (response->HasStdErr()) {
                if (has_both) {
                    message += "Stderr:\n";
                }
                message += PrefixLines(response->StdErr());
            }
            return message;
        };
        logger.Emit((has_err or has_out) ? LogLevel::Info : LogLevel::Debug,
                    std::move(build_message));
    }

    void static PrintError(
        Logger const& logger,
        gsl::not_null<DependencyGraph::ActionNode const*> const& action,
        gsl::not_null<Progress*> const& progress) {
        std::ostringstream msg{};
        if (action->Content().IsTreeOverlayAction() or
            action->Content().IsTreeAction()) {
            msg << "Failed to execute tree";
            if (action->Content().IsTreeOverlayAction()) {
                msg << "-overlay";
            }
            msg << " action.";
        }
        else {
            msg << "Failed to execute command ";
            msg << nlohmann::json(action->Command()).dump();
            msg << " in environment ";
            msg << nlohmann::json(action->Env()).dump();
        }
        auto const& origin_map = progress->OriginMap();
        auto const origins = origin_map.find(action->Content().Id());
        if (origins != origin_map.end() and not origins->second.empty()) {
            msg << "\nrequested by";
            for (auto const& origin : origins->second) {
                msg << "\n - ";
                msg << origin.first.ToShortString(
                    Evaluator::GetExpressionLogLimit());
                msg << "#";
                msg << origin.second;
            }
        }
        if (action->Content().IsTreeOverlayAction()) {
            msg << "\ninputs were";
            std::vector<DependencyGraph::NamedArtifactNodePtr> inputs =
                action->Dependencies();
            std::sort(inputs.begin(), inputs.end(), [](auto a, auto b) {
                return a.path < b.path;
            });
            for (auto const& input : inputs) {
                msg << "\n - " << input.node->Content().Info()->digest.hash()
                    << ":" << input.node->Content().Info()->digest.size() << ":"
                    << ToChar(input.node->Content().Info()->type);
            }
        }
        logger.Emit(LogLevel::Error, "{}", msg.str());
    }

    [[nodiscard]] static auto ScaleTime(std::chrono::milliseconds t,
                                        double f) -> std::chrono::milliseconds {
        return std::chrono::milliseconds(
            std::lround(static_cast<double>(t.count()) * f));
    }

    [[nodiscard]] static auto MergeProperties(
        const ExecutionProperties& base,
        const ExecutionProperties& overlay) {
        ExecutionProperties result = base;
        for (auto const& [k, v] : overlay) {
            result[k] = v;
        }
        return result;
    }

  private:
    /// \brief Get the alternative endpoint based on a specified set of platform
    /// properties. These are checked against the dispatch list of an existing
    /// remote context.
    [[nodiscard]] static auto GetAlternativeEndpoint(
        const ExecutionProperties& properties,
        const gsl::not_null<RemoteContext const*>& remote_context,
        HashFunction::Type hash_type,
        TmpDir::Ptr temp_space) -> std::unique_ptr<BazelApi> {
        for (auto const& [pred, endpoint] :
             remote_context->exec_config->dispatch) {
            bool match = true;
            for (auto const& [k, v] : pred) {
                auto const v_it = properties.find(k);
                if (not(v_it != properties.end() and v_it->second == v)) {
                    match = false;
                }
            }
            if (match) {
                Logger::Log(LogLevel::Debug, [endpoint = endpoint] {
                    return fmt::format("Dispatching action to endpoint {}",
                                       endpoint.ToJson().dump());
                });
                ExecutionConfiguration config;
                return std::make_unique<BazelApi>(
                    "alternative remote execution",
                    endpoint.host,
                    endpoint.port,
                    remote_context->auth,
                    remote_context->retry_config,
                    config,
                    HashFunction{hash_type},
                    std::move(temp_space));
            }
        }
        return nullptr;
    }
};

/// \brief Executor for using concrete Execution API.
class Executor {
    using Impl = ExecutorImpl;
    using CF = IExecutionAction::CacheFlag;

  public:
    /// \brief Create rebuilder for action comparision of two endpoints.
    /// \param context  Execution context. References all the required
    /// information needed to execute actions on a specified remote endpoint.
    /// \param logger   Overwrite the default logger. Useful for orchestrated
    /// builds, i.e., triggered by just serve.
    /// \param timeout  Timeout for action execution.
    explicit Executor(
        gsl::not_null<ExecutionContext const*> const& context,
        Logger const* logger = nullptr,  // log in caller logger, if given
        std::chrono::milliseconds timeout = IExecutionAction::kDefaultTimeout)
        : context_{*context}, logger_{logger}, timeout_{timeout} {}

    /// \brief Run an action in a blocking manner
    /// This method must be thread-safe as it could be called in parallel
    /// \param[in] action The action to execute.
    /// \returns True if execution was successful, false otherwise
    [[nodiscard]] auto Process(
        gsl::not_null<DependencyGraph::ActionNode const*> const& action)
        const noexcept -> bool {
        try {
            // to avoid always creating a logger we might not need, which is a
            // non-copyable and non-movable object, we need some code
            // duplication
            if (logger_ != nullptr) {
                auto const response = Impl::ExecuteAction(
                    *logger_,
                    action,
                    *context_.apis->remote,
                    Impl::MergeProperties(context_.remote_context->exec_config
                                              ->platform_properties,
                                          action->ExecutionProperties()),
                    context_.remote_context,
                    Impl::ScaleTime(timeout_, action->TimeoutScale()),
                    action->NoCache() ? CF::DoNotCacheOutput : CF::CacheOutput,
                    context_.statistics,
                    context_.progress);
                // check response and save digests of results
                if (not response) {
                    return true;
                }
                auto result = Impl::ParseResponse(*logger_,
                                                  *response,
                                                  action,
                                                  context_.statistics,
                                                  context_.progress);
                if (context_.profile) {
                    (*context_.profile)
                        ->NoteActionCompleted(action->Content().Id(),
                                              *response,
                                              action->Content().Cwd());
                }
                return result;
            }

            Logger logger("action:" + action->Content().Id());

            auto const response = Impl::ExecuteAction(
                logger,
                action,
                *context_.apis->remote,
                Impl::MergeProperties(
                    context_.remote_context->exec_config->platform_properties,
                    action->ExecutionProperties()),
                context_.remote_context,
                Impl::ScaleTime(timeout_, action->TimeoutScale()),
                action->NoCache() ? CF::DoNotCacheOutput : CF::CacheOutput,
                context_.statistics,
                context_.progress);

            // check response and save digests of results
            if (not response) {
                return true;
            }
            auto result = Impl::ParseResponse(logger,
                                              *response,
                                              action,
                                              context_.statistics,
                                              context_.progress);
            if (context_.profile) {
                (*context_.profile)
                    ->NoteActionCompleted(action->Content().Id(),
                                          *response,
                                          action->Content().Cwd());
            }
            return result;
        } catch (std::exception const& ex) {
            Logger::Log(
                LogLevel::Error,
                "Executor: Unexpected failure processing action with:\n{}",
                ex.what());
            return false;
        }
    }

    /// \brief Check artifact is available to the CAS or upload it.
    /// \param[in] artifact The artifact to process.
    /// \param[in] repo_config The repository configuration to use
    /// \returns True if artifact is available or uploaded, false otherwise
    [[nodiscard]] auto Process(
        gsl::not_null<DependencyGraph::ArtifactNode const*> const& artifact)
        const noexcept -> bool {
        try {
            // to avoid always creating a logger we might not need, which is a
            // non-copyable and non-movable object, we need some code
            // duplication
            if (logger_ != nullptr) {
                return Impl::VerifyOrUploadArtifact(
                    *logger_, artifact, context_.repo_config, *context_.apis);
            }

            Logger logger("artifact:" + ToHexString(artifact->Content().Id()));
            return Impl::VerifyOrUploadArtifact(
                logger, artifact, context_.repo_config, *context_.apis);
        } catch (std::exception const& ex) {
            Logger::Log(
                LogLevel::Error,
                "Executor: Unexpected failure checking artifact with:\n{}",
                ex.what());
            return false;
        }
    }

  private:
    ExecutionContext const& context_;
    Logger const* logger_;
    std::chrono::milliseconds timeout_;
};

/// \brief Rebuilder for running and comparing actions of two API endpoints.
class Rebuilder {
    using Impl = ExecutorImpl;
    using CF = IExecutionAction::CacheFlag;

  public:
    /// \brief Create rebuilder for action comparision of two endpoints.
    /// \param context  Execution context. References all the required
    /// information needed to perform a rebuild, during which the results of
    /// executing actions on the regular remote endpoint and the cache endpoint
    /// are compared.
    /// \param timeout  Timeout for action execution.
    explicit Rebuilder(
        gsl::not_null<ExecutionContext const*> const& context,
        std::chrono::milliseconds timeout = IExecutionAction::kDefaultTimeout)
        : context_{*context},
          api_cached_{context_.apis->MakeRemote(
              context_.remote_context->exec_config->cache_address,
              context_.remote_context->auth,
              context_.remote_context->retry_config)},
          timeout_{timeout} {}

    [[nodiscard]] auto Process(
        gsl::not_null<DependencyGraph::ActionNode const*> const& action)
        const noexcept -> bool {
        try {
            auto const& action_id = action->Content().Id();
            Logger logger("rebuild:" + action_id);
            auto response = Impl::ExecuteAction(
                logger,
                action,
                *context_.apis->remote,
                Impl::MergeProperties(
                    context_.remote_context->exec_config->platform_properties,
                    action->ExecutionProperties()),
                context_.remote_context,
                Impl::ScaleTime(timeout_, action->TimeoutScale()),
                CF::PretendCached,
                context_.statistics,
                context_.progress);

            if (not response) {
                return true;  // action without response (e.g., tree action)
            }

            Logger logger_cached("cached:" + action_id);
            auto response_cached = Impl::ExecuteAction(
                logger_cached,
                action,
                *api_cached_,
                Impl::MergeProperties(
                    context_.remote_context->exec_config->platform_properties,
                    action->ExecutionProperties()),
                context_.remote_context,
                Impl::ScaleTime(timeout_, action->TimeoutScale()),
                CF::FromCacheOnly,
                context_.statistics,
                context_.progress);

            if (not response_cached) {
                logger_cached.Emit(LogLevel::Error,
                                   "expected regular action with response");
                return false;
            }

            if (auto error = DetectFlakyAction(
                    *response, *response_cached, action->Content())) {
                logger_cached.Emit(LogLevel::Error, *error);
                return false;
            }
            return Impl::ParseResponse(logger,
                                       *response,
                                       action,
                                       context_.statistics,
                                       context_.progress,
                                       /*count_as_executed=*/true);
        } catch (std::exception const& ex) {
            Logger::Log(
                LogLevel::Error,
                "Rebuilder: Unexpected failure processing action with:\n{}",
                ex.what());
            return false;
        }
    }

    [[nodiscard]] auto Process(
        gsl::not_null<DependencyGraph::ArtifactNode const*> const& artifact)
        const noexcept -> bool {
        try {
            Logger logger("artifact:" + ToHexString(artifact->Content().Id()));
            return Impl::VerifyOrUploadArtifact(
                logger, artifact, context_.repo_config, *context_.apis);
        } catch (std::exception const& ex) {
            Logger::Log(
                LogLevel::Error,
                "Rebuilder: Unexpected failure checking artifact with:\n{}",
                ex.what());
            return false;
        }
    }

    [[nodiscard]] auto DumpFlakyActions() const -> nlohmann::json {
        std::unique_lock lock{m_};
        auto actions = nlohmann::json::object();
        for (auto const& [action_id, outputs] : flaky_actions_) {
            for (auto const& [path, infos] : outputs) {
                actions[action_id][path]["rebuilt"] = infos.first.ToJson();
                actions[action_id][path]["cached"] = infos.second.ToJson();
            }
        }
        return {{"flaky actions", actions}, {"cache misses", cache_misses_}};
    }

  private:
    ExecutionContext const& context_;
    gsl::not_null<IExecutionApi::Ptr> const api_cached_;
    std::chrono::milliseconds timeout_;
    mutable std::mutex m_;
    mutable std::vector<std::string> cache_misses_;
    mutable std::unordered_map<
        std::string,
        std::unordered_map<
            std::string,
            std::pair<Artifact::ObjectInfo, Artifact::ObjectInfo>>>
        flaky_actions_;

    [[nodiscard]] auto DetectFlakyAction(
        IExecutionResponse::Ptr const& response,
        IExecutionResponse::Ptr const& response_cached,
        Action const& action) const -> std::optional<std::string> {
        auto& stats = *context_.statistics;
        if (response and response_cached and
            response_cached->ActionDigest() == response->ActionDigest()) {
            stats.IncrementRebuiltActionComparedCounter();
            auto const artifacts = response->Artifacts();
            if (not artifacts) {
                return artifacts.error();
            }
            auto const artifacts_cached = response_cached->Artifacts();
            if (not artifacts_cached) {
                return artifacts_cached.error();
            }
            std::ostringstream msg{};
            try {
                for (auto const& [path, info] : *artifacts.value()) {
                    auto const& info_cached =
                        artifacts_cached.value()->at(path);
                    if (info != info_cached) {
                        RecordFlakyAction(
                            &msg, action, path, info, info_cached);
                    }
                }
            } catch (std::exception const& ex) {
                return ex.what();
            }
            if (msg.tellp() > 0) {
                stats.IncrementActionsFlakyCounter();
                bool tainted = action.MayFail() or action.NoCache();
                if (tainted) {
                    stats.IncrementActionsFlakyTaintedCounter();
                }
                Logger::Log(tainted ? LogLevel::Debug : LogLevel::Warning,
                            "{}",
                            msg.str());
            }
        }
        else {
            stats.IncrementRebuiltActionMissingCounter();
            std::unique_lock lock{m_};
            cache_misses_.emplace_back(action.Id());
        }
        return std::nullopt;  // ok
    }

    void RecordFlakyAction(gsl::not_null<std::ostringstream*> const& msg,
                           Action const& action,
                           std::string const& path,
                           Artifact::ObjectInfo const& rebuilt,
                           Artifact::ObjectInfo const& cached) const noexcept {
        auto const& action_id = action.Id();
        if (msg->tellp() <= 0) {
            bool tainted = action.MayFail() or action.NoCache();
            auto cmd = GetCmdString(action);
            (*msg) << "Found flaky " << (tainted ? "tainted " : "")
                   << "action:" << std::endl
                   << " - id: " << action_id << std::endl
                   << " - cmd: " << cmd << std::endl;
        }
        (*msg) << " - output '" << path << "' differs:" << std::endl
               << "   - " << rebuilt.ToString() << " (rebuilt)" << std::endl
               << "   - " << cached.ToString() << " (cached)" << std::endl;

        std::unique_lock lock{m_};
        auto& object_map = flaky_actions_[action_id];
        try {
            object_map.emplace(path, std::make_pair(rebuilt, cached));
        } catch (std::exception const& ex) {
            Logger::Log(LogLevel::Error,
                        "recoding flaky action failed with: {}",
                        ex.what());
        }
    }

    static auto GetCmdString(Action const& action) noexcept -> std::string {
        try {
            return nlohmann::json(action.Command()).dump();
        } catch (std::exception const& ex) {
            return fmt::format("<exception: {}>", ex.what());
        }
    }
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_ENGINE_EXECUTOR_EXECUTOR_HPP
