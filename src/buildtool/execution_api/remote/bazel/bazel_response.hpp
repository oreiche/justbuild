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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_RESPONSE_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_RESPONSE_HPP

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>  // std::move, std:pair

#include <grpcpp/support/status.h>

#include "gsl/gsl"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/common/execution_response.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_execution_client.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_network.hpp"
#include "src/utils/cpp/expected.hpp"

/// \brief Bazel implementation of the abstract Execution Response.
/// Access Bazel execution output data and obtain a Bazel Artifact.
class BazelResponse final : public IExecutionResponse {
    friend class BazelAction;

  public:
    auto Status() const noexcept -> StatusCode final {
        return output_.status.ok() ? StatusCode::Success : StatusCode::Failed;
    }
    auto HasStdErr() const noexcept -> bool final {
        return IsDigestNotEmpty(output_.action_result.stderr_digest());
    }
    auto HasStdOut() const noexcept -> bool final {
        return IsDigestNotEmpty(output_.action_result.stdout_digest());
    }
    auto StdErr() noexcept -> std::string final {
        return ReadStringBlob(output_.action_result.stderr_digest());
    }
    auto StdOut() noexcept -> std::string final {
        return ReadStringBlob(output_.action_result.stdout_digest());
    }
    auto StdErrDigest() noexcept -> std::optional<ArtifactDigest> final {
        auto digest = ArtifactDigestFactory::FromBazel(
            network_->GetHashFunction().GetType(),
            output_.action_result.stderr_digest());
        if (digest) {
            return *digest;
        }
        return std::nullopt;
    }
    auto StdOutDigest() noexcept -> std::optional<ArtifactDigest> final {
        auto digest = ArtifactDigestFactory::FromBazel(
            network_->GetHashFunction().GetType(),
            output_.action_result.stdout_digest());
        if (digest) {
            return *digest;
        }
        return std::nullopt;
    }
    auto ExitCode() const noexcept -> int final {
        return output_.action_result.exit_code();
    }
    auto IsCached() const noexcept -> bool final {
        return output_.cached_result;
    };

    auto ExecutionDuration() noexcept -> double final {
        return output_.duration;
    };

    auto ActionDigest() const noexcept -> std::string const& final {
        return action_id_;
    }

    auto Artifacts() noexcept
        -> expected<gsl::not_null<ArtifactInfos const*>, std::string> final;
    auto HasUpwardsSymlinks() noexcept -> expected<bool, std::string> final;

  private:
    std::string action_id_;
    std::shared_ptr<BazelNetwork> const network_;
    BazelExecutionClient::ExecutionOutput output_{};
    ArtifactInfos artifacts_;
    bool has_upwards_symlinks_ = false;  // only tracked in compatible mode
    bool populated_ = false;

    explicit BazelResponse(std::string action_id,
                           std::shared_ptr<BazelNetwork> network,
                           BazelExecutionClient::ExecutionOutput output)
        : action_id_{std::move(action_id)},
          network_{std::move(network)},
          output_{std::move(output)} {}

    [[nodiscard]] auto ReadStringBlob(bazel_re::Digest const& id) noexcept
        -> std::string;

    [[nodiscard]] static auto IsDigestNotEmpty(bazel_re::Digest const& id)
        -> bool {
        return id.size_bytes() != 0;
    }

    /// \brief Populates the stored data, once.
    /// \returns Error message on failure, nullopt on success.
    [[nodiscard]] auto Populate() noexcept -> std::optional<std::string>;

    /// \brief Tries to upload the tree rot and subdirectories. Performs also a
    /// symlinks check.
    /// \returns Pair of ArtifactDigest of root tree and flag signaling the
    /// presence of any upwards symlinks on success, error message on failure.
    [[nodiscard]] auto UploadTreeMessageDirectories(
        bazel_re::Tree const& tree) const
        -> expected<std::pair<ArtifactDigest, /*has_upwards_symlinks*/ bool>,
                    std::string>;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_RESPONSE_HPP
