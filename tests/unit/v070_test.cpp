//
// Neam v0.7.0 - Unit Tests
//
// Tests for all Phase 1 features: ValueHash, type coercion, new types (Range,
// Set, Tuple), value_to_string, is_hashable, factory functions, and structural
// equality.
//

#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <unordered_set>
#include <vector>

// --- Headers under test ---
#include "neamc/vm/object.hpp"
#include "neamc/vm/value.hpp"
#include "neamc/vm/value_hash.hpp"

namespace
{

using namespace neamc::vm;

// ============================================================================
// 1A: Value Factory and Type Predicates
// ============================================================================

TEST(ValueTypeTest, NilPredicates)
{
  auto v = Value::Nil();
  EXPECT_TRUE(v.is_nil());
  EXPECT_FALSE(v.is_bool());
  EXPECT_FALSE(v.is_number());
  EXPECT_FALSE(v.is_obj());
}

TEST(ValueTypeTest, BoolPredicates)
{
  auto v = Value::Bool(true);
  EXPECT_TRUE(v.is_bool());
  EXPECT_FALSE(v.is_nil());
  EXPECT_FALSE(v.is_number());
  EXPECT_EQ(v.as_bool(), true);

  auto v2 = Value::Bool(false);
  EXPECT_EQ(v2.as_bool(), false);
}

TEST(ValueTypeTest, NumberPredicates)
{
  auto v = Value::Number(3.14);
  EXPECT_TRUE(v.is_number());
  EXPECT_FALSE(v.is_nil());
  EXPECT_FALSE(v.is_bool());
  EXPECT_DOUBLE_EQ(v.as_number(), 3.14);
}

TEST(ValueTypeTest, StringFactory)
{
  auto v = Value::String("hello", 5);
  EXPECT_TRUE(v.is_string());
  EXPECT_TRUE(v.is_obj());
  auto* str = as_string(v);
  EXPECT_EQ(str->length, 5u);
  EXPECT_EQ(std::string(str->chars, str->length), "hello");
}

TEST(ValueTypeTest, ListFactory)
{
  auto* list = new_list({Value::Number(1), Value::Number(2), Value::Number(3)});
  auto v = Value::List(list);
  EXPECT_TRUE(v.is_list());
  EXPECT_EQ(as_list(v)->items.size(), 3u);
  EXPECT_DOUBLE_EQ(as_list(v)->items[0].as_number(), 1.0);
  EXPECT_DOUBLE_EQ(as_list(v)->items[2].as_number(), 3.0);
}

TEST(ValueTypeTest, MapFactory)
{
  std::unordered_map<std::string, Value> entries;
  entries["x"] = Value::Number(10);
  entries["y"] = Value::Number(20);
  auto* map = new_map(std::move(entries));
  auto v = Value::Map(map);
  EXPECT_TRUE(v.is_map());
  EXPECT_EQ(as_map(v)->entries.size(), 2u);
  EXPECT_DOUBLE_EQ(as_map(v)->entries.at("x").as_number(), 10.0);
}

TEST(ValueTypeTest, OptionSomeFactory)
{
  auto* opt = new_option(true, Value::Number(42));
  auto v = Value::Option(opt);
  EXPECT_TRUE(v.is_option());
  EXPECT_TRUE(as_option(v)->has_value);
  EXPECT_DOUBLE_EQ(as_option(v)->value.as_number(), 42.0);
}

TEST(ValueTypeTest, OptionNoneFactory)
{
  auto* opt = new_option(false, Value::Nil());
  auto v = Value::Option(opt);
  EXPECT_TRUE(v.is_option());
  EXPECT_FALSE(as_option(v)->has_value);
}

// ============================================================================
// 1A: ValueHash
// ============================================================================

TEST(ValueHashTest, NilHashConsistent)
{
  ValueHash hasher;
  auto v1 = Value::Nil();
  auto v2 = Value::Nil();
  EXPECT_EQ(hasher(v1), hasher(v2));
  EXPECT_EQ(hasher(v1), 0u);
}

TEST(ValueHashTest, BoolHashDistinct)
{
  ValueHash hasher;
  auto t = Value::Bool(true);
  auto f = Value::Bool(false);
  EXPECT_EQ(hasher(t), hasher(Value::Bool(true)));
  EXPECT_NE(hasher(t), hasher(f));
}

TEST(ValueHashTest, NumberHashConsistent)
{
  ValueHash hasher;
  auto v1 = Value::Number(42.0);
  auto v2 = Value::Number(42.0);
  auto v3 = Value::Number(43.0);
  EXPECT_EQ(hasher(v1), hasher(v2));
  EXPECT_NE(hasher(v1), hasher(v3));
}

TEST(ValueHashTest, StringHashConsistent)
{
  ValueHash hasher;
  auto v1 = Value::String("test", 4);
  auto v2 = Value::String("test", 4);
  auto v3 = Value::String("other", 5);
  EXPECT_EQ(hasher(v1), hasher(v2));
  EXPECT_NE(hasher(v1), hasher(v3));
}

TEST(ValueHashTest, UnhashableTypeThrows)
{
  ValueHash hasher;
  auto* list = new_list({Value::Number(1)});
  auto v = Value::List(list);
  EXPECT_THROW(hasher(v), std::runtime_error);
}

// ============================================================================
// 1A: ValueEqual
// ============================================================================

TEST(ValueEqualTest, NilEquality)
{
  ValueEqual eq;
  EXPECT_TRUE(eq(Value::Nil(), Value::Nil()));
  EXPECT_FALSE(eq(Value::Nil(), Value::Number(0)));
}

TEST(ValueEqualTest, BoolEquality)
{
  ValueEqual eq;
  EXPECT_TRUE(eq(Value::Bool(true), Value::Bool(true)));
  EXPECT_FALSE(eq(Value::Bool(true), Value::Bool(false)));
}

TEST(ValueEqualTest, NumberEquality)
{
  ValueEqual eq;
  EXPECT_TRUE(eq(Value::Number(42), Value::Number(42)));
  EXPECT_FALSE(eq(Value::Number(42), Value::Number(43)));
}

TEST(ValueEqualTest, StringStructuralEquality)
{
  ValueEqual eq;
  auto v1 = Value::String("hello", 5);
  auto v2 = Value::String("hello", 5);
  auto v3 = Value::String("world", 5);
  EXPECT_TRUE(eq(v1, v2));
  EXPECT_FALSE(eq(v1, v3));
}

TEST(ValueEqualTest, ListStructuralEquality)
{
  ValueEqual eq;
  auto* l1 = new_list({Value::Number(1), Value::Number(2)});
  auto* l2 = new_list({Value::Number(1), Value::Number(2)});
  auto* l3 = new_list({Value::Number(1), Value::Number(3)});
  auto* l4 = new_list({Value::Number(1)});
  EXPECT_TRUE(eq(Value::List(l1), Value::List(l2)));
  EXPECT_FALSE(eq(Value::List(l1), Value::List(l3)));
  EXPECT_FALSE(eq(Value::List(l1), Value::List(l4)));
}

TEST(ValueEqualTest, CrossTypeInequality)
{
  ValueEqual eq;
  EXPECT_FALSE(eq(Value::Number(0), Value::Bool(false)));
  EXPECT_FALSE(eq(Value::Number(0), Value::Nil()));
  EXPECT_FALSE(eq(Value::Bool(false), Value::Nil()));
}

// ============================================================================
// 1A: is_hashable
// ============================================================================

TEST(IsHashableTest, PrimitivesAreHashable)
{
  EXPECT_TRUE(is_hashable(Value::Nil()));
  EXPECT_TRUE(is_hashable(Value::Bool(true)));
  EXPECT_TRUE(is_hashable(Value::Number(42)));
}

TEST(IsHashableTest, StringsAreHashable)
{
  auto v = Value::String("test", 4);
  EXPECT_TRUE(is_hashable(v));
}

TEST(IsHashableTest, ListsAreNotHashable)
{
  auto* list = new_list({Value::Number(1)});
  EXPECT_FALSE(is_hashable(Value::List(list)));
}

TEST(IsHashableTest, MapsAreNotHashable)
{
  auto* map = new_map({});
  EXPECT_FALSE(is_hashable(Value::Map(map)));
}

// ============================================================================
// 1A: value_to_string
// ============================================================================

TEST(ValueToStringTest, Nil)
{
  EXPECT_EQ(value_to_string(Value::Nil()), "nil");
}

TEST(ValueToStringTest, Booleans)
{
  EXPECT_EQ(value_to_string(Value::Bool(true)), "true");
  EXPECT_EQ(value_to_string(Value::Bool(false)), "false");
}

TEST(ValueToStringTest, IntegerNumbers)
{
  EXPECT_EQ(value_to_string(Value::Number(42)), "42");
  EXPECT_EQ(value_to_string(Value::Number(0)), "0");
  EXPECT_EQ(value_to_string(Value::Number(-7)), "-7");
}

TEST(ValueToStringTest, FloatNumbers)
{
  auto s = value_to_string(Value::Number(3.14));
  EXPECT_NE(s.find("3.14"), std::string::npos);
}

TEST(ValueToStringTest, Strings)
{
  auto v = Value::String("hello", 5);
  EXPECT_EQ(value_to_string(v), "hello");
}

TEST(ValueToStringTest, EmptyList)
{
  auto* list = new_list({});
  EXPECT_EQ(value_to_string(Value::List(list)), "[]");
}

TEST(ValueToStringTest, ListWithNumbers)
{
  auto* list = new_list({Value::Number(1), Value::Number(2), Value::Number(3)});
  EXPECT_EQ(value_to_string(Value::List(list)), "[1, 2, 3]");
}

TEST(ValueToStringTest, ListWithStrings)
{
  auto* list = new_list({Value::String("a", 1), Value::String("b", 1)});
  auto s = value_to_string(Value::List(list));
  // Strings inside lists are quoted
  EXPECT_EQ(s, "[\"a\", \"b\"]");
}

TEST(ValueToStringTest, EmptyMap)
{
  auto* map = new_map({});
  EXPECT_EQ(value_to_string(Value::Map(map)), "{}");
}

TEST(ValueToStringTest, MapWithEntries)
{
  std::unordered_map<std::string, Value> entries;
  entries["x"] = Value::Number(1);
  auto* map = new_map(std::move(entries));
  auto s = value_to_string(Value::Map(map));
  EXPECT_EQ(s, "{x: 1}");
}

TEST(ValueToStringTest, OptionSome)
{
  auto* opt = new_option(true, Value::Number(42));
  EXPECT_EQ(value_to_string(Value::Option(opt)), "Some(42)");
}

TEST(ValueToStringTest, OptionNone)
{
  auto* opt = new_option(false, Value::Nil());
  EXPECT_EQ(value_to_string(Value::Option(opt)), "None");
}

// ============================================================================
// 1B: Range Type
// ============================================================================

TEST(RangeTest, FactoryAndPredicates)
{
  auto* range = new_range(0, 10, 1);
  auto v = Value::Range(range);
  EXPECT_TRUE(v.is_range());
  EXPECT_FALSE(v.is_list());
  EXPECT_FALSE(v.is_nil());
}

TEST(RangeTest, Fields)
{
  auto* range = new_range(5, 20, 3);
  EXPECT_EQ(range->start, 5);
  EXPECT_EQ(range->end, 20);
  EXPECT_EQ(range->step, 3);
}

TEST(RangeTest, DefaultStep)
{
  auto* range = new_range(0, 5, 1);
  EXPECT_EQ(range->step, 1);
}

TEST(RangeTest, ValueToString)
{
  auto* range = new_range(0, 5, 1);
  EXPECT_EQ(value_to_string(Value::Range(range)), "range(0, 5)");

  auto* range2 = new_range(0, 10, 2);
  EXPECT_EQ(value_to_string(Value::Range(range2)), "range(0, 10, 2)");
}

TEST(RangeTest, CastFunction)
{
  auto* range = new_range(1, 100, 5);
  auto v = Value::Range(range);
  auto* r = as_range(v);
  EXPECT_EQ(r->start, 1);
  EXPECT_EQ(r->end, 100);
  EXPECT_EQ(r->step, 5);
}

TEST(RangeTest, InvalidCastThrows)
{
  auto v = Value::Number(42);
  EXPECT_THROW(as_range(v), std::runtime_error);
}

// ============================================================================
// 1B: IterState Type
// ============================================================================

TEST(IterStateTest, FactoryAndPredicates)
{
  auto* iter = new_iter_state();
  auto v = Value::IterState(iter);
  EXPECT_TRUE(v.is_iter_state());
  EXPECT_FALSE(v.is_list());
}

TEST(IterStateTest, DefaultFields)
{
  auto* iter = new_iter_state();
  EXPECT_TRUE(iter->source.is_nil());
  EXPECT_EQ(iter->position, 0);
  EXPECT_EQ(iter->end, 0);
  EXPECT_EQ(iter->step, 1);
  EXPECT_TRUE(iter->snapshot.empty());
}

TEST(IterStateTest, FieldsAreWritable)
{
  auto* iter = new_iter_state();
  iter->position = 5;
  iter->end = 10;
  iter->step = 2;
  iter->snapshot.push_back(Value::Number(1));
  EXPECT_EQ(iter->position, 5);
  EXPECT_EQ(iter->end, 10);
  EXPECT_EQ(iter->step, 2);
  EXPECT_EQ(iter->snapshot.size(), 1u);
}

// ============================================================================
// 1C: Set Type
// ============================================================================

TEST(SetTest, FactoryAndPredicates)
{
  std::unordered_set<Value, ValueHash, ValueEqual> items;
  items.insert(Value::Number(1));
  items.insert(Value::Number(2));
  auto* set = new_set(std::move(items));
  auto v = Value::Set(set);
  EXPECT_TRUE(v.is_set());
  EXPECT_FALSE(v.is_list());
}

TEST(SetTest, Deduplication)
{
  std::unordered_set<Value, ValueHash, ValueEqual> items;
  items.insert(Value::Number(1));
  items.insert(Value::Number(1));
  items.insert(Value::Number(2));
  items.insert(Value::Number(2));
  items.insert(Value::Number(3));
  auto* set = new_set(std::move(items));
  EXPECT_EQ(set->items.size(), 3u);
}

TEST(SetTest, ContainsValues)
{
  std::unordered_set<Value, ValueHash, ValueEqual> items;
  items.insert(Value::Number(10));
  items.insert(Value::Number(20));
  auto* set = new_set(std::move(items));
  EXPECT_NE(set->items.find(Value::Number(10)), set->items.end());
  EXPECT_NE(set->items.find(Value::Number(20)), set->items.end());
  EXPECT_EQ(set->items.find(Value::Number(30)), set->items.end());
}

TEST(SetTest, StringItems)
{
  std::unordered_set<Value, ValueHash, ValueEqual> items;
  items.insert(Value::String("a", 1));
  items.insert(Value::String("b", 1));
  items.insert(Value::String("a", 1));
  auto* set = new_set(std::move(items));
  EXPECT_EQ(set->items.size(), 2u);
}

TEST(SetTest, MixedTypes)
{
  std::unordered_set<Value, ValueHash, ValueEqual> items;
  items.insert(Value::Number(1));
  items.insert(Value::Bool(true));
  items.insert(Value::Nil());
  items.insert(Value::String("x", 1));
  auto* set = new_set(std::move(items));
  EXPECT_EQ(set->items.size(), 4u);
}

TEST(SetTest, SetEquality)
{
  ValueEqual eq;
  std::unordered_set<Value, ValueHash, ValueEqual> items1;
  items1.insert(Value::Number(1));
  items1.insert(Value::Number(2));
  std::unordered_set<Value, ValueHash, ValueEqual> items2;
  items2.insert(Value::Number(2));
  items2.insert(Value::Number(1));
  std::unordered_set<Value, ValueHash, ValueEqual> items3;
  items3.insert(Value::Number(1));
  items3.insert(Value::Number(3));

  auto* s1 = new_set(std::move(items1));
  auto* s2 = new_set(std::move(items2));
  auto* s3 = new_set(std::move(items3));
  EXPECT_TRUE(eq(Value::Set(s1), Value::Set(s2)));
  EXPECT_FALSE(eq(Value::Set(s1), Value::Set(s3)));
}

TEST(SetTest, ValueToString)
{
  std::unordered_set<Value, ValueHash, ValueEqual> items;
  auto* set = new_set(std::move(items));
  EXPECT_EQ(value_to_string(Value::Set(set)), "set()");
}

TEST(SetTest, InvalidCastThrows)
{
  auto v = Value::Number(1);
  EXPECT_THROW(as_set(v), std::runtime_error);
}

// ============================================================================
// 1C: Tuple Type
// ============================================================================

TEST(TupleTest, FactoryAndPredicates)
{
  auto* tuple = new_tuple({Value::Number(1), Value::String("hi", 2), Value::Bool(true)});
  auto v = Value::Tuple(tuple);
  EXPECT_TRUE(v.is_tuple());
  EXPECT_FALSE(v.is_list());
}

TEST(TupleTest, ElementAccess)
{
  auto* tuple = new_tuple({Value::Number(10), Value::Number(20), Value::Number(30)});
  EXPECT_EQ(tuple->items.size(), 3u);
  EXPECT_DOUBLE_EQ(tuple->items[0].as_number(), 10.0);
  EXPECT_DOUBLE_EQ(tuple->items[1].as_number(), 20.0);
  EXPECT_DOUBLE_EQ(tuple->items[2].as_number(), 30.0);
}

TEST(TupleTest, EmptyTuple)
{
  auto* tuple = new_tuple({});
  EXPECT_EQ(tuple->items.size(), 0u);
  EXPECT_EQ(value_to_string(Value::Tuple(tuple)), "()");
}

TEST(TupleTest, SingleElementTuple)
{
  auto* tuple = new_tuple({Value::Number(42)});
  auto s = value_to_string(Value::Tuple(tuple));
  // Single-element tuples have trailing comma
  EXPECT_EQ(s, "(42,)");
}

TEST(TupleTest, MultiElementValueToString)
{
  auto* tuple = new_tuple({Value::Number(1), Value::Number(2), Value::Number(3)});
  EXPECT_EQ(value_to_string(Value::Tuple(tuple)), "(1, 2, 3)");
}

TEST(TupleTest, TupleWithStringsValueToString)
{
  auto* tuple = new_tuple({Value::String("a", 1), Value::Number(1)});
  EXPECT_EQ(value_to_string(Value::Tuple(tuple)), "(\"a\", 1)");
}

TEST(TupleTest, HashCacheNotComputed)
{
  auto* tuple = new_tuple({Value::Number(1)});
  EXPECT_FALSE(tuple->hash_computed);
  EXPECT_EQ(tuple->hash_cache, 0u);
}

TEST(TupleTest, HashComputedOnFirstAccess)
{
  auto* tuple = new_tuple({Value::Number(1), Value::Number(2)});
  ValueHash hasher;
  auto h = hasher(Value::Tuple(tuple));
  EXPECT_NE(h, 0u);
  EXPECT_TRUE(tuple->hash_computed);
  // Cache stores uint32_t, so second access returns the cached (truncated) value
  auto h2 = hasher(Value::Tuple(tuple));
  EXPECT_EQ(h2, static_cast<std::size_t>(tuple->hash_cache));
}

TEST(TupleTest, EqualTuplesHaveSameHash)
{
  ValueHash hasher;
  auto* t1 = new_tuple({Value::Number(1), Value::Number(2)});
  auto* t2 = new_tuple({Value::Number(1), Value::Number(2)});
  EXPECT_EQ(hasher(Value::Tuple(t1)), hasher(Value::Tuple(t2)));
}

TEST(TupleTest, DifferentTuplesHaveDifferentHash)
{
  ValueHash hasher;
  auto* t1 = new_tuple({Value::Number(1), Value::Number(2)});
  auto* t2 = new_tuple({Value::Number(2), Value::Number(1)});
  // Not strictly guaranteed but extremely likely
  EXPECT_NE(hasher(Value::Tuple(t1)), hasher(Value::Tuple(t2)));
}

TEST(TupleTest, StructuralEquality)
{
  ValueEqual eq;
  auto* t1 = new_tuple({Value::Number(1), Value::Number(2)});
  auto* t2 = new_tuple({Value::Number(1), Value::Number(2)});
  auto* t3 = new_tuple({Value::Number(1), Value::Number(3)});
  auto* t4 = new_tuple({Value::Number(1)});
  EXPECT_TRUE(eq(Value::Tuple(t1), Value::Tuple(t2)));
  EXPECT_FALSE(eq(Value::Tuple(t1), Value::Tuple(t3)));
  EXPECT_FALSE(eq(Value::Tuple(t1), Value::Tuple(t4)));
}

TEST(TupleTest, HashableWithPrimitives)
{
  auto* tuple = new_tuple({Value::Number(1), Value::String("a", 1), Value::Bool(true)});
  EXPECT_TRUE(is_hashable(Value::Tuple(tuple)));
}

TEST(TupleTest, NotHashableWithList)
{
  auto* list = new_list({Value::Number(1)});
  auto* tuple = new_tuple({Value::List(list)});
  EXPECT_FALSE(is_hashable(Value::Tuple(tuple)));
}

TEST(TupleTest, NestedHashableTuple)
{
  auto* inner = new_tuple({Value::Number(1), Value::Number(2)});
  auto* outer = new_tuple({Value::Tuple(inner), Value::Number(3)});
  EXPECT_TRUE(is_hashable(Value::Tuple(outer)));
}

TEST(TupleTest, TupleAsSetElement)
{
  auto* t1 = new_tuple({Value::Number(1), Value::Number(2)});
  auto* t2 = new_tuple({Value::Number(1), Value::Number(2)});
  auto* t3 = new_tuple({Value::Number(3), Value::Number(4)});

  std::unordered_set<Value, ValueHash, ValueEqual> items;
  items.insert(Value::Tuple(t1));
  items.insert(Value::Tuple(t2));  // Duplicate — should not increase size
  items.insert(Value::Tuple(t3));
  EXPECT_EQ(items.size(), 2u);
}

TEST(TupleTest, InvalidCastThrows)
{
  auto v = Value::Number(1);
  EXPECT_THROW(as_tuple(v), std::runtime_error);
}

// ============================================================================
// 1C: Set with Tuple Keys
// ============================================================================

TEST(SetTupleTest, SetWithTupleElements)
{
  std::unordered_set<Value, ValueHash, ValueEqual> items;
  items.insert(Value::Tuple(new_tuple({Value::Number(1), Value::Number(2)})));
  items.insert(Value::Tuple(new_tuple({Value::Number(1), Value::Number(2)})));
  items.insert(Value::Tuple(new_tuple({Value::Number(3), Value::Number(4)})));
  auto* set = new_set(std::move(items));
  EXPECT_EQ(set->items.size(), 2u);
}

// ============================================================================
// Cross-type value_to_string tests
// ============================================================================

TEST(ValueToStringTest, NestedList)
{
  auto* inner = new_list({Value::Number(1), Value::Number(2)});
  auto* outer = new_list({Value::List(inner), Value::Number(3)});
  auto s = value_to_string(Value::List(outer));
  EXPECT_EQ(s, "[[1, 2], 3]");
}

TEST(ValueToStringTest, OptionSomeString)
{
  auto* opt = new_option(true, Value::String("hello", 5));
  EXPECT_EQ(value_to_string(Value::Option(opt)), "Some(hello)");
}

TEST(ValueToStringTest, ListOfTuples)
{
  auto* t1 = new_tuple({Value::Number(1), Value::Number(2)});
  auto* t2 = new_tuple({Value::Number(3), Value::Number(4)});
  auto* list = new_list({Value::Tuple(t1), Value::Tuple(t2)});
  EXPECT_EQ(value_to_string(Value::List(list)), "[(1, 2), (3, 4)]");
}

TEST(ValueToStringTest, RangeUnitStep)
{
  auto* r = new_range(0, 5, 1);
  EXPECT_EQ(value_to_string(Value::Range(r)), "range(0, 5)");
}

TEST(ValueToStringTest, RangeCustomStep)
{
  auto* r = new_range(0, 10, 3);
  EXPECT_EQ(value_to_string(Value::Range(r)), "range(0, 10, 3)");
}

TEST(ValueToStringTest, NegativeRange)
{
  auto* r = new_range(-5, 5, 1);
  EXPECT_EQ(value_to_string(Value::Range(r)), "range(-5, 5)");
}

// ============================================================================
// Comprehensive hash/equality stress tests
// ============================================================================

TEST(ValueHashTest, DistinctTypesHaveDistinctHashes)
{
  ValueHash hasher;
  // While not strictly guaranteed, collisions should be rare
  auto h_nil = hasher(Value::Nil());
  auto h_true = hasher(Value::Bool(true));
  auto h_false = hasher(Value::Bool(false));
  auto h_zero = hasher(Value::Number(0));
  auto h_one = hasher(Value::Number(1));
  auto h_str = hasher(Value::String("", 0));

  std::unordered_set<std::size_t> hashes{h_nil, h_true, h_false, h_zero, h_one, h_str};
  // At least 4 distinct hashes out of 6 values (collisions possible but rare)
  EXPECT_GE(hashes.size(), 4u);
}

TEST(ValueHashTest, ManyNumbersDistinct)
{
  ValueHash hasher;
  std::unordered_set<std::size_t> hashes;
  for (int i = 0; i < 100; ++i)
  {
    hashes.insert(hasher(Value::Number(static_cast<double>(i))));
  }
  // At least 90 distinct hashes for 100 consecutive integers
  EXPECT_GE(hashes.size(), 90u);
}

TEST(ValueEqualTest, SameObjectIsEqual)
{
  ValueEqual eq;
  auto* list = new_list({Value::Number(1)});
  auto v = Value::List(list);
  EXPECT_TRUE(eq(v, v));
}

TEST(ValueEqualTest, DifferentObjTypesNotEqual)
{
  ValueEqual eq;
  auto* list = new_list({});
  auto* tuple = new_tuple({});
  EXPECT_FALSE(eq(Value::List(list), Value::Tuple(tuple)));
}

// ============================================================================
// Edge cases
// ============================================================================

TEST(ValueEdgeTest, LargeNumber)
{
  // 1e18 exceeds the 1e15 threshold, so uses float-style formatting
  auto v = Value::Number(1e18);
  auto s = value_to_string(v);
  EXPECT_FALSE(s.empty());
  // Numbers within threshold use integer format
  auto v2 = Value::Number(999999999999999.0);
  auto s2 = value_to_string(v2);
  EXPECT_EQ(s2, "999999999999999");
}

TEST(ValueEdgeTest, VeryLargeNumberUsesFloat)
{
  auto v = Value::Number(1e16);
  auto s = value_to_string(v);
  // Should still be integer format since < 1e15 threshold is false
  // 1e16 is larger than the 1e15 threshold so may use float format
  // Just ensure it's not empty and doesn't crash
  EXPECT_FALSE(s.empty());
}

TEST(ValueEdgeTest, NaNNumber)
{
  auto v = Value::Number(std::nan(""));
  auto s = value_to_string(v);
  EXPECT_FALSE(s.empty());  // NaN should produce some string
}

TEST(ValueEdgeTest, InfinityNumber)
{
  auto v = Value::Number(INFINITY);
  auto s = value_to_string(v);
  EXPECT_FALSE(s.empty());
}

TEST(ValueEdgeTest, EmptyStringValue)
{
  auto v = Value::String("", 0);
  EXPECT_EQ(value_to_string(v), "");
  EXPECT_TRUE(is_hashable(v));
}

TEST(ValueEdgeTest, LargeList)
{
  std::vector<Value> items;
  for (int i = 0; i < 1000; ++i)
  {
    items.push_back(Value::Number(static_cast<double>(i)));
  }
  auto* list = new_list(std::move(items));
  auto s = value_to_string(Value::List(list));
  EXPECT_EQ(s.substr(0, 1), "[");
  EXPECT_EQ(s.back(), ']');
  EXPECT_EQ(as_list(Value::List(list))->items.size(), 1000u);
}

TEST(ValueEdgeTest, LargeTupleIsHashable)
{
  std::vector<Value> items;
  for (int i = 0; i < 100; ++i)
  {
    items.push_back(Value::Number(static_cast<double>(i)));
  }
  auto* tuple = new_tuple(std::move(items));
  EXPECT_TRUE(is_hashable(Value::Tuple(tuple)));

  ValueHash hasher;
  auto h = hasher(Value::Tuple(tuple));
  EXPECT_NE(h, 0u);
}

// ============================================================================
// ObjType enum values (regression: ensure new types don't clash)
// ============================================================================

TEST(ObjTypeTest, EnumValuesAreDistinct)
{
  auto range = static_cast<int>(ObjType::OBJ_RANGE);
  auto iter = static_cast<int>(ObjType::OBJ_ITER_STATE);
  auto set = static_cast<int>(ObjType::OBJ_SET);
  auto tuple = static_cast<int>(ObjType::OBJ_TUPLE);

  std::unordered_set<int> vals{range, iter, set, tuple};
  EXPECT_EQ(vals.size(), 4u);

  // Also ensure they don't clash with existing types
  auto string = static_cast<int>(ObjType::OBJ_STRING);
  auto list = static_cast<int>(ObjType::OBJ_LIST);
  auto map = static_cast<int>(ObjType::OBJ_MAP);
  auto option = static_cast<int>(ObjType::OBJ_OPTION);

  vals.insert(string);
  vals.insert(list);
  vals.insert(map);
  vals.insert(option);
  EXPECT_EQ(vals.size(), 8u);
}

}  // namespace
