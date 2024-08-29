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

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/common/artifact_description.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/execution_api/local/config.hpp"
#include "src/buildtool/execution_api/local/context.hpp"
#include "src/buildtool/execution_api/local/local_api.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "test/utils/hermeticity/test_storage_config.hpp"

namespace {

[[nodiscard]] auto GetTestDir() -> std::filesystem::path {
    auto* tmp_dir = std::getenv("TEST_TMPDIR");
    if (tmp_dir != nullptr) {
        return tmp_dir;
    }
    return FileSystemManager::GetCurrentDirectory() /
           "test/buildtool/execution_api/local";
}

[[nodiscard]] inline auto CreateLocalExecConfig() noexcept
    -> LocalExecutionConfig {
    std::vector<std::string> launcher{"env"};
    auto* env_path = std::getenv("PATH");
    if (env_path != nullptr) {
        launcher.emplace_back(std::string{"PATH="} + std::string{env_path});
    }
    else {
        launcher.emplace_back("PATH=/bin:/usr/bin");
    }
    LocalExecutionConfig::Builder builder;
    if (auto config = builder.SetLauncher(std::move(launcher)).Build()) {
        return *std::move(config);
    }
    Logger::Log(LogLevel::Error, "Failure setting the local launcher.");
    std::exit(EXIT_FAILURE);
}

}  // namespace

TEST_CASE("LocalExecution: No input, no output", "[execution_api]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());
    auto const local_exec_config = CreateLocalExecConfig();

    // pack the local context instances to be passed to LocalApi
    LocalContext const local_context{.exec_config = &local_exec_config,
                                     .storage_config = &storage_config.Get(),
                                     .storage = &storage};

    RepositoryConfig repo_config{};

    auto api = LocalApi(&local_context, &repo_config);

    std::string test_content("test");
    std::vector<std::string> const cmdline = {"echo", "-n", test_content};
    auto action =
        api.CreateAction(*api.UploadTree({}), cmdline, "", {}, {}, {}, {});
    REQUIRE(action);

    SECTION("Cache execution result in action cache") {
        // run execution
        action->SetCacheFlag(IExecutionAction::CacheFlag::CacheOutput);
        auto output = action->Execute(nullptr);
        REQUIRE(output);

        // verify result
        CHECK_FALSE(output->IsCached());
        CHECK(output->StdOut() == test_content);

        output = action->Execute(nullptr);
        REQUIRE(output);
        CHECK(output->IsCached());
    }

    SECTION("Do not cache execution result in action cache") {
        // run execution
        action->SetCacheFlag(IExecutionAction::CacheFlag::DoNotCacheOutput);
        auto output = action->Execute(nullptr);
        REQUIRE(output);

        // verify result
        CHECK_FALSE(output->IsCached());
        CHECK(output->StdOut() == test_content);

        // ensure result IS STILL NOT in cache
        output = action->Execute(nullptr);
        REQUIRE(output);
        CHECK_FALSE(output->IsCached());
    }
}

TEST_CASE("LocalExecution: No input, no output, env variables used",
          "[execution_api]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());
    auto const local_exec_config = CreateLocalExecConfig();

    // pack the local context instances to be passed to LocalApi
    LocalContext const local_context{.exec_config = &local_exec_config,
                                     .storage_config = &storage_config.Get(),
                                     .storage = &storage};

    RepositoryConfig repo_config{};

    auto api = LocalApi(&local_context, &repo_config);

    std::string test_content("test from env var");
    std::vector<std::string> const cmdline = {
        "/bin/sh", "-c", "set -e\necho -n ${MYCONTENT}"};
    auto action = api.CreateAction(*api.UploadTree({}),
                                   cmdline,
                                   "",
                                   {},
                                   {},
                                   {{"MYCONTENT", test_content}},
                                   {});
    REQUIRE(action);

    SECTION("Cache execution result in action cache") {
        // run execution
        action->SetCacheFlag(IExecutionAction::CacheFlag::CacheOutput);
        auto output = action->Execute(nullptr);
        REQUIRE(output);

        // verify result
        CHECK_FALSE(output->IsCached());
        CHECK(output->StdOut() == test_content);

        // ensure result IS in cache
        output = action->Execute(nullptr);
        REQUIRE(output);
        CHECK(output->IsCached());
    }

    SECTION("Do not cache execution result in action cache") {
        // run execution
        action->SetCacheFlag(IExecutionAction::CacheFlag::DoNotCacheOutput);
        auto output = action->Execute(nullptr);
        REQUIRE(output);

        // verify result
        CHECK_FALSE(output->IsCached());
        CHECK(output->StdOut() == test_content);

        // ensure result IS STILL NOT in cache
        output = action->Execute(nullptr);
        REQUIRE(output);
        CHECK_FALSE(output->IsCached());
    }
}

TEST_CASE("LocalExecution: No input, create output", "[execution_api]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());
    auto const local_exec_config = CreateLocalExecConfig();

    // pack the local context instances to be passed to LocalApi
    LocalContext const local_context{.exec_config = &local_exec_config,
                                     .storage_config = &storage_config.Get(),
                                     .storage = &storage};

    RepositoryConfig repo_config{};

    auto api = LocalApi(&local_context, &repo_config);

    std::string test_content("test");
    auto test_digest = ArtifactDigest::Create<ObjectType::File>(
        storage_config.Get().hash_function, test_content);

    std::string output_path{"output_file"};
    std::vector<std::string> const cmdline = {
        "/bin/sh",
        "-c",
        "set -e\necho -n " + test_content + " > " + output_path};

    auto action = api.CreateAction(
        *api.UploadTree({}), cmdline, "", {output_path}, {}, {}, {});
    REQUIRE(action);

    SECTION("Cache execution result in action cache") {
        // run execution
        action->SetCacheFlag(IExecutionAction::CacheFlag::CacheOutput);
        auto output = action->Execute(nullptr);
        REQUIRE(output);

        // verify result
        CHECK_FALSE(output->IsCached());
        auto artifacts = output->Artifacts();
        REQUIRE(artifacts.contains(output_path));
        CHECK(artifacts.at(output_path).digest == test_digest);

        // ensure result IS in cache
        output = action->Execute(nullptr);
        REQUIRE(output);
        CHECK(output->IsCached());
    }

    SECTION("Do not cache execution result in action cache") {
        // run execution
        action->SetCacheFlag(IExecutionAction::CacheFlag::DoNotCacheOutput);
        auto output = action->Execute(nullptr);
        REQUIRE(output);

        // verify result
        CHECK_FALSE(output->IsCached());
        auto artifacts = output->Artifacts();
        REQUIRE(artifacts.contains(output_path));
        CHECK(artifacts.at(output_path).digest == test_digest);

        // ensure result IS STILL NOT in cache
        output = action->Execute(nullptr);
        REQUIRE(output);
        CHECK_FALSE(output->IsCached());
    }
}

TEST_CASE("LocalExecution: One input copied to output", "[execution_api]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());
    auto const local_exec_config = CreateLocalExecConfig();

    // pack the local context instances to be passed to LocalApi
    LocalContext const local_context{.exec_config = &local_exec_config,
                                     .storage_config = &storage_config.Get(),
                                     .storage = &storage};

    RepositoryConfig repo_config{};

    auto api = LocalApi(&local_context, &repo_config);

    std::string test_content("test");
    auto test_digest = ArtifactDigest::Create<ObjectType::File>(
        storage_config.Get().hash_function, test_content);
    REQUIRE(api.Upload(ArtifactBlobContainer{{ArtifactBlob{
                           test_digest, test_content, /*is_exec=*/false}}},
                       false));

    std::string input_path{"dir/subdir/input"};
    std::string output_path{"output_file"};

    std::vector<std::string> const cmdline = {"cp", input_path, output_path};

    auto local_artifact_opt =
        ArtifactDescription::CreateKnown(test_digest, ObjectType::File)
            .ToArtifact();
    auto local_artifact =
        DependencyGraph::ArtifactNode{std::move(local_artifact_opt)};

    auto action =
        api.CreateAction(*api.UploadTree({{input_path, &local_artifact}}),
                         cmdline,
                         "",
                         {output_path},
                         {},
                         {},
                         {});
    REQUIRE(action);

    SECTION("Cache execution result in action cache") {
        // run execution
        action->SetCacheFlag(IExecutionAction::CacheFlag::CacheOutput);
        auto output = action->Execute(nullptr);
        REQUIRE(output);

        // verify result
        CHECK_FALSE(output->IsCached());
        auto artifacts = output->Artifacts();
        REQUIRE(artifacts.contains(output_path));
        CHECK(artifacts.at(output_path).digest == test_digest);

        // ensure result IS in cache
        output = action->Execute(nullptr);
        REQUIRE(output);
        CHECK(output->IsCached());
    }

    SECTION("Do not cache execution result in action cache") {
        // run execution
        action->SetCacheFlag(IExecutionAction::CacheFlag::DoNotCacheOutput);
        auto output = action->Execute(nullptr);
        REQUIRE(output);

        // verify result
        CHECK_FALSE(output->IsCached());
        auto artifacts = output->Artifacts();
        REQUIRE(artifacts.contains(output_path));
        CHECK(artifacts.at(output_path).digest == test_digest);

        // ensure result IS STILL NOT in cache
        output = action->Execute(nullptr);
        REQUIRE(output);
        CHECK_FALSE(output->IsCached());
    }
}

TEST_CASE("LocalExecution: Cache failed action's result", "[execution_api]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());
    auto const local_exec_config = CreateLocalExecConfig();

    // pack the local context instances to be passed to LocalApi
    LocalContext const local_context{.exec_config = &local_exec_config,
                                     .storage_config = &storage_config.Get(),
                                     .storage = &storage};

    RepositoryConfig repo_config{};

    auto api = LocalApi(&local_context, &repo_config);

    auto flag = GetTestDir() / "flag";
    std::vector<std::string> const cmdline = {
        "sh", "-c", fmt::format("[ -f '{}' ]", flag.string())};

    auto action =
        api.CreateAction(*api.UploadTree({}), cmdline, "", {}, {}, {}, {});
    REQUIRE(action);

    action->SetCacheFlag(IExecutionAction::CacheFlag::CacheOutput);

    // run failed action
    auto failed = action->Execute(nullptr);
    REQUIRE(failed);
    CHECK_FALSE(failed->IsCached());
    CHECK(failed->ExitCode() != 0);

    REQUIRE(FileSystemManager::CreateFile(flag));

    // run success action (should rerun and overwrite)
    auto success = action->Execute(nullptr);
    REQUIRE(success);
    CHECK_FALSE(success->IsCached());
    CHECK(success->ExitCode() == 0);

    // rerun success action (should be served from cache)
    auto cached = action->Execute(nullptr);
    REQUIRE(cached);
    CHECK(cached->IsCached());
    CHECK(cached->ExitCode() == 0);

    CHECK(FileSystemManager::RemoveFile(flag));
}
