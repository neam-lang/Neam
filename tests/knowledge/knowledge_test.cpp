// RAG pipeline: chunking, vector store, retrieval strategies.
#include <gtest/gtest.h>
#include "neamc/vm/knowledge.hpp"
#include <cmath>
#include <numeric>

using namespace neamc::vm::knowledge;

// ===========================================================================
// VectorStore basics
// ===========================================================================

TEST(VectorStore, CreateEmpty) {
    VectorStore store(128);
    EXPECT_EQ(store.dimensions(), 128u);
    EXPECT_EQ(store.size(), 0u);
}

TEST(VectorStore, AddAndSearch) {
    VectorStore store(4);

    std::vector<float> emb1 = {1.0f, 0.0f, 0.0f, 0.0f};
    std::vector<float> emb2 = {0.0f, 1.0f, 0.0f, 0.0f};
    std::vector<float> emb3 = {0.9f, 0.1f, 0.0f, 0.0f};

    Chunk c1{"doc about cats", "cats_source", 0};
    Chunk c2{"doc about dogs", "dogs_source", 1};
    Chunk c3{"doc about cats and dogs", "both_source", 2};

    store.add(emb1, c1);
    store.add(emb2, c2);
    store.add(emb3, c3);
    EXPECT_EQ(store.size(), 3u);

    // Search for something close to emb1
    std::vector<float> query = {0.95f, 0.05f, 0.0f, 0.0f};
    auto results = store.search(query, 2);
    ASSERT_GE(results.size(), 1u);
    // First result should be most similar to query
    EXPECT_GT(results[0].score, 0.0f);
}

TEST(VectorStore, TopK) {
    VectorStore store(2);
    for (int i = 0; i < 10; ++i) {
        std::vector<float> emb = {static_cast<float>(i), 0.0f};
        Chunk c{"doc" + std::to_string(i), "source", static_cast<size_t>(i)};
        store.add(emb, c);
    }

    std::vector<float> query = {5.0f, 0.0f};
    auto results = store.search(query, 3);
    EXPECT_LE(results.size(), 3u);
}

TEST(VectorStore, EmptySearch) {
    VectorStore store(4);
    std::vector<float> query = {1.0f, 0.0f, 0.0f, 0.0f};
    auto results = store.search(query, 5);
    EXPECT_TRUE(results.empty());
}

TEST(VectorStore, MMRSearch) {
    VectorStore store(4);

    std::vector<float> emb1 = {1.0f, 0.0f, 0.0f, 0.0f};
    std::vector<float> emb2 = {0.99f, 0.01f, 0.0f, 0.0f};
    std::vector<float> emb3 = {0.0f, 1.0f, 0.0f, 0.0f};

    store.add(emb1, {"A", "source", 0});
    store.add(emb2, {"B very similar to A", "source", 1});
    store.add(emb3, {"C different", "source", 2});

    std::vector<float> query = {1.0f, 0.0f, 0.0f, 0.0f};
    auto results = store.search_mmr(query, 3, 0.5f);
    ASSERT_GE(results.size(), 1u);
    // MMR should diversify results
}

TEST(VectorStore, HybridSearch) {
    VectorStore store(4);

    std::vector<float> emb1 = {1.0f, 0.0f, 0.0f, 0.0f};
    store.add(emb1, {"text about cats", "source", 0});

    std::vector<float> emb2 = {0.0f, 1.0f, 0.0f, 0.0f};
    store.add(emb2, {"text about cats too", "source", 1});

    std::vector<float> query = {0.5f, 0.5f, 0.0f, 0.0f};
    auto results = store.search_hybrid(query, "cats", 2, 0.5f);
    ASSERT_GE(results.size(), 1u);
}

// ===========================================================================
// Embedding
// ===========================================================================

TEST(Embedding, HashDeterministic) {
    auto emb1 = embed_text("hello world", 128);
    auto emb2 = embed_text("hello world", 128);
    ASSERT_EQ(emb1.size(), 128u);
    ASSERT_EQ(emb2.size(), 128u);
    for (size_t i = 0; i < emb1.size(); ++i) {
        EXPECT_FLOAT_EQ(emb1[i], emb2[i]);
    }
}

TEST(Embedding, DifferentTextsDiffer) {
    auto emb1 = embed_text("hello", 128);
    auto emb2 = embed_text("world", 128);
    bool all_same = true;
    for (size_t i = 0; i < emb1.size(); ++i) {
        if (std::abs(emb1[i] - emb2[i]) > 1e-6f) {
            all_same = false;
            break;
        }
    }
    EXPECT_FALSE(all_same);
}

TEST(Embedding, CustomCallback) {
    auto callback = [](const std::string& /*text*/) -> std::vector<float> {
        return std::vector<float>(64, 0.5f);
    };
    auto emb = embed_text("test", 64, callback);
    ASSERT_EQ(emb.size(), 64u);
    for (float v : emb) {
        EXPECT_FLOAT_EQ(v, 0.5f);
    }
}

// ===========================================================================
// Ingester
// ===========================================================================

TEST(Ingester, ChunkText) {
    VectorStore store;
    Ingester ingester(store, 100, 20, "hash", nullptr);

    Source src;
    src.type = "text";
    src.content = "This is a test document that should be chunked into multiple pieces. "
                  "It has enough text to trigger chunking with a chunk size of 100 characters. "
                  "The overlap should create some shared content between chunks.";
    ingester.ingest(src);

    EXPECT_GT(store.size(), 0u);
}

TEST(Ingester, EmptyText) {
    VectorStore store;
    Ingester ingester(store, 100, 20, "hash", nullptr);

    Source src;
    src.type = "text";
    src.content = "";
    ingester.ingest(src);

    EXPECT_EQ(store.size(), 0u);
}

TEST(Ingester, SingleCharacter) {
    VectorStore store;
    Ingester ingester(store, 100, 20, "hash", nullptr);

    Source src;
    src.type = "text";
    src.content = "A";
    ingester.ingest(src);

    // Single char may or may not produce a chunk depending on impl
    // At least it should not crash
}

TEST(Ingester, LargeDocument) {
    VectorStore store;
    Ingester ingester(store, 500, 50, "hash", nullptr);

    Source src;
    src.type = "text";
    src.content = std::string(100000, 'x');  // 100KB
    ingester.ingest(src);

    EXPECT_GT(store.size(), 0u);
}

// ===========================================================================
// Retrieval strategies
// ===========================================================================

class RetrievalTest : public ::testing::Test {
protected:
    void SetUp() override {
        store_ = std::make_unique<VectorStore>(4);

        std::vector<float> emb1 = {1.0f, 0.0f, 0.0f, 0.0f};
        std::vector<float> emb2 = {0.0f, 1.0f, 0.0f, 0.0f};
        std::vector<float> emb3 = {0.5f, 0.5f, 0.0f, 0.0f};

        store_->add(emb1, {"cats are great", "source", 0});
        store_->add(emb2, {"dogs are loyal", "source", 1});
        store_->add(emb3, {"pets are wonderful", "source", 2});

        embed_cb_ = [](const std::string& /*text*/) -> std::vector<float> {
            return {0.8f, 0.2f, 0.0f, 0.0f};
        };
    }

    std::unique_ptr<VectorStore> store_;
    std::function<std::vector<float>(const std::string&)> embed_cb_;
    std::function<std::string(const std::string&)> llm_cb_ =
        [](const std::string& prompt) { return "mock LLM response for: " + prompt; };
};

TEST_F(RetrievalTest, BasicStrategy) {
    StrategyOptions opts;
    opts.top_k = 2;
    auto result = retrieve_with_strategy(*store_, "cats", Strategy::kBasic, opts, llm_cb_, embed_cb_);
    EXPECT_FALSE(result.documents.empty());
    EXPECT_EQ(result.strategy_used, "basic");
}

TEST_F(RetrievalTest, MMRStrategy) {
    StrategyOptions opts;
    opts.top_k = 2;
    opts.mmr_lambda = 0.5f;
    auto result = retrieve_with_strategy(*store_, "cats", Strategy::kMMR, opts, llm_cb_, embed_cb_);
    EXPECT_FALSE(result.documents.empty());
    EXPECT_EQ(result.strategy_used, "mmr");
}

TEST_F(RetrievalTest, HybridStrategy) {
    StrategyOptions opts;
    opts.top_k = 2;
    auto result = retrieve_with_strategy(*store_, "cats", Strategy::kHybrid, opts, llm_cb_, embed_cb_);
    EXPECT_FALSE(result.documents.empty());
    EXPECT_EQ(result.strategy_used, "hybrid");
}

TEST_F(RetrievalTest, HyDEStrategy) {
    StrategyOptions opts;
    opts.top_k = 2;
    opts.num_hypothetical = 1;
    auto result = retrieve_with_strategy(*store_, "cats", Strategy::kHyDE, opts, llm_cb_, embed_cb_);
    EXPECT_FALSE(result.documents.empty());
    EXPECT_EQ(result.strategy_used, "hyde");
}

TEST_F(RetrievalTest, SelfRAGStrategy) {
    StrategyOptions opts;
    opts.top_k = 2;
    auto result = retrieve_with_strategy(*store_, "cats", Strategy::kSelfRAG, opts, llm_cb_, embed_cb_);
    EXPECT_EQ(result.strategy_used, "self_rag");
}

TEST_F(RetrievalTest, CRAGStrategy) {
    StrategyOptions opts;
    opts.top_k = 2;
    auto result = retrieve_with_strategy(*store_, "cats", Strategy::kCRAG, opts, llm_cb_, embed_cb_);
    EXPECT_EQ(result.strategy_used, "crag");
}

TEST_F(RetrievalTest, AgenticStrategy) {
    StrategyOptions opts;
    opts.top_k = 2;
    auto result = retrieve_with_strategy(*store_, "cats", Strategy::kAgentic, opts, llm_cb_, embed_cb_);
    EXPECT_EQ(result.strategy_used, "agentic");
}

TEST_F(RetrievalTest, GraphRAGStrategy) {
    StrategyOptions opts;
    opts.top_k = 2;
    auto result = retrieve_with_strategy(*store_, "cats", Strategy::kGraphRAG, opts, llm_cb_, embed_cb_);
    EXPECT_EQ(result.strategy_used, "graph_rag");
}

// ===========================================================================
// Edge cases
// ===========================================================================

TEST(VectorStoreEdge, ZeroDimensions) {
    VectorStore store(0);
    EXPECT_EQ(store.dimensions(), 0u);
}

TEST(VectorStoreEdge, SetDimensions) {
    VectorStore store;
    store.set_dimensions(256);
    EXPECT_EQ(store.dimensions(), 256u);
}

TEST(RetrievalEdge, NullLLMCallback) {
    VectorStore store(4);
    std::vector<float> emb = {1.0f, 0.0f, 0.0f, 0.0f};
    store.add(emb, {"test", "source", 0});

    auto embed_cb = [](const std::string&) -> std::vector<float> {
        return {1.0f, 0.0f, 0.0f, 0.0f};
    };

    StrategyOptions opts;
    opts.top_k = 1;
    // Basic strategy should work without LLM callback
    auto result = retrieve_with_strategy(store, "test", Strategy::kBasic, opts, nullptr, embed_cb);
    EXPECT_FALSE(result.documents.empty());
}
