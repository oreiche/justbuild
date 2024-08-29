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

#include "src/buildtool/execution_api/execution_service/cas_server.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>  // std::move
#include <vector>

#include "fmt/core.h"
#include "src/buildtool/compatibility/compatibility.hpp"
#include "src/buildtool/compatibility/native_support.hpp"
#include "src/buildtool/execution_api/execution_service/cas_utils.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/storage/garbage_collector.hpp"
#include "src/utils/cpp/verify_hash.hpp"

namespace {
inline constexpr std::size_t kGitSHA1Length = 42;
inline constexpr std::size_t kSHA256Length = 64;

[[nodiscard]] auto IsValidHash(std::string const& x) -> bool {
    auto error_msg = IsAHash(x);
    auto const& length = x.size();
    return not error_msg and
           ((Compatibility::IsCompatible() and length == kSHA256Length) or
            length == kGitSHA1Length);
}

[[nodiscard]] auto ChunkingAlgorithmToString(
    ::bazel_re::ChunkingAlgorithm_Value type) -> std::string {
    switch (type) {
        case ::bazel_re::ChunkingAlgorithm_Value::
            ChunkingAlgorithm_Value_IDENTITY:
            return "IDENTITY";
        case ::bazel_re::ChunkingAlgorithm_Value::
            ChunkingAlgorithm_Value_RABINCDC_8KB:
            return "RABINCDC_8KB";
        case ::bazel_re::ChunkingAlgorithm_Value::
            ChunkingAlgorithm_Value_FASTCDC:
            return "FASTCDC";
        default:
            return "[Unknown Chunking Algorithm Type]";
    }
}

[[nodiscard]] auto CheckDigestConsistency(
    bazel_re::Digest const& ref,
    bazel_re::Digest const& computed) noexcept -> std::optional<std::string> {
    bool valid = ref.hash() == computed.hash();
    if (valid) {
        bool const check_sizes =
            Compatibility::IsCompatible() or ref.size_bytes() != 0;
        if (check_sizes) {
            valid = ref.size_bytes() == computed.size_bytes();
        }
    }
    if (not valid) {
        return fmt::format(
            "Blob {} is corrupted: provided digest {}:{} and digest computed "
            "from data {}:{} do not correspond.",
            ref.hash(),
            ref.hash(),
            ref.size_bytes(),
            computed.hash(),
            computed.size_bytes());
    }
    return std::nullopt;
}
}  // namespace

auto CASServiceImpl::FindMissingBlobs(
    ::grpc::ServerContext* /*context*/,
    const ::bazel_re::FindMissingBlobsRequest* request,
    ::bazel_re::FindMissingBlobsResponse* response) -> ::grpc::Status {
    auto lock = GarbageCollector::SharedLock(storage_config_);
    if (not lock) {
        auto str =
            fmt::format("FindMissingBlobs: could not acquire SharedLock");
        logger_.Emit(LogLevel::Error, str);
        return grpc::Status{grpc::StatusCode::INTERNAL, str};
    }
    for (auto const& x : request->blob_digests()) {
        auto const& hash = x.hash();

        bool is_in_cas = false;
        if (IsValidHash(hash)) {
            logger_.Emit(LogLevel::Trace, "FindMissingBlobs: {}", hash);
            ArtifactDigest const digest(x);
            is_in_cas =
                NativeSupport::IsTree(hash)
                    ? storage_.CAS().TreePath(digest).has_value()
                    : storage_.CAS().BlobPath(digest, false).has_value();
        }
        else {
            logger_.Emit(LogLevel::Error,
                         "FindMissingBlobs: unsupported digest {}",
                         hash);
        }

        if (not is_in_cas) {
            auto* d = response->add_missing_blob_digests();
            d->CopyFrom(x);
        }
    }
    return ::grpc::Status::OK;
}

auto CASServiceImpl::BatchUpdateBlobs(
    ::grpc::ServerContext* /*context*/,
    const ::bazel_re::BatchUpdateBlobsRequest* request,
    ::bazel_re::BatchUpdateBlobsResponse* response) -> ::grpc::Status {
    auto lock = GarbageCollector::SharedLock(storage_config_);
    if (not lock) {
        auto str =
            fmt::format("BatchUpdateBlobs: could not acquire SharedLock");
        logger_.Emit(LogLevel::Error, str);
        return grpc::Status{grpc::StatusCode::INTERNAL, str};
    }
    for (auto const& x : request->requests()) {
        auto const& hash = x.digest().hash();
        logger_.Emit(LogLevel::Trace, "BatchUpdateBlobs: {}", hash);
        if (not IsValidHash(hash)) {
            auto const str =
                fmt::format("BatchUpdateBlobs: unsupported digest {}", hash);
            logger_.Emit(LogLevel::Error, "{}", str);
            return ::grpc::Status{grpc::StatusCode::INVALID_ARGUMENT, str};
        }
        logger_.Emit(LogLevel::Trace, "BatchUpdateBlobs: {}", hash);
        auto* r = response->add_responses();
        r->mutable_digest()->CopyFrom(x.digest());

        bool const is_tree = NativeSupport::IsTree(hash);
        if (is_tree) {
            // In native mode: for trees, check whether the tree invariant holds
            // before storing the actual tree object.
            if (auto err = CASUtils::EnsureTreeInvariant(
                    x.digest(), x.data(), storage_)) {
                auto const str =
                    fmt::format("BatchUpdateBlobs: {}", *std::move(err));
                logger_.Emit(LogLevel::Error, "{}", str);
                return ::grpc::Status{grpc::StatusCode::FAILED_PRECONDITION,
                                      str};
            }
        }

        auto const cas_digest =
            is_tree
                ? storage_.CAS().StoreTree(x.data())
                : storage_.CAS().StoreBlob(x.data(), /*is_executable=*/false);

        if (not cas_digest) {
            auto const str =
                fmt::format("BatchUpdateBlobs: could not upload {} {}",
                            is_tree ? "tree" : "blob",
                            hash);
            logger_.Emit(LogLevel::Error, "{}", str);
            return ::grpc::Status{grpc::StatusCode::INTERNAL, str};
        }

        if (auto err = CheckDigestConsistency(x.digest(), *cas_digest)) {
            auto const str =
                fmt::format("BatchUpdateBlobs: {}", *std::move(err));
            logger_.Emit(LogLevel::Error, "{}", str);
            return ::grpc::Status{grpc::StatusCode::INVALID_ARGUMENT, str};
        }
    }
    return ::grpc::Status::OK;
}

auto CASServiceImpl::BatchReadBlobs(
    ::grpc::ServerContext* /*context*/,
    const ::bazel_re::BatchReadBlobsRequest* request,
    ::bazel_re::BatchReadBlobsResponse* response) -> ::grpc::Status {
    auto lock = GarbageCollector::SharedLock(storage_config_);
    if (not lock) {
        auto const str =
            fmt::format("BatchReadBlobs: Could not acquire SharedLock");
        logger_.Emit(LogLevel::Error, "{}", str);
        return grpc::Status{grpc::StatusCode::INTERNAL, str};
    }
    for (auto const& x : request->digests()) {
        auto* r = response->add_responses();
        r->mutable_digest()->CopyFrom(x);

        ArtifactDigest const digest(x);
        auto const path =
            NativeSupport::IsTree(x.hash())
                ? storage_.CAS().TreePath(digest)
                : storage_.CAS().BlobPath(digest, /*is_executable=*/false);

        if (not path) {
            google::rpc::Status status;
            status.set_code(grpc::StatusCode::NOT_FOUND);
            r->mutable_status()->CopyFrom(status);
            continue;
        }
        std::ifstream cert{*path};
        std::string tmp((std::istreambuf_iterator<char>(cert)),
                        std::istreambuf_iterator<char>());
        *(r->mutable_data()) = std::move(tmp);

        r->mutable_status()->CopyFrom(google::rpc::Status{});
    }
    return ::grpc::Status::OK;
}

auto CASServiceImpl::GetTree(
    ::grpc::ServerContext* /*context*/,
    const ::bazel_re::GetTreeRequest* /*request*/,
    ::grpc::ServerWriter<::bazel_re::GetTreeResponse>* /*writer*/)
    -> ::grpc::Status {
    auto const* str = "GetTree not implemented";
    logger_.Emit(LogLevel::Error, str);
    return ::grpc::Status{grpc::StatusCode::UNIMPLEMENTED, str};
}

auto CASServiceImpl::SplitBlob(::grpc::ServerContext* /*context*/,
                               const ::bazel_re::SplitBlobRequest* request,
                               ::bazel_re::SplitBlobResponse* response)
    -> ::grpc::Status {
    if (not request->has_blob_digest()) {
        auto str = fmt::format("SplitBlob: no blob digest provided");
        logger_.Emit(LogLevel::Error, str);
        return ::grpc::Status{grpc::StatusCode::INVALID_ARGUMENT, str};
    }

    auto const& blob_digest = request->blob_digest();
    if (not IsValidHash(blob_digest.hash())) {
        auto str =
            fmt::format("SplitBlob: unsupported digest {}", blob_digest.hash());
        logger_.Emit(LogLevel::Error, "{}", str);
        return ::grpc::Status{grpc::StatusCode::INVALID_ARGUMENT, str};
    }

    auto chunking_algorithm = request->chunking_algorithm();
    logger_.Emit(LogLevel::Debug,
                 "SplitBlob({}, {})",
                 blob_digest.hash(),
                 ChunkingAlgorithmToString(chunking_algorithm));

    // Print warning if unsupported chunking algorithm was requested.
    if (chunking_algorithm != ::bazel_re::ChunkingAlgorithm_Value::
                                  ChunkingAlgorithm_Value_IDENTITY and
        chunking_algorithm != ::bazel_re::ChunkingAlgorithm_Value::
                                  ChunkingAlgorithm_Value_FASTCDC) {
        logger_.Emit(
            LogLevel::Warning,
            "SplitBlob: unsupported chunking algorithm {}, will use default "
            "implementation {}",
            ChunkingAlgorithmToString(chunking_algorithm),
            ChunkingAlgorithmToString(::bazel_re::ChunkingAlgorithm_Value::
                                          ChunkingAlgorithm_Value_FASTCDC));
    }

    // Acquire garbage collection lock.
    auto lock = GarbageCollector::SharedLock(storage_config_);
    if (not lock) {
        auto str =
            fmt::format("SplitBlob: could not acquire garbage collection lock");
        logger_.Emit(LogLevel::Error, str);
        return ::grpc::Status{grpc::StatusCode::INTERNAL, str};
    }

    // Split blob into chunks.
    auto split_result = chunking_algorithm ==
                                ::bazel_re::ChunkingAlgorithm_Value::
                                    ChunkingAlgorithm_Value_IDENTITY
                            ? CASUtils::SplitBlobIdentity(blob_digest, storage_)
                            : CASUtils::SplitBlobFastCDC(blob_digest, storage_);

    if (not split_result) {
        auto const& status = split_result.error();
        auto str = fmt::format("SplitBlob: {}", status.error_message());
        logger_.Emit(LogLevel::Error, "{}", str);
        return ::grpc::Status{status.error_code(), str};
    }

    auto const& chunk_digests = *split_result;
    logger_.Emit(LogLevel::Debug, [&blob_digest, &chunk_digests]() {
        std::stringstream ss{};
        ss << "Split blob " << blob_digest.hash() << ":"
           << blob_digest.size_bytes() << " into " << chunk_digests.size()
           << " chunks: [ ";
        for (auto const& chunk_digest : chunk_digests) {
            ss << chunk_digest.hash() << ":" << chunk_digest.size_bytes()
               << " ";
        }
        ss << "]";
        return ss.str();
    });

    std::copy(chunk_digests.cbegin(),
              chunk_digests.cend(),
              pb::back_inserter(response->mutable_chunk_digests()));
    return ::grpc::Status::OK;
}

auto CASServiceImpl::SpliceBlob(::grpc::ServerContext* /*context*/,
                                const ::bazel_re::SpliceBlobRequest* request,
                                ::bazel_re::SpliceBlobResponse* response)
    -> ::grpc::Status {
    if (not request->has_blob_digest()) {
        auto str = fmt::format("SpliceBlob: no blob digest provided");
        logger_.Emit(LogLevel::Error, str);
        return ::grpc::Status{grpc::StatusCode::INVALID_ARGUMENT, str};
    }

    auto const& blob_digest = request->blob_digest();
    if (not IsValidHash(blob_digest.hash())) {
        auto str = fmt::format("SpliceBlob: unsupported digest {}",
                               blob_digest.hash());
        logger_.Emit(LogLevel::Error, "{}", str);
        return ::grpc::Status{grpc::StatusCode::INVALID_ARGUMENT, str};
    }

    logger_.Emit(LogLevel::Debug,
                 "SpliceBlob({}, {} chunks)",
                 blob_digest.hash(),
                 request->chunk_digests().size());

    // Acquire garbage collection lock.
    auto lock = GarbageCollector::SharedLock(storage_config_);
    if (not lock) {
        auto str = fmt::format(
            "SpliceBlob: could not acquire garbage collection lock");
        logger_.Emit(LogLevel::Error, str);
        return ::grpc::Status{grpc::StatusCode::INTERNAL, str};
    }

    auto chunk_digests = std::vector<bazel_re::Digest>{};
    chunk_digests.reserve(request->chunk_digests().size());
    for (auto const& x : request->chunk_digests()) {
        if (not IsValidHash(x.hash())) {
            auto const str =
                fmt::format("SpliceBlob: unsupported digest {}", x.hash());
            logger_.Emit(LogLevel::Error, "{}", str);
            return ::grpc::Status{grpc::StatusCode::INVALID_ARGUMENT, str};
        }
        chunk_digests.push_back(x);
    }

    // Splice blob from chunks.
    auto splice_result =
        CASUtils::SpliceBlob(blob_digest, chunk_digests, storage_);
    if (not splice_result) {
        auto const& status = splice_result.error();
        auto const str = fmt::format("SpliceBlob: {}", status.error_message());
        logger_.Emit(LogLevel::Error, "{}", str);
        return ::grpc::Status{status.error_code(), str};
    }
    if (auto err = CheckDigestConsistency(blob_digest, *splice_result)) {
        auto const str = fmt::format("SpliceBlob: {}", *err);
        logger_.Emit(LogLevel::Error, "{}", str);
        return ::grpc::Status{grpc::StatusCode::INVALID_ARGUMENT, str};
    }

    response->mutable_blob_digest()->CopyFrom(*splice_result);
    return ::grpc::Status::OK;
}
