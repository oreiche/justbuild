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

#ifndef INCLUDED_SRC_BUILD_SERVE_API_SERVE_SERVICE_TARGET_HPP
#define INCLUDED_SRC_BUILD_SERVE_API_SERVE_SERVICE_TARGET_HPP

#include <filesystem>
#include <memory>
#include <optional>

#include "gsl/gsl"
#include "justbuild/just_serve/just_serve.grpc.pb.h"
#include "src/buildtool/common/remote/remote_common.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/execution_api/common/create_execution_api.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/logging/logger.hpp"

class TargetService final : public justbuild::just_serve::Target::Service {
  public:
    // Given a target-level caching key, returns the computed value. In doing
    // so, it can build on the associated end-point passing the
    // RemoteExecutionProperties contained in the ServeTargetRequest.
    //
    // If the status has a code different from `OK`, the response MUST not be
    // used.
    //
    // Errors:
    // * `FAILED_PRECONDITION`: Failed to find required information in the CAS
    //   or the target cache key is malformed.
    // * `UNAVAILABLE`: Could not communicate with the remote execution
    //   endpoint.
    // * `INTERNAL`: Internally, something is very broken.
    auto ServeTarget(
        ::grpc::ServerContext* /*context*/,
        const ::justbuild::just_serve::ServeTargetRequest* /*request*/,
        ::justbuild::just_serve::ServeTargetResponse* /*response*/)
        -> ::grpc::Status override;

    // Given the target-level root tree and the name of an export target,
    // returns the list of flexible variables from that target's description.
    //
    // If the status has a code different from `OK`, the response MUST not be
    // used.
    //
    // Errors:
    // * `FAILED_PRECONDITION`: An error occurred in retrieving the
    //   configuration of the requested target, such as missing entries
    //   (target-root, target file, target name), unparsable target file, or
    //   requested target not being of "type" : "export".
    // * `INTERNAL`: Internally, something is very broken.
    auto ServeTargetVariables(
        ::grpc::ServerContext* /*context*/,
        const ::justbuild::just_serve::ServeTargetVariablesRequest* request,
        ::justbuild::just_serve::ServeTargetVariablesResponse* response)
        -> ::grpc::Status override;

  private:
    std::shared_ptr<Logger> logger_{std::make_shared<Logger>("target-service")};

    // remote execution endpoint used for remote building
    gsl::not_null<IExecutionApi::Ptr> const remote_api_{
        CreateExecutionApi(RemoteExecutionConfig::RemoteAddress(),
                           std::nullopt,
                           "serve-remote-execution")};
    // used for storing and retrieving target-level cache entries
    gsl::not_null<IExecutionApi::Ptr> const local_api_{
        CreateExecutionApi(std::nullopt)};

    /// \brief Check if tree exists in the given repository.
    [[nodiscard]] static auto IsTreeInRepo(
        std::string const& tree_id,
        std::filesystem::path const& repo_path,
        std::shared_ptr<Logger> const& logger) -> bool;

    /// \brief For a given tree id, find the known repository that can serve it.
    [[nodiscard]] static auto GetServingRepository(
        std::string const& tree_id,
        std::shared_ptr<Logger> const& logger)
        -> std::optional<std::filesystem::path>;

    /// \brief Parse the stored repository configuration blob and populate the
    /// RepositoryConfig instance.
    /// \returns nullopt on success, error message as a string otherwise.
    [[nodiscard]] static auto DetermineRoots(
        std::string const& main_repo,
        std::filesystem::path const& repo_config_path,
        gsl::not_null<RepositoryConfig*> const& repository_config,
        std::shared_ptr<Logger> const& logger) -> std::optional<std::string>;

    /// \brief Get the blob content at given path inside a Git tree.
    /// \returns If tree found, pair of "no-internal-errors" flag and content of
    /// blob at the path specified if blob exists, nullopt otherwise.
    [[nodiscard]] static auto GetBlobContent(
        std::filesystem::path const& repo_path,
        std::string const& tree_id,
        std::string const& rel_path,
        std::shared_ptr<Logger> const& logger)
        -> std::optional<std::pair<bool, std::optional<std::string>>>;
};

#endif  // INCLUDED_SRC_BUILD_SERVE_API_SERVE_SERVICE_TARGET_HPP
