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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_CONFIG_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_CONFIG_HPP

#ifdef __unix__
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#else
#error "Non-unix is not supported yet"
#endif

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <fmt/core.h>
#include <gsl-lite/gsl-lite.hpp>
#include <nlohmann/json.hpp>

#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/compatibility/compatibility.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"

/// \brief Store global build system configuration.
class LocalExecutionConfig {
    struct ConfigData {

        // Build root directory. All the cache dirs are subdirs of build_root.
        // By default, build_root is set to $HOME/.cache/just.
        // If the user uses --local-build-root PATH,
        // then build_root will be set to PATH.
        std::filesystem::path build_root{};

        // cache_root points to the root of the cache dirs.
        std::filesystem::path cache_root{};

        // cache_root_generations holds root directories for all cache
        // generations (default: two generations). The latest generation points
        // to one the following directories:
        // build_root/protocol-dependent/generation-0/{git-sha1,compatible-sha256}
        // git-sha1 is the current default. If the user passes the flag
        // --compatible, then the subfolder compatible_sha256 is used
        std::vector<std::filesystem::path> cache_root_generations{"", ""};

        // Launcher to be prepended to action's command before executed.
        // Default: ["env", "--"]
        std::vector<std::string> launcher{"env", "--"};
    };

    // different folder for different caching protocol
    [[nodiscard]] static auto UpdatePathForCompatibility(
        std::filesystem::path const& dir) -> std::filesystem::path {
        return dir / (Compatibility::IsCompatible() ? "compatible-sha256"
                                                    : "git-sha1");
    }

  public:
    [[nodiscard]] static auto SetBuildRoot(
        std::filesystem::path const& dir) noexcept -> bool {
        if (FileSystemManager::IsRelativePath(dir)) {
            Logger::Log(LogLevel::Error,
                        "Build root must be absolute path but got '{}'.",
                        dir.string());
            return false;
        }
        Data().build_root = dir;
        // In case we re-set build_root, we are sure that the cache roots are
        // recomputed as well.
        Data().cache_root = std::filesystem::path{};
        Data().cache_root_generations =
            std::vector(NumGenerations(), std::filesystem::path{});
        // Pre-initialize cache roots to avoid race condition during lazy
        // initialization by multiple threads.
        for (int i = 0; i < NumGenerations(); ++i) {
            [[maybe_unused]] auto root = CacheRoot(i);
        }
        return true;
    }

    [[nodiscard]] static auto SetLauncher(
        std::vector<std::string> const& launcher) noexcept -> bool {
        try {
            Data().launcher = launcher;
        } catch (std::exception const& e) {
            Logger::Log(LogLevel::Error,
                        "when setting the local launcher\n{}",
                        e.what());
            return false;
        }
        return true;
    }

    /// \brief Specifies the number of cache generations.
    static auto SetNumGenerations(int num_generations) noexcept -> void {
        gsl_ExpectsAudit(num_generations > 0);
        Data().cache_root_generations =
            std::vector(num_generations, std::filesystem::path{});
    }

    [[nodiscard]] static auto NumGenerations() noexcept -> int {
        return Data().cache_root_generations.size();
    }

    /// \brief User directory.
    [[nodiscard]] static auto GetUserDir() noexcept -> std::filesystem::path {
        return GetUserHome() / ".cache" / "just";
    }

    /// \brief Build directory, defaults to user directory if not set
    [[nodiscard]] static auto BuildRoot() noexcept -> std::filesystem::path {
        auto& build_root = Data().build_root;
        if (build_root.empty()) {
            build_root = GetUserDir();
        }
        return build_root;
    }

    [[nodiscard]] static auto CacheRoot() noexcept -> std::filesystem::path {
        auto& cache_root = Data().cache_root;
        if (cache_root.empty()) {
            cache_root = BuildRoot() / "protocol-dependent";
        }
        return cache_root;
    }

    [[nodiscard]] static auto CacheRoot(int index) noexcept
        -> std::filesystem::path {
        gsl_ExpectsAudit(index >= 0 and
                         index < Data().cache_root_generations.size());
        auto& cache_root = Data().cache_root_generations[index];
        if (cache_root.empty()) {
            auto generation =
                std::string{"generation-"} + std::to_string(index);
            cache_root = CacheRoot() / generation;
        }
        return cache_root;
    }

    [[nodiscard]] static auto CacheRootDir(int index) noexcept
        -> std::filesystem::path {
        return UpdatePathForCompatibility(CacheRoot(index));
    }

    // CAS directory based on the type of the file.
    template <ObjectType kType>
    [[nodiscard]] static inline auto CASDir(int index) noexcept
        -> std::filesystem::path {
        char t = ToChar(kType);
        if constexpr (kType == ObjectType::Tree) {
            if (Compatibility::IsCompatible()) {
                t = ToChar(ObjectType::File);
            }
        }
        static auto const kSuffix = std::string{"cas"} + t;
        return CacheRootDir(index) / kSuffix;
    }

    /// \brief Action cache directory
    [[nodiscard]] static auto ActionCacheDir(int index) noexcept
        -> std::filesystem::path {
        return CacheRootDir(index) / "ac";
    }

    /// \brief Target cache root directory
    [[nodiscard]] static auto TargetCacheRoot(int index) noexcept
        -> std::filesystem::path {
        return CacheRootDir(index) / "tc";
    }

    /// \brief Target cache directory for the used execution backend.
    [[nodiscard]] static auto TargetCacheDir(int index) noexcept
        -> std::filesystem::path {
        return TargetCacheRoot(index) /
               ArtifactDigest::Create<ObjectType::File>(
                   ExecutionBackendDescription())
                   .hash();
    }

    /// \brief String representation of the used execution backend.
    [[nodiscard]] static auto ExecutionBackendDescription() noexcept
        -> std::string {
        auto address = RemoteExecutionConfig::RemoteAddress();
        auto properties = RemoteExecutionConfig::PlatformProperties();
        try {
            // json::dump with json::error_handler_t::replace will not throw an
            // exception if invalid UTF-8 sequences are detected in the input.
            // Instead, it will replace them with the UTF-8 replacement
            // character, but still it needs to be inside a try-catch clause to
            // ensure the noexcept modifier of the enclosing function.
            return nlohmann::json{
                {"remote_address",
                 address ? nlohmann::json{fmt::format(
                               "{}:{}", address->host, address->port)}
                         : nlohmann::json{}},
                {"platform_properties", properties}}
                .dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
        } catch (...) {
            return "";
        }
    }

    [[nodiscard]] static auto GetLauncher() noexcept
        -> std::vector<std::string> {
        return Data().launcher;
    }

    /// \brief Determine user root directory
    [[nodiscard]] static auto GetUserHome() noexcept -> std::filesystem::path {
        char const* root{nullptr};

#ifdef __unix__
        root = std::getenv("HOME");
        if (root == nullptr) {
            root = getpwuid(getuid())->pw_dir;
        }
#endif

        if (root == nullptr) {
            Logger::Log(LogLevel::Error,
                        "Cannot determine user home directory.");
            std::exit(EXIT_FAILURE);
        }

        return root;
    }

  private:
    [[nodiscard]] static auto Data() noexcept -> ConfigData& {
        static ConfigData instance{};
        return instance;
    }
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_CONFIG_HPP
