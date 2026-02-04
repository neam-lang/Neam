#include "neamc/stdlib/list.hpp"

#include <gtest/gtest.h>

namespace
{
using neamc::stdlib::List;

TEST(ListTest, BasicOperations)
{
  List<int> list{1, 2, 3, 4, 5};

  EXPECT_EQ(list.len(), 5u);
  EXPECT_FALSE(list.is_empty());
  EXPECT_EQ(list[0], 1);
  EXPECT_EQ(list.first().unwrap(), 1);
  EXPECT_EQ(list.last().unwrap(), 5);
}

TEST(ListTest, PushPop)
{
  List<int> list;

  list.push(1);
  list.push(2);
  list.push(3);

  EXPECT_EQ(list.pop().unwrap(), 3);
  EXPECT_EQ(list.pop().unwrap(), 2);
  EXPECT_EQ(list.len(), 1u);
}

TEST(ListTest, FunctionalMap)
{
  List<int> numbers{1, 2, 3, 4, 5};

  auto doubled = numbers.map([](int x) { return x * 2; });

  EXPECT_EQ(doubled.len(), 5u);
  EXPECT_EQ(doubled[0], 2);
  EXPECT_EQ(doubled[4], 10);
}

TEST(ListTest, FunctionalFilter)
{
  List<int> numbers{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

  auto evens = numbers.filter([](int x) { return x % 2 == 0; });

  EXPECT_EQ(evens.len(), 5u);
  EXPECT_EQ(evens[0], 2);
  EXPECT_EQ(evens[4], 10);
}

TEST(ListTest, FunctionalFold)
{
  List<int> numbers{1, 2, 3, 4, 5};

  int sum = numbers.fold(0, [](int acc, int x) { return acc + x; });

  EXPECT_EQ(sum, 15);
}
}  // namespace
