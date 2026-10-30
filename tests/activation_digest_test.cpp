// SPDX-License-Identifier: GPL-3.0-or-later
#include "activation_schema_support.hpp"
#include "test_tmp_root.hpp"
#include "activation/codec_detail.hpp"
#include "hash/sha256.hpp"
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <unistd.h>
using namespace d1_test;
namespace fs = std::filesystem;
namespace g = athena::generation;

namespace {
struct Fixture {
    fs::path root;
    Fixture() {
        root = athena::test::make_temporary_directory("athena-d1-document");
        fs::create_directory(root / "7");
    }
    ~Fixture() { std::error_code error; fs::remove_all(root, error); }
};
void write(const fs::path& path, std::string_view bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    require(static_cast<bool>(file), "Write fixture");
}
template<class F> void io_rejects(F action) {
    try { action(); }
    catch (const std::runtime_error& error) { require(*error.what(), "Missing I/O diagnostic"); return; }
    throw std::runtime_error("Unsafe generation input accepted");
}
template<class F> void generation_rejects(F action) {
    try { action(); }
    catch (const athena::core::TypeError& error) { require(*error.what(), "Missing generation diagnostic"); return; }
    throw std::runtime_error("Invalid generation document accepted");
}
void generation_digests() {
    const std::string document = "id = 7\nentries = []\n";
    const auto value = g::validate_generation_document(document, 7);
    require(value.bytes == document && value.generation.id == 7, "Generation bytes/ID changed");
    require(value.generation.compatibility == g::GenerationCompatibility::Historical, "Historical provenance rewritten");
    // Fixed vector calculated independently with Python hashlib and struct.pack('>Q').
    require(value.digest.value() == "sha256:317b0fc669a9b5df8f599d1571b4e928fbcfefe39786922c508391e13523bc81", "Generation known vector");
    require(g::validate_generation_document(document, 7).digest == value.digest, "Exact bytes unstable");
    require(g::validate_generation_document(document + "\n", 7).digest != value.digest, "Whitespace omitted from document identity");
    require(g::validate_generation_document(document + "# a\n", 7).digest !=
        g::validate_generation_document(document + "# b\n", 7).digest, "One-byte change omitted");
    require(athena::hash::sha256(document) != value.digest.value(), "Generation domain separation missing");
    std::string unframed("athena-generation-document-v1\0", sizeof("athena-generation-document-v1\0") - 1);
    unframed += document;
    require(athena::hash::sha256(unframed) != value.digest.value(), "Generation length framing missing");
    std::string wrong_domain("athena-generation-document-v1", sizeof("athena-generation-document-v1") - 1);
    athena::activation::codec_detail::append_u64(wrong_domain, document.size()); wrong_domain += document;
    require(athena::hash::sha256(wrong_domain) != value.digest.value(), "Generation domain NUL missing");
    const auto modern = g::serialize(g::make_generation(7, {}));
    require(g::validate_generation_document(modern, 7).bytes == modern, "Modern published bytes changed");
    const auto schema2 = g::validate_generation_document("schema_version = 2\nid = 7\nmemberships = []\n", 7);
    require(schema2.generation.compatibility == g::GenerationCompatibility::Historical, "Schema 2 provenance lost");
    rejects([&] { (void)g::validate_generation_document(document, 8); }, SchemaErrorCode::Reference);
    rejects([&] { (void)g::validate_generation_document(std::string(max_document_size + 1, ' '), 7); }, SchemaErrorCode::Bounds);
    rejects([&] { (void)g::validate_generation_document(document, max_generation + 1); }, SchemaErrorCode::Bounds);
    const auto at_bound = document + "#" + std::string(max_document_size - document.size() - 2, ' ') + "\n";
    require(g::validate_generation_document(at_bound, 7).bytes.size() == max_document_size, "Exact generation bound rejected");
    for (const auto& malformed : {"", "id = -1\n", "id = 7.0\n", "id = 7\nunknown = 1\n", "id = 7\nentries = [", "id = 7\nid = 7\n"})
        generation_rejects([&] { (void)g::validate_generation_document(malformed, 7); });
    for (const auto& digest : {std::string(""), digest('A'), digest('g'), std::string("sha256:aa"), digest('a') + "0"})
        rejects([&] { (void)g::GenerationDocumentDigest(digest); }, SchemaErrorCode::Identity);

    Fixture fixture;
    const auto metadata = fixture.root / "7/generation.toml";
    io_rejects([&] { (void)g::read_generation_document(fixture.root, 7); });
    write(metadata, document);
    const auto loaded = g::read_generation_document(fixture.root, 7);
    require(loaded.bytes == document && loaded.digest == value.digest, "Validated read hashed different bytes");
    const std::string raw_component = "authority-\x80\xff \t\n";
    const auto byte_authority = fixture.root / fs::path(raw_component);
    require(byte_authority.filename().native() == raw_component, "Native filesystem path bytes changed");
    fs::create_directories(byte_authority / "7");
    write(byte_authority / "7/generation.toml", document);
    require(g::read_generation_document(byte_authority, 7).digest == value.digest,
        "Non-UTF-8 filesystem authority read changed document identity");
    auto byte_intent = intent();
    byte_intent.generation_authority = byte_authority.native();
    require(parse_activation_intent(serialize(byte_intent)) == byte_intent,
        "Native filesystem locator bytes changed in intent codec");
    std::ifstream after(metadata, std::ios::binary);
    require(std::string(std::istreambuf_iterator<char>(after), {}) == document, "Reader rewrote metadata");
    fs::remove(metadata); fs::create_symlink("../other.toml", metadata);
    io_rejects([&] { (void)g::read_generation_document(fixture.root, 7); });
    fs::remove(metadata); fs::create_directory(metadata);
    io_rejects([&] { (void)g::read_generation_document(fixture.root, 7); });
    fs::remove(metadata);
    write(metadata, document);
    const int sparse = ::open(metadata.c_str(), O_WRONLY | O_CLOEXEC);
    require(sparse >= 0 && ::ftruncate(sparse, max_document_size + 1) == 0, "Oversize fixture"); ::close(sparse);
    rejects([&] { (void)g::read_generation_document(fixture.root, 7); }, SchemaErrorCode::Bounds);
    write(metadata, document);
    bool substituted = false;
    const g::GenerationRead substitute = [&](int fd, void* data, std::size_t size) -> ssize_t {
        if (!substituted) {
            substituted = true;
            fs::rename(metadata, fixture.root / "old.toml");
            write(metadata, document); // Identical bytes, different named inode.
        }
        return ::read(fd, data, size);
    };
    rejects([&] { (void)g::read_generation_document(fixture.root, 7, substitute); }, SchemaErrorCode::Metadata);
    require(substituted, "Substitution seam not executed");
    bool changed = false;
    const g::GenerationRead in_place = [&](int fd, void* data, std::size_t size) -> ssize_t {
        if (!changed) { changed = true; write(metadata, document + "\n"); }
        return ::read(fd, data, size);
    };
    rejects([&] { (void)g::read_generation_document(fixture.root, 7, in_place); }, SchemaErrorCode::Metadata);
    write(metadata, document);
    bool replaced_directory = false;
    const g::GenerationRead substitute_directory = [&](int fd, void* data, std::size_t size) -> ssize_t {
        if (!replaced_directory) {
            replaced_directory = true;
            fs::rename(fixture.root / "7", fixture.root / "old-dir");
            fs::create_directory(fixture.root / "7"); write(metadata, document);
        }
        return ::read(fd, data, size);
    };
    rejects([&] { (void)g::read_generation_document(fixture.root, 7, substitute_directory); }, SchemaErrorCode::Metadata);
    bool interrupted = false;
    const g::GenerationRead short_reads = [&](int fd, void* data, std::size_t size) -> ssize_t {
        if (!interrupted) { interrupted = true; errno = EINTR; return -1; }
        return ::read(fd, data, std::min(size, std::size_t(1)));
    };
    require(g::read_generation_document(fixture.root, 7, short_reads).digest == value.digest, "Short read/EINTR changed digest");
    fs::remove_all(fixture.root / "7"); fs::create_symlink("old-dir", fixture.root / "7");
    io_rejects([&] { (void)g::read_generation_document(fixture.root, 7); });
}
void status_and_plan_digest() {
    const auto intent_value = intent(); const auto plan_value = plan();
    const auto intent_bytes = serialize(intent_value), plan_bytes = serialize(plan_value);
    require(intent_bytes.size() == 413 && plan_bytes.size() == 404, "Known vector document bytes changed");
    const auto value = derive_plan_digest(intent_value, plan_value);
    require(value.value() == "sha256:bcc87f0c02669ca8177a24e9647a61c00f8669dda7fec1692aa459422d7f161e", "Plan known vector");
    require(digest_plan_documents(intent_bytes, plan_bytes) == value, "Canonical document digest mismatch");
    auto changed_intent = intent_value; changed_intent.target_generation = 43;
    require(derive_plan_digest(changed_intent, plan_value) != value, "Intent excluded from digest");
    auto changed_plan = plan_value; changed_plan.operations[1].path = "bin/other";
    require(derive_plan_digest(intent_value, changed_plan) != value, "Activation excluded from digest");
    changed_plan.activation_id = old_id;
    rejects([&] { (void)derive_plan_digest(intent_value, changed_plan); }, SchemaErrorCode::Reference);
    rejects([&] { (void)digest_plan_documents(intent_bytes + "\n", plan_bytes); }, SchemaErrorCode::Noncanonical);
    const auto raw = athena::hash::sha256(intent_bytes + plan_bytes);
    require(raw != value.value(), "Plan domain/length framing missing");
    auto old_plan = plan_value;
    old_plan.operations = {{0, "old", RemoveLink{0, {adoption_id, 0}}}};
    rejects([&] { (void)derive_plan_digest(intent_value, old_plan); }, SchemaErrorCode::Reference);
    auto adopted_intent = intent_value;
    adopted_intent.base = SelectedBase{HistoricalCurrent{41, "41\n"}, g::GenerationDocumentDigest(digest('c')), AdoptionOwnership{adoption_id}};
    require(sha256_text(derive_plan_digest(adopted_intent, old_plan).value()), "Matching adoption namespace rejected");
    std::get<SelectedBase>(adopted_intent.base).ownership = EmptyOwnership{};
    rejects([&] { (void)derive_plan_digest(adopted_intent, old_plan); }, SchemaErrorCode::Reference);
    const ActivationStatus complete{id, value, TerminalOutcome::Complete}, aborted{id, value, TerminalOutcome::Aborted};
    for (const auto& status : {complete, aborted}) {
        const auto bytes = serialize(status);
        require(parse_activation_status(bytes, id) == status, "Terminal status roundtrip");
        require(serialize(parse_activation_status(bytes)) == bytes, "Terminal bytes changed");
        rejects([&] { (void)parse_activation_status(bytes, old_id); }, SchemaErrorCode::Reference);
        rejects([&] { (void)parse_activation_status(replace(bytes, value.value(), digest('A'))); }, SchemaErrorCode::Identity);
        rejects([&] { (void)parse_activation_status(replace(bytes, id.value(), "bad")); }, SchemaErrorCode::Identity);
    }
    require(serialize(complete) != serialize(aborted), "Outcomes serialized identically");
    require(derive_plan_digest(intent_value, plan_value) == value, "Status affected immutable plan digest");
    require(serialize(complete) == "schema_version = 1\nactivation_id = \"" + id.value() + "\"\nplan_digest = \"" + value.value() + "\"\noutcome = \"complete\"\n", "Status canonical key order");
    rejects([&] { (void)parse_activation_status(replace(serialize(complete), "complete", "projecting")); }, SchemaErrorCode::Schema);
    rejects([&] { (void)parse_activation_status(replace(serialize(complete), "outcome = \"complete\"", "outcome = true")); }, SchemaErrorCode::Schema);
    rejects([&] { (void)serialize(ActivationStatus{id, value, static_cast<TerminalOutcome>(99)}); }, SchemaErrorCode::Schema);
}
}
int main() { return run([] { generation_digests(); status_and_plan_digest(); }); }
