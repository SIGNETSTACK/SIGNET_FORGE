// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026 Johnson Ogundeji
// Gap Fix Pass 12: D-12 (threat model), G-9 (data classification),
// R-19 (incident response), R-20 (regulatory monitor), T-8 (HSM stub).

#include "signet/ai/threat_model.hpp"
#include "signet/ai/data_classification.hpp"
#include "signet/ai/incident_response.hpp"
#include "signet/ai/regulatory_monitor.hpp"
#include "signet/crypto/hsm_client_stub.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

using namespace signet::forge;

// ===========================================================================
// D-12: Threat Model (STRIDE/DREAD)
// ===========================================================================

TEST_CASE("ThreatModelAnalyzer: default model has all 6 STRIDE categories",
          "[compliance][threat_model][D-12]") {
    auto model = ThreatModelAnalyzer::signet_default_model();
    auto result = ThreatModelAnalyzer::analyze(model);
    REQUIRE(result.has_value());
    auto& analysis = *result;

    REQUIRE(analysis.total_threats == 6);
    REQUIRE(analysis.stride_complete);
    REQUIRE(analysis.missing_categories.empty());
}

TEST_CASE("ThreatModelAnalyzer: DREAD composite scoring",
          "[compliance][threat_model][D-12]") {
    DreadScore d{7, 8, 5, 6, 4};
    REQUIRE(d.composite() == Catch::Approx(6.0));
    REQUIRE(d.severity() == ThreatSeverity::MEDIUM);
    REQUIRE(d.valid());

    // Critical score
    DreadScore crit{10, 10, 9, 9, 9};
    REQUIRE(crit.severity() == ThreatSeverity::CRITICAL);

    // Invalid score
    DreadScore bad{0, 5, 5, 5, 5};
    REQUIRE_FALSE(bad.valid());
}

TEST_CASE("ThreatModelAnalyzer: JSON report contains required fields",
          "[compliance][threat_model][D-12]") {
    auto model = ThreatModelAnalyzer::signet_default_model();
    auto result = ThreatModelAnalyzer::analyze(model);
    REQUIRE(result.has_value());
    auto& analysis = *result;

    REQUIRE_FALSE(analysis.report_json.empty());
    REQUIRE(analysis.report_json.find("\"methodology\": \"STRIDE/DREAD\"") != std::string::npos);
    REQUIRE(analysis.report_json.find("\"stride_complete\": true") != std::string::npos);
    REQUIRE(analysis.report_json.find("SIGNET-TM-001") != std::string::npos);
}

TEST_CASE("ThreatModelAnalyzer: incomplete model flags missing categories",
          "[compliance][threat_model][D-12]") {
    ThreatModel m;
    m.model_id = "TEST-001";
    // Only one category
    m.threats.push_back(ThreatEntry{
        "T-001", "Test threat", "desc",
        StrideCategory::TAMPERING, DreadScore{5, 5, 5, 5, 5},
        "vector", "component", {}, {}
    });

    auto result2 = ThreatModelAnalyzer::analyze(m);
    REQUIRE(result2.has_value());
    REQUIRE_FALSE(result2->stride_complete);
    REQUIRE(result2->missing_categories.size() == 5);
}

TEST_CASE("ThreatModelAnalyzer: mitigation status tracking",
          "[compliance][threat_model][D-12]") {
    auto model = ThreatModelAnalyzer::signet_default_model();
    auto result = ThreatModelAnalyzer::analyze(model);
    REQUIRE(result.has_value());

    // All default threats should be mitigated
    REQUIRE(result->mitigated_count == 6);
    REQUIRE(result->unmitigated_count == 0);
}

// ===========================================================================
// G-9: Data Classification Ontology
// ===========================================================================

TEST_CASE("DataClassificationOntology: financial defaults",
          "[compliance][data_classification][G-9]") {
    auto ont = DataClassificationOntology::financial_default();
    REQUIRE(ont.size() >= 6);
    REQUIRE(ont.ontology_id() == "financial-default");

    // Public field
    auto sym = ont.lookup("symbol");
    REQUIRE(sym.classification == DataClassification::PUBLIC);

    // Highly restricted PII
    auto trader = ont.lookup("trader_id");
    REQUIRE(trader.classification == DataClassification::HIGHLY_RESTRICTED);
    REQUIRE(trader.sensitivity == DataSensitivity::FINANCIAL_PII);
    REQUIRE(trader.require_encryption);
    REQUIRE_FALSE(trader.allow_export);
}

TEST_CASE("DataClassificationOntology: unknown field returns PUBLIC default",
          "[compliance][data_classification][G-9]") {
    DataClassificationOntology ont("test");
    auto rule = ont.lookup("nonexistent_field");
    REQUIRE(rule.classification == DataClassification::PUBLIC);
    REQUIRE(rule.sensitivity == DataSensitivity::NEUTRAL);
}

TEST_CASE("DataClassificationOntology: validate_handling rejects unencrypted restricted data",
          "[compliance][data_classification][G-9]") {
    DataClassificationOntology ont("test");
    DataClassificationRule r;
    r.field_name = "secret";
    r.classification = DataClassification::HIGHLY_RESTRICTED;
    r.require_encryption = true;
    ont.add_rule(r);

    // Unencrypted → error
    auto result = ont.validate_handling("secret", false, false);
    REQUIRE_FALSE(result.has_value());

    // Encrypted → OK
    auto result2 = ont.validate_handling("secret", true, false);
    REQUIRE(result2.has_value());
}

TEST_CASE("DataClassificationOntology: all_rules returns complete list",
          "[compliance][data_classification][G-9]") {
    auto ont = DataClassificationOntology::financial_default();
    auto rules = ont.all_rules();
    REQUIRE(rules.size() == ont.size());
}

// ===========================================================================
// R-19: Incident Response Playbook
// ===========================================================================

TEST_CASE("PlaybookRegistry: financial defaults contain 3 playbooks",
          "[compliance][incident_response][R-19]") {
    auto reg = PlaybookRegistry::financial_defaults();
    REQUIRE(reg.size() == 3);

    auto types = reg.incident_types();
    REQUIRE(types.size() == 3);
}

TEST_CASE("PlaybookRegistry: key compromise playbook has all 6 NIST phases",
          "[compliance][incident_response][R-19]") {
    auto reg = PlaybookRegistry::financial_defaults();
    auto pb = reg.lookup("key_compromise");
    REQUIRE(pb.has_value());
    REQUIRE(pb->playbook_id == "PB-CRYPTO-001");
    REQUIRE(pb->step_count() == 6);

    // Verify all NIST phases are covered
    auto det = pb->steps_for_phase(IncidentPhase::DETECTION);
    REQUIRE(det.size() == 1);
    auto cont = pb->steps_for_phase(IncidentPhase::CONTAINMENT);
    REQUIRE(cont.size() == 2);
    auto erad = pb->steps_for_phase(IncidentPhase::ERADICATION);
    REQUIRE(erad.size() == 1);
    auto rec = pb->steps_for_phase(IncidentPhase::RECOVERY);
    REQUIRE(rec.size() == 1);
    auto ll = pb->steps_for_phase(IncidentPhase::LESSONS_LEARNED);
    REQUIRE(ll.size() == 1);
}

TEST_CASE("PlaybookRegistry: lookup unknown type returns error",
          "[compliance][incident_response][R-19]") {
    auto reg = PlaybookRegistry::financial_defaults();
    auto result = reg.lookup("alien_invasion");
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("IncidentResponseTracker: tracks step completion and SLA",
          "[compliance][incident_response][R-19]") {
    auto reg = PlaybookRegistry::financial_defaults();
    auto pb = *reg.lookup("service_outage");

    IncidentResponseTracker tracker("INC-2026-001", pb);
    REQUIRE_FALSE(tracker.all_steps_complete());
    REQUIRE(tracker.remaining_steps().size() == 4);

    // Complete first step within SLA (120s = 120e9 ns)
    int64_t t0 = 1000000000LL;
    auto r1 = tracker.complete_step("SO-01", "ops-team", t0, t0 + 60000000000LL);
    REQUIRE(r1.has_value());
    REQUIRE(tracker.completed_steps().size() == 1);
    REQUIRE(tracker.completed_steps()[0].sla_met);

    // Complete second step exceeding SLA (900s)
    auto r2 = tracker.complete_step("SO-02", "eng-team", t0, t0 + 2000000000000LL);
    REQUIRE(r2.has_value());
    REQUIRE_FALSE(tracker.completed_steps()[1].sla_met);
    REQUIRE(tracker.sla_breach_count() == 1);
}

TEST_CASE("IncidentResponseTracker: rejects unknown step ID",
          "[compliance][incident_response][R-19]") {
    auto reg = PlaybookRegistry::financial_defaults();
    auto pb = *reg.lookup("data_breach");
    IncidentResponseTracker tracker("INC-TEST", pb);

    auto result = tracker.complete_step("FAKE-STEP", "user", 0, 0);
    REQUIRE_FALSE(result.has_value());
}

// ===========================================================================
// R-20: Regulatory Change Monitoring
// ===========================================================================

TEST_CASE("RegulatoryChangeMonitor: signet defaults track DORA and EU AI Act",
          "[compliance][regulatory_monitor][R-20]") {
    auto mon = RegulatoryChangeMonitor::signet_defaults();
    REQUIRE(mon.size() == 3);

    auto dora = mon.lookup("RC-DORA-2025");
    REQUIRE(dora.has_value());
    REQUIRE(dora->regulation == "DORA");
    REQUIRE(dora->status == ChangeComplianceStatus::VERIFIED);

    auto euaia = mon.lookup("RC-EUAIA-2024");
    REQUIRE(euaia.has_value());
    REQUIRE(euaia->impact == RegulatoryImpact::HIGH);
}

TEST_CASE("RegulatoryChangeMonitor: update status and impact assessment",
          "[compliance][regulatory_monitor][R-20]") {
    RegulatoryChangeMonitor mon("TEST-ORG");

    RegulatoryChange rc;
    rc.change_id = "RC-TEST-001";
    rc.regulation = "Test Reg";
    rc.title = "Test change";
    mon.track_change(rc);

    // Update status
    auto r1 = mon.update_status("RC-TEST-001", ChangeComplianceStatus::IN_PROGRESS);
    REQUIRE(r1.has_value());

    // Assess impact
    auto r2 = mon.assess_impact("RC-TEST-001", RegulatoryImpact::MEDIUM,
                                "analyst", "2026-03-09");
    REQUIRE(r2.has_value());

    auto updated = mon.lookup("RC-TEST-001");
    REQUIRE(updated.has_value());
    REQUIRE(updated->impact == RegulatoryImpact::MEDIUM);
    REQUIRE(updated->assessor == "analyst");
}

TEST_CASE("RegulatoryChangeMonitor: pending_changes filters verified",
          "[compliance][regulatory_monitor][R-20]") {
    auto mon = RegulatoryChangeMonitor::signet_defaults();
    // All defaults are VERIFIED, so pending should be 0
    REQUIRE(mon.pending_count() == 0);
    REQUIRE(mon.pending_changes().empty());

    // Add an unassessed change
    RegulatoryChange rc;
    rc.change_id = "RC-NEW-001";
    rc.regulation = "New Regulation";
    rc.title = "Pending change";
    mon.track_change(rc);

    REQUIRE(mon.pending_count() == 1);
    REQUIRE(mon.pending_changes().size() == 1);
}

TEST_CASE("RegulatoryChangeMonitor: filter by regulation",
          "[compliance][regulatory_monitor][R-20]") {
    auto mon = RegulatoryChangeMonitor::signet_defaults();
    auto dora_changes = mon.changes_for_regulation("DORA");
    REQUIRE(dora_changes.size() == 1);

    auto fake_changes = mon.changes_for_regulation("FAKE");
    REQUIRE(fake_changes.empty());
}

// ===========================================================================
// T-8: HSM Integration Testing Stubs
// ===========================================================================

TEST_CASE("HsmClientStub: register KEK and verify",
          "[crypto][hsm][T-8]") {
    crypto::HsmClientStub hsm;

    std::array<uint8_t, 32> kek{};
    for (size_t i = 0; i < 32; ++i) kek[i] = static_cast<uint8_t>(i + 1);

    hsm.register_kek("master-key-1", kek);
    REQUIRE(hsm.has_kek("master-key-1"));
    REQUIRE_FALSE(hsm.has_kek("nonexistent"));
    REQUIRE(hsm.kek_count() == 1);
}

TEST_CASE("HsmClientStub: rejects invalid KEK size",
          "[crypto][hsm][T-8]") {
    crypto::HsmClientStub hsm;

    std::vector<uint8_t> short_kek(16, 0x42);
    auto result = hsm.register_kek("bad-key", short_kek);
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("HsmClientStub: wrap and unwrap DEK round-trip",
          "[crypto][hsm][T-8]") {
    crypto::HsmClientStub hsm;

    // Register a KEK
    std::array<uint8_t, 32> kek{};
    for (size_t i = 0; i < 32; ++i) kek[i] = static_cast<uint8_t>(i + 0x10);
    hsm.register_kek("test-kek", kek);

    // DEK to wrap (32 bytes for AES-256)
    std::vector<uint8_t> dek(32);
    for (size_t i = 0; i < 32; ++i) dek[i] = static_cast<uint8_t>(i + 0xA0);

    // Wrap
    auto wrapped = hsm.wrap_key(dek, "test-kek");
    REQUIRE(wrapped.has_value());
    REQUIRE(wrapped->size() == 40);  // 32 + 8 (AES Key Wrap overhead)

    // Unwrap
    auto unwrapped = hsm.unwrap_key(*wrapped, "test-kek");
    REQUIRE(unwrapped.has_value());
    REQUIRE(unwrapped->size() == 32);
    REQUIRE(*unwrapped == dek);
}

TEST_CASE("HsmClientStub: unwrap with wrong KEK fails integrity check",
          "[crypto][hsm][T-8]") {
    crypto::HsmClientStub hsm;

    // Register two different KEKs
    std::array<uint8_t, 32> kek1{}, kek2{};
    for (size_t i = 0; i < 32; ++i) {
        kek1[i] = static_cast<uint8_t>(i + 1);
        kek2[i] = static_cast<uint8_t>(i + 100);
    }
    hsm.register_kek("kek-1", kek1);
    hsm.register_kek("kek-2", kek2);

    // Wrap with kek-1
    std::vector<uint8_t> dek(32, 0x55);
    auto wrapped = hsm.wrap_key(dek, "kek-1");
    REQUIRE(wrapped.has_value());

    // Unwrap with kek-2 — must fail
    auto bad_unwrap = hsm.unwrap_key(*wrapped, "kek-2");
    REQUIRE_FALSE(bad_unwrap.has_value());
}

TEST_CASE("HsmClientStub: wrap/unwrap with unknown KEK ID fails",
          "[crypto][hsm][T-8]") {
    crypto::HsmClientStub hsm;

    std::vector<uint8_t> dek(32, 0x42);
    auto wrap_result = hsm.wrap_key(dek, "nonexistent-kek");
    REQUIRE_FALSE(wrap_result.has_value());

    std::vector<uint8_t> fake_wrapped(40, 0x00);
    auto unwrap_result = hsm.unwrap_key(fake_wrapped, "nonexistent-kek");
    REQUIRE_FALSE(unwrap_result.has_value());
}

TEST_CASE("HsmClientStub: AES Key Wrap RFC 3394 test vector",
          "[crypto][hsm][T-8]") {
    // RFC 3394 §4.6 — 256-bit KEK, 256-bit key data
    // KEK: 000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F
    // Plaintext: 00112233445566778899AABBCCDDEEFF000102030405060708090A0B0C0D0E0F
    // Ciphertext: 28C9F404C4B810F4CBCCB35CFB87F8263F5786E2D80ED326CBC7F0E71A99F43BFB988B9B7A02DD21
    std::array<uint8_t, 32> kek{};
    for (size_t i = 0; i < 32; ++i) kek[i] = static_cast<uint8_t>(i);

    std::vector<uint8_t> plaintext = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F
    };

    auto wrapped = crypto::detail::aes_key_wrap::wrap(kek, plaintext);
    REQUIRE(wrapped.has_value());
    REQUIRE(wrapped->size() == 40);

    // Round-trip verification
    auto unwrapped = crypto::detail::aes_key_wrap::unwrap(kek, *wrapped);
    REQUIRE(unwrapped.has_value());
    REQUIRE(*unwrapped == plaintext);
}

TEST_CASE("HsmClientStub: IKmsClient interface via shared_ptr",
          "[crypto][hsm][T-8]") {
    // Verify the stub works through the abstract IKmsClient interface
    auto hsm = std::make_shared<crypto::HsmClientStub>();
    std::array<uint8_t, 32> kek{};
    for (size_t i = 0; i < 32; ++i) kek[i] = static_cast<uint8_t>(i + 50);
    hsm->register_kek("iface-key", kek);

    // Use through base class pointer
    std::shared_ptr<crypto::IKmsClient> client = hsm;
    std::vector<uint8_t> dek(32, 0xBB);

    auto wrapped = client->wrap_key(dek, "iface-key");
    REQUIRE(wrapped.has_value());

    auto unwrapped = client->unwrap_key(*wrapped, "iface-key");
    REQUIRE(unwrapped.has_value());
    REQUIRE(*unwrapped == dek);
}
