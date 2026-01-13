#include "neamc/stdlib/map.hpp"
#include "neamc/stdlib/set.hpp"

#include <string>

#include <gtest/gtest.h>

namespace
{
using neamc::stdlib::Map;
using neamc::stdlib::Set;

TEST(MapTest, InsertAndGet)
{
  Map<std::string, int> ages;

  ages.insert("Alice", 30);
  ages.insert("Bob", 25);

  EXPECT_TRUE(ages.get("Alice").is_some());
  EXPECT_EQ(ages.get("Alice").unwrap(), 30);
  EXPECT_TRUE(ages.get("Charlie").is_none());
}

TEST(MapTest, EntryAPI)
{
  Map<std::string, int> counts;

  counts.entry("word").or_insert(0);
  counts.entry("word").and_modify([](int& v) { v += 1; });
  counts.entry("word").and_modify([](int& v) { v += 1; });

  EXPECT_EQ(counts.get("word").unwrap(), 2);
}

TEST(MapTest, SetOperations)
{
  Set<int> a{1, 2, 3, 4, 5};
  Set<int> b{4, 5, 6, 7, 8};

  auto union_set = a.union_with(b);
  auto intersection = a.intersection(b);
  auto difference = a.difference(b);

  EXPECT_EQ(union_set.len(), 8u);
  EXPECT_EQ(intersection.len(), 2u);
  EXPECT_EQ(difference.len(), 3u);
}
}  // namespace
