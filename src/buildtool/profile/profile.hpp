// Copyright 2025 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_BUILDTOOL_PROFILE_PROFILE_HPP
#define INCLUDED_SRC_BUILDTOOL_PROFILE_PROFILE_HPP

#include <ctime>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "nlohmann/json.hpp"
#include "src/buildtool/execution_api/common/execution_response.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/main/cli.hpp"

class Profile {
  public:
    explicit Profile(std::string output_file, CommandLineArguments const& cli)
        : output_file_{std::move(output_file)} {
        profile_ = nlohmann::json::object();
        profile_["start time"] = std::time(nullptr);
        SetCLI(cli);
    }

    void Write(int exit_code);
    void SetRemoteExecutionConfig(RemoteExecutionConfig const&);
    void SetTarget(nlohmann::json target);
    void SetConfiguration(nlohmann::json configuration);
    void NoteActionCompleted(std::string const& id,
                             IExecutionResponse::Ptr const& response,
                             std::string const& cwd);
    void NoteAnalysisError(std::string const& error_message);
    void StartBuild();
    void StopBuild();

  private:
    struct ActionData {
        bool cached;
        double duration;
        int exit_code;
        std::optional<std::string> out;
        std::optional<std::string> err;
        std::unordered_map<std::string, std::string> artifacts;
    };

    std::optional<std::string> output_file_;
    nlohmann::json profile_;
    std::unordered_map<std::string, ActionData> actions_;
    std::vector<std::string> analysis_errors_;
    std::mutex mutex_;

    void SetCLI(CommandLineArguments const& cli);
};

#endif
