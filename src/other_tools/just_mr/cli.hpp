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

#ifndef INCLUDED_SRC_OTHER_TOOLS_JUST_MR_CLI_HPP
#define INCLUDED_SRC_OTHER_TOOLS_JUST_MR_CLI_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "CLI/CLI.hpp"
#include "fmt/core.h"
#include "gsl-lite/gsl-lite.hpp"
#include "nlohmann/json.hpp"
#include "src/buildtool/execution_api/local/config.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/other_tools/just_mr/utils.hpp"

constexpr auto kDefaultLogLevel = LogLevel::Progress;
constexpr auto kDefaultTimeout = std::chrono::milliseconds{300000};

/// \brief Arguments common to all just-mr subcommands
struct MultiRepoCommonArguments {
    std::optional<std::filesystem::path> repository_config{std::nullopt};
    std::optional<std::filesystem::path> checkout_locations_file{std::nullopt};
    std::vector<std::string> explicit_distdirs{};
    JustMR::PathsPtr just_mr_paths = std::make_shared<JustMR::Paths>();
    std::optional<std::filesystem::path> just_path{std::nullopt};
    std::optional<std::string> main{std::nullopt};
    std::optional<std::filesystem::path> rc_path{std::nullopt};
    bool norc{false};
    std::size_t jobs{std::max(1U, std::thread::hardware_concurrency())};
};

struct MultiRepoSetupArguments {
    std::optional<std::string> sub_main{std::nullopt};
    bool sub_all{false};
};

struct MultiRepoFetchArguments {
    std::optional<std::filesystem::path> fetch_dir{std::nullopt};
};

struct MultiRepoUpdateArguments {
    std::vector<std::string> repos_to_update{};
};

struct MultiRepoJustSubCmdsArguments {
    std::optional<std::string> subcmd_name{std::nullopt};
    std::vector<std::string> additional_just_args{};
    std::unordered_map<std::string, std::vector<std::string>> just_args{};
};

static inline void SetupMultiRepoCommonArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<MultiRepoCommonArguments*> const& clargs) {
    // repository config is mandatory
    app->add_option_function<std::string>(
           "-C, --repository-config",
           [clargs](auto const& repository_config_raw) {
               clargs->repository_config = std::filesystem::weakly_canonical(
                   std::filesystem::absolute(repository_config_raw));
           },
           "Repository-description file to use.")
        ->type_name("FILE");
    app->add_option_function<std::string>(
           "--local-build-root",
           [clargs](auto const& local_build_root_raw) {
               clargs->just_mr_paths->root = std::filesystem::weakly_canonical(
                   std::filesystem::absolute(local_build_root_raw));
           },
           "Root for CAS, repository space, etc.")
        ->type_name("PATH");
    app->add_option_function<std::string>(
           "-L",
           [clargs](auto const& checkout_locations_raw) {
               clargs->checkout_locations_file =
                   std::filesystem::weakly_canonical(
                       std::filesystem::absolute(checkout_locations_raw));
           },
           "Specification file for checkout locations.")
        ->type_name("CHECKOUT_LOCATION");
    app->add_option_function<std::string>(
           "--distdir",
           [clargs](auto const& distdir_raw) {
               clargs->explicit_distdirs.emplace_back(
                   std::filesystem::weakly_canonical(std::filesystem::absolute(
                       std::filesystem::path(distdir_raw))));
           },
           "Directory to look for distfiles before fetching.")
        ->type_name("PATH")
        ->trigger_on_parse();  // run callback on all instances while parsing,
                               // not after all parsing is done
    app->add_option("--just", clargs->just_path, "Path to the just binary.")
        ->type_name("PATH");
    app->add_option("--main",
                    clargs->main,
                    "Main repository to consider from the configuration.")
        ->type_name("MAIN");
    app->add_option_function<std::string>(
           "--rc",
           [clargs](auto const& rc_path_raw) {
               clargs->rc_path = std::filesystem::weakly_canonical(
                   std::filesystem::absolute(rc_path_raw));
           },
           "Use just-mrrc file from custom path.")
        ->type_name("RCFILE");
    app->add_flag("--norc", clargs->norc, "Do not use any just-mrrc file.");
    app->add_option("-j, --jobs",
                    clargs->jobs,
                    "Number of jobs to run (Default: Number of cores).")
        ->type_name("NUM");
}

static inline void SetupMultiRepoSetupArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<MultiRepoSetupArguments*> const& clargs) {
    app->add_option("main-repo",
                    clargs->sub_main,
                    "Main repository to consider from the configuration.")
        ->type_name("");
    app->add_flag("--all",
                  clargs->sub_all,
                  "Consider all repositories in the configuration.");
}

static inline void SetupMultiRepoFetchArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<MultiRepoFetchArguments*> const& clargs) {
    app->add_option_function<std::string>(
           "-o",
           [clargs](auto const& fetch_dir_raw) {
               clargs->fetch_dir = std::filesystem::weakly_canonical(
                   std::filesystem::absolute(fetch_dir_raw));
           },
           "Directory to write distfiles when fetching.")
        ->type_name("PATH");
}

static inline void SetupMultiRepoUpdateArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<MultiRepoUpdateArguments*> const& clargs) {
    // take all remaining args as positional
    app->add_option("repo", clargs->repos_to_update, "Repository to update.")
        ->type_name("");
}

#endif  // INCLUDED_SRC_OTHER_TOOLS_JUST_MR_CLI_HPP