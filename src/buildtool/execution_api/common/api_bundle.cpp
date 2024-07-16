// Copyright 2024 Huawei Cloud Computing Technology Co., Ltd.
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

#include "src/buildtool/execution_api/common/api_bundle.hpp"

#include "src/buildtool/common/remote/retry_config.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_common.hpp"
#include "src/buildtool/execution_api/local/local_api.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_api.hpp"

ApiBundle::ApiBundle(
    gsl::not_null<StorageConfig const*> const& storage_config,
    gsl::not_null<Storage const*> const& storage,
    gsl::not_null<LocalExecutionConfig const*> const& local_exec_config,
    RepositoryConfig const* repo_config,
    gsl::not_null<Auth const*> const& authentication,
    gsl::not_null<RemoteExecutionConfig const*> const& remote_exec_config)
    : local{std::make_shared<LocalApi>(storage_config,
                                       storage,
                                       local_exec_config,
                                       repo_config)},  // needed by remote
      auth{*authentication},                           // needed by remote
      remote_config{*remote_exec_config},              // needed by remote
      remote{CreateRemote(remote_exec_config->remote_address)} {}

auto ApiBundle::CreateRemote(std::optional<ServerAddress> const& address) const
    -> gsl::not_null<IExecutionApi::Ptr> {
    if (address) {
        ExecutionConfiguration config;
        config.skip_cache_lookup = false;
        return std::make_shared<BazelApi>("remote-execution",
                                          address->host,
                                          address->port,
                                          &auth,
                                          &RetryConfig::Instance(),
                                          config);
    }
    return local;
}
