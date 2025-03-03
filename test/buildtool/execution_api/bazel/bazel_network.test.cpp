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

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include <grpc/grpc.h>

#include "catch2/catch_test_macros.hpp"
#include "gsl/gsl"
#include "src/buildtool/common/artifact_blob.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/protocol_traits.hpp"
#include "src/buildtool/common/remote/remote_common.hpp"
#include "src/buildtool/common/remote/retry_config.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/crypto/hash_info.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_network_reader.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/utils/cpp/expected.hpp"
#include "test/utils/hermeticity/test_hash_function_type.hpp"
#include "test/utils/remote_execution/test_auth_config.hpp"
#include "test/utils/remote_execution/test_remote_config.hpp"

constexpr std::size_t kLargeSize = GRPC_DEFAULT_MAX_RECV_MESSAGE_LENGTH + 1;

TEST_CASE("Bazel network: write/read blobs", "[execution_api]") {
    std::string instance_name{"remote-execution"};

    auto auth_config = TestAuthConfig::ReadFromEnvironment();
    REQUIRE(auth_config);

    auto remote_config = TestRemoteConfig::ReadFromEnvironment();
    REQUIRE(remote_config);
    REQUIRE(remote_config->remote_address);

    RetryConfig retry_config{};  // default retry config

    HashFunction const hash_function{TestHashType::ReadFromEnvironment()};

    auto network = BazelNetwork{instance_name,
                                remote_config->remote_address->host,
                                remote_config->remote_address->port,
                                &*auth_config,
                                &retry_config,
                                {},
                                hash_function};

    std::string content_foo("foo");
    std::string content_bar("bar");
    std::string content_baz(kLargeSize, 'x');  // single larger blob

    auto const foo =
        ArtifactBlob::FromMemory(hash_function, ObjectType::File, content_foo);
    REQUIRE(foo.has_value());
    auto const bar =
        ArtifactBlob::FromMemory(hash_function, ObjectType::File, content_bar);
    REQUIRE(bar.has_value());
    auto const baz =
        ArtifactBlob::FromMemory(hash_function, ObjectType::File, content_baz);
    REQUIRE(baz.has_value());

    // Search blobs via digest
    REQUIRE(network.UploadBlobs({*foo, *bar, *baz}));

    // Read blobs in order
    auto reader = network.CreateReader();
    std::vector<ArtifactDigest> to_read{foo->GetDigest(),
                                        bar->GetDigest(),
                                        baz->GetDigest(),
                                        bar->GetDigest(),
                                        foo->GetDigest()};
    std::vector<ArtifactBlob> blobs{};
    for (auto next : reader.ReadIncrementally(&to_read)) {
        blobs.insert(blobs.end(), next.begin(), next.end());
    }

    // Check order maintained
    REQUIRE(blobs.size() == 5);
    for (auto const& blob : blobs) {
        REQUIRE(blob.ReadContent() != nullptr);
    }
    CHECK(*blobs[0].ReadContent() == content_foo);
    CHECK(*blobs[1].ReadContent() == content_bar);
    CHECK(*blobs[2].ReadContent() == content_baz);
    CHECK(*blobs[3].ReadContent() == content_bar);
    CHECK(*blobs[4].ReadContent() == content_foo);
}

TEST_CASE("Bazel network: read blobs with unknown size", "[execution_api]") {
    HashFunction const hash_function{TestHashType::ReadFromEnvironment()};
    if (not ProtocolTraits::IsNative(hash_function.GetType())) {
        // only supported in native mode
        return;
    }

    std::string instance_name{"remote-execution"};

    auto auth_config = TestAuthConfig::ReadFromEnvironment();
    REQUIRE(auth_config);

    auto remote_config = TestRemoteConfig::ReadFromEnvironment();
    REQUIRE(remote_config);
    REQUIRE(remote_config->remote_address);

    RetryConfig retry_config{};  // default retry config

    auto network = BazelNetwork{instance_name,
                                remote_config->remote_address->host,
                                remote_config->remote_address->port,
                                &*auth_config,
                                &retry_config,
                                {},
                                hash_function};

    std::string content_foo("foo");
    std::string content_bar(kLargeSize, 'x');  // single larger blob

    // Create and upload blobs:
    {
        auto const foo_blob = ArtifactBlob::FromMemory(
            hash_function, ObjectType::File, content_foo);
        REQUIRE(foo_blob.has_value());
        auto const bar_blob = ArtifactBlob::FromMemory(
            hash_function, ObjectType::File, content_bar);
        REQUIRE(bar_blob.has_value());
        REQUIRE(network.UploadBlobs({*foo_blob, *bar_blob}));
    }

    // Create digests of unknown size:
    auto const foo_digest = ArtifactDigest{
        HashInfo::HashData(hash_function, content_foo, /*is_tree=*/false),
        /*size_unknown=*/0};
    auto const bar_digest = ArtifactDigest{
        HashInfo::HashData(hash_function, content_bar, /*is_tree=*/false),
        /*size_unknown=*/0};

    // Read blobs
    auto reader = network.CreateReader();
    std::vector<ArtifactDigest> to_read{foo_digest, bar_digest};
    std::vector<ArtifactBlob> blobs{};
    for (auto next : reader.ReadIncrementally(&to_read)) {
        blobs.insert(blobs.end(), next.begin(), next.end());
    }

    // Check order maintained
    REQUIRE(blobs.size() == 2);
    for (auto const& blob : blobs) {
        REQUIRE(blob.ReadContent() != nullptr);
    }
    CHECK(*blobs[0].ReadContent() == content_foo);
    CHECK(*blobs[1].ReadContent() == content_bar);
}
