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

#include <string>

#include "catch2/catch.hpp"
#include "src/buildtool/execution_api/local/local_storage.hpp"
#include "test/utils/hermeticity/local.hpp"

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "LocalStorage: Add blob to storage from bytes",
                 "[execution_api]") {
    std::string test_bytes("test");

    LocalStorage storage{};
    auto test_digest = ArtifactDigest::Create<ObjectType::File>(test_bytes);

    // check blob not in storage
    CHECK(not storage.BlobPath(test_digest, true));
    CHECK(not storage.BlobPath(test_digest, false));

    // ensure previous calls did not accidentially create the blob
    CHECK(not storage.BlobPath(test_digest, true));
    CHECK(not storage.BlobPath(test_digest, false));

    SECTION("Add non-executable blob to storage") {
        CHECK(storage.StoreBlob(test_bytes, false));

        auto file_path = storage.BlobPath(test_digest, false);
        REQUIRE(file_path);
        CHECK(FileSystemManager::IsFile(*file_path));
        CHECK(not FileSystemManager::IsExecutable(*file_path));

        auto exe_path = storage.BlobPath(test_digest, true);
        REQUIRE(exe_path);
        CHECK(FileSystemManager::IsFile(*exe_path));
        CHECK(FileSystemManager::IsExecutable(*exe_path));
        CHECK(not FileSystemManager::IsExecutable(*file_path));
    }

    SECTION("Add executable blob to storage") {
        CHECK(storage.StoreBlob(test_bytes, true));

        auto file_path = storage.BlobPath(test_digest, false);
        REQUIRE(file_path);
        CHECK(FileSystemManager::IsFile(*file_path));
        CHECK(not FileSystemManager::IsExecutable(*file_path));

        auto exe_path = storage.BlobPath(test_digest, true);
        REQUIRE(exe_path);
        CHECK(FileSystemManager::IsFile(*exe_path));
        CHECK(FileSystemManager::IsExecutable(*exe_path));
        CHECK(not FileSystemManager::IsExecutable(*file_path));
    }
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "LocalStorage: Add blob to storage from non-executable file",
                 "[execution_api]") {
    std::filesystem::path non_exec_file{
        "test/buildtool/execution_api/data/non_executable_file"};

    LocalStorage storage{};
    auto test_blob = CreateBlobFromFile(non_exec_file);
    REQUIRE(test_blob);

    // check blob not in storage
    CHECK(not storage.BlobPath(test_blob->digest, true));
    CHECK(not storage.BlobPath(test_blob->digest, false));

    // ensure previous calls did not accidentially create the blob
    CHECK(not storage.BlobPath(test_blob->digest, true));
    CHECK(not storage.BlobPath(test_blob->digest, false));

    SECTION("Add blob to storage without specifying x-bit") {
        CHECK(storage.StoreBlob(non_exec_file));

        auto file_path = storage.BlobPath(test_blob->digest, false);
        REQUIRE(file_path);
        CHECK(FileSystemManager::IsFile(*file_path));
        CHECK(not FileSystemManager::IsExecutable(*file_path));

        auto exe_path = storage.BlobPath(test_blob->digest, true);
        REQUIRE(exe_path);
        CHECK(FileSystemManager::IsFile(*exe_path));
        CHECK(FileSystemManager::IsExecutable(*exe_path));
        CHECK(not FileSystemManager::IsExecutable(*file_path));
    }

    SECTION("Add non-executable blob to storage") {
        CHECK(storage.StoreBlob(non_exec_file, false));

        auto file_path = storage.BlobPath(test_blob->digest, false);
        REQUIRE(file_path);
        CHECK(FileSystemManager::IsFile(*file_path));
        CHECK(not FileSystemManager::IsExecutable(*file_path));

        auto exe_path = storage.BlobPath(test_blob->digest, true);
        REQUIRE(exe_path);
        CHECK(FileSystemManager::IsFile(*exe_path));
        CHECK(FileSystemManager::IsExecutable(*exe_path));
        CHECK(not FileSystemManager::IsExecutable(*file_path));
    }

    SECTION("Add executable blob to storage") {
        CHECK(storage.StoreBlob(non_exec_file, true));

        auto file_path = storage.BlobPath(test_blob->digest, false);
        REQUIRE(file_path);
        CHECK(FileSystemManager::IsFile(*file_path));
        CHECK(not FileSystemManager::IsExecutable(*file_path));

        auto exe_path = storage.BlobPath(test_blob->digest, true);
        REQUIRE(exe_path);
        CHECK(FileSystemManager::IsFile(*exe_path));
        CHECK(FileSystemManager::IsExecutable(*exe_path));
        CHECK(not FileSystemManager::IsExecutable(*file_path));
    }
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "LocalStorage: Add blob to storage from executable file",
                 "[execution_api]") {
    std::filesystem::path exec_file{
        "test/buildtool/execution_api/data/executable_file"};

    LocalStorage storage{};
    auto test_blob = CreateBlobFromFile(exec_file);
    REQUIRE(test_blob);

    // check blob not in storage
    CHECK(not storage.BlobPath(test_blob->digest, true));
    CHECK(not storage.BlobPath(test_blob->digest, false));

    // ensure previous calls did not accidentially create the blob
    CHECK(not storage.BlobPath(test_blob->digest, true));
    CHECK(not storage.BlobPath(test_blob->digest, false));

    SECTION("Add blob to storage without specifying x-bit") {
        CHECK(storage.StoreBlob(exec_file));

        auto file_path = storage.BlobPath(test_blob->digest, false);
        REQUIRE(file_path);
        CHECK(FileSystemManager::IsFile(*file_path));
        CHECK(not FileSystemManager::IsExecutable(*file_path));

        auto exe_path = storage.BlobPath(test_blob->digest, true);
        REQUIRE(exe_path);
        CHECK(FileSystemManager::IsFile(*exe_path));
        CHECK(FileSystemManager::IsExecutable(*exe_path));
        CHECK(not FileSystemManager::IsExecutable(*file_path));
    }

    SECTION("Add non-executable blob to storage") {
        CHECK(storage.StoreBlob(exec_file, false));

        auto file_path = storage.BlobPath(test_blob->digest, false);
        REQUIRE(file_path);
        CHECK(FileSystemManager::IsFile(*file_path));
        CHECK(not FileSystemManager::IsExecutable(*file_path));

        auto exe_path = storage.BlobPath(test_blob->digest, true);
        REQUIRE(exe_path);
        CHECK(FileSystemManager::IsFile(*exe_path));
        CHECK(FileSystemManager::IsExecutable(*exe_path));
        CHECK(not FileSystemManager::IsExecutable(*file_path));
    }

    SECTION("Add executable blob to storage") {
        CHECK(storage.StoreBlob(exec_file, true));

        auto file_path = storage.BlobPath(test_blob->digest, false);
        REQUIRE(file_path);
        CHECK(FileSystemManager::IsFile(*file_path));
        CHECK(not FileSystemManager::IsExecutable(*file_path));

        auto exe_path = storage.BlobPath(test_blob->digest, true);
        REQUIRE(exe_path);
        CHECK(FileSystemManager::IsFile(*exe_path));
        CHECK(FileSystemManager::IsExecutable(*exe_path));
        CHECK(not FileSystemManager::IsExecutable(*file_path));
    }
}
