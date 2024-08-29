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

#ifndef INCLUDED_SRC_OTHER_TOOLS_GIT_OPERATIONS_GIT_REPO_REMOTE_HPP
#define INCLUDED_SRC_OTHER_TOOLS_GIT_OPERATIONS_GIT_REPO_REMOTE_HPP

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "src/buildtool/file_system/git_repo.hpp"
#include "src/buildtool/storage/config.hpp"

extern "C" {
struct git_config;
}

/// \brief Extension to a Git repository, allowing remote Git operations.
class GitRepoRemote : public GitRepo {
  public:
    GitRepoRemote() = delete;  // no default ctor
    ~GitRepoRemote() noexcept = default;

    // allow only move, no copy
    GitRepoRemote(GitRepoRemote const&) = delete;
    GitRepoRemote(GitRepoRemote&&) noexcept;
    auto operator=(GitRepoRemote const&) = delete;
    auto operator=(GitRepoRemote&& other) noexcept -> GitRepoRemote&;

    /// \brief Factory to wrap existing open CAS in a "fake" repository.
    [[nodiscard]] static auto Open(GitCASPtr git_cas) noexcept
        -> std::optional<GitRepoRemote>;

    /// \brief Factory to open existing real repository at given location.
    [[nodiscard]] static auto Open(
        std::filesystem::path const& repo_path) noexcept
        -> std::optional<GitRepoRemote>;

    /// \brief Factory to initialize and open new real repository at location.
    /// Returns nullopt if repository init fails even after repeated tries.
    [[nodiscard]] static auto InitAndOpen(
        std::filesystem::path const& repo_path,
        bool is_bare) noexcept -> std::optional<GitRepoRemote>;

    /// \brief Retrieve commit hash from remote branch given its name.
    /// Only possible with real repository and thus non-thread-safe.
    /// If non-null, use given config snapshot to interact with config entries;
    /// otherwise, use a snapshot from the current repo and share pointer to it.
    /// Returns the retrieved commit hash, or nullopt if failure.
    /// It guarantees the logger is called exactly once with fatal if failure.
    [[nodiscard]] auto GetCommitFromRemote(
        std::shared_ptr<git_config> cfg,
        std::string const& repo_url,
        std::string const& branch,
        anon_logger_ptr const& logger) noexcept -> std::optional<std::string>;

    /// \brief Fetch from given remote. It can either fetch a given named
    /// branch, or it can fetch with base refspecs.
    /// Only possible with real repository and thus non-thread-safe.
    /// If non-null, use given config snapshot to interact with config entries;
    /// otherwise, use a snapshot from the current repo and share pointer to it.
    /// Returns a success flag. It guarantees the logger is called
    /// exactly once with fatal if failure.
    [[nodiscard]] auto FetchFromRemote(std::shared_ptr<git_config> cfg,
                                       std::string const& repo_url,
                                       std::optional<std::string> const& branch,
                                       anon_logger_ptr const& logger) noexcept
        -> bool;

    /// \brief Get commit from given branch on the remote. If URL is SSH, shells
    /// out to system git to perform an ls-remote call, ensuring correct
    /// handling of the remote connection settings (in particular proxy and
    /// SSH). For non-SSH URLs, the branch commit is retrieved asynchronously
    /// using libgit2.
    /// Returns the commit hash, as a string, or nullopt if failure.
    /// It guarantees the logger is called exactly once with fatal if failure.
    [[nodiscard]] auto UpdateCommitViaTmpRepo(
        StorageConfig const& storage_config,
        std::string const& repo_url,
        std::string const& branch,
        std::vector<std::string> const& inherit_env,
        std::string const& git_bin,
        std::vector<std::string> const& launcher,
        anon_logger_ptr const& logger) const noexcept
        -> std::optional<std::string>;

    /// \brief Fetch from a remote. If URL is SSH, shells out to system git to
    /// retrieve packs in a safe manner, with the only side-effect being that
    /// there can be some redundancy in the fetched packs.
    /// For non-SSH URLs an asynchronous fetch is performed using libgit2.
    /// Uses either a given branch, or fetches all (with base refspecs).
    /// Returns a success flag.
    /// It guarantees the logger is called exactly once with fatal if failure.
    [[nodiscard]] auto FetchViaTmpRepo(
        StorageConfig const& storage_config,
        std::string const& repo_url,
        std::optional<std::string> const& branch,
        std::vector<std::string> const& inherit_env,
        std::string const& git_bin,
        std::vector<std::string> const& launcher,
        anon_logger_ptr const& logger) noexcept -> bool;

  private:
    /// \brief Open "fake" repository wrapper for existing CAS.
    explicit GitRepoRemote(GitCASPtr git_cas) noexcept;
    /// \brief Open real repository at given location.
    explicit GitRepoRemote(std::filesystem::path const& repo_path) noexcept;
    /// \brief Construct from inherited class.
    explicit GitRepoRemote(GitRepo&&) noexcept;
};

#endif  // INCLUDED_SRC_OTHER_TOOLS_GIT_OPERATIONS_GIT_REPO_REMOTE_HPP
