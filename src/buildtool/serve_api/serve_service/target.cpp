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

#include "src/buildtool/serve_api/serve_service/target.hpp"

#include "fmt/core.h"
#include "src/buildtool/build_engine/base_maps/entity_name.hpp"
#include "src/buildtool/build_engine/base_maps/entity_name_data.hpp"
#include "src/buildtool/build_engine/expression/configuration.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/build_engine/expression/expression_ptr.hpp"
#include "src/buildtool/build_engine/target_map/configured_target.hpp"
#include "src/buildtool/build_engine/target_map/result_map.hpp"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/graph_traverser/graph_traverser.hpp"
#include "src/buildtool/main/analyse.hpp"
#include "src/buildtool/main/build_utils.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#include "src/buildtool/progress_reporting/progress_reporter.hpp"
#include "src/buildtool/serve_api/remote/config.hpp"
#include "src/buildtool/serve_api/serve_service/target_utils.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/buildtool/storage/target_cache_key.hpp"
#include "src/utils/cpp/verify_hash.hpp"

auto TargetService::GetDispatchList(ArtifactDigest const& dispatch_digest)
    -> std::variant<::grpc::Status, dispatch_t> {
    using result_t = std::variant<::grpc::Status, dispatch_t>;
    // get blob from remote cas
    auto const& dispatch_info = Artifact::ObjectInfo{.digest = dispatch_digest,
                                                     .type = ObjectType::File};
    if (!local_api_->IsAvailable(dispatch_digest) and
        !remote_api_->RetrieveToCas({dispatch_info}, &*local_api_)) {
        return result_t(
            std::in_place_index<0>,
            ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION,
                           fmt::format("Could not retrieve from "
                                       "remote-execution end point blob {}",
                                       dispatch_info.ToString())});
    }
    // get blob content
    auto const& dispatch_str = local_api_->RetrieveToMemory(dispatch_info);
    if (not dispatch_str) {
        // this should not fail unless something really broke...
        return result_t(
            std::in_place_index<0>,
            ::grpc::Status{
                ::grpc::StatusCode::INTERNAL,
                fmt::format("Unexpected failure in retrieving blob {} from CAS",
                            dispatch_info.ToString())});
    }
    // parse content
    auto parsed = ParseDispatch(*dispatch_str);
    if (parsed.index() == 0) {
        // pass the parsing error forward
        return result_t(std::in_place_index<0>,
                        ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION,
                                       std::get<0>(parsed)});
    }
    return result_t(std::in_place_index<1>, std::get<1>(parsed));
}

auto TargetService::ServeTarget(
    ::grpc::ServerContext* /*context*/,
    const ::justbuild::just_serve::ServeTargetRequest* request,
    ::justbuild::just_serve::ServeTargetResponse* response) -> ::grpc::Status {
    // check target cache key hash for validity
    if (auto error_msg = IsAHash(request->target_cache_key_id().hash());
        error_msg) {
        logger_->Emit(LogLevel::Error, *error_msg);
        return ::grpc::Status{::grpc::StatusCode::INVALID_ARGUMENT, *error_msg};
    }
    auto const& target_cache_key_digest =
        ArtifactDigest{request->target_cache_key_id()};

    // acquire lock for CAS
    auto lock = GarbageCollector::SharedLock();
    if (!lock) {
        auto error_msg = fmt::format("Could not acquire gc SharedLock");
        logger_->Emit(LogLevel::Error, error_msg);
        return ::grpc::Status{::grpc::StatusCode::INTERNAL, error_msg};
    }

    // start filling in the backend description
    auto address = RemoteExecutionConfig::RemoteAddress();
    // read in the execution properties and add it to the description;
    // Important: we will need to pass these platform properties also to the
    // executor (via the graph_traverser) in order for the build to be properly
    // dispatched to the correct remote-execution endpoint.
    auto platform_properties = std::map<std::string, std::string>{};
    for (auto const& p : request->execution_properties()) {
        platform_properties[p.name()] = p.value();
    }
    auto description = nlohmann::json{
        {"remote_address", address ? address->ToJson() : nlohmann::json{}},
        {"platform_properties", platform_properties}};

    // read in the dispatch list and add it to the description, if not empty
    if (auto error_msg = IsAHash(request->dispatch_info().hash()); error_msg) {
        logger_->Emit(LogLevel::Error, *error_msg);
        return ::grpc::Status{::grpc::StatusCode::INVALID_ARGUMENT, *error_msg};
    }
    auto const& dispatch_info_digest = ArtifactDigest{request->dispatch_info()};
    auto res = GetDispatchList(dispatch_info_digest);
    if (res.index() == 0) {
        auto err = std::get<0>(res);
        logger_->Emit(LogLevel::Error, err.error_message());
        return err;
    }
    // keep dispatch list, as it needs to be passed to the executor (via the
    // graph_traverser)
    auto dispatch_list = std::get<1>(res);
    // parse dispatch list to json and add to description
    auto dispatch_json = nlohmann::json::array();
    try {
        for (auto const& [props, endpoint] : dispatch_list) {
            auto entry = nlohmann::json::array();
            entry.push_back(nlohmann::json(props));
            entry.push_back(endpoint.ToJson());
            dispatch_json.push_back(entry);
        }
    } catch (std::exception const& ex) {
        logger_->Emit(
            LogLevel::Info,
            fmt::format("Parsing dispatch list to JSON failed with:\n{}",
                        ex.what()));
    }
    if (not dispatch_json.empty()) {
        description["endpoint dispatch list"] = std::move(dispatch_json);
    }

    // add backend description to CAS;
    // we match the sharding strategy from regular just builds, i.e., allowing
    // fields with invalid UTF-8 characters to be considered for the serialized
    // JSON description, but using the UTF-8 replacement character to solve any
    // decoding errors.
    auto const description_str = description.dump(
        2, ' ', false, nlohmann::json::error_handler_t::replace);
    auto execution_backend_dgst =
        Storage::Instance().CAS().StoreBlob(description_str);
    if (not execution_backend_dgst) {
        logger_->Emit(
            LogLevel::Error,
            "Failed to store execution backend description in local CAS");
        return ::grpc::Status{
            ::grpc::StatusCode::INTERNAL,
            "Failed to store execution backend description in local CAS"};
    }
    auto untagged_execution_backend = ArtifactDigest{*execution_backend_dgst};
    auto const& execution_info = Artifact::ObjectInfo{
        .digest = untagged_execution_backend, .type = ObjectType::File};
    if (!local_api_->RetrieveToCas({execution_info}, &*remote_api_)) {
        auto msg = fmt::format("Failed to upload blob {} to remote CAS",
                               execution_info.ToString());
        logger_->Emit(LogLevel::Error, msg);
        return ::grpc::Status{::grpc::StatusCode::UNAVAILABLE, msg};
    }

    // get a target cache instance with the correct computed shard
    auto const& tc = Storage::Instance().TargetCache().WithShard(
        untagged_execution_backend.hash());
    auto const& tc_key =
        TargetCacheKey{{target_cache_key_digest, ObjectType::File}};

    // check if target-level cache entry has already been computed
    if (auto target_entry = tc->Read(tc_key); target_entry) {

        // make sure all artifacts referenced in the target cache value are in
        // the remote cas
        std::vector<Artifact::ObjectInfo> artifacts;
        if (!target_entry->first.ToArtifacts(&artifacts)) {
            auto msg = fmt::format(
                "Failed to extract artifacts from target cache entry {}",
                target_entry->second.ToString());
            logger_->Emit(LogLevel::Error, msg);
            return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, msg};
        }
        if (!local_api_->RetrieveToCas(artifacts, &*remote_api_)) {
            auto msg = fmt::format(
                "Failed to upload to remote cas the artifacts referenced in "
                "the target cache entry {}",
                target_entry->second.ToString());
            logger_->Emit(LogLevel::Error, msg);
            return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, msg};
        }

        // make sure the target cache value is in the remote cas
        if (!local_api_->RetrieveToCas({target_entry->second}, &*remote_api_)) {
            auto msg = fmt::format(
                "Failed to upload to remote cas the target cache entry {}",
                target_entry->second.ToString());
            logger_->Emit(LogLevel::Error, msg);
            return ::grpc::Status{::grpc::StatusCode::UNAVAILABLE, msg};
        }

        *(response->mutable_target_value()) =
            std::move(target_entry->second.digest);
        return ::grpc::Status::OK;
    }

    // get target description from remote cas
    auto const& target_cache_key_info = Artifact::ObjectInfo{
        .digest = target_cache_key_digest, .type = ObjectType::File};

    if (!local_api_->IsAvailable(target_cache_key_digest) and
        !remote_api_->RetrieveToCas({target_cache_key_info}, &*local_api_)) {
        auto msg = fmt::format(
            "could not retrieve from remote-execution end point blob {}",
            target_cache_key_info.ToString());
        logger_->Emit(LogLevel::Error, msg);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, msg};
    }

    auto const& target_description_str =
        local_api_->RetrieveToMemory(target_cache_key_info);
    if (not target_description_str) {
        // this should not fail unless something really broke...
        auto msg =
            fmt::format("Unexpected failure in retrieving blob {} from CAS",
                        target_cache_key_info.ToString());
        logger_->Emit(LogLevel::Error, msg);
        return ::grpc::Status{::grpc::StatusCode::INTERNAL, msg};
    }

    ExpressionPtr target_description_dict{};
    try {
        target_description_dict = Expression::FromJson(
            nlohmann::json::parse(*target_description_str));
    } catch (std::exception const& ex) {
        auto msg = fmt::format("Parsing TargetCacheKey {} failed with:\n{}",
                               target_cache_key_digest.hash(),
                               ex.what());
        logger_->Emit(LogLevel::Error, msg);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, msg};
    }
    if (not target_description_dict.IsNotNull() or
        not target_description_dict->IsMap()) {
        auto msg =
            fmt::format("TargetCacheKey {} should contain a map, but found {}",
                        target_cache_key_digest.hash(),
                        target_description_dict.ToJson().dump());
        logger_->Emit(LogLevel::Error, msg);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, msg};
    }

    std::string error_msg{};  // buffer to store various error messages

    // utility function to check the correctness of the TargetCacheKey
    auto check_key = [&target_description_dict,
                      this,
                      &target_cache_key_digest,
                      &error_msg](std::string const& key) -> bool {
        if (!target_description_dict->At(key)) {
            error_msg =
                fmt::format("TargetCacheKey {} does not contain key \"{}\"",
                            target_cache_key_digest.hash(),
                            key);
            logger_->Emit(LogLevel::Error, error_msg);
            return false;
        }
        return true;
    };

    if (!check_key("repo_key") or !check_key("target_name") or
        !check_key("effective_config")) {
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION,
                              error_msg};
    }

    // get repository config blob path
    auto const& repo_key =
        target_description_dict->Get("repo_key", Expression::none_t{});
    if (not repo_key.IsNotNull() or not repo_key->IsString()) {
        auto msg = fmt::format(
            "TargetCacheKey {}: \"repo_key\" value should be a string, but "
            "found {}",
            target_cache_key_digest.hash(),
            repo_key.ToJson().dump());
        logger_->Emit(LogLevel::Error, msg);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, msg};
    }
    ArtifactDigest repo_key_dgst{repo_key->String(), 0, /*is_tree=*/false};
    if (!local_api_->IsAvailable(repo_key_dgst) and
        !remote_api_->RetrieveToCas(
            {Artifact::ObjectInfo{.digest = repo_key_dgst,
                                  .type = ObjectType::File}},
            &*local_api_)) {
        auto msg = fmt::format(
            "Could not retrieve from remote-execution end point blob {}",
            repo_key->String());
        logger_->Emit(LogLevel::Error, msg);
        return ::grpc::Status{::grpc::StatusCode::UNAVAILABLE, msg};
    }
    auto repo_config_path = Storage::Instance().CAS().BlobPath(
        repo_key_dgst, /*is_executable=*/false);
    if (not repo_config_path) {
        auto msg = fmt::format("Repository configuration blob {} not in CAS",
                               repo_key->String());
        logger_->Emit(LogLevel::Error, msg);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, msg};
    }

    // populate the RepositoryConfig instance
    RepositoryConfig repository_config{};
    std::string const main_repo{"0"};  // known predefined main repository name
    if (auto err_msg = DetermineRoots(
            main_repo, *repo_config_path, &repository_config, logger_)) {
        logger_->Emit(LogLevel::Error, *err_msg);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION,
                              *err_msg};
    }

    // get the target name
    auto const& target_expr =
        target_description_dict->Get("target_name", Expression::none_t{});
    if (not target_expr.IsNotNull() or not target_expr->IsString()) {
        auto msg = fmt::format(
            "TargetCacheKey {}: \"target_name\" value should be a string, but"
            " found {}",
            target_cache_key_digest.hash(),
            target_expr.ToJson().dump());
        logger_->Emit(LogLevel::Error, msg);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, msg};
    }
    auto target_name = nlohmann::json::object();
    try {
        target_name = nlohmann::json::parse(target_expr->String());
    } catch (std::exception const& ex) {
        auto msg = fmt::format(
            "TargetCacheKey {}: parsing \"target_name\" failed with:\n{}",
            target_cache_key_digest.hash(),
            ex.what());
        logger_->Emit(LogLevel::Error, msg);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, msg};
    }

    // get the effective config of the export target
    auto const& config_expr =
        target_description_dict->Get("effective_config", Expression::none_t{});
    if (not config_expr.IsNotNull() or not config_expr->IsString()) {
        auto msg = fmt::format(
            "TargetCacheKey {}: \"effective_config\" value should be a string,"
            " but found {}",
            target_cache_key_digest.hash(),
            config_expr.ToJson().dump());
        logger_->Emit(LogLevel::Error, msg);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, msg};
    }
    Configuration config{};
    try {
        config = Configuration{
            Expression::FromJson(nlohmann::json::parse(config_expr->String()))};
    } catch (std::exception const& ex) {
        auto msg = fmt::format(
            "TargetCacheKey {}: parsing \"effective_config\" failed with:\n{}",
            target_cache_key_digest.hash(),
            ex.what());
        logger_->Emit(LogLevel::Error, msg);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, msg};
    }

    // get the ConfiguredTarget
    auto entity = BuildMaps::Base::ParseEntityNameFromJson(
        target_name,
        BuildMaps::Base::EntityName{
            BuildMaps::Base::NamedTarget{main_repo, ".", ""}},
        &repository_config,
        [&error_msg, &target_name](std::string const& parse_err) {
            error_msg = fmt::format("Parsing target name {} failed with:\n {} ",
                                    target_name.dump(),
                                    parse_err);
        });
    if (not entity) {
        logger_->Emit(LogLevel::Error, error_msg);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION,
                              error_msg};
    }

    BuildMaps::Target::ResultTargetMap result_map{RemoteServeConfig::Jobs()};
    auto configured_target = BuildMaps::Target::ConfiguredTarget{
        .target = std::move(*entity), .config = std::move(config)};

    // analyse the configured target
    auto result = AnalyseTarget(configured_target,
                                &result_map,
                                &repository_config,
                                RemoteServeConfig::Jobs(),
                                std::nullopt /*request_action_input*/);

    if (not result) {
        auto msg = fmt::format("Failed to analyse target {}",
                               configured_target.target.ToString());
        logger_->Emit(LogLevel::Error, msg);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, msg};
    }

    // get the output artifacts
    auto const [artifacts, runfiles] = ReadOutputArtifacts(result->target);

    // get the result map outputs
    auto const& [actions, blobs, trees] = result_map.ToResult();

    // collect cache targets and artifacts for target-level caching
    auto const cache_targets = result_map.CacheTargets();
    auto cache_artifacts = CollectNonKnownArtifacts(cache_targets);

    // Clean up result map, now that it is no longer needed
    {
        TaskSystem ts;
        result_map.Clear(&ts);
    }

    auto jobs = RemoteServeConfig::BuildJobs();
    if (jobs == 0) {
        jobs = RemoteServeConfig::Jobs();
    }

    // setup graph traverser
    GraphTraverser::CommandLineArguments traverser_args{};
    traverser_args.jobs = jobs;
    traverser_args.build.timeout = RemoteServeConfig::ActionTimeout();
    traverser_args.stage = std::nullopt;
    traverser_args.rebuild = std::nullopt;
    GraphTraverser const traverser{std::move(traverser_args),
                                   &repository_config,
                                   std::move(platform_properties),
                                   std::move(dispatch_list),
                                   ProgressReporter::Reporter()};

    // perform build
    auto build_result = traverser.BuildAndStage(
        artifacts, runfiles, actions, blobs, trees, std::move(cache_artifacts));

    if (not build_result) {
        auto msg = fmt::format("Build for target {} failed",
                               configured_target.target.ToString());
        logger_->Emit(LogLevel::Error, msg);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, msg};
    }

    WriteTargetCacheEntries(cache_targets,
                            build_result->extra_infos,
                            jobs,
                            traverser.GetLocalApi(),
                            traverser.GetRemoteApi(),
                            RemoteServeConfig::TCStrategy(),
                            *tc);

    if (build_result->failed_artifacts) {
        auto msg =
            fmt::format("Build result for target {} contains failed artifacts ",
                        configured_target.target.ToString());
        logger_->Emit(LogLevel::Warning, msg);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, msg};
    }

    // make sure remote CAS has all artifacts
    if (auto target_entry = tc->Read(tc_key); target_entry) {
        // make sure the target cache value is in the remote cas
        if (!local_api_->RetrieveToCas({target_entry->second}, &*remote_api_)) {
            auto msg = fmt::format(
                "Failed to upload to remote cas the target cache entry {}",
                target_entry->second.ToString());
            logger_->Emit(LogLevel::Error, msg);
            return ::grpc::Status{::grpc::StatusCode::UNAVAILABLE, msg};
        }
        *(response->mutable_target_value()) =
            std::move(target_entry->second.digest);
        return ::grpc::Status::OK;
    }

    // target cache value missing -- internally something is very wrong
    auto msg = fmt::format("Failed to read TargetCacheKey {} after store",
                           target_cache_key_digest.hash());
    logger_->Emit(LogLevel::Error, msg);
    return ::grpc::Status{::grpc::StatusCode::INTERNAL, msg};
}

auto TargetService::ServeTargetVariables(
    ::grpc::ServerContext* /*context*/,
    const ::justbuild::just_serve::ServeTargetVariablesRequest* request,
    ::justbuild::just_serve::ServeTargetVariablesResponse* response)
    -> ::grpc::Status {
    auto const& root_tree{request->root_tree()};
    auto const& target_file{request->target_file()};
    auto const& target{request->target()};
    // retrieve content of target file
    std::optional<std::string> target_file_content{std::nullopt};
    bool tree_found{false};
    // try in local build root Git cache
    if (auto res = GetBlobContent(
            StorageConfig::GitRoot(), root_tree, target_file, logger_)) {
        tree_found = true;
        if (res->first) {
            if (not res->second) {
                // tree exists, but does not contain target file
                auto err = fmt::format(
                    "Target-root {} found, but does not contain target file {}",
                    root_tree,
                    target_file);
                return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION,
                                      err};
            }
            target_file_content = *res->second;
        }
    }
    if (not target_file_content) {
        // try given extra repositories, in order
        for (auto const& path : RemoteServeConfig::KnownRepositories()) {
            if (auto res =
                    GetBlobContent(path, root_tree, target_file, logger_)) {
                tree_found = true;
                if (res->first) {
                    if (not res->second) {
                        // tree exists, but does not contain target file
                        auto err = fmt::format(
                            "Target-root {} found, but does not contain target "
                            "file {}",
                            root_tree,
                            target_file);
                        return ::grpc::Status{
                            ::grpc::StatusCode::FAILED_PRECONDITION, err};
                    }
                    target_file_content = *res->second;
                    break;
                }
            }
        }
    }
    // report if failed to find root tree
    if (not target_file_content) {
        if (tree_found) {
            // something went wrong trying to read the target file blob
            auto err =
                fmt::format("Could not read target file {}", target_file);
            return ::grpc::Status{::grpc::StatusCode::INTERNAL, err};
        }
        // tree not found
        auto err = fmt::format("Missing target-root tree {}", root_tree);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, err};
    }
    // parse target file as json
    ExpressionPtr map{nullptr};
    try {
        map = Expression::FromJson(nlohmann::json::parse(*target_file_content));
    } catch (std::exception const& e) {
        auto err = fmt::format(
            "Failed to parse target file {} as json with error:\n{}",
            target_file,
            e.what());
        logger_->Emit(LogLevel::Error, err);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, err};
    }
    if (not map->IsMap()) {
        auto err =
            fmt::format("Target file {} should contain a map, but found:\n{}",
                        target_file,
                        map->ToString());
        logger_->Emit(LogLevel::Error, err);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, err};
    }
    // do validity checks (target present, is export, flexible_config valid)
    auto target_desc = map->At(target);
    if (not target_desc) {
        // target is not present
        auto err = fmt::format(
            "Missing target {} in target file {}", target, target_file);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, err};
    }
    auto export_desc = target_desc->get()->At("type");
    if (not export_desc) {
        auto err = fmt::format(
            "Missing \"type\" field for target {} in target file {}.",
            target,
            target_file);
        logger_->Emit(LogLevel::Error, err);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, err};
    }
    if (not export_desc->get()->IsString()) {
        auto err = fmt::format(
            "Field \"type\" for target {} in target file {} should be a "
            "string, but found:\n{}",
            target,
            target_file,
            export_desc->get()->ToString());
        logger_->Emit(LogLevel::Error, err);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, err};
    }
    if (export_desc->get()->String() != "export") {
        // target is not of "type" : "export"
        auto err =
            fmt::format(R"(target {} is not of "type" : "export")", target);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, err};
    }
    auto flexible_config = target_desc->get()->At("flexible_config");
    if (not flexible_config) {
        // respond with success and an empty flexible_config list
        return ::grpc::Status::OK;
    }
    if (not flexible_config->get()->IsList()) {
        auto err = fmt::format(
            "Field \"flexible_config\" for target {} in target file {} should "
            "be a list, but found {}",
            target,
            target_file,
            flexible_config->get()->ToString());
        logger_->Emit(LogLevel::Error, err);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, err};
    }
    // populate response with flexible_config list
    auto flexible_config_list = flexible_config->get()->List();
    std::vector<std::string> fclist{};
    for (auto const& elem : flexible_config_list) {
        if (not elem->IsString()) {
            auto err = fmt::format(
                "Field \"flexible_config\" for target {} in target file {} has "
                "non-string entry {}",
                target,
                target_file,
                elem->ToString());
            logger_->Emit(LogLevel::Error, err);
            response->clear_flexible_config();  // must be unset
            return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, err};
        }
        response->add_flexible_config(elem->String());
    }
    // respond with success
    return ::grpc::Status::OK;
}

auto TargetService::ServeTargetDescription(
    ::grpc::ServerContext* /*context*/,
    const ::justbuild::just_serve::ServeTargetDescriptionRequest* request,
    ::justbuild::just_serve::ServeTargetDescriptionResponse* response)
    -> ::grpc::Status {
    auto const& root_tree{request->root_tree()};
    auto const& target_file{request->target_file()};
    auto const& target{request->target()};
    // retrieve content of target file
    std::optional<std::string> target_file_content{std::nullopt};
    bool tree_found{false};
    // try in local build root Git cache
    if (auto res = GetBlobContent(
            StorageConfig::GitRoot(), root_tree, target_file, logger_)) {
        tree_found = true;
        if (res->first) {
            if (not res->second) {
                // tree exists, but does not contain target file
                auto err = fmt::format(
                    "Target-root {} found, but does not contain target file {}",
                    root_tree,
                    target_file);
                return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION,
                                      err};
            }
            target_file_content = *res->second;
        }
    }
    if (not target_file_content) {
        // try given extra repositories, in order
        for (auto const& path : RemoteServeConfig::KnownRepositories()) {
            if (auto res =
                    GetBlobContent(path, root_tree, target_file, logger_)) {
                tree_found = true;
                if (res->first) {
                    if (not res->second) {
                        // tree exists, but does not contain target file
                        auto err = fmt::format(
                            "Target-root {} found, but does not contain target "
                            "file {}",
                            root_tree,
                            target_file);
                        return ::grpc::Status{
                            ::grpc::StatusCode::FAILED_PRECONDITION, err};
                    }
                    target_file_content = *res->second;
                    break;
                }
            }
        }
    }
    // report if failed to find root tree
    if (not target_file_content) {
        if (tree_found) {
            // something went wrong trying to read the target file blob
            auto err =
                fmt::format("Could not read target file {}", target_file);
            return ::grpc::Status{::grpc::StatusCode::INTERNAL, err};
        }
        // tree not found
        auto err = fmt::format("Missing target-root tree {}", root_tree);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, err};
    }
    // parse target file as json
    ExpressionPtr map{nullptr};
    try {
        map = Expression::FromJson(nlohmann::json::parse(*target_file_content));
    } catch (std::exception const& e) {
        auto err = fmt::format(
            "Failed to parse target file {} as json with error:\n{}",
            target_file,
            e.what());
        logger_->Emit(LogLevel::Error, err);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, err};
    }
    if (not map->IsMap()) {
        auto err =
            fmt::format("Target file {} should contain a map, but found:\n{}",
                        target_file,
                        map->ToString());
        logger_->Emit(LogLevel::Error, err);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, err};
    }
    // do validity checks (target is present and is of "type": "export")
    auto target_desc = map->At(target);
    if (not target_desc) {
        // target is not present
        auto err = fmt::format(
            "Missing target {} in target file {}", target, target_file);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, err};
    }
    auto export_desc = target_desc->get()->At("type");
    if (not export_desc) {
        auto err = fmt::format(
            "Missing \"type\" field for target {} in target file {}.",
            target,
            target_file);
        logger_->Emit(LogLevel::Error, err);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, err};
    }
    if (not export_desc->get()->IsString()) {
        auto err = fmt::format(
            "Field \"type\" for target {} in target file {} should be a "
            "string, but found:\n{}",
            target,
            target_file,
            export_desc->get()->ToString());
        logger_->Emit(LogLevel::Error, err);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, err};
    }
    if (export_desc->get()->String() != "export") {
        // target is not of "type" : "export"
        auto err =
            fmt::format(R"(target {} is not of "type" : "export")", target);
        return ::grpc::Status{::grpc::StatusCode::FAILED_PRECONDITION, err};
    }
    // populate response description object with fields as-is
    nlohmann::json desc{};
    if (auto doc = target_desc->get()->Get("doc", Expression::none_t{});
        doc.IsNotNull()) {
        desc["doc"] = doc->ToJson();
    }
    if (auto config_doc =
            target_desc->get()->Get("config_doc", Expression::none_t{});
        config_doc.IsNotNull()) {
        desc["config_doc"] = config_doc->ToJson();
    }
    if (auto flexible_config =
            target_desc->get()->Get("flexible_config", Expression::none_t{});
        flexible_config.IsNotNull()) {
        desc["flexible_config"] = flexible_config->ToJson();
    }

    // acquire lock for CAS
    auto lock = GarbageCollector::SharedLock();
    if (!lock) {
        auto error_msg = fmt::format("Could not acquire gc SharedLock");
        logger_->Emit(LogLevel::Error, error_msg);
        return ::grpc::Status{::grpc::StatusCode::INTERNAL, error_msg};
    }

    // store description blob to local CAS and sync with remote CAS;
    // we keep the documentation strings as close to actual as possible, so we
    // do not fail if they contain invalid UTF-8 characters, instead we use the
    // UTF-8 replacement character to solve any decoding errors.
    if (auto dgst = Storage::Instance().CAS().StoreBlob(
            desc.dump(2, ' ', false, nlohmann::json::error_handler_t::replace),
            /*is_executable=*/false)) {
        auto const& artifact_dgst = ArtifactDigest{*dgst};
        if (not local_api_->RetrieveToCas(
                {Artifact::ObjectInfo{.digest = artifact_dgst,
                                      .type = ObjectType::File}},
                &*remote_api_)) {
            auto error_msg = fmt::format(
                "Failed to upload to remote cas the description blob {}",
                artifact_dgst.hash());
            logger_->Emit(LogLevel::Error, error_msg);
            return ::grpc::Status{::grpc::StatusCode::UNAVAILABLE, error_msg};
        }

        // populate response
        *(response->mutable_description_id()) = *dgst;
        return ::grpc::Status::OK;
    }
    // failed to store blob
    const auto* const error_msg =
        "Failed to store description blob to local cas";
    logger_->Emit(LogLevel::Error, error_msg);
    return ::grpc::Status{::grpc::StatusCode::INTERNAL, error_msg};
}
