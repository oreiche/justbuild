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

#ifndef CAS_SERVER_HPP
#define CAS_SERVER_HPP

#include <optional>
#include <string>

#include "build/bazel/remote/execution/v2/remote_execution.grpc.pb.h"
#include "gsl/gsl"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/storage/storage.hpp"

class CASServiceImpl final
    : public bazel_re::ContentAddressableStorage::Service {
  public:
    // Determine if blobs are present in the CAS.
    //
    // Clients can use this API before uploading blobs to determine which ones
    // are already present in the CAS and do not need to be uploaded again.
    //
    // There are no method-specific errors.
    auto FindMissingBlobs(::grpc::ServerContext* context,
                          const ::bazel_re::FindMissingBlobsRequest* request,
                          ::bazel_re::FindMissingBlobsResponse* response)
        -> ::grpc::Status override;
    // Upload many blobs at once.
    //
    // The server may enforce a limit of the combined total size of blobs
    // to be uploaded using this API. This limit may be obtained using the
    // [Capabilities][build.bazel.remote.execution.v2.Capabilities] API.
    // Requests exceeding the limit should either be split into smaller
    // chunks or uploaded using the
    // [ByteStream API][google.bytestream.ByteStream], as appropriate.
    //
    // This request is equivalent to calling a Bytestream `Write` request
    // on each individual blob, in parallel. The requests may succeed or fail
    // independently.
    //
    // Errors:
    //
    // * `INVALID_ARGUMENT`: The client attempted to upload more than the
    //   server supported limit.
    //
    // Individual requests may return the following errors, additionally:
    //
    // * `RESOURCE_EXHAUSTED`: There is insufficient disk quota to store the
    // blob.
    // * `INVALID_ARGUMENT`: The
    // [Digest][build.bazel.remote.execution.v2.Digest] does not match the
    // provided data.
    auto BatchUpdateBlobs(::grpc::ServerContext* context,
                          const ::bazel_re::BatchUpdateBlobsRequest* request,
                          ::bazel_re::BatchUpdateBlobsResponse* response)
        -> ::grpc::Status override;
    // Download many blobs at once.
    //
    // The server may enforce a limit of the combined total size of blobs
    // to be downloaded using this API. This limit may be obtained using the
    // [Capabilities][build.bazel.remote.execution.v2.Capabilities] API.
    // Requests exceeding the limit should either be split into smaller
    // chunks or downloaded using the
    // [ByteStream API][google.bytestream.ByteStream], as appropriate.
    //
    // This request is equivalent to calling a Bytestream `Read` request
    // on each individual blob, in parallel. The requests may succeed or fail
    // independently.
    //
    // Errors:
    //
    // * `INVALID_ARGUMENT`: The client attempted to read more than the
    //   server supported limit.
    //
    // Every error on individual read will be returned in the corresponding
    // digest status.
    auto BatchReadBlobs(::grpc::ServerContext* context,
                        const ::bazel_re::BatchReadBlobsRequest* request,
                        ::bazel_re::BatchReadBlobsResponse* response)
        -> ::grpc::Status override;
    // Fetch the entire directory tree rooted at a node.
    //
    // This request must be targeted at a
    // [Directory][build.bazel.remote.execution.v2.Directory] stored in the
    // [ContentAddressableStorage][build.bazel.remote.execution.v2.ContentAddressableStorage]
    // (CAS). The server will enumerate the `Directory` tree recursively and
    // return every node descended from the root.
    //
    // The GetTreeRequest.page_token parameter can be used to skip ahead in
    // the stream (e.g. when retrying a partially completed and aborted
    // request), by setting it to a value taken from
    // GetTreeResponse.next_page_token of the last successfully processed
    // GetTreeResponse).
    //
    // The exact traversal order is unspecified and, unless retrieving
    // subsequent pages from an earlier request, is not guaranteed to be stable
    // across multiple invocations of `GetTree`.
    //
    // If part of the tree is missing from the CAS, the server will return the
    // portion present and omit the rest.
    //
    // Errors:
    //
    // * `NOT_FOUND`: The requested tree root is not present in the CAS.
    auto GetTree(::grpc::ServerContext* context,
                 const ::bazel_re::GetTreeRequest* request,
                 ::grpc::ServerWriter<::bazel_re::GetTreeResponse>* writer)
        -> ::grpc::Status override;
    // Split a blob into chunks.
    //
    // This splitting API aims to reduce download traffic between client and
    // server, e.g., if a client needs to fetch a large blob that just has been
    // modified slightly since the last built. In this case, there is no need to
    // fetch the entire blob data, but just the binary differences between the
    // two blob versions, which are typically determined by deduplication
    // techniques such as content-defined chunking.
    //
    // Clients can use this API before downloading a blob to determine which
    // parts of the blob are already present locally and do not need to be
    // downloaded again. The server splits the blob into chunks according to a
    // specified content-defined chunking algorithm and returns a list of the
    // chunk digests in the order in which the chunks have to be concatenated to
    // assemble the requested blob.
    //
    // A client can expect the following guarantees from the server if a split
    // request is answered successfully:
    //  1. The blob chunks are stored in CAS.
    //  2. Concatenating the blob chunks in the order of the digest list
    //     returned by the server results in the original blob.
    //
    // The usage of this API is optional for clients but it allows them to
    // download only the missing parts of a large blob instead of the entire
    // blob data, which in turn can considerably reduce download network
    // traffic.
    //
    // Since the generated chunks are stored as blobs, they underlie the same
    // lifetimes as other blobs. However, their lifetime is extended if they are
    // part of the result of a split blob request.
    //
    // For the client, it is recommended to verify whether the digest of the
    // blob assembled by the fetched chunks results in the requested blob
    // digest.
    //
    // If several clients use blob splitting, it is recommended that they
    // request the same splitting algorithm to benefit from each others chunking
    // data. In combination with blob splicing, an agreement about the chunking
    // algorithm is recommended since both client as well as server side can
    // benefit from each others chunking data.
    //
    // Errors:
    //
    // * `NOT_FOUND`: The requested blob is not present in the CAS.
    // * `RESOURCE_EXHAUSTED`: There is insufficient disk quota to store the
    //   blob chunks.
    auto SplitBlob(::grpc::ServerContext* context,
                   const ::bazel_re::SplitBlobRequest* request,
                   ::bazel_re::SplitBlobResponse* response)
        -> ::grpc::Status override;
    // Splice a blob from chunks.
    //
    // This is the complementary operation to the
    // [ContentAddressableStorage.SplitBlob][build.bazel.remote.execution.v2.ContentAddressableStorage.SplitBlob]
    // function to handle the splitted upload of large blobs to save upload
    // traffic.
    //
    // If a client needs to upload a large blob and is able to split a blob into
    // chunks locally according to some content-defined chunking algorithm, it
    // can first determine which parts of the blob are already available in the
    // remote CAS and upload the missing chunks, and then use this API to
    // instruct the server to splice the original blob from the remotely
    // available blob chunks.
    //
    // In order to ensure data consistency of the CAS, the server will verify
    // the spliced result whether digest calculation results in the provided
    // digest from the request and will reject a splice request if this check
    // fails.
    //
    // The usage of this API is optional for clients but it allows them to
    // upload only the missing parts of a large blob instead of the entire blob
    // data, which in turn can considerably reduce upload network traffic.
    //
    // In order to split a blob into chunks, it is recommended for the client to
    // use one of the servers' advertised chunking algorithms by
    // [CacheCapabilities.supported_chunking_algorithms][build.bazel.remote.execution.v2.CacheCapabilities.supported_chunking_algorithms]
    // to benefit from each others chunking data. If several clients use blob
    // splicing, it is recommended that they use the same splitting algorithm to
    // split their blobs into chunk.
    //
    // Errors:
    //
    // * `NOT_FOUND`: At least one of the blob chunks is not present in the CAS.
    // * `RESOURCE_EXHAUSTED`: There is insufficient disk quota to store the
    //   spliced blob.
    // * `INVALID_ARGUMENT`: The digest of the spliced blob is different from
    //   the provided expected digest.
    auto SpliceBlob(::grpc::ServerContext* context,
                    const ::bazel_re::SpliceBlobRequest* request,
                    ::bazel_re::SpliceBlobResponse* response)
        -> ::grpc::Status override;

  private:
    [[nodiscard]] auto CheckDigestConsistency(bazel_re::Digest const& ref,
                                              bazel_re::Digest const& computed)
        const noexcept -> std::optional<std::string>;

    gsl::not_null<Storage const*> storage_ = &Storage::Instance();
    Logger logger_{"execution-service"};
};
#endif
