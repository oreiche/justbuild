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

#include <cstddef>
#include <string>
#include <vector>

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/compatibility/compatibility.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_blob.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_execution_client.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_network.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/file_system/object_type.hpp"

constexpr std::size_t kLargeSize = GRPC_DEFAULT_MAX_RECV_MESSAGE_LENGTH + 1;

TEST_CASE("Bazel network: write/read blobs", "[execution_api]") {
    auto const& info = RemoteExecutionConfig::RemoteAddress();
    std::string instance_name{"remote-execution"};
    auto network = BazelNetwork{instance_name, info->host, info->port, {}};

    std::string content_foo("foo");
    std::string content_bar("bar");
    std::string content_baz(kLargeSize, 'x');  // single larger blob

    BazelBlob foo{ArtifactDigest::Create<ObjectType::File>(content_foo),
                  content_foo,
                  /*is_exec=*/false};
    BazelBlob bar{ArtifactDigest::Create<ObjectType::File>(content_bar),
                  content_bar,
                  /*is_exec=*/false};
    BazelBlob baz{ArtifactDigest::Create<ObjectType::File>(content_baz),
                  content_baz,
                  /*is_exec=*/false};

    // Search blobs via digest
    REQUIRE(network.UploadBlobs(BlobContainer{{foo, bar, baz}}));

    // Read blobs in order
    auto reader = network.ReadBlobs(
        {foo.digest, bar.digest, baz.digest, bar.digest, foo.digest});
    std::vector<BazelBlob> blobs{};
    while (true) {
        auto next = reader.Next();
        if (next.empty()) {
            break;
        }
        blobs.insert(blobs.end(), next.begin(), next.end());
    }

    // Check order maintained
    REQUIRE(blobs.size() == 5);
    CHECK(blobs[0].data == content_foo);
    CHECK(blobs[1].data == content_bar);
    CHECK(blobs[2].data == content_baz);
    CHECK(blobs[3].data == content_bar);
    CHECK(blobs[4].data == content_foo);
}

TEST_CASE("Bazel network: read blobs with unknown size", "[execution_api]") {
    if (Compatibility::IsCompatible()) {
        // only supported in native mode
        return;
    }

    auto const& info = RemoteExecutionConfig::RemoteAddress();
    std::string instance_name{"remote-execution"};
    auto network = BazelNetwork{instance_name, info->host, info->port, {}};

    std::string content_foo("foo");
    std::string content_bar(kLargeSize, 'x');  // single larger blob

    BazelBlob foo{ArtifactDigest::Create<ObjectType::File>(content_foo),
                  content_foo,
                  /*is_exec=*/false};
    BazelBlob bar{ArtifactDigest::Create<ObjectType::File>(content_bar),
                  content_bar,
                  /*is_exec=*/false};

    // Upload blobs
    REQUIRE(network.UploadBlobs(BlobContainer{{foo, bar}}));

    // Set size to unknown
    foo.digest.set_size_bytes(0);
    bar.digest.set_size_bytes(0);

    // Read blobs
    auto reader = network.ReadBlobs({foo.digest, bar.digest});
    std::vector<BazelBlob> blobs{};
    while (true) {
        auto next = reader.Next();
        if (next.empty()) {
            break;
        }
        blobs.insert(blobs.end(), next.begin(), next.end());
    }

    // Check order maintained
    REQUIRE(blobs.size() == 2);
    CHECK(blobs[0].data == content_foo);
    CHECK(blobs[1].data == content_bar);
}
