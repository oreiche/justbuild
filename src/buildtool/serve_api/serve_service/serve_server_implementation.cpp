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

#ifndef BOOTSTRAP_BUILD_TOOL

#include "src/buildtool/serve_api/serve_service/serve_server_implementation.hpp"

#include <iostream>
#include <memory>
#include <variant>

#ifdef __unix__
#include <sys/types.h>
#else
#error "Non-unix is not supported yet"
#endif

#include "fmt/core.h"
#include "grpcpp/grpcpp.h"
#include "nlohmann/json.hpp"
#include "src/buildtool/common/protocol_traits.hpp"
#include "src/buildtool/common/remote/port.hpp"
#include "src/buildtool/execution_api/execution_service/ac_server.hpp"
#include "src/buildtool/execution_api/execution_service/bytestream_server.hpp"
#include "src/buildtool/execution_api/execution_service/capabilities_server.hpp"
#include "src/buildtool/execution_api/execution_service/cas_server.hpp"
#include "src/buildtool/execution_api/execution_service/execution_server.hpp"
#include "src/buildtool/execution_api/execution_service/operations_server.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/git_repo.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/serve_api/serve_service/configuration.hpp"
#include "src/buildtool/serve_api/serve_service/source_tree.hpp"
#include "src/buildtool/serve_api/serve_service/target.hpp"

namespace {
template <typename T>
auto TryWrite(std::string const& file, T const& content) noexcept -> bool {
    std::ofstream of{file};
    if (not of.good()) {
        Logger::Log(LogLevel::Error,
                    "Could not open {}. Make sure to have write permissions",
                    file);
        return false;
    }
    of << content;
    return true;
}
}  // namespace

auto ServeServerImpl::Create(std::optional<std::string> interface,
                             std::optional<int> port,
                             std::optional<std::string> info_file,
                             std::optional<std::string> pid_file) noexcept
    -> std::optional<ServeServerImpl> {
    ServeServerImpl server;
    if (interface) {
        server.interface_ = std::move(*interface);
    }
    if (port) {
        auto parsed_port = ParsePort(*port);
        if (parsed_port) {
            server.port_ = static_cast<int>(*parsed_port);
        }
        else {
            Logger::Log(LogLevel::Error, "Invalid port '{}'", *port);
            return std::nullopt;
        }
    }
    if (info_file) {
        server.info_file_ = std::move(*info_file);
    }
    if (pid_file) {
        server.pid_file_ = std::move(*pid_file);
    }
    return std::move(server);
}

auto ServeServerImpl::Run(
    RemoteServeConfig const& serve_config,
    gsl::not_null<LocalContext const*> const& local_context,
    gsl::not_null<RemoteContext const*> const& remote_context,
    std::optional<ServeApi> const& serve,
    ApiBundle const& apis,
    std::optional<std::uint8_t> op_exponent,
    bool with_execute) -> bool {
    // make sure the git root directory is properly initialized
    if (not FileSystemManager::CreateDirectory(
            local_context->storage_config->GitRoot())) {
        Logger::Log(LogLevel::Error,
                    "Could not create directory {}. Aborting",
                    local_context->storage_config->GitRoot().string());
        return false;
    }
    if (not GitRepo::InitAndOpen(local_context->storage_config->GitRoot(),
                                 true)) {
        Logger::Log(
            LogLevel::Error,
            fmt::format("could not initialize bare git repository {}",
                        local_context->storage_config->GitRoot().string()));
        return false;
    }

    auto const hash_type =
        local_context->storage_config->hash_function.GetType();
    SourceTreeService sts{&serve_config, local_context, &apis};
    TargetService ts{&serve_config,
                     local_context,
                     remote_context,
                     &apis,
                     serve ? &*serve : nullptr};
    ConfigurationService cs{hash_type, remote_context->exec_config};

    grpc::ServerBuilder builder;

    builder.RegisterService(&sts);
    builder.RegisterService(&ts);
    builder.RegisterService(&cs);

    // the user has not given any remote-execution endpoint
    // so we start a "just-execute instance" on the same process
    [[maybe_unused]] ExecutionServiceImpl es{
        local_context, &*apis.local, op_exponent};
    [[maybe_unused]] ActionCacheServiceImpl ac{local_context};
    [[maybe_unused]] CASServiceImpl cas{local_context};
    [[maybe_unused]] BytestreamServiceImpl b{local_context};
    [[maybe_unused]] CapabilitiesServiceImpl cap{hash_type};
    [[maybe_unused]] OperationsServiceImpl op{&es.GetOpCache()};
    if (with_execute) {
        builder.RegisterService(&es)
            .RegisterService(&ac)
            .RegisterService(&cas)
            .RegisterService(&b)
            .RegisterService(&cap)
            .RegisterService(&op);
    }

    // check authentication credentials; currently only TLS/SSL is supported
    std::shared_ptr<grpc::ServerCredentials> creds;
    if (const auto* tls_auth =
            std::get_if<Auth::TLS>(&remote_context->auth->method);
        tls_auth != nullptr) {
        auto tls_opts = grpc::SslServerCredentialsOptions{};

        tls_opts.pem_root_certs = tls_auth->ca_cert;
        grpc::SslServerCredentialsOptions::PemKeyCertPair keycert = {
            tls_auth->server_key, tls_auth->server_cert};

        tls_opts.pem_key_cert_pairs.emplace_back(keycert);

        creds = grpc::SslServerCredentials(tls_opts);
    }
    else {
        creds = grpc::InsecureServerCredentials();
    }

    builder.AddListeningPort(
        fmt::format("{}:{}", interface_, port_), creds, &port_);

    auto server = builder.BuildAndStart();
    if (not server) {
        Logger::Log(LogLevel::Error, "Could not start serve service");
        return false;
    }

    auto pid = getpid();

    nlohmann::json const& info = {
        {"interface", interface_}, {"port", port_}, {"pid", pid}};

    if (not pid_file_.empty()) {
        if (not TryWrite(pid_file_, pid)) {
            server->Shutdown();
            return false;
        }
    }

    auto const& info_str = nlohmann::to_string(info);
    Logger::Log(LogLevel::Info,
                "{}serve{} service{} started: {}",
                ProtocolTraits::IsNative(hash_type) ? "" : "compatible ",
                with_execute ? " and execute" : "",
                with_execute ? "s" : "",
                info_str);

    if (not info_file_.empty()) {
        if (not TryWrite(info_file_, info_str)) {
            server->Shutdown();
            return false;
        }
    }
    server->Wait();
    return true;
}

#endif  // BOOTSTRAP_BUILD_TOOL
