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

#include "src/buildtool/file_system/precomputed_root.hpp"

#include <compare>
#include <exception>

#include "fmt/core.h"
#include "gsl/gsl"
#include "src/utils/cpp/hash_combine.hpp"

namespace {
template <typename T>
[[nodiscard]] auto ParseImpl(nlohmann::json const& /*root*/)
    -> expected<T, std::string> {
    Ensures(false);
}

template <typename T>
[[nodiscard]] auto ParsePrecomputed(nlohmann::json const& root) noexcept
    -> expected<PrecomputedRoot, std::string> {
    try {
        auto parsed_root = ParseImpl<T>(root);
        if (not parsed_root) {
            return unexpected{fmt::format("While parsing {} root {}:\n{}",
                                          T::kMarker,
                                          root.dump(),
                                          std::move(parsed_root).error())};
        }
        return PrecomputedRoot{*std::move(parsed_root)};
    } catch (const std::exception& e) {
        return unexpected{fmt::format("While parsing {} root {}:\n{}",
                                      T::kMarker,
                                      root.dump(),
                                      e.what())};
    }
}

template <>
[[nodiscard]] auto ParseImpl<ComputedRoot>(nlohmann::json const& root)
    -> expected<ComputedRoot, std::string> {
    if ((root.size() != ComputedRoot::kSchemeLength) and
        (root.size() != ComputedRoot::kSchemeLength + 1)) {
        return unexpected{fmt::format(
            "The root has a wrong number of arguments: {}\nThe scheme requires "
            "[<scheme>, <root>, <module>, <name>, <config>] optionally "
            "followed by a pragma object",
            root.dump())};
    }

    if (not root[1].is_string()) {
        return unexpected{fmt::format(
            "The root has a wrong type of <root>. Expected a string, got {}",
            root[1].dump())};
    }

    if (not root[2].is_string()) {
        return unexpected{fmt::format(
            "The root has a wrong type of <module>. Expected a string, got {}",
            root[2].dump())};
    }
    if (not root[3].is_string()) {
        return unexpected{fmt::format(
            "The root has a wrong type of <name>. Expected a string, got {}",
            root[3].dump())};
    }
    if (not root[4].is_object()) {
        return unexpected{
            fmt::format("The root has a wrong type of <config>. Expected a "
                        "plain json object, got {}",
                        root[4].dump())};
    }

    auto absent = false;

    if (root.size() > ComputedRoot::kSchemeLength) {
        if (not root[ComputedRoot::kSchemeLength].is_object()) {
            return unexpected{
                fmt::format("The root has a wrong type of optional pragma "
                            "argument. Expected a "
                            "plain json object, got {}",
                            root[ComputedRoot::kSchemeLength].dump())};
        }
        if (root[ComputedRoot::kSchemeLength].contains("absent")) {
            auto absent_entry = root[ComputedRoot::kSchemeLength]["absent"];
            if (not absent_entry.is_boolean()) {
                return unexpected{
                    fmt::format("Expected pragma entry \"absent\" to be "
                                "boolean, but found {}",
                                absent_entry.dump())};
            }
            absent = absent_entry;
        }
    }

    return ComputedRoot{.repository = std::string{root[1]},
                        .target_module = std::string{root[2]},
                        .target_name = std::string{root[3]},
                        .config = root[4],
                        .absent = absent};
}

template <>
[[nodiscard]] auto ParseImpl<TreeStructureRoot>(nlohmann::json const& root)
    -> expected<TreeStructureRoot, std::string> {
    if (root.size() != TreeStructureRoot::kSchemeLength and
        root.size() != TreeStructureRoot::kSchemePragmaLength) {
        return unexpected{
            fmt::format("The root has a wrong number of arguments: {}\nThe "
                        "scheme requires [<scheme>, <root>] optionally "
                        "followed by a pragma object",
                        root.dump())};
    }

    if (not root[1].is_string()) {
        return unexpected{fmt::format(
            "The root has a wrong type of <root>. Expected a string, got {}",
            root[1].dump())};
    }

    bool absent = false;
    if (root.size() == TreeStructureRoot::kSchemePragmaLength) {
        if (not root[2].is_object()) {
            return unexpected{
                fmt::format("The root has a wrong type of optional pragma "
                            "argument. Expected a plain json object, got {}",
                            root[2].dump())};
        }
        if (root[2].contains("absent")) {
            auto const& absent_entry = root[2].at("absent");
            if (not absent_entry.is_null() and not absent_entry.is_boolean()) {
                return unexpected{
                    fmt::format("Expected pragma entry \"absent\" to be "
                                "boolean, but found {}",
                                absent_entry.dump())};
            }
            absent =
                absent_entry.is_boolean() and absent_entry.template get<bool>();
        }
    }

    return TreeStructureRoot{.repository = root[1].template get<std::string>(),
                             .absent = absent};
}
}  // namespace

auto ComputedRoot::operator==(ComputedRoot const& other) const noexcept
    -> bool {
    return repository == other.repository and
           target_module == other.target_module and
           target_name == other.target_name and config == other.config and
           absent == other.absent;
}

auto ComputedRoot::operator<(ComputedRoot const& other) const noexcept -> bool {
    if (auto const res = repository <=> other.repository; res != 0) {
        return res < 0;
    }
    if (auto const res = target_module <=> other.target_module; res != 0) {
        return res < 0;
    }
    if (auto const res = target_name <=> other.target_name; res != 0) {
        return res < 0;
    }
    if (absent != other.absent) {
        return absent;
    }
    return config < other.config;
}

auto ComputedRoot::ToString() const -> std::string {
    return fmt::format("([\"@\", {}, {}, {}{}], {})",
                       nlohmann::json(repository).dump(),
                       nlohmann::json(target_module).dump(),
                       nlohmann::json(target_name).dump(),
                       absent ? ", {\"absent\": true}" : "",
                       config.dump());
}

auto ComputedRoot::ComputeHash() const -> std::size_t {
    size_t seed{};
    hash_combine<std::string>(&seed, kMarker);
    hash_combine<std::string>(&seed, repository);
    hash_combine<std::string>(&seed, target_module);
    hash_combine<std::string>(&seed, target_name);
    hash_combine<bool>(&seed, absent);
    hash_combine<nlohmann::json>(&seed, config);
    return seed;
}

auto TreeStructureRoot::operator==(
    TreeStructureRoot const& other) const noexcept -> bool {
    return absent == other.absent and repository == other.repository;
}

auto TreeStructureRoot::operator<(TreeStructureRoot const& other) const noexcept
    -> bool {
    if (absent != other.absent) {
        return absent;
    }
    return repository < other.repository;
}

auto TreeStructureRoot::ToString() const -> std::string {
    return fmt::format("[\"tree structure\", {}{}]",
                       nlohmann::json(repository).dump(),
                       absent ? ", {\"absent\": true}" : "");
}

auto TreeStructureRoot::ComputeHash() const -> std::size_t {
    std::size_t seed{};
    hash_combine<std::string>(&seed, kMarker);
    hash_combine<std::string>(&seed, repository);
    hash_combine<bool>(&seed, absent);
    return seed;
}

auto PrecomputedRoot::Parse(nlohmann::json const& root) noexcept
    -> expected<PrecomputedRoot, std::string> {
    if ((not root.is_array()) or root.empty()) {
        return unexpected{
            fmt::format("The root is empty or has unsupported format: \"{}\"",
                        root.dump())};
    }

    if (root[0] == ComputedRoot::kMarker) {
        return ParsePrecomputed<ComputedRoot>(root);
    }
    if (root[0] == TreeStructureRoot::kMarker) {
        return ParsePrecomputed<TreeStructureRoot>(root);
    }

    return unexpected{
        fmt::format("Unknown precomputed type of the root {}", root.dump())};
}

auto PrecomputedRoot::ToString() const noexcept -> std::string {
    try {
        return std::visit([](auto const& r) { return r.ToString(); }, root_);
    } catch (...) {
        Ensures(false);
    }
}

auto PrecomputedRoot::GetReferencedRepository() const noexcept -> std::string {
    try {
        return std::visit([](auto const& r) { return r.repository; }, root_);
    } catch (...) {
        Ensures(false);
    }
}

auto PrecomputedRoot::ComputeHash(root_t const& root) -> std::size_t {
    return std::visit([](auto const& r) { return r.ComputeHash(); }, root);
}
