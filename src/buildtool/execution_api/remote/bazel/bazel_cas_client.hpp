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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_CAS_CLIENT_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_CAS_CLIENT_HPP

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include <grpcpp/support/status.h>

#include "build/bazel/remote/execution/v2/remote_execution.grpc.pb.h"
#include "fmt/core.h"
#include "gsl/gsl"
#include "src/buildtool/auth/authentication.hpp"
#include "src/buildtool/common/artifact_blob.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/common/remote/port.hpp"
#include "src/buildtool/common/remote/retry_config.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_capabilities_client.hpp"
#include "src/buildtool/execution_api/remote/bazel/bytestream_client.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/tmp_dir.hpp"

/// Implements client side for serivce defined here:
/// https://github.com/bazelbuild/remote-apis/blob/e1fe21be4c9ae76269a5a63215bb3c72ed9ab3f0/build/bazel/remote/execution/v2/remote_execution.proto#L317
class BazelCasClient {
  public:
    explicit BazelCasClient(
        std::string const& server,
        Port port,
        gsl::not_null<Auth const*> const& auth,
        gsl::not_null<RetryConfig const*> const& retry_config,
        gsl::not_null<BazelCapabilitiesClient const*> const& capabilities,
        TmpDir::Ptr temp_space) noexcept;

    /// \brief Find missing blobs
    /// \param[in] instance_name Name of the CAS instance
    /// \param[in] digests       The blob digests to search for
    /// \returns The digests of blobs not found in CAS
    [[nodiscard]] auto FindMissingBlobs(
        std::string const& instance_name,
        std::unordered_set<ArtifactDigest> const& digests) const noexcept
        -> std::unordered_set<ArtifactDigest>;

    /// \brief Upload multiple blobs in batch transfer
    /// \param[in] instance_name Name of the CAS instance
    /// \param[in] blobs         Blobs to upload
    /// \returns The count of successfully updated blobs
    [[nodiscard]] auto BatchUpdateBlobs(
        std::string const& instance_name,
        std::unordered_set<ArtifactBlob> const& blobs) const noexcept
        -> std::size_t;

    /// \brief Read multiple blobs in batch transfer
    /// \param[in] instance_name Name of the CAS instance
    /// \param[in] blobs         Blob digests to read
    /// \returns The blobs successfully read
    [[nodiscard]] auto BatchReadBlobs(
        std::string const& instance_name,
        std::unordered_set<ArtifactDigest> const& blobs) const noexcept
        -> std::unordered_set<ArtifactBlob>;

    /// \brief Upload single blob via bytestream
    /// \param[in] instance_name Name of the CAS instance
    /// \param[in] blob          The blob to upload
    /// \returns Boolean indicating successful upload
    [[nodiscard]] auto UpdateSingleBlob(std::string const& instance_name,
                                        ArtifactBlob const& blob) const noexcept
        -> bool;

    /// \brief Read single blob via incremental bytestream reader
    /// \param[in] instance_name Name of the CAS instance
    /// \param[in] digest        Blob digest to read
    /// \returns Incremental bytestream reader.
    [[nodiscard]] auto IncrementalReadSingleBlob(
        std::string const& instance_name,
        ArtifactDigest const& digest) const noexcept
        -> ByteStreamClient::IncrementalReader;

    /// \brief Read single blob via bytestream
    /// \param[in] instance_name Name of the CAS instance
    /// \param[in] digest        Blob digest to read
    /// \returns The blob successfully read
    [[nodiscard]] auto ReadSingleBlob(std::string const& instance_name,
                                      ArtifactDigest const& digest)
        const noexcept -> std::optional<ArtifactBlob>;

    /// @brief Split single blob into chunks
    /// @param[in] instance_name    Name of the CAS instance
    /// @param[in] blob_digest      Blob digest to be splitted
    /// @return The chunk digests of the splitted blob
    [[nodiscard]] auto SplitBlob(std::string const& instance_name,
                                 bazel_re::Digest const& blob_digest)
        const noexcept -> std::optional<std::vector<bazel_re::Digest>>;

    /// @brief Splice blob from chunks at the remote side
    /// @param[in] instance_name    Name of the CAS instance
    /// @param[in] blob_digest      Expected digest of the spliced blob
    /// @param[in] chunk_digests    The chunk digests of the splitted blob
    /// @return Whether the splice call was successful
    [[nodiscard]] auto SpliceBlob(
        std::string const& instance_name,
        bazel_re::Digest const& blob_digest,
        std::vector<bazel_re::Digest> const& chunk_digests) const noexcept
        -> std::optional<bazel_re::Digest>;

    [[nodiscard]] auto BlobSplitSupport(
        std::string const& instance_name) const noexcept -> bool;

    [[nodiscard]] auto BlobSpliceSupport(
        std::string const& instance_name) const noexcept -> bool;

    [[nodiscard]] auto GetMaxBatchTransferSize(
        std::string const& instance_name) const noexcept -> std::size_t;

    [[nodiscard]] auto GetTempSpace() const noexcept -> TmpDir::Ptr {
        return temp_space_;
    }

  private:
    std::unique_ptr<ByteStreamClient> stream_;
    RetryConfig const& retry_config_;
    BazelCapabilitiesClient const& capabilities_;
    TmpDir::Ptr temp_space_;
    std::unique_ptr<bazel_re::ContentAddressableStorage::Stub> stub_;
    Logger logger_{"RemoteCasClient"};

    [[nodiscard]] static auto CreateGetTreeRequest(
        std::string const& instance_name,
        bazel_re::Digest const& root_digest,
        int page_size,
        std::string const& page_token) noexcept -> bazel_re::GetTreeRequest;

    /// \brief Utility class for supporting the Retry strategy while parsing a
    /// BatchResponse
    template <typename TContent>
    struct RetryProcessBatchResponse {
        bool ok{false};
        std::vector<TContent> result{};
        bool exit_retry_loop{false};
        std::optional<std::string> error_msg{};
    };

    // If this function is defined in the .cpp file, clang raises an error
    // while linking
    template <class TContent, class TInner, class TResponse>
    [[nodiscard]] auto ProcessBatchResponse(
        TResponse const& response,
        std::function<void(std::vector<TContent>*, TInner const&)> const&
            inserter) const noexcept -> RetryProcessBatchResponse<TContent> {
        std::vector<TContent> output;
        for (auto const& res : response.responses()) {
            auto const& res_status = res.status();
            if (res_status.code() == static_cast<int>(grpc::StatusCode::OK)) {
                inserter(&output, res);
            }
            else {
                auto exit_retry_loop =
                    (res_status.code() !=
                     static_cast<int>(grpc::StatusCode::UNAVAILABLE));
                return {.ok = false,
                        .exit_retry_loop = exit_retry_loop,
                        .error_msg =
                            fmt::format("While processing batch response: {}",
                                        res_status.ShortDebugString())};
            }
        }
        return {.ok = true, .result = std::move(output)};
    }
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_CAS_CLIENT_HPP
