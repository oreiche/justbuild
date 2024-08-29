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

#include <memory>

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/common/statistics.hpp"
#include "src/buildtool/execution_api/local/config.hpp"
#include "src/buildtool/execution_api/local/context.hpp"
#include "src/buildtool/execution_api/local/local_api.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/execution_engine/executor/executor.hpp"
#include "src/buildtool/progress_reporting/progress.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "test/buildtool/execution_engine/executor/executor_api.test.hpp"
#include "test/utils/hermeticity/test_storage_config.hpp"
#include "test/utils/remote_execution/test_auth_config.hpp"

TEST_CASE("Executor<LocalApi>: Upload blob", "[executor]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());
    LocalExecutionConfig local_exec_config{};

    // pack the local context instances to be passed to LocalApi
    LocalContext const local_context{.exec_config = &local_exec_config,
                                     .storage_config = &storage_config.Get(),
                                     .storage = &storage};

    RepositoryConfig repo_config{};
    TestBlobUpload(&repo_config, [&] {
        return std::make_unique<LocalApi>(&local_context, &repo_config);
    });
}

TEST_CASE("Executor<LocalApi>: Compile hello world", "[executor]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());
    LocalExecutionConfig local_exec_config{};

    // pack the local context instances to be passed to LocalApi
    LocalContext const local_context{.exec_config = &local_exec_config,
                                     .storage_config = &storage_config.Get(),
                                     .storage = &storage};

    RepositoryConfig repo_config{};
    Statistics stats{};
    Progress progress{};
    auto auth_config = TestAuthConfig::ReadFromEnvironment();
    REQUIRE(auth_config);
    TestHelloWorldCompilation(
        &repo_config,
        &stats,
        &progress,
        [&] {
            return std::make_unique<LocalApi>(&local_context, &repo_config);
        },
        &*auth_config);
}

TEST_CASE("Executor<LocalApi>: Compile greeter", "[executor]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());
    LocalExecutionConfig local_exec_config{};

    // pack the local context instances to be passed to LocalApi
    LocalContext const local_context{.exec_config = &local_exec_config,
                                     .storage_config = &storage_config.Get(),
                                     .storage = &storage};

    RepositoryConfig repo_config{};
    Statistics stats{};
    Progress progress{};
    auto auth_config = TestAuthConfig::ReadFromEnvironment();
    REQUIRE(auth_config);
    TestGreeterCompilation(
        &repo_config,
        &stats,
        &progress,
        [&] {
            return std::make_unique<LocalApi>(&local_context, &repo_config);
        },
        &*auth_config);
}

TEST_CASE("Executor<LocalApi>: Upload and download trees", "[executor]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());
    LocalExecutionConfig local_exec_config{};

    // pack the local context instances to be passed to LocalApi
    LocalContext const local_context{.exec_config = &local_exec_config,
                                     .storage_config = &storage_config.Get(),
                                     .storage = &storage};

    RepositoryConfig repo_config{};
    Statistics stats{};
    Progress progress{};
    auto auth_config = TestAuthConfig::ReadFromEnvironment();
    REQUIRE(auth_config);
    TestUploadAndDownloadTrees(
        &repo_config,
        &stats,
        &progress,
        [&] {
            return std::make_unique<LocalApi>(&local_context, &repo_config);
        },
        &*auth_config);
}

TEST_CASE("Executor<LocalApi>: Retrieve output directories", "[executor]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());
    LocalExecutionConfig local_exec_config{};

    // pack the local context instances to be passed to LocalApi
    LocalContext const local_context{.exec_config = &local_exec_config,
                                     .storage_config = &storage_config.Get(),
                                     .storage = &storage};

    RepositoryConfig repo_config{};
    Statistics stats{};
    Progress progress{};
    auto auth_config = TestAuthConfig::ReadFromEnvironment();
    REQUIRE(auth_config);
    TestRetrieveOutputDirectories(
        &repo_config,
        &stats,
        &progress,
        [&] {
            return std::make_unique<LocalApi>(&local_context, &repo_config);
        },
        &*auth_config);
}
