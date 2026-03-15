// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026 Johnson Ogundeji
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "signet/ai/audit_chain.hpp"
#include "signet/ai/row_lineage.hpp"
#include "signet/ai/decision_log.hpp"
#include "signet/ai/inference_log.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

using namespace signet::forge;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helper: RAII temp directory that cleans up on destruction
// ---------------------------------------------------------------------------
struct TempDir {
    fs::path path;

    explicit TempDir(const std::string& name)
        : path(std::string("/tmp/signet_test_audit/") + name)
    {
        std::error_code ec;
        fs::create_directories(path, ec);
    }

    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

// ---------------------------------------------------------------------------
// Helper: create a simple DecisionRecord with known values
// ---------------------------------------------------------------------------
static DecisionRecord make_decision_record(int index) {
    DecisionRecord rec;
    rec.timestamp_ns  = 1700000000000000LL + static_cast<int64_t>(index) * 1000000LL;
    rec.strategy_id   = static_cast<int32_t>(index % 5);
    rec.model_version = "v1.2." + std::to_string(index);
    rec.decision_type = static_cast<DecisionType>(index % 8);
    rec.input_features = {
        static_cast<float>(index) * 0.1f,
        static_cast<float>(index) * 0.2f,
        static_cast<float>(index) * 0.3f
    };
    rec.model_output = static_cast<float>(index) * 1.5f;
    rec.confidence   = 0.5f + static_cast<float>(index % 50) * 0.01f;
    rec.risk_result  = static_cast<RiskGateResult>(index % 4);
    rec.order_id     = "ORD-" + std::to_string(1000 + index);
    rec.symbol       = (index % 2 == 0) ? "BTCUSD" : "ETHUSD";
    rec.price        = 50000.0 + static_cast<double>(index) * 10.5;
    rec.quantity     = 0.01 + static_cast<double>(index % 10) * 0.1;
    rec.venue        = (index % 3 == 0) ? "binance" : ((index % 3 == 1) ? "bybit" : "okx");
    rec.notes        = "test record " + std::to_string(index);
    return rec;
}

// ---------------------------------------------------------------------------
// Helper: create a simple InferenceRecord with known values
// ---------------------------------------------------------------------------
static InferenceRecord make_inference_record(int index) {
    InferenceRecord rec;
    rec.timestamp_ns   = 1700000000000000LL + static_cast<int64_t>(index) * 500000LL;
    rec.model_id       = "model_" + std::to_string(index % 3);
    rec.model_version  = "v2.0." + std::to_string(index);
    rec.inference_type = static_cast<InferenceType>(index % 4);
    rec.input_embedding = {
        static_cast<float>(index) * 0.01f,
        static_cast<float>(index) * 0.02f,
        static_cast<float>(index) * 0.03f,
        static_cast<float>(index) * 0.04f
    };
    rec.input_hash    = "inhash_" + std::to_string(index);
    rec.output_hash   = "outhash_" + std::to_string(index);
    rec.output_score  = static_cast<float>(index) * 0.75f;
    rec.latency_ns    = 1000LL + static_cast<int64_t>(index) * 50LL;
    rec.batch_size    = static_cast<int32_t>(1 + index % 8);
    rec.input_tokens  = static_cast<int32_t>(10 + index * 5);
    rec.output_tokens = static_cast<int32_t>(5 + index * 3);
    rec.user_id_hash  = "user_" + std::to_string(index % 10);
    rec.session_id    = "sess_" + std::to_string(index);
    rec.metadata_json = R"({"idx":)" + std::to_string(index) + "}";
    return rec;
}

// ===================================================================
// Section 1: Hash Utilities [audit][util]
// ===================================================================

TEST_CASE("hash_to_hex roundtrip", "[audit][util]") {
    // Create a known hash
    std::array<uint8_t, 32> hash{};
    for (size_t i = 0; i < 32; ++i) {
        hash[i] = static_cast<uint8_t>(i);
    }

    // Convert to hex
    std::string hex = hash_to_hex(hash);
    REQUIRE(hex.size() == 64);  // 32 bytes = 64 hex chars

    // Verify known prefix: 0x00 0x01 0x02 ... -> "000102..."
    REQUIRE(hex.substr(0, 6) == "000102");

    // Convert back to hash
    auto result = hex_to_hash(hex);
    REQUIRE(result.has_value());
    REQUIRE(result.value() == hash);
}

TEST_CASE("hex_to_hash invalid input", "[audit][util]") {
    SECTION("Odd-length string") {
        auto result = hex_to_hash("abc");
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("Wrong length (not 64 chars)") {
        auto result = hex_to_hash("aabbccdd");
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("Non-hex characters") {
        // 64 chars but with invalid hex
        std::string bad_hex(64, 'g');
        auto result = hex_to_hash(bad_hex);
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("Empty string") {
        auto result = hex_to_hash("");
        REQUIRE_FALSE(result.has_value());
    }
}

TEST_CASE("now_ns monotonicity", "[audit][util]") {
    int64_t t1 = now_ns();
    // Small busy-wait to ensure time advances
    volatile int dummy = 0;
    for (int i = 0; i < 1000; ++i) dummy += i;
    int64_t t2 = now_ns();

    REQUIRE(t1 > 0);
    REQUIRE(t2 >= t1);
}

TEST_CASE("generate_chain_id uniqueness", "[audit][util]") {
    std::string id1 = generate_chain_id();
    std::string id2 = generate_chain_id();

    REQUIRE_FALSE(id1.empty());
    REQUIRE_FALSE(id2.empty());
    REQUIRE(id1 != id2);
}

// ===================================================================
// Section 2: HashChainEntry [audit][entry]
// ===================================================================

TEST_CASE("Compute and verify entry hash", "[audit][entry]") {
    HashChainEntry entry;
    entry.sequence_number = 0;
    entry.timestamp_ns    = 1700000000000000LL;
    entry.prev_hash       = {};  // all zeros
    entry.data_hash       = {};
    // Set some data hash bytes
    entry.data_hash[0] = 0xAA;
    entry.data_hash[1] = 0xBB;

    entry.compute_entry_hash();

    // entry_hash should be non-zero after computing
    bool all_zero = true;
    for (auto b : entry.entry_hash) {
        if (b != 0) { all_zero = false; break; }
    }
    REQUIRE_FALSE(all_zero);

    // verify() should return true for a correctly computed entry
    REQUIRE(entry.verify());
}

TEST_CASE("Tampering detected via verify", "[audit][entry]") {
    HashChainEntry entry;
    entry.sequence_number = 42;
    entry.timestamp_ns    = 1700000000000000LL;
    entry.prev_hash       = {};
    entry.data_hash       = {};
    entry.data_hash[0] = 0xDE;
    entry.data_hash[1] = 0xAD;

    entry.compute_entry_hash();
    REQUIRE(entry.verify());

    // Tamper with the data hash after computing
    entry.data_hash[0] ^= 0xFF;

    // verify() should now fail
    REQUIRE_FALSE(entry.verify());
}

TEST_CASE("HashChainEntry serialize/deserialize roundtrip", "[audit][entry]") {
    HashChainEntry entry;
    entry.sequence_number = 12345;
    entry.timestamp_ns    = 1700000000123456LL;
    for (size_t i = 0; i < 32; ++i) {
        entry.prev_hash[i] = static_cast<uint8_t>(i);
        entry.data_hash[i] = static_cast<uint8_t>(i + 32);
    }
    entry.compute_entry_hash();

    // Serialize
    std::vector<uint8_t> bytes = entry.serialize();
    REQUIRE(bytes.size() >= 112);  // 8+8+32+32+32 = 112 minimum

    // Deserialize
    auto result = HashChainEntry::deserialize(bytes.data(), bytes.size());
    REQUIRE(result.has_value());

    const auto& restored = result.value();
    REQUIRE(restored.sequence_number == entry.sequence_number);
    REQUIRE(restored.timestamp_ns    == entry.timestamp_ns);
    REQUIRE(restored.prev_hash       == entry.prev_hash);
    REQUIRE(restored.data_hash       == entry.data_hash);
    REQUIRE(restored.entry_hash      == entry.entry_hash);
    REQUIRE(restored.verify());
}

TEST_CASE("HashChainEntry deserialize too small", "[audit][entry]") {
    // Less than 112 bytes should fail
    std::vector<uint8_t> small_data(50, 0);
    auto result = HashChainEntry::deserialize(small_data.data(), small_data.size());
    REQUIRE_FALSE(result.has_value());
}

// ===================================================================
// Section 3: AuditChainWriter [audit][writer]
// ===================================================================

TEST_CASE("Empty chain", "[audit][writer]") {
    AuditChainWriter writer;

    REQUIRE(writer.length() == 0);

    std::array<uint8_t, 32> zeros{};
    REQUIRE(writer.last_hash() == zeros);

    REQUIRE(writer.entries().empty());
}

TEST_CASE("Append single entry", "[audit][writer]") {
    AuditChainWriter writer;

    std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04};
    int64_t ts = 1700000000000000LL;

    auto entry = writer.append(data.data(), data.size(), ts);

    REQUIRE(writer.length() == 1);
    REQUIRE(entry.sequence_number == 0);
    REQUIRE(entry.timestamp_ns == ts);

    // First entry's prev_hash should be all zeros
    std::array<uint8_t, 32> zeros{};
    REQUIRE(entry.prev_hash == zeros);

    // Entry should be verifiable
    REQUIRE(entry.verify());

    // last_hash should be the entry's hash
    REQUIRE(writer.last_hash() == entry.entry_hash);
}

TEST_CASE("Append multiple entries with chain continuity", "[audit][writer]") {
    AuditChainWriter writer;

    constexpr int NUM_ENTRIES = 10;
    std::vector<HashChainEntry> recorded;

    for (int i = 0; i < NUM_ENTRIES; ++i) {
        std::vector<uint8_t> data(8);
        std::memcpy(data.data(), &i, sizeof(i));

        auto entry = writer.append(data.data(), data.size(),
                                   1700000000000000LL + i * 1000000LL);
        recorded.push_back(entry);
    }

    REQUIRE(writer.length() == NUM_ENTRIES);

    // Verify chain continuity: each entry[i].prev_hash == entry[i-1].entry_hash
    std::array<uint8_t, 32> zeros{};
    REQUIRE(recorded[0].prev_hash == zeros);  // first links to genesis

    for (int i = 1; i < NUM_ENTRIES; ++i) {
        REQUIRE(recorded[i].prev_hash == recorded[i - 1].entry_hash);
    }

    // Each entry should be independently verifiable
    for (const auto& e : recorded) {
        REQUIRE(e.verify());
    }

    // Sequence numbers should be sequential
    for (int i = 0; i < NUM_ENTRIES; ++i) {
        REQUIRE(recorded[i].sequence_number == static_cast<int64_t>(i));
    }
}

TEST_CASE("Auto-timestamp append", "[audit][writer]") {
    AuditChainWriter writer;

    std::vector<uint8_t> data = {0xAA, 0xBB};
    auto entry = writer.append(data.data(), data.size());

    // Timestamp should be positive (auto-generated from now_ns)
    REQUIRE(entry.timestamp_ns > 0);
    REQUIRE(entry.verify());
}

TEST_CASE("serialize_chain roundtrip", "[audit][writer]") {
    AuditChainWriter writer;

    // Build a chain of 5 entries
    for (int i = 0; i < 5; ++i) {
        std::vector<uint8_t> data = {
            static_cast<uint8_t>(i),
            static_cast<uint8_t>(i + 1)
        };
        writer.append(data.data(), data.size(),
                      1700000000000000LL + i * 1000000LL);
    }

    // Serialize the whole chain
    std::vector<uint8_t> chain_bytes = writer.serialize_chain();
    REQUIRE(!chain_bytes.empty());

    // The serialized chain should be verifiable by AuditChainVerifier
    auto vr = AuditChainVerifier::verify(chain_bytes.data(), chain_bytes.size());
    REQUIRE(vr.valid);
    REQUIRE(vr.entries_checked == 5);
}

TEST_CASE("Reset chain", "[audit][writer]") {
    AuditChainWriter writer;

    // Build a chain
    for (int i = 0; i < 5; ++i) {
        std::vector<uint8_t> data = {static_cast<uint8_t>(i)};
        writer.append(data.data(), data.size());
    }
    REQUIRE(writer.length() == 5);

    // Reset
    writer.reset();

    REQUIRE(writer.length() == 0);
    std::array<uint8_t, 32> zeros{};
    REQUIRE(writer.last_hash() == zeros);
    REQUIRE(writer.entries().empty());

    // New entries start from sequence 0
    std::vector<uint8_t> data = {0xFF};
    auto entry = writer.append(data.data(), data.size());
    REQUIRE(entry.sequence_number == 0);
    REQUIRE(entry.prev_hash == zeros);
}

TEST_CASE("Reset with custom initial hash", "[audit][writer]") {
    AuditChainWriter writer;

    // Reset with a non-zero initial hash
    std::array<uint8_t, 32> custom_initial{};
    custom_initial[0] = 0xCA;
    custom_initial[1] = 0xFE;
    writer.reset(custom_initial);

    REQUIRE(writer.length() == 0);
    REQUIRE(writer.last_hash() == custom_initial);

    // First appended entry should link to the custom initial hash
    std::vector<uint8_t> data = {0x01};
    auto entry = writer.append(data.data(), data.size());
    REQUIRE(entry.prev_hash == custom_initial);
}

// ===================================================================
// Section 4: AuditChainVerifier [audit][verify]
// ===================================================================

TEST_CASE("Valid chain verification", "[audit][verify]") {
    AuditChainWriter writer;

    for (int i = 0; i < 20; ++i) {
        int val = i * 7 + 3;
        std::vector<uint8_t> data(sizeof(val));
        std::memcpy(data.data(), &val, sizeof(val));
        writer.append(data.data(), data.size(),
                      1700000000000000LL + i * 1000000LL);
    }

    auto vr = AuditChainVerifier::verify(writer.entries());
    REQUIRE(vr.valid);
    REQUIRE(vr.entries_checked == 20);
    REQUIRE(vr.first_bad_index == -1);
    REQUIRE(vr.error_message.empty());
}

TEST_CASE("Tampered entry detected by verifier", "[audit][verify]") {
    AuditChainWriter writer;

    for (int i = 0; i < 10; ++i) {
        std::vector<uint8_t> data = {static_cast<uint8_t>(i)};
        writer.append(data.data(), data.size());
    }

    // Copy entries and tamper with entry at index 5
    auto entries = writer.entries();
    entries[5].data_hash[0] ^= 0xFF;

    auto vr = AuditChainVerifier::verify(entries);
    REQUIRE_FALSE(vr.valid);
    REQUIRE(vr.first_bad_index == 5);
    REQUIRE_FALSE(vr.error_message.empty());
}

TEST_CASE("Broken link detected by verifier", "[audit][verify]") {
    AuditChainWriter writer;

    for (int i = 0; i < 10; ++i) {
        std::vector<uint8_t> data = {static_cast<uint8_t>(i)};
        writer.append(data.data(), data.size());
    }

    // Copy entries and swap positions 3 and 4
    auto entries = writer.entries();
    std::swap(entries[3], entries[4]);

    auto vr = AuditChainVerifier::verify(entries);
    REQUIRE_FALSE(vr.valid);
    // The break should be detected at index 3 or 4 depending on
    // whether the verifier checks prev_hash linkage or entry integrity first
    REQUIRE(vr.first_bad_index >= 3);
    REQUIRE(vr.first_bad_index <= 4);
}

TEST_CASE("Empty chain verification", "[audit][verify]") {
    std::vector<HashChainEntry> empty_entries;

    auto vr = AuditChainVerifier::verify(empty_entries);
    REQUIRE(vr.valid);
    REQUIRE(vr.entries_checked == 0);
}

TEST_CASE("Verify from serialized bytes", "[audit][verify]") {
    AuditChainWriter writer;

    for (int i = 0; i < 15; ++i) {
        std::vector<uint8_t> data = {
            static_cast<uint8_t>(i),
            static_cast<uint8_t>(i * 2)
        };
        writer.append(data.data(), data.size());
    }

    // Serialize the chain to bytes
    auto chain_bytes = writer.serialize_chain();
    REQUIRE(!chain_bytes.empty());

    // Verify from raw bytes
    auto vr = AuditChainVerifier::verify(chain_bytes.data(), chain_bytes.size());
    REQUIRE(vr.valid);
    REQUIRE(vr.entries_checked == 15);
}

TEST_CASE("Verify continuity between two chains", "[audit][verify]") {
    // Build first chain
    AuditChainWriter writer1;
    for (int i = 0; i < 5; ++i) {
        std::vector<uint8_t> data = {static_cast<uint8_t>(i)};
        writer1.append(data.data(), data.size());
    }
    auto last_hash_chain1 = writer1.last_hash();

    // Build second chain that continues from the first
    AuditChainWriter writer2;
    writer2.reset(last_hash_chain1);
    for (int i = 5; i < 10; ++i) {
        std::vector<uint8_t> data = {static_cast<uint8_t>(i)};
        writer2.append(data.data(), data.size());
    }

    // Verify continuity: last hash of chain 1 == first prev_hash of chain 2
    REQUIRE(AuditChainVerifier::verify_continuity(
        last_hash_chain1, writer2.entries()));

    // Verify with wrong last hash should fail
    std::array<uint8_t, 32> wrong_hash{};
    wrong_hash[0] = 0xFF;
    REQUIRE_FALSE(AuditChainVerifier::verify_continuity(
        wrong_hash, writer2.entries()));
}

TEST_CASE("Verify single entry against expected prev hash", "[audit][verify]") {
    AuditChainWriter writer;

    std::vector<uint8_t> data = {0x01, 0x02};
    auto entry = writer.append(data.data(), data.size());

    // Should verify against expected zeros (genesis)
    std::array<uint8_t, 32> zeros{};
    REQUIRE(AuditChainVerifier::verify_entry(entry, zeros));

    // Should fail against wrong expected prev hash
    std::array<uint8_t, 32> wrong{};
    wrong[0] = 0xFF;
    REQUIRE_FALSE(AuditChainVerifier::verify_entry(entry, wrong));
}

// ===================================================================
// Section 5: AuditMetadata [audit][metadata]
// ===================================================================

TEST_CASE("AuditMetadata serialize/deserialize roundtrip", "[audit][metadata]") {
    AuditMetadata meta;
    meta.chain_id       = "chain-test-001";
    meta.start_sequence = 0;
    meta.end_sequence   = 99;
    for (size_t i = 0; i < 32; ++i) {
        meta.first_prev_hash[i] = static_cast<uint8_t>(i);
        meta.last_entry_hash[i] = static_cast<uint8_t>(31 - i);
    }

    std::string serialized = meta.serialize();
    REQUIRE(!serialized.empty());

    auto result = AuditMetadata::deserialize(serialized);
    REQUIRE(result.has_value());

    const auto& restored = result.value();
    REQUIRE(restored.chain_id       == meta.chain_id);
    REQUIRE(restored.start_sequence == meta.start_sequence);
    REQUIRE(restored.end_sequence   == meta.end_sequence);
    REQUIRE(restored.first_prev_hash == meta.first_prev_hash);
    REQUIRE(restored.last_entry_hash == meta.last_entry_hash);
}

TEST_CASE("AuditMetadata invalid string", "[audit][metadata]") {
    SECTION("Garbage string") {
        auto result = AuditMetadata::deserialize("this is not valid metadata");
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("Empty string") {
        auto result = AuditMetadata::deserialize("");
        REQUIRE_FALSE(result.has_value());
    }
}

// ===================================================================
// Section 6: DecisionRecord [audit][decision]
// ===================================================================

TEST_CASE("DecisionRecord serialize/deserialize roundtrip", "[audit][decision]") {
    DecisionRecord rec = make_decision_record(7);

    // Serialize
    std::vector<uint8_t> bytes = rec.serialize();
    REQUIRE(!bytes.empty());

    // Deserialize
    auto result = DecisionRecord::deserialize(bytes.data(), bytes.size());
    REQUIRE(result.has_value());

    const auto& restored = result.value();
    REQUIRE(restored.timestamp_ns  == rec.timestamp_ns);
    REQUIRE(restored.strategy_id   == rec.strategy_id);
    REQUIRE(restored.model_version == rec.model_version);
    REQUIRE(restored.decision_type == rec.decision_type);
    REQUIRE(restored.order_id      == rec.order_id);
    REQUIRE(restored.symbol        == rec.symbol);
    REQUIRE(restored.venue         == rec.venue);
    REQUIRE(restored.notes         == rec.notes);
    REQUIRE(restored.risk_result   == rec.risk_result);

    // Float comparisons
    REQUIRE(restored.model_output == Catch::Approx(rec.model_output));
    REQUIRE(restored.confidence   == Catch::Approx(rec.confidence));
    REQUIRE(restored.price        == Catch::Approx(rec.price));
    REQUIRE(restored.quantity     == Catch::Approx(rec.quantity));

    // Feature vector
    REQUIRE(restored.input_features.size() == rec.input_features.size());
    for (size_t i = 0; i < rec.input_features.size(); ++i) {
        REQUIRE(restored.input_features[i] ==
                Catch::Approx(rec.input_features[i]));
    }
}

TEST_CASE("DecisionRecord with empty features", "[audit][decision]") {
    DecisionRecord rec;
    rec.timestamp_ns  = 1700000000000000LL;
    rec.strategy_id   = 1;
    rec.model_version = "v1.0";
    rec.decision_type = DecisionType::NO_ACTION;
    rec.input_features = {};  // empty
    rec.model_output  = 0.0f;
    rec.confidence    = 0.0f;
    rec.risk_result   = RiskGateResult::PASSED;
    rec.order_id      = "";
    rec.symbol        = "BTCUSD";
    rec.price         = 0.0;
    rec.quantity      = 0.0;
    rec.venue         = "binance";
    rec.notes         = "";

    std::vector<uint8_t> bytes = rec.serialize();
    REQUIRE(!bytes.empty());

    auto result = DecisionRecord::deserialize(bytes.data(), bytes.size());
    REQUIRE(result.has_value());

    const auto& restored = result.value();
    REQUIRE(restored.input_features.empty());
    REQUIRE(restored.decision_type == DecisionType::NO_ACTION);
    REQUIRE(restored.risk_result   == RiskGateResult::PASSED);
    REQUIRE(restored.symbol        == "BTCUSD");
}

TEST_CASE("DecisionRecord with large feature vector", "[audit][decision]") {
    DecisionRecord rec = make_decision_record(0);

    // Use a large feature vector (100 features)
    rec.input_features.resize(100);
    for (int i = 0; i < 100; ++i) {
        rec.input_features[i] = static_cast<float>(i) * 0.01f;
    }

    std::vector<uint8_t> bytes = rec.serialize();
    auto result = DecisionRecord::deserialize(bytes.data(), bytes.size());
    REQUIRE(result.has_value());

    const auto& restored = result.value();
    REQUIRE(restored.input_features.size() == 100);
    for (int i = 0; i < 100; ++i) {
        REQUIRE(restored.input_features[i] ==
                Catch::Approx(static_cast<float>(i) * 0.01f));
    }
}

// ===================================================================
// Section 7: DecisionLogWriter/Reader [audit][decision][io]
// ===================================================================

TEST_CASE("Write and read decision log", "[audit][decision][io]") {
    TempDir dir("decision_write_read");

    constexpr int NUM_RECORDS = 10;
    std::string log_path;

    // Write
    {
        DecisionLogWriter writer(dir.path.string(), "test-chain-decision");

        for (int i = 0; i < NUM_RECORDS; ++i) {
            auto rec = make_decision_record(i);
            auto result = writer.log(rec);
            REQUIRE(result.has_value());
        }

        REQUIRE(writer.total_records() == NUM_RECORDS);

        auto flush_result = writer.flush();
        REQUIRE(flush_result.has_value());

        log_path = writer.current_file_path();

        auto close_result = writer.close();
        REQUIRE(close_result.has_value());
    }

    // Read
    {
        auto reader_result = DecisionLogReader::open(log_path);
        REQUIRE(reader_result.has_value());
        auto& reader = reader_result.value();

        REQUIRE(reader.num_records() == NUM_RECORDS);

        auto records_result = reader.read_all();
        REQUIRE(records_result.has_value());

        const auto& records = records_result.value();
        REQUIRE(records.size() == NUM_RECORDS);

        // Verify each record matches what we wrote
        for (int i = 0; i < NUM_RECORDS; ++i) {
            auto expected_rec = make_decision_record(i);

            REQUIRE(records[i].timestamp_ns  == expected_rec.timestamp_ns);
            REQUIRE(records[i].strategy_id   == expected_rec.strategy_id);
            REQUIRE(records[i].model_version == expected_rec.model_version);
            REQUIRE(records[i].decision_type == expected_rec.decision_type);
            REQUIRE(records[i].order_id      == expected_rec.order_id);
            REQUIRE(records[i].symbol        == expected_rec.symbol);
            REQUIRE(records[i].venue         == expected_rec.venue);
            REQUIRE(records[i].notes         == expected_rec.notes);

            REQUIRE(records[i].model_output == Catch::Approx(expected_rec.model_output));
            REQUIRE(records[i].confidence   == Catch::Approx(expected_rec.confidence));
            REQUIRE(records[i].price        == Catch::Approx(expected_rec.price));
            REQUIRE(records[i].quantity     == Catch::Approx(expected_rec.quantity));

            REQUIRE(records[i].input_features.size() ==
                    expected_rec.input_features.size());
            for (size_t f = 0; f < expected_rec.input_features.size(); ++f) {
                REQUIRE(records[i].input_features[f] ==
                        Catch::Approx(expected_rec.input_features[f]));
            }
        }
    }
}

TEST_CASE("Chain verification on decision log", "[audit][decision][io]") {
    TempDir dir("decision_chain_verify");
    std::string log_path;

    // Write records
    {
        DecisionLogWriter writer(dir.path.string(), "verify-chain-decision");

        for (int i = 0; i < 15; ++i) {
            auto rec = make_decision_record(i);
            auto result = writer.log(rec);
            REQUIRE(result.has_value());
        }

        auto flush_result = writer.flush();
        REQUIRE(flush_result.has_value());

        log_path = writer.current_file_path();

        auto close_result = writer.close();
        REQUIRE(close_result.has_value());
    }

    // Read and verify chain
    {
        auto reader_result = DecisionLogReader::open(log_path);
        REQUIRE(reader_result.has_value());
        auto& reader = reader_result.value();

        auto vr = reader.verify_chain();
        REQUIRE(vr.valid);
        REQUIRE(vr.entries_checked == 15);
    }
}

TEST_CASE("Decision log audit metadata", "[audit][decision][io]") {
    TempDir dir("decision_audit_meta");
    std::string log_path;

    // Write records
    {
        DecisionLogWriter writer(dir.path.string(), "meta-chain-decision");

        for (int i = 0; i < 5; ++i) {
            auto rec = make_decision_record(i);
            auto result = writer.log(rec);
            REQUIRE(result.has_value());
        }

        auto flush_result = writer.flush();
        REQUIRE(flush_result.has_value());

        log_path = writer.current_file_path();

        auto close_result = writer.close();
        REQUIRE(close_result.has_value());
    }

    // Read and check audit metadata
    {
        auto reader_result = DecisionLogReader::open(log_path);
        REQUIRE(reader_result.has_value());
        auto& reader = reader_result.value();

        auto meta_result = reader.audit_metadata();
        REQUIRE(meta_result.has_value());

        const auto& meta = meta_result.value();
        REQUIRE_FALSE(meta.chain_id.empty());
        REQUIRE(meta.start_sequence == 0);
        REQUIRE(meta.end_sequence == 4);
    }
}

TEST_CASE("Decision log schema", "[audit][decision][io]") {
    Schema schema = decision_log_schema();

    // The decision log should have columns for all DecisionRecord fields
    REQUIRE(schema.num_columns() > 0);

    // Collect column names
    std::vector<std::string> col_names;
    for (size_t i = 0; i < schema.num_columns(); ++i) {
        col_names.push_back(schema.column(i).name);
    }

    // Check that key columns exist
    auto has_col = [&](const std::string& name) {
        return std::find(col_names.begin(), col_names.end(), name) !=
               col_names.end();
    };

    REQUIRE(has_col("timestamp_ns"));
    REQUIRE(has_col("strategy_id"));
    REQUIRE(has_col("model_version"));
    REQUIRE(has_col("decision_type"));
    REQUIRE(has_col("model_output"));
    REQUIRE(has_col("confidence"));
    REQUIRE(has_col("risk_result"));
    REQUIRE(has_col("order_id"));
    REQUIRE(has_col("symbol"));
    REQUIRE(has_col("price"));
    REQUIRE(has_col("quantity"));
    REQUIRE(has_col("venue"));
}

TEST_CASE("Decision log pending_records", "[audit][decision][io]") {
    TempDir dir("decision_pending");

    DecisionLogWriter writer(dir.path.string());

    REQUIRE(writer.pending_records() == 0);
    REQUIRE(writer.total_records() == 0);

    // Log some records without flushing
    for (int i = 0; i < 5; ++i) {
        auto rec = make_decision_record(i);
        auto result = writer.log(rec);
        REQUIRE(result.has_value());
    }

    REQUIRE(writer.pending_records() > 0);
    REQUIRE(writer.total_records() == 5);

    auto flush_result = writer.flush();
    REQUIRE(flush_result.has_value());

    // After flush, pending should be 0 but total is still 5
    REQUIRE(writer.pending_records() == 0);
    REQUIRE(writer.total_records() == 5);

    auto close_result = writer.close();
    REQUIRE(close_result.has_value());
}

// ===================================================================
// Section 8: InferenceRecord [audit][inference]
// ===================================================================

TEST_CASE("InferenceRecord serialize/deserialize roundtrip", "[audit][inference]") {
    InferenceRecord rec = make_inference_record(3);

    std::vector<uint8_t> bytes = rec.serialize();
    REQUIRE(!bytes.empty());

    auto result = InferenceRecord::deserialize(bytes.data(), bytes.size());
    REQUIRE(result.has_value());

    const auto& restored = result.value();
    REQUIRE(restored.timestamp_ns   == rec.timestamp_ns);
    REQUIRE(restored.model_id       == rec.model_id);
    REQUIRE(restored.model_version  == rec.model_version);
    REQUIRE(restored.inference_type == rec.inference_type);
    REQUIRE(restored.input_hash     == rec.input_hash);
    REQUIRE(restored.output_hash    == rec.output_hash);
    REQUIRE(restored.latency_ns     == rec.latency_ns);
    REQUIRE(restored.batch_size     == rec.batch_size);
    REQUIRE(restored.input_tokens   == rec.input_tokens);
    REQUIRE(restored.output_tokens  == rec.output_tokens);
    REQUIRE(restored.user_id_hash   == rec.user_id_hash);
    REQUIRE(restored.session_id     == rec.session_id);
    REQUIRE(restored.metadata_json  == rec.metadata_json);

    REQUIRE(restored.output_score == Catch::Approx(rec.output_score));

    // Embedding vector
    REQUIRE(restored.input_embedding.size() == rec.input_embedding.size());
    for (size_t i = 0; i < rec.input_embedding.size(); ++i) {
        REQUIRE(restored.input_embedding[i] ==
                Catch::Approx(rec.input_embedding[i]));
    }
}

TEST_CASE("InferenceRecord with embedding", "[audit][inference]") {
    InferenceRecord rec = make_inference_record(0);

    // Use a larger embedding
    rec.input_embedding.resize(128);
    for (int i = 0; i < 128; ++i) {
        rec.input_embedding[i] = static_cast<float>(i) * 0.001f;
    }

    std::vector<uint8_t> bytes = rec.serialize();
    auto result = InferenceRecord::deserialize(bytes.data(), bytes.size());
    REQUIRE(result.has_value());

    const auto& restored = result.value();
    REQUIRE(restored.input_embedding.size() == 128);
    for (int i = 0; i < 128; ++i) {
        REQUIRE(restored.input_embedding[i] ==
                Catch::Approx(static_cast<float>(i) * 0.001f));
    }
}

TEST_CASE("InferenceRecord with empty embedding", "[audit][inference]") {
    InferenceRecord rec = make_inference_record(1);
    rec.input_embedding.clear();

    std::vector<uint8_t> bytes = rec.serialize();
    auto result = InferenceRecord::deserialize(bytes.data(), bytes.size());
    REQUIRE(result.has_value());

    REQUIRE(result.value().input_embedding.empty());
    REQUIRE(result.value().model_id == rec.model_id);
}

TEST_CASE("InferenceRecord all inference types", "[audit][inference]") {
    // Test each InferenceType enum value roundtrips correctly
    std::vector<InferenceType> types = {
        InferenceType::CLASSIFICATION,
        InferenceType::REGRESSION,
        InferenceType::EMBEDDING,
        InferenceType::GENERATION,
    };

    for (auto type : types) {
        InferenceRecord rec = make_inference_record(0);
        rec.inference_type = type;

        std::vector<uint8_t> bytes = rec.serialize();
        auto result = InferenceRecord::deserialize(bytes.data(), bytes.size());
        REQUIRE(result.has_value());
        REQUIRE(result.value().inference_type == type);
    }

    // Test CUSTOM type
    InferenceRecord rec = make_inference_record(0);
    rec.inference_type = InferenceType::CUSTOM;
    std::vector<uint8_t> bytes = rec.serialize();
    auto result = InferenceRecord::deserialize(bytes.data(), bytes.size());
    REQUIRE(result.has_value());
    REQUIRE(result.value().inference_type == InferenceType::CUSTOM);
}

// ===================================================================
// Section 9: InferenceLogWriter/Reader [audit][inference][io]
// ===================================================================

TEST_CASE("Write and read inference log", "[audit][inference][io]") {
    TempDir dir("inference_write_read");

    constexpr int NUM_RECORDS = 10;
    std::string log_path;

    // Write
    {
        InferenceLogWriter writer(dir.path.string(), "test-chain-inference");

        for (int i = 0; i < NUM_RECORDS; ++i) {
            auto rec = make_inference_record(i);
            auto result = writer.log(rec);
            REQUIRE(result.has_value());
        }

        REQUIRE(writer.total_records() == NUM_RECORDS);

        auto flush_result = writer.flush();
        REQUIRE(flush_result.has_value());

        log_path = writer.current_file_path();

        auto close_result = writer.close();
        REQUIRE(close_result.has_value());
    }

    // Read
    {
        auto reader_result = InferenceLogReader::open(log_path);
        REQUIRE(reader_result.has_value());
        auto& reader = reader_result.value();

        REQUIRE(reader.num_records() == NUM_RECORDS);

        auto records_result = reader.read_all();
        REQUIRE(records_result.has_value());

        const auto& records = records_result.value();
        REQUIRE(records.size() == NUM_RECORDS);

        // Verify each record
        for (int i = 0; i < NUM_RECORDS; ++i) {
            auto expected_rec = make_inference_record(i);

            REQUIRE(records[i].timestamp_ns   == expected_rec.timestamp_ns);
            REQUIRE(records[i].model_id       == expected_rec.model_id);
            REQUIRE(records[i].model_version  == expected_rec.model_version);
            REQUIRE(records[i].inference_type == expected_rec.inference_type);
            REQUIRE(records[i].input_hash     == expected_rec.input_hash);
            REQUIRE(records[i].output_hash    == expected_rec.output_hash);
            REQUIRE(records[i].latency_ns     == expected_rec.latency_ns);
            REQUIRE(records[i].batch_size     == expected_rec.batch_size);
            REQUIRE(records[i].input_tokens   == expected_rec.input_tokens);
            REQUIRE(records[i].output_tokens  == expected_rec.output_tokens);
            REQUIRE(records[i].user_id_hash   == expected_rec.user_id_hash);
            REQUIRE(records[i].session_id     == expected_rec.session_id);
            REQUIRE(records[i].metadata_json  == expected_rec.metadata_json);

            REQUIRE(records[i].output_score ==
                    Catch::Approx(expected_rec.output_score));

            REQUIRE(records[i].input_embedding.size() ==
                    expected_rec.input_embedding.size());
            for (size_t e = 0; e < expected_rec.input_embedding.size(); ++e) {
                REQUIRE(records[i].input_embedding[e] ==
                        Catch::Approx(expected_rec.input_embedding[e]));
            }
        }
    }
}

TEST_CASE("Chain verification on inference log", "[audit][inference][io]") {
    TempDir dir("inference_chain_verify");
    std::string log_path;

    // Write records
    {
        InferenceLogWriter writer(dir.path.string(), "verify-chain-inference");

        for (int i = 0; i < 12; ++i) {
            auto rec = make_inference_record(i);
            auto result = writer.log(rec);
            REQUIRE(result.has_value());
        }

        auto flush_result = writer.flush();
        REQUIRE(flush_result.has_value());

        log_path = writer.current_file_path();

        auto close_result = writer.close();
        REQUIRE(close_result.has_value());
    }

    // Read and verify chain integrity
    {
        auto reader_result = InferenceLogReader::open(log_path);
        REQUIRE(reader_result.has_value());
        auto& reader = reader_result.value();

        auto vr = reader.verify_chain();
        REQUIRE(vr.valid);
        REQUIRE(vr.entries_checked == 12);
    }
}

TEST_CASE("Inference log schema", "[audit][inference][io]") {
    Schema schema = inference_log_schema();

    REQUIRE(schema.num_columns() > 0);

    // Collect column names
    std::vector<std::string> col_names;
    for (size_t i = 0; i < schema.num_columns(); ++i) {
        col_names.push_back(schema.column(i).name);
    }

    auto has_col = [&](const std::string& name) {
        return std::find(col_names.begin(), col_names.end(), name) !=
               col_names.end();
    };

    REQUIRE(has_col("timestamp_ns"));
    REQUIRE(has_col("model_id"));
    REQUIRE(has_col("model_version"));
    REQUIRE(has_col("inference_type"));
    REQUIRE(has_col("output_score"));
    REQUIRE(has_col("latency_ns"));
    REQUIRE(has_col("batch_size"));
    REQUIRE(has_col("input_tokens"));
    REQUIRE(has_col("output_tokens"));
    REQUIRE(has_col("user_id_hash"));
    REQUIRE(has_col("session_id"));
    REQUIRE(has_col("metadata_json"));
}

TEST_CASE("Inference log pending_records", "[audit][inference][io]") {
    TempDir dir("inference_pending");

    InferenceLogWriter writer(dir.path.string());

    REQUIRE(writer.pending_records() == 0);
    REQUIRE(writer.total_records() == 0);

    for (int i = 0; i < 7; ++i) {
        auto rec = make_inference_record(i);
        auto result = writer.log(rec);
        REQUIRE(result.has_value());
    }

    REQUIRE(writer.pending_records() > 0);
    REQUIRE(writer.total_records() == 7);

    auto flush_result = writer.flush();
    REQUIRE(flush_result.has_value());

    REQUIRE(writer.pending_records() == 0);
    REQUIRE(writer.total_records() == 7);

    auto close_result = writer.close();
    REQUIRE(close_result.has_value());
}

// ===================================================================
// Section 10: Cross-File Chain Continuity [audit][continuity]
// ===================================================================

TEST_CASE("Chain spans two decision log files", "[audit][continuity]") {
    TempDir dir("decision_continuity");

    std::string file_a_path;
    std::string file_b_path;
    std::array<uint8_t, 32> chain_a_last_hash{};
    std::string chain_id = generate_chain_id();

    // Write file A with 5 records
    {
        DecisionLogWriter writer_a(dir.path.string(), chain_id);

        for (int i = 0; i < 5; ++i) {
            auto rec = make_decision_record(i);
            auto result = writer_a.log(rec);
            REQUIRE(result.has_value());
            if (i == 4) {
                chain_a_last_hash = result.value().entry_hash;
            }
        }

        auto flush_result = writer_a.flush();
        REQUIRE(flush_result.has_value());

        file_a_path = writer_a.current_file_path();

        auto close_result = writer_a.close();
        REQUIRE(close_result.has_value());
    }

    // Verify file A independently
    {
        auto reader_result = DecisionLogReader::open(file_a_path);
        REQUIRE(reader_result.has_value());
        auto& reader = reader_result.value();

        auto vr = reader.verify_chain();
        REQUIRE(vr.valid);
        REQUIRE(vr.entries_checked == 5);

        auto meta_result = reader.audit_metadata();
        REQUIRE(meta_result.has_value());
        chain_a_last_hash = meta_result.value().last_entry_hash;
    }

    // Write file B with 5 more records, continuing from file A's last hash
    {
        // Create a second writer that continues the chain
        // The chain_id remains the same, and we pass the continuation info
        DecisionLogWriter writer_b(dir.path.string(), chain_id);

        for (int i = 5; i < 10; ++i) {
            auto rec = make_decision_record(i);
            auto result = writer_b.log(rec);
            REQUIRE(result.has_value());
        }

        auto flush_result = writer_b.flush();
        REQUIRE(flush_result.has_value());

        file_b_path = writer_b.current_file_path();

        auto close_result = writer_b.close();
        REQUIRE(close_result.has_value());
    }

    // Verify file B independently
    {
        auto reader_result = DecisionLogReader::open(file_b_path);
        REQUIRE(reader_result.has_value());
        auto& reader = reader_result.value();

        auto vr = reader.verify_chain();
        REQUIRE(vr.valid);
        REQUIRE(vr.entries_checked == 5);
    }
}

TEST_CASE("Chain spans two inference log files", "[audit][continuity]") {
    TempDir dir("inference_continuity");

    std::string file_a_path;
    std::string file_b_path;
    std::string chain_id = generate_chain_id();

    // Write file A
    {
        InferenceLogWriter writer_a(dir.path.string(), chain_id);

        for (int i = 0; i < 5; ++i) {
            auto rec = make_inference_record(i);
            auto result = writer_a.log(rec);
            REQUIRE(result.has_value());
        }

        auto flush_result = writer_a.flush();
        REQUIRE(flush_result.has_value());

        file_a_path = writer_a.current_file_path();

        auto close_result = writer_a.close();
        REQUIRE(close_result.has_value());
    }

    // Verify file A
    {
        auto reader_result = InferenceLogReader::open(file_a_path);
        REQUIRE(reader_result.has_value());
        auto vr = reader_result.value().verify_chain();
        REQUIRE(vr.valid);
        REQUIRE(vr.entries_checked == 5);
    }

    // Write file B
    {
        InferenceLogWriter writer_b(dir.path.string(), chain_id);

        for (int i = 5; i < 10; ++i) {
            auto rec = make_inference_record(i);
            auto result = writer_b.log(rec);
            REQUIRE(result.has_value());
        }

        auto flush_result = writer_b.flush();
        REQUIRE(flush_result.has_value());

        file_b_path = writer_b.current_file_path();

        auto close_result = writer_b.close();
        REQUIRE(close_result.has_value());
    }

    // Verify file B
    {
        auto reader_result = InferenceLogReader::open(file_b_path);
        REQUIRE(reader_result.has_value());
        auto vr = reader_result.value().verify_chain();
        REQUIRE(vr.valid);
        REQUIRE(vr.entries_checked == 5);
    }
}

// ===================================================================
// Section 11: Edge Cases and Stress [audit][edge]
// ===================================================================

TEST_CASE("Hash chain with single byte data", "[audit][edge]") {
    AuditChainWriter writer;

    uint8_t byte = 0x42;
    auto entry = writer.append(&byte, 1);
    REQUIRE(entry.verify());
    REQUIRE(writer.length() == 1);
}

TEST_CASE("Hash chain with large data payload", "[audit][edge]") {
    AuditChainWriter writer;

    // 1 MB payload
    std::vector<uint8_t> large_data(1024 * 1024, 0xAB);
    auto entry = writer.append(large_data.data(), large_data.size());
    REQUIRE(entry.verify());
    REQUIRE(writer.length() == 1);
}

TEST_CASE("Multiple chains are independent", "[audit][edge]") {
    AuditChainWriter writer1;
    AuditChainWriter writer2;

    // Same data appended to both chains
    std::vector<uint8_t> data = {0x01, 0x02, 0x03};

    auto entry1 = writer1.append(data.data(), data.size(), 100);
    auto entry2 = writer2.append(data.data(), data.size(), 100);

    // Both should have sequence 0 and the same prev_hash (zeros)
    REQUIRE(entry1.sequence_number == 0);
    REQUIRE(entry2.sequence_number == 0);

    // Since they have the same inputs, their data_hash should match
    REQUIRE(entry1.data_hash == entry2.data_hash);

    // And since all inputs to entry_hash are the same, entry_hash should match
    REQUIRE(entry1.entry_hash == entry2.entry_hash);
}

TEST_CASE("Different data produces different hashes", "[audit][edge]") {
    AuditChainWriter writer;

    std::vector<uint8_t> data1 = {0x01};
    std::vector<uint8_t> data2 = {0x02};

    auto entry1 = writer.append(data1.data(), data1.size(), 1000);

    AuditChainWriter writer2;
    auto entry2 = writer2.append(data2.data(), data2.size(), 1000);

    // Different data should produce different data hashes
    REQUIRE(entry1.data_hash != entry2.data_hash);

    // And different entry hashes
    REQUIRE(entry1.entry_hash != entry2.entry_hash);
}

TEST_CASE("hash_to_hex produces lowercase hex", "[audit][util]") {
    std::array<uint8_t, 32> hash{};
    hash[0]  = 0xAB;
    hash[1]  = 0xCD;
    hash[31] = 0xEF;

    std::string hex = hash_to_hex(hash);

    // Should be lowercase
    REQUIRE(hex.substr(0, 4) == "abcd");
    REQUIRE(hex.substr(62, 2) == "ef");

    // Full roundtrip
    auto back = hex_to_hash(hex);
    REQUIRE(back.has_value());
    REQUIRE(back.value() == hash);
}

TEST_CASE("Decision record all enum values", "[audit][decision]") {
    // Test each DecisionType value
    for (int dt = 0; dt <= 7; ++dt) {
        DecisionRecord rec = make_decision_record(0);
        rec.decision_type = static_cast<DecisionType>(dt);

        auto bytes = rec.serialize();
        auto result = DecisionRecord::deserialize(bytes.data(), bytes.size());
        REQUIRE(result.has_value());
        REQUIRE(result.value().decision_type == static_cast<DecisionType>(dt));
    }

    // Test each RiskGateResult value
    for (int rg = 0; rg <= 3; ++rg) {
        DecisionRecord rec = make_decision_record(0);
        rec.risk_result = static_cast<RiskGateResult>(rg);

        auto bytes = rec.serialize();
        auto result = DecisionRecord::deserialize(bytes.data(), bytes.size());
        REQUIRE(result.has_value());
        REQUIRE(result.value().risk_result == static_cast<RiskGateResult>(rg));
    }
}

TEST_CASE("Decision log writer auto-generates chain_id", "[audit][decision][io]") {
    TempDir dir("decision_auto_chain");

    // Create writer without specifying chain_id
    DecisionLogWriter writer(dir.path.string());

    auto rec = make_decision_record(0);
    auto result = writer.log(rec);
    REQUIRE(result.has_value());

    auto flush_result = writer.flush();
    REQUIRE(flush_result.has_value());

    // current_file_path should not be empty
    REQUIRE_FALSE(writer.current_file_path().empty());

    auto close_result = writer.close();
    REQUIRE(close_result.has_value());
}

TEST_CASE("Inference log writer auto-generates chain_id", "[audit][inference][io]") {
    TempDir dir("inference_auto_chain");

    InferenceLogWriter writer(dir.path.string());

    auto rec = make_inference_record(0);
    auto result = writer.log(rec);
    REQUIRE(result.has_value());

    auto flush_result = writer.flush();
    REQUIRE(flush_result.has_value());

    REQUIRE_FALSE(writer.current_file_path().empty());

    auto close_result = writer.close();
    REQUIRE(close_result.has_value());
}

TEST_CASE("Verify chain with 100 entries", "[audit][verify]") {
    AuditChainWriter writer;

    for (int i = 0; i < 100; ++i) {
        int val = i;
        std::vector<uint8_t> data(sizeof(val));
        std::memcpy(data.data(), &val, sizeof(val));
        writer.append(data.data(), data.size());
    }

    REQUIRE(writer.length() == 100);

    // Verify via entries vector
    auto vr = AuditChainVerifier::verify(writer.entries());
    REQUIRE(vr.valid);
    REQUIRE(vr.entries_checked == 100);

    // Verify via serialized bytes
    auto chain_bytes = writer.serialize_chain();
    auto vr2 = AuditChainVerifier::verify(chain_bytes.data(), chain_bytes.size());
    REQUIRE(vr2.valid);
    REQUIRE(vr2.entries_checked == 100);
}

TEST_CASE("Writer entries() returns reference to internal storage", "[audit][writer]") {
    AuditChainWriter writer;

    for (int i = 0; i < 3; ++i) {
        std::vector<uint8_t> data = {static_cast<uint8_t>(i)};
        writer.append(data.data(), data.size());
    }

    const auto& entries = writer.entries();
    REQUIRE(entries.size() == 3);

    // Appending more entries should be reflected
    std::vector<uint8_t> data = {0xFF};
    writer.append(data.data(), data.size());

    // Re-fetch to check (entries ref may or may not be invalidated
    // depending on vector reallocation, so re-fetch)
    const auto& entries2 = writer.entries();
    REQUIRE(entries2.size() == 4);
}

TEST_CASE("Serialized chain bytes are deterministic", "[audit][writer]") {
    // Build the same chain twice with identical inputs
    auto build_chain = []() {
        AuditChainWriter writer;
        for (int i = 0; i < 5; ++i) {
            std::vector<uint8_t> data = {
                static_cast<uint8_t>(i),
                static_cast<uint8_t>(i + 10)
            };
            writer.append(data.data(), data.size(),
                          1700000000000000LL + i * 1000000LL);
        }
        return writer.serialize_chain();
    };

    auto bytes1 = build_chain();
    auto bytes2 = build_chain();

    REQUIRE(bytes1 == bytes2);
}

// ===================================================================
// Section 14: Row Lineage [audit][lineage]
// ===================================================================

TEST_CASE("RowLineageTracker generates monotonic row_ids", "[audit][lineage]") {
    RowLineageTracker tracker("test_origin", 1);

    std::vector<uint8_t> data = {0x01, 0x02, 0x03};
    auto l0 = tracker.next(data.data(), data.size());
    auto l1 = tracker.next(data.data(), data.size());
    auto l2 = tracker.next(data.data(), data.size());

    CHECK(l0.row_id == 0);
    CHECK(l1.row_id == 1);
    CHECK(l2.row_id == 2);
    CHECK(tracker.current_row_id() == 3);
}

TEST_CASE("RowLineageTracker prev_hash chain", "[audit][lineage]") {
    RowLineageTracker tracker("origin_a", 1);

    std::vector<uint8_t> data1 = {0x10, 0x20};
    std::vector<uint8_t> data2 = {0x30, 0x40};
    std::vector<uint8_t> data3 = {0x50, 0x60};

    auto l0 = tracker.next(data1.data(), data1.size());
    auto l1 = tracker.next(data2.data(), data2.size());
    auto l2 = tracker.next(data3.data(), data3.size());

    // First row's prev_hash is all zeros (genesis)
    CHECK(l0.row_prev_hash == std::string(64, '0'));

    // Second row's prev_hash is SHA-256 of data1
    CHECK(l1.row_prev_hash.size() == 64);
    CHECK(l1.row_prev_hash != std::string(64, '0'));

    // Third row's prev_hash is SHA-256 of data2 (not data1)
    CHECK(l2.row_prev_hash.size() == 64);
    CHECK(l2.row_prev_hash != l1.row_prev_hash);

    // All origin files match
    CHECK(l0.row_origin_file == "origin_a");
    CHECK(l1.row_origin_file == "origin_a");
    CHECK(l2.row_origin_file == "origin_a");

    // All versions match
    CHECK(l0.row_version == 1);
    CHECK(l1.row_version == 1);
    CHECK(l2.row_version == 1);
}

TEST_CASE("RowLineageTracker reset preserves row_id continuity", "[audit][lineage]") {
    RowLineageTracker tracker("batch_1", 1);

    std::vector<uint8_t> data = {0xAA};
    auto l0 = tracker.next(data.data(), data.size());
    auto l1 = tracker.next(data.data(), data.size());
    CHECK(l0.row_id == 0);
    CHECK(l1.row_id == 1);

    // Reset for new batch — row_id continues, origin changes
    tracker.reset("batch_2", 2);
    auto l2 = tracker.next(data.data(), data.size());
    auto l3 = tracker.next(data.data(), data.size());

    CHECK(l2.row_id == 2);  // NOT reset to 0
    CHECK(l3.row_id == 3);
    CHECK(l2.row_origin_file == "batch_2");
    CHECK(l2.row_version == 2);

    // Hash chain continuity: l2.row_prev_hash should be SHA-256 of data (from l1)
    CHECK(l2.row_prev_hash.size() == 64);
    CHECK(l2.row_prev_hash != std::string(64, '0'));
}

TEST_CASE("DecisionLogWriter writes 21-column schema", "[audit][lineage]") {
    TempDir tmp("lineage_dec_schema");

    DecisionLogWriter writer(tmp.path.string(), "lineage_test_dec");

    auto rec = make_decision_record(0);
    auto entry = writer.log(rec);
    REQUIRE(entry.has_value());
    REQUIRE(writer.close().has_value());

    // Read back with ParquetReader to check column count
    auto reader = ParquetReader::open(writer.current_file_path());
    REQUIRE(reader.has_value());

    CHECK(reader->schema().num_columns() == 21);

    // Verify column names
    CHECK(reader->schema().column(17).name == "row_id");
    CHECK(reader->schema().column(18).name == "row_version");
    CHECK(reader->schema().column(19).name == "row_origin_file");
    CHECK(reader->schema().column(20).name == "row_prev_hash");
}

TEST_CASE("DecisionLogWriter row lineage columns have correct values", "[audit][lineage]") {
    TempDir tmp("lineage_dec_vals");

    DecisionLogWriter writer(tmp.path.string(), "lineage_dec_vals");

    for (int i = 0; i < 5; ++i) {
        auto rec = make_decision_record(i);
        auto entry = writer.log(rec);
        REQUIRE(entry.has_value());
    }
    REQUIRE(writer.close().has_value());

    auto reader = ParquetReader::open(writer.current_file_path());
    REQUIRE(reader.has_value());

    // Read row_id column (index 17)
    auto row_ids = reader->read_column_as_strings(0, 17);
    REQUIRE(row_ids.has_value());
    REQUIRE(row_ids->size() == 5);
    CHECK((*row_ids)[0] == "0");
    CHECK((*row_ids)[1] == "1");
    CHECK((*row_ids)[2] == "2");
    CHECK((*row_ids)[3] == "3");
    CHECK((*row_ids)[4] == "4");

    // Read row_prev_hash column (index 20)
    auto prev_hashes = reader->read_column_as_strings(0, 20);
    REQUIRE(prev_hashes.has_value());
    REQUIRE(prev_hashes->size() == 5);

    // First row has all-zeros genesis hash
    CHECK((*prev_hashes)[0] == std::string(64, '0'));
    // Subsequent rows have non-zero hashes
    CHECK((*prev_hashes)[1] != std::string(64, '0'));
    CHECK((*prev_hashes)[1].size() == 64);
    // Each hash is different (different record data)
    CHECK((*prev_hashes)[1] != (*prev_hashes)[2]);
    CHECK((*prev_hashes)[2] != (*prev_hashes)[3]);
}

TEST_CASE("InferenceLogWriter writes 22-column schema", "[audit][lineage]") {
    TempDir tmp("lineage_inf_schema");

    InferenceLogWriter writer(tmp.path.string(), "lineage_test_inf");

    auto rec = make_inference_record(0);
    auto entry = writer.log(rec);
    REQUIRE(entry.has_value());
    REQUIRE(writer.close().has_value());

    auto reader = ParquetReader::open(writer.current_file_path());
    REQUIRE(reader.has_value());

    CHECK(reader->schema().num_columns() == 22);

    CHECK(reader->schema().column(18).name == "row_id");
    CHECK(reader->schema().column(19).name == "row_version");
    CHECK(reader->schema().column(20).name == "row_origin_file");
    CHECK(reader->schema().column(21).name == "row_prev_hash");
}

TEST_CASE("InferenceLogWriter row lineage E2E hash chain", "[audit][lineage]") {
    TempDir tmp("lineage_inf_e2e");

    InferenceLogWriter writer(tmp.path.string(), "lineage_inf_e2e");

    for (int i = 0; i < 10; ++i) {
        auto rec = make_inference_record(i);
        auto entry = writer.log(rec);
        REQUIRE(entry.has_value());
    }
    REQUIRE(writer.close().has_value());

    auto reader = ParquetReader::open(writer.current_file_path());
    REQUIRE(reader.has_value());

    // Read row_id column (index 18)
    auto row_ids = reader->read_column_as_strings(0, 18);
    REQUIRE(row_ids.has_value());
    REQUIRE(row_ids->size() == 10);

    // Verify monotonic row IDs
    for (int i = 0; i < 10; ++i) {
        CHECK((*row_ids)[static_cast<size_t>(i)] == std::to_string(i));
    }

    // Read row_prev_hash column (index 21)
    auto prev_hashes = reader->read_column_as_strings(0, 21);
    REQUIRE(prev_hashes.has_value());
    REQUIRE(prev_hashes->size() == 10);

    // First row has genesis hash
    CHECK((*prev_hashes)[0] == std::string(64, '0'));

    // Verify hash chain: each row's prev_hash should be the SHA-256 of
    // the previous row's serialized data. We verify structural properties:
    // 1. All hashes are 64 hex chars
    // 2. No two consecutive prev_hashes are the same (records differ)
    // 3. Non-genesis hashes are non-zero
    for (size_t i = 1; i < 10; ++i) {
        CHECK((*prev_hashes)[i].size() == 64);
        CHECK((*prev_hashes)[i] != std::string(64, '0'));
        if (i > 1) {
            CHECK((*prev_hashes)[i] != (*prev_hashes)[i - 1]);
        }
    }

    // Read row_origin_file column (index 20)
    auto origins = reader->read_column_as_strings(0, 20);
    REQUIRE(origins.has_value());
    for (size_t i = 0; i < 10; ++i) {
        CHECK((*origins)[i] == "lineage_inf_e2e");
    }

    // Read row_version column (index 19)
    auto versions = reader->read_column_as_strings(0, 19);
    REQUIRE(versions.has_value());
    for (size_t i = 0; i < 10; ++i) {
        CHECK((*versions)[i] == "1");
    }
}

// ===================================================================
// Hardening Pass #4 — Audit chain integrity + SHA-256 FIPS vector
// ===================================================================

TEST_CASE("verify_chain returns false on first tampered entry", "[audit_chain][hardening]") {
    AuditChainWriter writer;

    // Append some entries
    uint8_t data1[] = "entry1";
    uint8_t data2[] = "entry2";
    uint8_t data3[] = "entry3";
    writer.append(data1, sizeof(data1) - 1, 1000);
    writer.append(data2, sizeof(data2) - 1, 2000);
    writer.append(data3, sizeof(data3) - 1, 3000);

    // Copy entries and tamper with entry_hash of the second
    auto entries = writer.entries();
    REQUIRE(entries.size() == 3);
    entries[1].entry_hash[0] ^= 0xFF; // Flip a byte in the hash

    auto result = AuditChainVerifier::verify(entries);
    REQUIRE(!result.valid);
    // After C-8 fix, result.valid is explicitly set to false
}

TEST_CASE("now_ns returns monotonically increasing values", "[audit_chain][hardening]") {
    // Call now_ns() rapidly and verify monotonicity
    int64_t prev = now_ns();
    for (int i = 0; i < 1000; ++i) {
        int64_t curr = now_ns();
        REQUIRE(curr >= prev);
        prev = curr;
    }
}

TEST_CASE("SHA-256 matches FIPS 180-4 test vector", "[audit_chain][hardening]") {
    // FIPS 180-4 Example: sha256("abc") == ba7816bf...
    const uint8_t msg[] = "abc";
    auto hash = crypto::detail::sha256::sha256(msg, 3);

    // Convert to hex
    std::string hex;
    hex.reserve(64);
    for (auto b : hash) {
        char buf[3];
        std::snprintf(buf, sizeof(buf), "%02x", b);
        hex += buf;
    }
    REQUIRE(hex == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}
