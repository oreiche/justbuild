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

#include "src/buildtool/execution_api/remote/bazel/bazel_network.hpp"

#include <algorithm>
#include <iterator>
#include <unordered_map>
#include <utility>

#include "src/buildtool/execution_api/common/message_limits.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/transformed_range.hpp"

BazelNetwork::BazelNetwork(
    std::string instance_name,
    std::string const& host,
    Port port,
    gsl::not_null<Auth const*> const& auth,
    gsl::not_null<RetryConfig const*> const& retry_config,
    ExecutionConfiguration const& exec_config,
    gsl::not_null<HashFunction const*> const& hash_function) noexcept
    : instance_name_{std::move(instance_name)},
      cas_{std::make_unique<BazelCasClient>(host, port, auth, retry_config)},
      ac_{std::make_unique<BazelAcClient>(host, port, auth, retry_config)},
      exec_{std::make_unique<BazelExecutionClient>(host,
                                                   port,
                                                   auth,
                                                   retry_config)},
      exec_config_{exec_config},
      hash_function_{*hash_function} {}

auto BazelNetwork::IsAvailable(bazel_re::Digest const& digest) const noexcept
    -> bool {
    return cas_
        ->FindMissingBlobs(instance_name_,
                           std::vector<bazel_re::Digest>{digest})
        .empty();
}

auto BazelNetwork::IsAvailable(std::vector<bazel_re::Digest> const& digests)
    const noexcept -> std::vector<bazel_re::Digest> {
    return cas_->FindMissingBlobs(instance_name_, digests);
}

auto BazelNetwork::SplitBlob(bazel_re::Digest const& blob_digest) const noexcept
    -> std::optional<std::vector<bazel_re::Digest>> {
    return cas_->SplitBlob(hash_function_, instance_name_, blob_digest);
}

auto BazelNetwork::SpliceBlob(
    bazel_re::Digest const& blob_digest,
    std::vector<bazel_re::Digest> const& chunk_digests) const noexcept
    -> std::optional<bazel_re::Digest> {
    return cas_->SpliceBlob(
        hash_function_, instance_name_, blob_digest, chunk_digests);
}

auto BazelNetwork::BlobSplitSupport() const noexcept -> bool {
    return cas_->BlobSplitSupport(hash_function_, instance_name_);
}

auto BazelNetwork::BlobSpliceSupport() const noexcept -> bool {
    return cas_->BlobSpliceSupport(hash_function_, instance_name_);
}

template <class TIter>
auto BazelNetwork::DoUploadBlobs(TIter const& first,
                                 TIter const& last) noexcept -> bool {
    try {
        // Partition the blobs according to their size. The first group collects
        // all the blobs that can be uploaded in batch, the second group gathers
        // blobs whose size exceeds the kMaxBatchTransferSize threshold.
        //
        // The blobs belonging to the second group are uploaded via the
        // bytestream api.
        std::vector<gsl::not_null<BazelBlob const*>> sorted;
        sorted.reserve(std::distance(first, last));
        std::transform(
            first, last, std::back_inserter(sorted), [](BazelBlob const& b) {
                return &b;
            });

        auto it = std::stable_partition(
            sorted.begin(), sorted.end(), [](BazelBlob const* x) {
                return x->data->size() <= kMaxBatchTransferSize;
            });
        auto digests_count =
            cas_->BatchUpdateBlobs(instance_name_, sorted.begin(), it);

        return digests_count == std::distance(sorted.begin(), it) &&
               std::all_of(it, sorted.end(), [this](BazelBlob const* x) {
                   return cas_->UpdateSingleBlob(instance_name_, *x);
               });
    } catch (...) {
        Logger::Log(LogLevel::Warning, "Unknown exception");
        return false;
    }
}

auto BazelNetwork::UploadBlobs(BazelBlobContainer&& blobs,
                               bool skip_find_missing) noexcept -> bool {
    if (skip_find_missing) {
        auto blob_range = blobs.Blobs();
        return DoUploadBlobs(blob_range.begin(), blob_range.end());
    }

    // find digests of blobs missing in CAS
    auto missing_digests = cas_->FindMissingBlobs(instance_name_, blobs);

    if (not missing_digests.empty()) {
        // update missing blobs
        auto missing_blobs = blobs.RelatedBlobs(missing_digests);
        return DoUploadBlobs(missing_blobs.begin(), missing_blobs.end());
    }
    return true;
}

auto BazelNetwork::ExecuteBazelActionSync(
    bazel_re::Digest const& action) noexcept
    -> std::optional<BazelExecutionClient::ExecutionOutput> {
    auto response =
        exec_->Execute(instance_name_, action, exec_config_, true /*wait*/);

    if (response.state ==
        BazelExecutionClient::ExecutionResponse::State::Ongoing) {
        Logger::Log(
            LogLevel::Trace, "Waiting for {}", response.execution_handle);
        response = exec_->WaitExecution(response.execution_handle);
    }
    if (response.state !=
            BazelExecutionClient::ExecutionResponse::State::Finished or
        not response.output) {
        Logger::Log(LogLevel::Warning,
                    "Failed to execute action with execution id {}.",
                    action.hash());
        return std::nullopt;
    }

    return response.output;
}

auto BazelNetwork::CreateReader() const noexcept -> BazelNetworkReader {
    return BazelNetworkReader{instance_name_, cas_.get(), &hash_function_};
}

auto BazelNetwork::GetCachedActionResult(
    bazel_re::Digest const& action,
    std::vector<std::string> const& output_files) const noexcept
    -> std::optional<bazel_re::ActionResult> {
    return ac_->GetActionResult(
        instance_name_, action, false, false, output_files);
}
