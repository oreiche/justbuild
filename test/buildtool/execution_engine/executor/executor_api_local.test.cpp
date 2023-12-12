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

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/execution_api/local/local_api.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/execution_engine/executor/executor.hpp"
#include "test/buildtool/execution_engine/executor/executor_api.test.hpp"
#include "test/utils/hermeticity/local.hpp"

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "Executor<LocalApi>: Upload blob",
                 "[executor]") {
    RepositoryConfig repo_config{};
    TestBlobUpload(&repo_config,
                   [&] { return std::make_unique<LocalApi>(&repo_config); });
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "Executor<LocalApi>: Compile hello world",
                 "[executor]") {
    RepositoryConfig repo_config{};
    TestHelloWorldCompilation(
        &repo_config, [&] { return std::make_unique<LocalApi>(&repo_config); });
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "Executor<LocalApi>: Compile greeter",
                 "[executor]") {
    RepositoryConfig repo_config{};
    TestGreeterCompilation(
        &repo_config, [&] { return std::make_unique<LocalApi>(&repo_config); });
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "Executor<LocalApi>: Upload and download trees",
                 "[executor]") {
    RepositoryConfig repo_config{};
    TestUploadAndDownloadTrees(
        &repo_config, [&] { return std::make_unique<LocalApi>(&repo_config); });
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "Executor<LocalApi>: Retrieve output directories",
                 "[executor]") {
    RepositoryConfig repo_config{};
    TestRetrieveOutputDirectories(
        &repo_config, [&] { return std::make_unique<LocalApi>(&repo_config); });
}
