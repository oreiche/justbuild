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

#define CATCH_CONFIG_RUNNER
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <thread>

#include "catch2/catch.hpp"
#include "src/buildtool/compatibility/compatibility.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "test/utils/logging/log_config.hpp"
#include "test/utils/test_env.hpp"

namespace {

void wait_for_grpc_to_shutdown() {
    // grpc_shutdown_blocking(); // not working
    std::this_thread::sleep_for(std::chrono::seconds(1));
}

/// \brief Configure remote execution from test environment. In case the
/// environment variable is malformed, we write a message and stop execution.
/// \returns true   If remote execution was successfully configured.
[[nodiscard]] auto ConfigureRemoteExecution() -> bool {
    ReadCompatibilityFromEnv();
    if (not ReadTLSAuthArgsFromEnv()) {
        return false;
    }
    HashFunction::SetHashType(Compatibility::IsCompatible()
                                  ? HashFunction::JustHash::Compatible
                                  : HashFunction::JustHash::Native);
    auto address = ReadRemoteAddressFromEnv();
    if (address and not RemoteExecutionConfig::SetRemoteAddress(*address)) {
        Logger::Log(LogLevel::Error, "parsing address '{}' failed.", *address);
        std::exit(EXIT_FAILURE);
    }
    for (auto const& property : ReadPlatformPropertiesFromEnv()) {
        if (not RemoteExecutionConfig::AddPlatformProperty(property)) {
            Logger::Log(
                LogLevel::Error, "parsing property '{}' failed.", property);
            std::exit(EXIT_FAILURE);
        }
    }
    return static_cast<bool>(RemoteExecutionConfig::RemoteAddress());
}

}  // namespace

auto main(int argc, char* argv[]) -> int {
    ConfigureLogging();

    // In case remote execution address is not valid, we skip tests. This is in
    // order to avoid tests being dependent on the environment.
    if (not ConfigureRemoteExecution()) {
        return EXIT_SUCCESS;
    }

    int result = Catch::Session().run(argc, argv);

    // valgrind fails if we terminate before grpc's async shutdown threads exit
    wait_for_grpc_to_shutdown();

    return result;
}
