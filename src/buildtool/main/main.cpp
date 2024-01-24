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

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "src/buildtool/build_engine/base_maps/entity_name.hpp"
#include "src/buildtool/build_engine/expression/evaluator.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/build_engine/target_map/target_map.hpp"
#include "src/buildtool/common/artifact_description.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/compatibility/compatibility.hpp"
#include "src/buildtool/execution_api/local/config.hpp"
#include "src/buildtool/file_system/file_root.hpp"
#include "src/buildtool/main/analyse.hpp"
#include "src/buildtool/main/cli.hpp"
#include "src/buildtool/main/constants.hpp"
#include "src/buildtool/main/describe.hpp"
#include "src/buildtool/main/diagnose.hpp"
#include "src/buildtool/main/exit_codes.hpp"
#include "src/buildtool/main/install_cas.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/garbage_collector.hpp"
#include "src/buildtool/storage/target_cache.hpp"
#ifndef BOOTSTRAP_BUILD_TOOL
#include "src/buildtool/auth/authentication.hpp"
#include "src/buildtool/common/remote/retry_parameters.hpp"
#include "src/buildtool/execution_api/execution_service/operation_cache.hpp"
#include "src/buildtool/execution_api/execution_service/server_implementation.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/graph_traverser/graph_traverser.hpp"
#include "src/buildtool/main/serve.hpp"
#include "src/buildtool/progress_reporting/progress_reporter.hpp"
#include "src/buildtool/serve_api/remote/config.hpp"
#include "src/buildtool/serve_api/serve_service/serve_server_implementation.hpp"
#include "src/buildtool/storage/garbage_collector.hpp"
#endif  // BOOTSTRAP_BUILD_TOOL
#include "src/buildtool/logging/log_config.hpp"
#include "src/buildtool/logging/log_sink_cmdline.hpp"
#include "src/buildtool/logging/log_sink_file.hpp"
#include "src/buildtool/main/build_utils.hpp"
#include "src/buildtool/main/version.hpp"
#include "src/buildtool/multithreading/async_map_consumer.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#include "src/utils/cpp/concepts.hpp"
#include "src/utils/cpp/json.hpp"

namespace {

namespace Base = BuildMaps::Base;
namespace Target = BuildMaps::Target;

void SetupDefaultLogging() {
    LogConfig::SetLogLimit(kDefaultLogLevel);
    LogConfig::SetSinks({LogSinkCmdLine::CreateFactory()});
}

void SetupLogging(LogArguments const& clargs) {
    LogConfig::SetLogLimit(clargs.log_limit);
    LogConfig::SetSinks({LogSinkCmdLine::CreateFactory(not clargs.plain_log)});
    for (auto const& log_file : clargs.log_files) {
        LogConfig::AddSink(LogSinkFile::CreateFactory(
            log_file,
            clargs.log_append ? LogSinkFile::Mode::Append
                              : LogSinkFile::Mode::Overwrite));
    }
}

#ifndef BOOTSTRAP_BUILD_TOOL
void SetupExecutionConfig(EndpointArguments const& eargs,
                          BuildArguments const& bargs,
                          RebuildArguments const& rargs) {
    using StorageConfig = StorageConfig;
    using LocalConfig = LocalExecutionConfig;
    using RemoteConfig = RemoteExecutionConfig;
    if (not(not eargs.local_root or
            (StorageConfig::SetBuildRoot(*eargs.local_root))) or
        not(not bargs.local_launcher or
            LocalConfig::SetLauncher(*bargs.local_launcher))) {
        Logger::Log(LogLevel::Error, "Failed to configure local execution.");
        std::exit(kExitFailure);
    }
    for (auto const& property : eargs.platform_properties) {
        if (not RemoteConfig::AddPlatformProperty(property)) {
            Logger::Log(LogLevel::Error,
                        "Adding platform property '{}' failed.",
                        property);
            std::exit(kExitFailure);
        }
    }
    if (eargs.remote_execution_address) {
        if (not RemoteConfig::SetRemoteAddress(
                *eargs.remote_execution_address)) {
            Logger::Log(LogLevel::Error,
                        "Setting remote execution address '{}' failed.",
                        *eargs.remote_execution_address);
            std::exit(kExitFailure);
        }
    }
    if (eargs.remote_execution_dispatch_file) {
        if (not RemoteConfig::SetRemoteExecutionDispatch(
                *eargs.remote_execution_dispatch_file)) {
            Logger::Log(
                LogLevel::Error,
                "Setting remote execution dispatch based on file '{}' failed.",
                eargs.remote_execution_dispatch_file->string());
            std::exit(kExitFailure);
        }
    }
    if (rargs.cache_endpoint) {
        if (not(RemoteConfig::SetCacheAddress(*rargs.cache_endpoint) ==
                (*rargs.cache_endpoint != "local"))) {
            Logger::Log(LogLevel::Error,
                        "Setting cache endpoint address '{}' failed.",
                        *rargs.cache_endpoint);
            std::exit(kExitFailure);
        }
    }
}

void SetupServeConfig(ServeArguments const& srvargs,
                      CommonArguments const& cargs,
                      BuildArguments const& bargs,
                      TCArguments const& tc) {
    if (srvargs.remote_serve_address) {
        if (not RemoteServeConfig::SetRemoteAddress(
                *srvargs.remote_serve_address)) {
            Logger::Log(LogLevel::Error,
                        "Setting serve service address '{}' failed.",
                        *srvargs.remote_serve_address);
            std::exit(kExitFailure);
        }
    }
    if (not srvargs.repositories.empty() and
        not RemoteServeConfig::SetKnownRepositories(srvargs.repositories)) {
        Logger::Log(LogLevel::Error,
                    "Setting serve service repositories failed.");
        std::exit(kExitFailure);
    }
    // make parallelism and build options available for remote builds
    if (not RemoteServeConfig::SetJobs(cargs.jobs)) {
        Logger::Log(LogLevel::Error, "Setting jobs failed.");
        std::exit(kExitFailure);
    }
    if (bargs.build_jobs > 0 and
        not RemoteServeConfig::SetBuildJobs(bargs.build_jobs)) {
        Logger::Log(LogLevel::Error, "Setting build jobs failed.");
        std::exit(kExitFailure);
    }
    if (not RemoteServeConfig::SetActionTimeout(bargs.timeout)) {
        Logger::Log(LogLevel::Error, "Setting action timeout failed.");
        std::exit(kExitFailure);
    }
    RemoteServeConfig::SetTCStrategy(tc.target_cache_write_strategy);
    if (tc.target_cache_write_strategy == TargetCacheWriteStrategy::Disable) {
        Logger::Log(LogLevel::Info,
                    "Target-level cache writing of serve service is disabled.");
    }
}

void SetupAuthConfig(CommonAuthArguments const& authargs,
                     ClientAuthArguments const& client_authargs,
                     ServerAuthArguments const& server_authargs) {
    auto use_tls = false;
    if (authargs.tls_ca_cert) {
        use_tls = true;
        if (not Auth::TLS::SetCACertificate(*authargs.tls_ca_cert)) {
            Logger::Log(LogLevel::Error,
                        "Could not read '{}' certificate.",
                        authargs.tls_ca_cert->string());
            std::exit(kExitFailure);
        }
    }
    if (client_authargs.tls_client_cert) {
        use_tls = true;
        if (not Auth::TLS::SetClientCertificate(
                *client_authargs.tls_client_cert)) {
            Logger::Log(LogLevel::Error,
                        "Could not read '{}' certificate.",
                        client_authargs.tls_client_cert->string());
            std::exit(kExitFailure);
        }
    }
    if (client_authargs.tls_client_key) {
        use_tls = true;
        if (not Auth::TLS::SetClientKey(*client_authargs.tls_client_key)) {
            Logger::Log(LogLevel::Error,
                        "Could not read '{}' key.",
                        client_authargs.tls_client_key->string());
            std::exit(kExitFailure);
        }
    }

    if (server_authargs.tls_server_cert) {
        use_tls = true;
        if (not Auth::TLS::SetServerCertificate(
                *server_authargs.tls_server_cert)) {
            Logger::Log(LogLevel::Error,
                        "Could not read '{}' certificate.",
                        server_authargs.tls_server_cert->string());
            std::exit(kExitFailure);
        }
    }
    if (server_authargs.tls_server_key) {
        use_tls = true;
        if (not Auth::TLS::SetServerKey(*server_authargs.tls_server_key)) {
            Logger::Log(LogLevel::Error,
                        "Could not read '{}' key.",
                        server_authargs.tls_server_key->string());
            std::exit(kExitFailure);
        }
    }

    if (use_tls) {
        if (not Auth::TLS::Validate()) {
            std::exit(kExitFailure);
        }
    }
}

void SetupExecutionServiceConfig(ServiceArguments const& args) {
    if (args.port) {
        if (!ServerImpl::SetPort(*args.port)) {
            Logger::Log(LogLevel::Error, "Invalid port '{}'", *args.port);
            std::exit(kExitFailure);
        }
    }
    if (args.info_file) {
        if (!ServerImpl::SetInfoFile(*args.info_file)) {
            Logger::Log(LogLevel::Error,
                        "Invalid info-file '{}'",
                        args.info_file->string());
            std::exit(kExitFailure);
        }
    }
    if (args.interface) {
        if (!ServerImpl::SetInterface(*args.interface)) {
            Logger::Log(LogLevel::Error,
                        "Invalid interface '{}'",
                        args.info_file->string());
            std::exit(kExitFailure);
        }
    }
    if (args.pid_file) {
        if (!ServerImpl::SetPidFile(*args.pid_file)) {
            Logger::Log(LogLevel::Error,
                        "Invalid pid-file '{}'",
                        args.info_file->string());
            std::exit(kExitFailure);
        }
    }
    if (args.op_exponent) {
        OperationCache::SetExponent(*args.op_exponent);
    }
}

void SetupServeServiceConfig(ServiceArguments const& args) {
    if (args.port) {
        if (!ServeServerImpl::SetPort(*args.port)) {
            Logger::Log(LogLevel::Error, "Invalid port '{}'", *args.port);
            std::exit(kExitFailure);
        }
    }
    if (args.info_file) {
        if (!ServeServerImpl::SetInfoFile(*args.info_file)) {
            Logger::Log(LogLevel::Error,
                        "Invalid info-file '{}'",
                        args.info_file->string());
            std::exit(kExitFailure);
        }
    }
    if (args.interface) {
        if (!ServeServerImpl::SetInterface(*args.interface)) {
            Logger::Log(LogLevel::Error,
                        "Invalid interface '{}'",
                        args.info_file->string());
            std::exit(kExitFailure);
        }
    }
    if (args.pid_file) {
        if (!ServeServerImpl::SetPidFile(*args.pid_file)) {
            Logger::Log(LogLevel::Error,
                        "Invalid pid-file '{}'",
                        args.info_file->string());
            std::exit(kExitFailure);
        }
    }
}

void SetupHashFunction() {
    HashFunction::SetHashType(Compatibility::IsCompatible()
                                  ? HashFunction::JustHash::Compatible
                                  : HashFunction::JustHash::Native);
}

void SetupRetryConfig(RetryArguments const& args) {
    if (args.max_attempts) {
        if (!Retry::SetMaxAttempts(*args.max_attempts)) {
            Logger::Log(LogLevel::Error, "Invalid value for max-attempts.");
            std::exit(kExitFailure);
        }
    }
    if (args.initial_backoff_seconds) {
        if (!Retry::SetInitialBackoffSeconds(*args.initial_backoff_seconds)) {
            Logger::Log(LogLevel::Error,
                        "Invalid value for initial-backoff-seconds.");
            std::exit(kExitFailure);
        }
    }
    if (args.max_backoff_seconds) {
        if (!Retry::SetMaxBackoffSeconds(*args.max_backoff_seconds)) {
            Logger::Log(LogLevel::Error,
                        "Invalid value for max-backoff-seconds.");
            std::exit(kExitFailure);
        }
    }
}

#endif  // BOOTSTRAP_BUILD_TOOL

// returns path relative to `root`.
[[nodiscard]] auto FindRoot(std::filesystem::path const& subdir,
                            FileRoot const& root,
                            std::vector<std::string> const& markers)
    -> std::optional<std::filesystem::path> {
    Expects(subdir.is_relative());
    auto current = subdir;
    while (true) {
        for (auto const& marker : markers) {
            if (root.Exists(current / marker)) {
                return current;
            }
        }
        if (current.empty()) {
            break;
        }
        current = current.parent_path();
    }
    return std::nullopt;
}

[[nodiscard]] auto ReadConfiguration(AnalysisArguments const& clargs) noexcept
    -> Configuration {
    Configuration config{};
    if (not clargs.config_file.empty()) {
        if (not FileSystemManager::Exists(clargs.config_file)) {
            Logger::Log(LogLevel::Error,
                        "Config file {} does not exist.",
                        clargs.config_file.string());
            std::exit(kExitFailure);
        }
        try {
            std::ifstream fs(clargs.config_file);
            auto map = Expression::FromJson(nlohmann::json::parse(fs));
            if (not map->IsMap()) {
                Logger::Log(LogLevel::Error,
                            "Config file {} does not contain a map.",
                            clargs.config_file.string());
                std::exit(kExitFailure);
            }
            config = Configuration{map};
        } catch (std::exception const& e) {
            Logger::Log(LogLevel::Error,
                        "Parsing config file {} failed with error:\n{}",
                        clargs.config_file.string(),
                        e.what());
            std::exit(kExitFailure);
        }
    }

    for (auto const& s : clargs.defines) {
        try {
            auto map = Expression::FromJson(nlohmann::json::parse(s));
            if (not map->IsMap()) {
                Logger::Log(LogLevel::Error,
                            "Defines entry {} does not contain a map.",
                            nlohmann::json(s).dump());
                std::exit(kExitFailure);
            }
            config = config.Update(map);
        } catch (std::exception const& e) {
            Logger::Log(LogLevel::Error,
                        "Parsing defines entry {} failed with error:\n{}",
                        nlohmann::json(s).dump(),
                        e.what());
            std::exit(kExitFailure);
        }
    }

    return config;
}

[[nodiscard]] auto DetermineCurrentModule(
    std::filesystem::path const& workspace_root,
    FileRoot const& target_root,
    std::string const& target_file_name) -> std::string {
    std::filesystem::path cwd{};
    try {
        cwd = std::filesystem::current_path();
    } catch (std::exception const& e) {
        Logger::Log(LogLevel::Warning,
                    "Cannot determine current working directory ({}), assuming "
                    "top-level module is requested",
                    e.what());
        return ".";
    }

    auto subdir = std::filesystem::proximate(cwd, workspace_root);
    if (subdir.is_relative() and (*subdir.begin() != "..")) {
        // cwd is subdir of workspace_root
        if (auto root_dir = FindRoot(subdir, target_root, {target_file_name})) {
            return root_dir->string();
        }
    }
    return ".";
}

[[nodiscard]] auto ReadConfiguredTarget(
    std::string const& main_repo,
    std::optional<std::filesystem::path> const& main_ws_root,
    gsl::not_null<RepositoryConfig*> const& repo_config,
    AnalysisArguments const& clargs) -> Target::ConfiguredTarget {
    auto const* target_root = repo_config->TargetRoot(main_repo);
    if (target_root == nullptr) {
        Logger::Log(LogLevel::Error,
                    "Cannot obtain target root for main repo {}.",
                    main_repo);
        std::exit(kExitFailure);
    }
    auto current_module = std::string{"."};
    std::string target_file_name = *repo_config->TargetFileName(main_repo);
    if (main_ws_root) {
        // module detection only works if main workspace is on the file system
        current_module = DetermineCurrentModule(
            *main_ws_root, *target_root, target_file_name);
    }
    auto config = ReadConfiguration(clargs);
    if (clargs.target) {
        auto entity = Base::ParseEntityNameFromJson(
            *clargs.target,
            Base::EntityName{Base::NamedTarget{main_repo, current_module, ""}},
            repo_config,
            [&clargs](std::string const& parse_err) {
                Logger::Log(LogLevel::Error,
                            "Parsing target name {} failed with:\n{}",
                            clargs.target->dump(),
                            parse_err);
            });
        if (not entity) {
            std::exit(kExitFailure);
        }
        return Target::ConfiguredTarget{.target = std::move(*entity),
                                        .config = std::move(config)};
    }
#ifndef BOOTSTRAP_BUILD_TOOL
    if (target_root->IsAbsent()) {
        // since the user has not specified the target to build, we use the most
        // reasonable default value:
        //
        // module -> "." (i.e., current module)
        // target -> ""  (i.e., firstmost lexicographical target name)

        auto target = nlohmann::json::parse(R"([".",""])");
        Logger::Log(LogLevel::Debug,
                    "Detected absent target root for repo {} and no target was "
                    "given. Assuming default target {}",
                    main_repo,
                    target.dump());
        auto entity = Base::ParseEntityNameFromJson(
            target,
            Base::EntityName{Base::NamedTarget{main_repo, current_module, ""}},
            repo_config,
            [&target](std::string const& parse_err) {
                Logger::Log(LogLevel::Error,
                            "Parsing target name {} failed with:\n{}",
                            target.dump(),
                            parse_err);
            });
        if (not entity) {
            std::exit(kExitFailure);
        }
        return Target::ConfiguredTarget{.target = std::move(*entity),
                                        .config = std::move(config)};
    }
#endif
    auto const target_file =
        (std::filesystem::path{current_module} / target_file_name).string();
    if (not target_root->IsFile(target_file)) {
        Logger::Log(LogLevel::Error, "Expected file at {}.", target_file);
        std::exit(kExitFailure);
    }
    auto file_content = target_root->ReadContent(target_file);
    if (not file_content) {
        Logger::Log(LogLevel::Error, "Cannot read file {}.", target_file);
        std::exit(kExitFailure);
    }
    auto json = nlohmann::json();
    try {
        json = nlohmann::json::parse(*file_content);
    } catch (std::exception const& e) {
        Logger::Log(LogLevel::Error,
                    "While searching for the default target in {}:\n"
                    "Failed to parse json with error {}",
                    target_file,
                    e.what());
        std::exit(kExitFailure);
    }
    if (not json.is_object()) {
        Logger::Log(
            LogLevel::Error, "Invalid content in target file {}.", target_file);
        std::exit(kExitFailure);
    }
    if (json.empty()) {
        Logger::Log(LogLevel::Error,
                    "Missing target descriptions in file {}.",
                    target_file);
        std::exit(kExitFailure);
    }
    return Target::ConfiguredTarget{
        .target = Base::EntityName{Base::NamedTarget{
            main_repo, current_module, json.begin().key()}},
        .config = std::move(config)};
}

[[nodiscard]] auto DetermineWorkspaceRootByLookingForMarkers() noexcept
    -> std::filesystem::path {
    std::filesystem::path cwd{};
    try {
        cwd = std::filesystem::current_path();
    } catch (std::exception const& e) {
        Logger::Log(LogLevel::Warning,
                    "Failed to determine current working directory ({})",
                    e.what());
        Logger::Log(LogLevel::Error, "Could not determine workspace root.");
        std::exit(kExitFailure);
    }
    auto root = cwd.root_path();
    cwd = std::filesystem::relative(cwd, root);
    auto root_dir = FindRoot(cwd, FileRoot{root}, kRootMarkers);
    if (not root_dir) {
        Logger::Log(LogLevel::Error, "Could not determine workspace root.");
        std::exit(kExitFailure);
    }
    return root / *root_dir;
}

// Set all roots and name mappings from the command-line arguments and
// return the name of the main repository and main workspace path if local.
auto DetermineRoots(gsl::not_null<RepositoryConfig*> const& repository_config,
                    CommonArguments const& cargs,
                    AnalysisArguments const& aargs)
    -> std::pair<std::string, std::optional<std::filesystem::path>> {
    std::optional<std::filesystem::path> main_ws_root;
    auto repo_config = nlohmann::json::object();
    if (cargs.repository_config) {
        try {
            std::ifstream fs(*cargs.repository_config);
            repo_config = nlohmann::json::parse(fs);
            if (not repo_config.is_object()) {
                Logger::Log(
                    LogLevel::Error,
                    "Repository configuration file {} does not contain a map.",
                    (*cargs.repository_config).string());
                std::exit(kExitFailure);
            }
        } catch (std::exception const& e) {
            Logger::Log(LogLevel::Error,
                        "Parsing repository configuration file {} failed with "
                        "error:\n{}",
                        (*cargs.repository_config).string(),
                        e.what());
            std::exit(kExitFailure);
        }
    }

    std::string main_repo;

    if (cargs.main) {
        main_repo = *cargs.main;
    }
    else if (auto main_it = repo_config.find("main");
             main_it != repo_config.end()) {
        if (not main_it->is_string()) {
            Logger::Log(LogLevel::Error,
                        "Repository config: main has to be a string");
            std::exit(kExitFailure);
        }
        main_repo = *main_it;
    }
    else if (auto repos_it = repo_config.find("repositories");
             repos_it != repo_config.end() and not repos_it->empty()) {
        // take lexicographical first key as main (nlohmann::json is sorted)
        main_repo = repos_it->begin().key();
    }

    auto repos = nlohmann::json::object();
    auto repos_it = repo_config.find("repositories");
    if (repos_it != repo_config.end()) {
        if (not repos_it->is_object()) {
            Logger::Log(LogLevel::Error,
                        "Repository config: repositories has to be a map");
            std::exit(kExitFailure);
        }
        repos = *repos_it;
    }
    if (not repos.contains(main_repo)) {
        repos[main_repo] = nlohmann::json::object();
    }

    std::string error_msg;
    for (auto const& [repo, desc] : repos.items()) {
        std::optional<FileRoot> ws_root{};
        bool const is_main_repo{repo == main_repo};
        auto it_ws = desc.find("workspace_root");
        if (it_ws != desc.end()) {
            if (auto parsed_root = FileRoot::ParseRoot(
                    repo, "workspace_root", *it_ws, &error_msg)) {
                ws_root = std::move(parsed_root->first);
                if (is_main_repo and parsed_root->second.has_value()) {
                    main_ws_root = std::move(parsed_root->second);
                }
            }
            else {
                Logger::Log(LogLevel::Error, error_msg);
                std::exit(kExitFailure);
            }
        }
        if (is_main_repo) {
            // command line argument always overwrites what is eventually found
            // in the config file
            if (cargs.workspace_root) {
                main_ws_root = *cargs.workspace_root;
            }
            else if (not ws_root) {
                main_ws_root = DetermineWorkspaceRootByLookingForMarkers();
            }
            if (main_ws_root.has_value()) {
                ws_root = FileRoot{*main_ws_root};
            }
        }
        if (not ws_root) {
            Logger::Log(LogLevel::Error,
                        "Unknown workspace root for repository {}",
                        repo);
            std::exit(kExitFailure);
        }
        auto info = RepositoryConfig::RepositoryInfo{std::move(*ws_root)};
        auto parse_keyword_root = [&desc = desc,
                                   &repo = repo,
                                   &error_msg = error_msg,
                                   is_main_repo](FileRoot* keyword_root,
                                                 std::string const& keyword,
                                                 auto const& keyword_carg) {
            auto it = desc.find(keyword);
            if (it != desc.end()) {
                if (auto parsed_root =
                        FileRoot::ParseRoot(repo, keyword, *it, &error_msg)) {
                    (*keyword_root) = parsed_root->first;
                }
                else {
                    Logger::Log(LogLevel::Error, error_msg);
                    std::exit(kExitFailure);
                }
            }

            if (is_main_repo && keyword_carg) {
                *keyword_root = FileRoot{*keyword_carg};
            }
        };

        info.target_root = info.workspace_root;
        parse_keyword_root(&info.target_root, "target_root", aargs.target_root);

        info.rule_root = info.target_root;
        parse_keyword_root(&info.rule_root, "rule_root", aargs.rule_root);

        info.expression_root = info.rule_root;
        parse_keyword_root(
            &info.expression_root, "expression_root", aargs.expression_root);

        auto it_bindings = desc.find("bindings");
        if (it_bindings != desc.end()) {
            if (not it_bindings->is_object()) {
                Logger::Log(
                    LogLevel::Error,
                    "bindings has to be a string-string map, but found {}",
                    it_bindings->dump());
                std::exit(kExitFailure);
            }
            for (auto const& [local_name, global_name] : it_bindings->items()) {
                if (not repos.contains(global_name)) {
                    Logger::Log(LogLevel::Error,
                                "Binding {} for {} in {} does not refer to a "
                                "defined repository.",
                                global_name,
                                local_name,
                                repo);
                    std::exit(kExitFailure);
                }
                info.name_mapping[local_name] = global_name;
            }
        }
        auto parse_keyword_file_name = [&desc = desc, is_main_repo](
                                           std::string* keyword_file_name,
                                           std::string const& keyword,
                                           auto const& keyword_carg) {
            auto it = desc.find(keyword);
            if (it != desc.end()) {
                *keyword_file_name = *it;
            }

            if (is_main_repo && keyword_carg) {
                *keyword_file_name = *keyword_carg;
            }
        };

        parse_keyword_file_name(
            &info.target_file_name, "target_file_name", aargs.target_file_name);

        parse_keyword_file_name(
            &info.rule_file_name, "rule_file_name", aargs.rule_file_name);

        parse_keyword_file_name(&info.expression_file_name,
                                "expression_file_name",
                                aargs.expression_file_name);

        repository_config->SetInfo(repo, std::move(info));
    }

    return {main_repo, main_ws_root};
}

void ReportTaintedness(const AnalysisResult& result) {
    if (result.target->Tainted().empty()) {
        // Never report untainted targets
        return;
    }

    // To ensure proper quoting, go through json.
    nlohmann::json tainted{};
    for (auto const& s : result.target->Tainted()) {
        tainted.push_back(s);
    }
    Logger::Log(LogLevel::Info, "Target tainted {}.", tainted.dump());
}

auto DetermineNonExplicitTarget(
    std::string const& main_repo,
    std::optional<std::filesystem::path> const& main_ws_root,
    gsl::not_null<RepositoryConfig*> const& repo_config,
    AnalysisArguments const& clargs)
    -> std::optional<BuildMaps::Target::ConfiguredTarget> {
    auto id =
        ReadConfiguredTarget(main_repo, main_ws_root, repo_config, clargs);
    switch (id.target.GetNamedTarget().reference_t) {
        case Base::ReferenceType::kFile:
            std::cout << id.ToString() << " is a source file." << std::endl;
            return std::nullopt;
        case Base::ReferenceType::kTree:
            std::cout << id.ToString() << " is a tree." << std::endl;
            return std::nullopt;
        case Base::ReferenceType::kGlob:
            std::cout << id.ToString() << " is a glob." << std::endl;
            return std::nullopt;
        case Base::ReferenceType::kSymlink:
            std::cout << id.ToString() << " is a symlink." << std::endl;
            return std::nullopt;
        case Base::ReferenceType::kTarget:
            return id;
    }
    return std::nullopt;  // make gcc happy
}

void DumpArtifactsToBuild(
    std::map<std::string, ArtifactDescription> const& artifacts,
    std::map<std::string, ArtifactDescription> const& runfiles,
    const std::filesystem::path& file_path) {
    nlohmann::json to_build{};
    for (auto const& [path, artifact] : runfiles) {
        to_build[path] = artifact.ToJson();
    }
    for (auto const& [path, artifact] : artifacts) {
        to_build[path] = artifact.ToJson();
    }
    auto const dump_string = IndentListsOnlyUntilDepth(to_build, 2, 1);
    std::ofstream os(file_path);
    os << dump_string << std::endl;
}

}  // namespace

auto main(int argc, char* argv[]) -> int {
    SetupDefaultLogging();
    try {
        auto arguments = ParseCommandLineArguments(argc, argv);

        if (arguments.cmd == SubCommand::kVersion) {
            std::cout << version() << std::endl;
            return kExitSuccess;
        }

        // just serve configures all from its config file, so parse that before
        // doing further setup steps
#ifndef BOOTSTRAP_BUILD_TOOL
        if (arguments.cmd == SubCommand::kServe) {
            ReadJustServeConfig(&arguments);
        }
#endif  // BOOTSTRAP_BUILD_TOOL

        SetupLogging(arguments.log);
        if (arguments.analysis.expression_log_limit) {
            Evaluator::SetExpressionLogLimit(
                *arguments.analysis.expression_log_limit);
        }

        // global repository configuration
        RepositoryConfig repo_config{};

#ifndef BOOTSTRAP_BUILD_TOOL
        /**
         * The current implementation of libgit2 uses pthread_key_t incorrectly
         * on POSIX systems to handle thread-specific data, which requires us to
         * explicitly make sure the main thread is the first one to call
         * git_libgit2_init. Future versions of libgit2 will hopefully fix this.
         */
        GitContext::Create();

        SetupHashFunction();
        SetupExecutionConfig(
            arguments.endpoint, arguments.build, arguments.rebuild);
        SetupServeConfig(
            arguments.serve, arguments.common, arguments.build, arguments.tc);
        SetupAuthConfig(arguments.auth, arguments.cauth, arguments.sauth);

        if (arguments.cmd == SubCommand::kGc) {
            if (GarbageCollector::TriggerGarbageCollection()) {
                return kExitSuccess;
            }
            return kExitFailure;
        }

        if (arguments.cmd == SubCommand::kExecute) {
            SetupExecutionServiceConfig(arguments.service);
            if (!ServerImpl::Instance().Run()) {
                return kExitFailure;
            }
            return kExitSuccess;
        }

        if (arguments.cmd == SubCommand::kServe) {
            SetupServeServiceConfig(arguments.service);
            if (!ServeServerImpl::Instance().Run(
                    !RemoteExecutionConfig::RemoteAddress())) {
                return kExitFailure;
            }
            return kExitSuccess;
        }

        // If no execution endpoint was given, the client should default to the
        // serve endpoint, if given
        if (not RemoteExecutionConfig::RemoteAddress() and
            arguments.serve.remote_serve_address) {
            if (!RemoteExecutionConfig::SetRemoteAddress(
                    *arguments.serve.remote_serve_address)) {
                Logger::Log(LogLevel::Error,
                            "Setting remote execution address '{}' failed.",
                            *arguments.serve.remote_serve_address);
                std::exit(kExitFailure);
            }
            Logger::Log(LogLevel::Info,
                        "Using '{}' as the remote execution endpoint.",
                        *arguments.serve.remote_serve_address);
        }
#endif  // BOOTSTRAP_BUILD_TOOL

        auto jobs = arguments.build.build_jobs > 0 ? arguments.build.build_jobs
                                                   : arguments.common.jobs;

        auto stage_args = arguments.cmd == SubCommand::kInstall or
                                  arguments.cmd == SubCommand::kInstallCas or
                                  arguments.cmd == SubCommand::kTraverse
                              ? std::make_optional(std::move(arguments.stage))
                              : std::nullopt;

        auto rebuild_args =
            arguments.cmd == SubCommand::kRebuild
                ? std::make_optional(std::move(arguments.rebuild))
                : std::nullopt;

#ifndef BOOTSTRAP_BUILD_TOOL
        SetupRetryConfig(arguments.retry);
        GraphTraverser const traverser{
            {jobs,
             std::move(arguments.build),
             std::move(stage_args),
             std::move(rebuild_args)},
            &repo_config,
            RemoteExecutionConfig::PlatformProperties(),
            RemoteExecutionConfig::DispatchList(),
            ProgressReporter::Reporter()};

        if (arguments.cmd == SubCommand::kInstallCas) {
            if (not repo_config.SetGitCAS(StorageConfig::GitRoot())) {
                Logger::Log(LogLevel::Debug,
                            "Failed set Git CAS {}.",
                            StorageConfig::GitRoot().string());
            }
            return FetchAndInstallArtifacts(traverser.GetRemoteApi(),
                                            traverser.GetLocalApi(),
                                            arguments.fetch)
                       ? kExitSuccess
                       : kExitFailure;
        }
#endif  // BOOTSTRAP_BUILD_TOOL

        auto [main_repo, main_ws_root] =
            DetermineRoots(&repo_config, arguments.common, arguments.analysis);

#ifndef BOOTSTRAP_BUILD_TOOL
        auto lock = GarbageCollector::SharedLock();
        if (not lock) {
            return kExitFailure;
        }

        if (arguments.cmd == SubCommand::kTraverse) {
            if (arguments.graph.git_cas) {
                if (Compatibility::IsCompatible()) {
                    Logger::Log(LogLevel::Error,
                                "Command line options {} and {} cannot be used "
                                "together.",
                                "--git-cas",
                                "--compatible");
                    std::exit(EXIT_FAILURE);
                }
                if (not repo_config.SetGitCAS(*arguments.graph.git_cas)) {
                    Logger::Log(LogLevel::Warning,
                                "Failed set Git CAS {}.",
                                arguments.graph.git_cas->string());
                }
            }
            if (traverser.BuildAndStage(arguments.graph.graph_file,
                                        arguments.graph.artifacts)) {
                return kExitSuccess;
            }
        }
        else if (arguments.cmd == SubCommand::kDescribe) {
            if (auto id = DetermineNonExplicitTarget(main_repo,
                                                     main_ws_root,
                                                     &repo_config,
                                                     arguments.analysis)) {
                return arguments.describe.describe_rule
                           ? DescribeUserDefinedRule(
                                 id->target,
                                 &repo_config,
                                 arguments.common.jobs,
                                 arguments.describe.print_json)
                           : DescribeTarget(*id,
                                            &repo_config,
                                            arguments.common.jobs,
                                            arguments.describe.print_json);
            }
            return kExitFailure;
        }

        else {
#endif  // BOOTSTRAP_BUILD_TOOL
            BuildMaps::Target::ResultTargetMap result_map{
                arguments.common.jobs};
            auto id = ReadConfiguredTarget(
                main_repo, main_ws_root, &repo_config, arguments.analysis);
            auto result =
                AnalyseTarget(id,
                              &result_map,
                              &repo_config,
                              arguments.common.jobs,
                              arguments.analysis.request_action_input);
            if (result) {
                if (arguments.analysis.graph_file) {
                    result_map.ToFile(*arguments.analysis.graph_file);
                }
                auto const [artifacts, runfiles] =
                    ReadOutputArtifacts(result->target);
                if (arguments.analysis.artifacts_to_build_file) {
                    DumpArtifactsToBuild(
                        artifacts,
                        runfiles,
                        *arguments.analysis.artifacts_to_build_file);
                }
                if (arguments.cmd == SubCommand::kAnalyse) {
                    DiagnoseResults(*result, result_map, arguments.diagnose);
                    ReportTaintedness(*result);
                    // Clean up in parallel
                    {
                        TaskSystem ts;
                        result_map.Clear(&ts);
                    }
                    return kExitSuccess;
                }
#ifndef BOOTSTRAP_BUILD_TOOL
                Logger::Log(LogLevel::Info,
                            "Analysed target {}",
                            result->id.ToString());

                auto const& stat = Statistics::Instance();
                {
                    auto cached = stat.ExportsCachedCounter();
                    auto uncached = stat.ExportsUncachedCounter();
                    auto not_eligible = stat.ExportsNotEligibleCounter();
                    Logger::Log(cached + uncached + not_eligible > 0
                                    ? LogLevel::Info
                                    : LogLevel::Debug,
                                "Export targets found: {} cached, {} uncached, "
                                "{} not eligible for caching",
                                cached,
                                uncached,
                                not_eligible);
                }

                ReportTaintedness(*result);
                auto const& [actions, blobs, trees] = result_map.ToResult();

                // collect cache targets and artifacts for target-level caching
                auto const cache_targets = result_map.CacheTargets();
                auto cache_artifacts = CollectNonKnownArtifacts(cache_targets);

                // Clean up result map, now that it is no longer needed
                {
                    TaskSystem ts;
                    result_map.Clear(&ts);
                }

                Logger::Log(
                    LogLevel::Info,
                    "{}ing{} {}.",
                    arguments.cmd == SubCommand::kRebuild ? "Rebuild" : "Build",
                    result->modified ? fmt::format(" input of action {} of",
                                                   *(result->modified))
                                     : "",
                    result->id.ToString());

                auto build_result =
                    traverser.BuildAndStage(artifacts,
                                            runfiles,
                                            actions,
                                            blobs,
                                            trees,
                                            std::move(cache_artifacts));
                if (build_result) {
                    WriteTargetCacheEntries(
                        cache_targets,
                        build_result->extra_infos,
                        jobs,
                        traverser.GetLocalApi(),
                        traverser.GetRemoteApi(),
                        arguments.tc.target_cache_write_strategy);

                    // Repeat taintedness message to make the user aware that
                    // the artifacts are not for production use.
                    ReportTaintedness(*result);
                    if (build_result->failed_artifacts) {
                        Logger::Log(LogLevel::Warning,
                                    "Build result contains failed artifacts.");
                    }
                    return build_result->failed_artifacts
                               ? kExitSuccessFailedArtifacts
                               : kExitSuccess;
                }
            }
#endif  // BOOTSTRAP_BUILD_TOOL
        }
    } catch (std::exception const& ex) {
        Logger::Log(
            LogLevel::Error, "Caught exception with message: {}", ex.what());
    }
    return kExitFailure;
}
