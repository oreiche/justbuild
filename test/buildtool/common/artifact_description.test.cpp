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

#include "src/buildtool/common/artifact_description.hpp"

#include <filesystem>
#include <string>

#include "catch2/catch_test_macros.hpp"
#include "nlohmann/json.hpp"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/file_system/object_type.hpp"

[[nodiscard]] auto operator==(Artifact const& lhs,
                              Artifact const& rhs) -> bool {
    return lhs.Id() == rhs.Id() and lhs.FilePath() == rhs.FilePath() and
           lhs.Info() == rhs.Info();
}

TEST_CASE("Local artifact", "[artifact_description]") {
    auto local_desc = ArtifactDescription::CreateLocal(
        std::filesystem::path{"local_path"}, "repo");
    auto from_json = ArtifactDescription::FromJson(local_desc.ToJson());
    CHECK(local_desc == *from_json);
}

TEST_CASE("Known artifact", "[artifact_description]") {
    SECTION("File object") {
        auto known_desc = ArtifactDescription::CreateKnown(
            ArtifactDigest{std::string{"f_fake_hash"}, 0, /*is_tree=*/false},
            ObjectType::File);
        auto from_json = ArtifactDescription::FromJson(known_desc.ToJson());
        CHECK(known_desc == *from_json);
    }
    SECTION("Executable object") {
        auto known_desc = ArtifactDescription::CreateKnown(
            ArtifactDigest{std::string{"x_fake_hash"}, 1, /*is_tree=*/false},
            ObjectType::Executable);
        auto from_json = ArtifactDescription::FromJson(known_desc.ToJson());
        CHECK(known_desc == *from_json);
    }
    SECTION("Symlink object") {
        auto known_desc = ArtifactDescription::CreateKnown(
            ArtifactDigest{std::string{"l_fake_hash"}, 2, /*is_tree=*/false},
            ObjectType::Symlink);
        auto from_json = ArtifactDescription::FromJson(known_desc.ToJson());
        CHECK(known_desc == *from_json);
    }
}

TEST_CASE("Action artifact", "[artifact_description]") {
    auto action_desc = ArtifactDescription::CreateAction(
        "action_id", std::filesystem::path{"out_path"});
    auto from_json = ArtifactDescription::FromJson(action_desc.ToJson());
    CHECK(action_desc == *from_json);
}

TEST_CASE("From JSON", "[artifact_description]") {
    auto local = ArtifactDescription::CreateLocal("local", "repo").ToJson();
    auto known =
        ArtifactDescription::CreateKnown(
            ArtifactDigest{"hash", 0, /*is_tree=*/false}, ObjectType::File)
            .ToJson();
    auto action = ArtifactDescription::CreateAction("id", "output").ToJson();

    SECTION("Parse artifacts") {
        CHECK(ArtifactDescription::FromJson(local));
        CHECK(ArtifactDescription::FromJson(known));
        CHECK(ArtifactDescription::FromJson(action));
    }

    SECTION("Parse artifact without mandatory type") {
        local.erase("type");
        known.erase("type");
        action.erase("type");
        CHECK_FALSE(ArtifactDescription::FromJson(local));
        CHECK_FALSE(ArtifactDescription::FromJson(known));
        CHECK_FALSE(ArtifactDescription::FromJson(action));
    }

    SECTION("Parse artifact without mandatory data") {
        local.erase("data");
        known.erase("data");
        action.erase("data");
        CHECK_FALSE(ArtifactDescription::FromJson(local));
        CHECK_FALSE(ArtifactDescription::FromJson(known));
        CHECK_FALSE(ArtifactDescription::FromJson(action));
    }

    SECTION("Parse local artifact without mandatory path") {
        local["data"]["path"] = 0;
        CHECK_FALSE(ArtifactDescription::FromJson(local));

        local["data"].erase("path");
        CHECK_FALSE(ArtifactDescription::FromJson(local));
    }

    SECTION("Parse known artifact") {
        SECTION("without mandatory id") {
            known["data"]["id"] = 0;
            CHECK_FALSE(ArtifactDescription::FromJson(known));

            known["data"].erase("id");
            CHECK_FALSE(ArtifactDescription::FromJson(known));
        }
        SECTION("without mandatory size") {
            known["data"]["size"] = "0";
            CHECK_FALSE(ArtifactDescription::FromJson(known));

            known["data"].erase("size");
            CHECK_FALSE(ArtifactDescription::FromJson(known));
        }
        SECTION("without mandatory file_type") {
            known["data"]["file_type"] = "more_than_one_char";
            CHECK_FALSE(ArtifactDescription::FromJson(known));

            known["data"].erase("file_type");
            CHECK_FALSE(ArtifactDescription::FromJson(known));
        }
    }

    SECTION("Parse action artifact") {
        SECTION("without mandatory id") {
            action["data"]["id"] = 0;
            CHECK_FALSE(ArtifactDescription::FromJson(action));

            action["data"].erase("id");
            CHECK_FALSE(ArtifactDescription::FromJson(action));
        }
        SECTION("without mandatory path") {
            action["data"]["path"] = 0;
            CHECK_FALSE(ArtifactDescription::FromJson(action));

            action["data"].erase("path");
            CHECK_FALSE(ArtifactDescription::FromJson(action));
        }
    }
}

TEST_CASE("Description missing mandatory key/value pair",
          "[artifact_description]") {
    nlohmann::json const missing_type = {{"data", {{"path", "some/path"}}}};
    CHECK(not ArtifactDescription::FromJson(missing_type));
    nlohmann::json const missing_data = {{"type", "LOCAL"}};
    CHECK(not ArtifactDescription::FromJson(missing_data));
}

TEST_CASE("Local artifact description contains incorrect value for \"data\"",
          "[artifact_description]") {
    nlohmann::json const local_art_missing_path = {
        {"type", "LOCAL"}, {"data", nlohmann::json::object()}};
    CHECK(not ArtifactDescription::FromJson(local_art_missing_path));
}

TEST_CASE("Known artifact description contains incorrect value for \"data\"",
          "[artifact_description]") {
    std::string file_type{};
    file_type += ToChar(ObjectType::File);
    SECTION("missing \"id\"") {
        nlohmann::json const known_art_missing_id = {
            {"type", "KNOWN"},
            {"data", {{"size", 15}, {"file_type", file_type}}}};
        CHECK(not ArtifactDescription::FromJson(known_art_missing_id));
    }
    SECTION("missing \"size\"") {
        nlohmann::json const known_art_missing_size = {
            {"type", "KNOWN"},
            {"data", {{"id", "known_input"}, {"file_type", file_type}}}};
        CHECK(not ArtifactDescription::FromJson(known_art_missing_size));
    }
    SECTION("missing \"file_type\"") {
        nlohmann::json const known_art_missing_file_type = {
            {"type", "KNOWN"}, {"data", {{"id", "known_input"}, {"size", 15}}}};

        CHECK(not ArtifactDescription::FromJson(known_art_missing_file_type));
    }
}

TEST_CASE("Action artifact description contains incorrect value for \"data\"",
          "[artifact_description]") {
    nlohmann::json const action_art_missing_id = {
        {"type", "ACTION"}, {"data", {{"path", "output/path"}}}};
    CHECK(not ArtifactDescription::FromJson(action_art_missing_id));

    nlohmann::json const action_art_missing_path = {
        {"type", "ACTION"}, {"data", {{"id", "action_id"}}}};
    CHECK(not ArtifactDescription::FromJson(action_art_missing_path));
}
