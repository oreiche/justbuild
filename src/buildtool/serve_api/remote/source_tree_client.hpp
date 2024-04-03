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

#ifndef INCLUDED_SRC_BUILDTOOL_SERVE_API_SOURCE_TREE_CLIENT_HPP
#define INCLUDED_SRC_BUILDTOOL_SERVE_API_SOURCE_TREE_CLIENT_HPP

#include <memory>
#include <string>
#include <unordered_map>
#include <variant>

#include "justbuild/just_serve/just_serve.grpc.pb.h"
#include "src/buildtool/common/remote/port.hpp"
#include "src/buildtool/file_system/symlinks_map/pragma_special.hpp"
#include "src/buildtool/logging/logger.hpp"

/// Implements client side for SourceTree service defined in:
/// src/buildtool/serve_api/serve_service/just_serve.proto
class SourceTreeClient {
  public:
    SourceTreeClient(std::string const& server, Port port) noexcept;

    // An error + data union type
    using result_t = std::variant<bool, std::string>;

    /// \brief Retrieve the Git tree of a given commit, if known by the
    /// endpoint. It is a fatal error if the commit is known to the endpoint but
    /// no tree is able to be returned.
    /// \param[in] commit_id Hash of the Git commit to look up.
    /// \param[in] subdir Relative path of the tree inside commit.
    /// \param[in] sync_tree Sync tree to the remote-execution endpoint.
    /// \returns An error + data union, where at index 0 we return a fatal flag,
    /// with false meaning non-fatal failure (commit or subtree not found), and
    /// at index 1 the tree identifier on success.
    [[nodiscard]] auto ServeCommitTree(std::string const& commit_id,
                                       std::string const& subdir,
                                       bool sync_tree) noexcept -> result_t;

    /// \brief Retrieve the Git tree of an archive content, if known by the
    /// endpoint. It is a fatal error if the content blob is known to the
    /// endpoint but no tree is able to be returned.
    /// \param[in] content Hash of the archive content to look up.
    /// \param[in] archive_type Type of archive ("archive"|"zip").
    /// \param[in] subdir Relative path of the tree inside archive.
    /// \param[in] resolve_symlinks Optional enum to state how symlinks in the
    /// archive should be handled if the tree has to be actually computed.
    /// \param[in] sync_tree Sync tree to the remote-execution endpoint.
    /// \returns An error + data union, where at index 0 we return a fatal flag,
    /// with false meaning non-fatal failure (content blob not found), and at
    /// index 1 the tree identifier on success.
    [[nodiscard]] auto ServeArchiveTree(
        std::string const& content,
        std::string const& archive_type,
        std::string const& subdir,
        std::optional<PragmaSpecial> const& resolve_symlinks,
        bool sync_tree) noexcept -> result_t;

    /// \brief Retrieve the Git tree of a directory of distfiles, if all the
    /// content blobs are known by the endpoint. It is a fatal error if all
    /// content blobs are known but no tree is able to be returned.
    /// \param[in] distfiles Mapping from distfile names to content blob ids.
    /// \param[in] sync_tree Sync tree and all ditfile blobs to the
    /// remote-execution endpoint.
    /// \returns An error + data union, where at index 0 we return a fatal flag,
    /// with false meaning non-fatal failure (at least one distfile blob
    /// missing), and at index 1 the tree identifier on success.
    [[nodiscard]] auto ServeDistdirTree(
        std::shared_ptr<std::unordered_map<std::string, std::string>> const&
            distfiles,
        bool sync_tree) noexcept -> result_t;

    /// \brief Retrieve the Git tree of a foreign-file directory, if all content
    /// blobs are known to the end point and, as a side effect, make that tree
    /// known to the serve endpoint.
    [[nodiscard]] auto ServeForeignFileTree(const std::string& content,
                                            const std::string& name,
                                            bool executable) noexcept
        -> result_t;

    /// \brief Make a given content blob available in remote CAS, if known by
    /// serve remote.
    /// \param[in] content Hash of the archive content to look up.
    /// \returns Flag to state whether content is in remote CAS.
    [[nodiscard]] auto ServeContent(std::string const& content) noexcept
        -> bool;

    /// \brief Make a given tree available in remote CAS, if known by serve
    /// remote.
    /// \param[in] tree_id Identifier of the Git tree to look up.
    /// \returns Flag to state whether tree is in remote CAS.
    [[nodiscard]] auto ServeTree(std::string const& tree_id) noexcept -> bool;

    /// \brief Checks if the serve endpoint has a given tree locally available
    /// and makes it available for a serve-orchestrated build.
    /// \param[in] tree_id Identifier of the Git tree to look up.
    /// \returns Flag to state whether tree is known or not, or nullopt on
    /// errors.
    [[nodiscard]] auto CheckRootTree(std::string const& tree_id) noexcept
        -> std::optional<bool>;

    /// \brief Retrieve tree from the CAS of the associated remote-execution
    /// endpoint and makes it available for a serve-orchestrated build.
    /// \param[in] tree_id Identifier of the Git tree to retrieve.
    /// \returns Flag to state whether tree was successfully imported into the
    /// local Git storage or not.
    [[nodiscard]] auto GetRemoteTree(std::string const& tree_id) noexcept
        -> bool;

  private:
    std::unique_ptr<justbuild::just_serve::SourceTree::Stub> stub_;
    Logger logger_{"RemoteSourceTreeClient"};
};

#endif  // INCLUDED_SRC_BUILDTOOL_SERVE_API_SOURCE_TREE_CLIENT_HPP
