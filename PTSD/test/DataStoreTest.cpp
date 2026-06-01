#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "Util/DataStore.hpp"

using Util::DataStore;

namespace {
// Writes `contents` to `path`, overwriting any existing file.
void WriteFile(const std::string &path, const std::string &contents) {
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    ASSERT_TRUE(out.is_open()) << "could not open temp file: " << path;
    out << contents;
    out.close();
}

// Removes a file, ignoring failures (e.g. when it was never created).
void RemoveFile(const std::string &path) {
    std::remove(path.c_str());
}
} // namespace

// NOLINTBEGIN(readability-magic-numbers)

TEST(DataStoreTest, LoadParsesFields) {
    const std::string path = "datastore_test_load.json";
    WriteFile(path, R"({"name":"pistol","atk":7})");

    DataStore store;
    const nlohmann::json &doc = store.Load(path);

    EXPECT_EQ(doc.at("name").get<std::string>(), "pistol");
    EXPECT_EQ(doc.at("atk").get<int>(), 7);

    RemoveFile(path);
}

TEST(DataStoreTest, HasReflectsLoadState) {
    const std::string path = "datastore_test_has.json";
    WriteFile(path, R"({"value":1})");

    DataStore store;
    EXPECT_FALSE(store.Has(path));

    store.Load(path);
    EXPECT_TRUE(store.Has(path));

    RemoveFile(path);
}

TEST(DataStoreTest, RemoveDropsEntry) {
    const std::string path = "datastore_test_remove.json";
    WriteFile(path, R"({"value":2})");

    DataStore store;
    store.Load(path);
    EXPECT_TRUE(store.Has(path));

    store.Remove(path);
    EXPECT_FALSE(store.Has(path));

    // Removing an absent key must be a no-op.
    store.Remove(path);
    EXPECT_FALSE(store.Has(path));

    RemoveFile(path);
}

TEST(DataStoreTest, ClearEmptiesStore) {
    const std::string pathA = "datastore_test_clear_a.json";
    const std::string pathB = "datastore_test_clear_b.json";
    WriteFile(pathA, R"({"value":1})");
    WriteFile(pathB, R"({"value":2})");

    DataStore store;
    store.Load(pathA);
    store.Load(pathB);
    EXPECT_EQ(store.Size(), 2U);

    store.Clear();
    EXPECT_EQ(store.Size(), 0U);
    EXPECT_FALSE(store.Has(pathA));
    EXPECT_FALSE(store.Has(pathB));

    RemoveFile(pathA);
    RemoveFile(pathB);
}

TEST(DataStoreTest, SizeTracksEntries) {
    const std::string pathA = "datastore_test_size_a.json";
    const std::string pathB = "datastore_test_size_b.json";
    WriteFile(pathA, R"({"value":1})");
    WriteFile(pathB, R"({"value":2})");

    DataStore store;
    EXPECT_EQ(store.Size(), 0U);

    store.Load(pathA);
    EXPECT_EQ(store.Size(), 1U);

    // Re-loading the same path must not grow the cache.
    store.Load(pathA);
    EXPECT_EQ(store.Size(), 1U);

    store.Load(pathB);
    EXPECT_EQ(store.Size(), 2U);

    store.Remove(pathA);
    EXPECT_EQ(store.Size(), 1U);

    RemoveFile(pathA);
    RemoveFile(pathB);
}

TEST(DataStoreTest, SecondLoadReturnsCachedValue) {
    const std::string path = "datastore_test_cache.json";
    WriteFile(path, R"({"atk":7})");

    DataStore store;
    const nlohmann::json &first = store.Load(path);
    EXPECT_EQ(first.at("atk").get<int>(), 7);

    // Mutate the file on disk after the first load.
    WriteFile(path, R"({"atk":99})");

    // A second load with the same path must return the cached document,
    // proving the file was not re-read.
    const nlohmann::json &second = store.Load(path);
    EXPECT_EQ(second.at("atk").get<int>(), 7);

    // After removal, a fresh load reflects the new on-disk state.
    store.Remove(path);
    const nlohmann::json &reloaded = store.Load(path);
    EXPECT_EQ(reloaded.at("atk").get<int>(), 99);

    RemoveFile(path);
}

TEST(DataStoreTest, CachedAfterFileDeleted) {
    const std::string path = "datastore_test_deleted.json";
    WriteFile(path, R"({"atk":7})");

    DataStore store;
    EXPECT_EQ(store.Load(path).at("atk").get<int>(), 7);

    // Delete the file after caching; the cached value must survive.
    RemoveFile(path);
    EXPECT_EQ(store.Load(path).at("atk").get<int>(), 7);

    // After removal the file is gone, so a fresh load yields an empty object.
    store.Remove(path);
    const nlohmann::json &reloaded = store.Load(path);
    EXPECT_TRUE(reloaded.is_object());
    EXPECT_TRUE(reloaded.empty());
}

TEST(DataStoreTest, MissingFileReturnsEmptyObject) {
    DataStore store;

    const nlohmann::json &doc = store.Load("does_not_exist_xyz.json");

    EXPECT_TRUE(doc.is_object());
    EXPECT_TRUE(doc.empty());
    // The missing path is still cached so the lookup is not retried on disk.
    EXPECT_TRUE(store.Has("does_not_exist_xyz.json"));
}

TEST(DataStoreTest, MalformedFileReturnsEmptyObject) {
    const std::string path = "datastore_test_malformed.json";
    WriteFile(path, R"({"atk":)"); // truncated / invalid JSON

    DataStore store;
    const nlohmann::json &doc = store.Load(path);

    EXPECT_TRUE(doc.is_object());
    EXPECT_TRUE(doc.empty());

    RemoveFile(path);
}

// NOLINTEND(readability-magic-numbers)
