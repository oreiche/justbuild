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

#ifndef INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_GIT_REPO_HPP
#define INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_GIT_REPO_HPP

#include <functional>

#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/file_system/git_cas.hpp"

extern "C" {
struct git_repository;
}

/// \brief Git repository logic.
/// Models both a real repository, owning the underlying ODB
/// (non-thread-safe), as well as a "fake" repository, which only wraps an
/// existing ODB, allowing thread-safe operations.
class GitRepo {
  public:
    // Stores the data for defining a single Git tree entry, which consists of
    // a name (flat basename) and an object type (file/executable/tree).
    struct tree_entry_t {
        tree_entry_t(std::string n, ObjectType t)
            : name{std::move(n)}, type{t} {}
        std::string name;
        ObjectType type;
        [[nodiscard]] auto operator==(tree_entry_t const& other) const noexcept
            -> bool {
            return name == other.name and type == other.type;
        }
    };

    // Tree entries by raw id. The same id might refer to multiple entries.
    // Note that sharding by id is used as this format enables a more efficient
    // internal implementation for creating trees.
    using tree_entries_t =
        std::unordered_map<std::string, std::vector<tree_entry_t>>;

    // Checks whether a list of symlinks given by their hashes are non-upwards,
    // based on content read from an actual backend.
    using SymlinksCheckFunc =
        std::function<bool(std::vector<bazel_re::Digest> const&)>;

    GitRepo() = delete;  // no default ctor
    ~GitRepo() noexcept = default;

    // allow only move, no copy
    GitRepo(GitRepo const&) = delete;
    GitRepo(GitRepo&&) noexcept;
    auto operator=(GitRepo const&) = delete;
    auto operator=(GitRepo&& other) noexcept -> GitRepo&;

    /// \brief Factory to wrap existing open CAS in a "fake" repository.
    [[nodiscard]] static auto Open(GitCASPtr git_cas) noexcept
        -> std::optional<GitRepo>;

    /// \brief Factory to open existing real repository at given location.
    [[nodiscard]] static auto Open(
        std::filesystem::path const& repo_path) noexcept
        -> std::optional<GitRepo>;

    /// \brief Factory to initialize and open new real repository at location.
    /// Returns nullopt if repository init fails even after repeated tries.
    [[nodiscard]] static auto InitAndOpen(
        std::filesystem::path const& repo_path,
        bool is_bare) noexcept -> std::optional<GitRepo>;

    [[nodiscard]] auto GetGitCAS() const noexcept -> GitCASPtr;

    [[nodiscard]] auto IsRepoFake() const noexcept -> bool;

    /// \brief Read entries from tree in CAS.
    /// Reading a tree must be backed by an object database. Therefore, a real
    /// repository is required.
    /// \param id         The object id.
    /// \param check_symlinks   Function to check non-upwardness condition.
    /// \param is_hex_id  Specify whether `id` is hex string or raw.
    /// \param ignore_special   If set, treat symlinks as absent.
    [[nodiscard]] auto ReadTree(std::string const& id,
                                SymlinksCheckFunc const& check_symlinks,
                                bool is_hex_id = false,
                                bool ignore_special = false) const noexcept
        -> std::optional<tree_entries_t>;

    /// \brief Create a flat tree from entries and store tree in CAS.
    /// Creating a tree must be backed by an object database. Therefore, a real
    /// repository is required. Furthermore, all entries must be available in
    /// the underlying object database and object types must correctly reflect
    /// the type of the object found in the database.
    /// \param entries  The entries to create the tree from.
    /// \returns The raw object id as string, if successful.
    [[nodiscard]] auto CreateTree(GitRepo::tree_entries_t const& entries)
        const noexcept -> std::optional<std::string>;

    /// \brief Read entries from tree data (without object db).
    /// \param data       The tree object as plain data.
    /// \param id         The object id.
    /// \param check_symlinks   Function to check non-upwardness condition.
    /// \param is_hex_id  Specify whether `id` is hex string or raw.
    /// \returns The tree entries.
    [[nodiscard]] static auto ReadTreeData(
        std::string const& data,
        std::string const& id,
        SymlinksCheckFunc const& check_symlinks,
        bool is_hex_id = false) noexcept -> std::optional<tree_entries_t>;

    /// \brief Create a flat shallow (without objects in db) tree and return it.
    /// Creates a tree object from the entries without access to the actual
    /// blobs. Objects are not required to be available in the underlying object
    /// database. It is sufficient to provide the raw object id and and object
    /// type for every entry.
    /// \param entries  The entries to create the tree from.
    /// \returns A pair of raw object id and the tree object content.
    [[nodiscard]] static auto CreateShallowTree(
        GitRepo::tree_entries_t const& entries) noexcept
        -> std::optional<std::pair<std::string, std::string>>;

    using anon_logger_t = std::function<void(std::string const&, bool)>;
    using anon_logger_ptr = std::shared_ptr<anon_logger_t>;

    /// \brief Stage all in current path and commit with given message.
    /// Only possible with real repository and thus non-thread-safe.
    /// Returns the commit hash, or nullopt if failure.
    /// It guarantees the logger is called exactly once with fatal if failure.
    [[nodiscard]] auto StageAndCommitAllAnonymous(
        std::string const& message,
        anon_logger_ptr const& logger) noexcept -> std::optional<std::string>;

    /// \brief Create annotated tag for given commit.
    /// Only possible with real repository and thus non-thread-safe.
    /// Returns success flag.
    /// It guarantees the logger is called exactly once with fatal if failure.
    [[nodiscard]] auto KeepTag(std::string const& commit,
                               std::string const& message,
                               anon_logger_ptr const& logger) noexcept -> bool;

    /// \brief Retrieves the commit of the HEAD reference.
    /// Only possible with real repository and thus non-thread-safe.
    /// Returns the commit hash as a string, or nullopt if failure.
    /// It guarantees the logger is called exactly once with fatal if failure.
    [[nodiscard]] auto GetHeadCommit(anon_logger_ptr const& logger) noexcept
        -> std::optional<std::string>;

    /// \brief Get the tree id of a subtree given the root commit
    /// Calling it from a fake repository allows thread-safe use.
    /// Returns the subtree hash, as a string, or nullopt if failure.
    /// It guarantees the logger is called exactly once with fatal if failure.
    [[nodiscard]] auto GetSubtreeFromCommit(
        std::string const& commit,
        std::string const& subdir,
        anon_logger_ptr const& logger) noexcept -> std::optional<std::string>;

    /// \brief Get the tree id of a subtree given the root tree hash
    /// Calling it from a fake repository allows thread-safe use.
    /// Returns the subtree hash, as a string, or nullopt if failure.
    /// It guarantees the logger is called exactly once with fatal if failure.
    [[nodiscard]] auto GetSubtreeFromTree(
        std::string const& tree_id,
        std::string const& subdir,
        anon_logger_ptr const& logger) noexcept -> std::optional<std::string>;

    /// \brief Get the tree id of a subtree given a filesystem directory path.
    /// Calling it from a fake repository allows thread-safe use.
    /// Will search for the root of the repository containing the path,
    /// and deduce the subdir relationship in the Git tree.
    /// Requires for the HEAD commit to be already known.
    /// Returns the tree hash, as a string, or nullopt if failure.
    /// It guarantees the logger is called exactly once with fatal if failure.
    [[nodiscard]] auto GetSubtreeFromPath(
        std::filesystem::path const& fpath,
        std::string const& head_commit,
        anon_logger_ptr const& logger) noexcept -> std::optional<std::string>;

    /// \brief Check if given commit is part of the local repository.
    /// Calling it from a fake repository allows thread-safe use.
    /// Returns a status of commit presence, or nullopt if failure.
    /// It guarantees the logger is called exactly once with fatal if failure.
    [[nodiscard]] auto CheckCommitExists(std::string const& commit,
                                         anon_logger_ptr const& logger) noexcept
        -> std::optional<bool>;

    /// \brief Try to retrieve the root of the repository containing the
    /// given path, if the path is actually part of a repository.
    /// Returns the git folder if path is in a git repo, empty string if path is
    /// not in a git repo, or nullopt if failure.
    /// It guarantees the logger is called exactly once with fatal if failure.
    [[nodiscard]] static auto GetRepoRootFromPath(
        std::filesystem::path const& fpath,
        anon_logger_ptr const& logger) noexcept
        -> std::optional<std::filesystem::path>;

    /// \brief Check if given tree ID is present in the directory structure of
    /// the local repository.
    /// Calling it from a fake repository allows thread-safe use.
    /// Returns a status of tree presence, or nullopt if failure.
    /// It guarantees the logger is called exactly once with fatal if failure.
    [[nodiscard]] auto CheckTreeExists(std::string const& tree_id,
                                       anon_logger_ptr const& logger) noexcept
        -> std::optional<bool>;

  private:
    /// \brief Wrapped git_repository with guarded destructor.
    /// Kept privately nested to avoid misuse of its raw pointer members.
    class GuardedRepo {
      public:
        GuardedRepo() noexcept = delete;
        explicit GuardedRepo(std::shared_mutex* mutex) noexcept;
        ~GuardedRepo() noexcept;

        // prohibit moves and copies
        GuardedRepo(GuardedRepo const&) = delete;
        GuardedRepo(GuardedRepo&& other) = delete;
        auto operator=(GuardedRepo const&) = delete;
        auto operator=(GuardedRepo&& other) = delete;

        // get the bare pointer
        [[nodiscard]] auto Ptr() -> git_repository*;
        [[nodiscard]] auto PtrRef() -> git_repository**;

      private:
        std::shared_mutex* mutex_;
        git_repository* repo_{nullptr};
    };

    using GuardedRepoPtr = std::shared_ptr<GuardedRepo>;

    // IMPORTANT! The GitCAS object must be defined before the repo object to
    // keep the GitContext alive until cleanup ends.
    GitCASPtr git_cas_{nullptr};
    GuardedRepoPtr repo_{nullptr};
    // default to real repo, as that is non-thread-safe
    bool is_repo_fake_{false};

  protected:
    /// \brief Open "fake" repository wrapper for existing CAS.
    explicit GitRepo(GitCASPtr git_cas) noexcept;
    /// \brief Open real repository at given location.
    explicit GitRepo(std::filesystem::path const& repo_path) noexcept;

    [[nodiscard]] auto GetRepoRef() const noexcept -> GuardedRepoPtr;

    [[nodiscard]] auto GetGitPath() const noexcept
        -> std::filesystem::path const&;

    [[nodiscard]] auto GetGitOdb() const noexcept
        -> std::unique_ptr<git_odb, decltype(&odb_closer)> const&;

    /// \brief Helper function to allocate and populate the char** pointer of a
    /// git_strarray from a vector of standard strings. User MUST use
    /// git_strarray_dispose to deallocate the inner pointer when the strarray
    /// is not needed anymore!
    static void PopulateStrarray(
        git_strarray* array,
        std::vector<std::string> const& string_list) noexcept;
};

#endif  // INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_GIT_REPO_HPP