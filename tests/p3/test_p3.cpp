// tests/p3/test_p3.cpp — YOUR P3 TEST SUITE.
//
// The five tests below are worked examples. Keep them, and add your own
// until you have AT LEAST 15 (spec §6 lists what must be covered).
// Name every test <area>_<what>: reg_ for the registry, ws_ for the
// workspace, calc_ for the calculator.
//
// Build & run:   cmake --build build && ./build/test_p3
// One area:      ./build/test_p3 reg_        (names starting with "reg_")
// One test:      ./build/test_p3 =reg_add_and_find

#include "../mini_test.h"
#include "../test_helpers.h"     // TempDir, write_text, args1, ...

#include "tools/calculator.h"
#include "tools/file_tools.h"
#include "tools/tool_registry.h"
#include "tools/workspace.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

// A tool whose name you choose — use it to fill the registry in tests.
class NamedTool : public Tool {
public:
    explicit NamedTool(std::string n) : n_(std::move(n)), schema_(JsonValue::object()) {}
    std::string name() const override { return n_; }
    std::string description() const override { return "test tool"; }
    const JsonValue& schema() const override { return schema_; }
    ToolResult invoke(const JsonValue&, ToolContext&) override { return ToolResult::success(n_); }
private:
    std::string n_;
    JsonValue   schema_;   // an empty JSON object: {}
};

// Tokenize with the provided tokenizer, then run YOUR calc_evaluate.
bool eval(const std::string& expr, double* value, std::string* error) {
    std::vector<CalcToken> tokens;
    if (!calc_tokenize(expr, &tokens, error)) return false;
    return calc_evaluate(tokens, value, error);
}

}  // namespace

// ---------------------------------------------------------------- examples

TEST(reg_add_and_find) {
    ToolRegistry r;
    auto t = std::make_unique<NamedTool>("alpha");
    Tool* raw = t.get();                        // remember the address before moving
    REQUIRE(r.add(std::move(t)));
    CHECK(r.size() == 1);
    CHECK(r.find("alpha") == raw);              // same object, not a copy
    CHECK(r.find("beta") == nullptr);
}

TEST(reg_duplicate_keeps_original) {
    ToolRegistry r;
    auto first = std::make_unique<NamedTool>("alpha");
    Tool* original = first.get();
    REQUIRE(r.add(std::move(first)));
    CHECK(!r.add(std::make_unique<NamedTool>("alpha")));
    CHECK(r.find("alpha") == original);
    CHECK(r.size() == 1);
}

TEST(ws_rejects_parent_escape) {
    TempDir d;
    Workspace ws(d.path());
    fs::path out;
    std::string reason;
    REQUIRE(ws.resolve("notes/todo.txt", &out, &reason));   // sanity: accepts *something*
    CHECK(out == ws.root() / "notes" / "todo.txt");
    CHECK(!ws.resolve("../etc/passwd", &out, &reason));
    CHECK(!reason.empty());
}

TEST(calc_precedence) {
    double v = 0;
    std::string err;
    REQUIRE(eval("2 + 3 * 4", &v, &err));
    CHECK(v == 14);
}

TEST(calc_division_by_zero) {
    double v = 0;
    std::string err;
    CHECK(!eval("1 / 0", &v, &err));
    CHECK(err == "division by zero");
}

// -------------------------------------------------------- workspace coverage

TEST(ws_accepts_relative_and_nonexistent) {
    TempDir d;
    Workspace ws(d.path());
    const std::vector<std::pair<std::string, std::string>> paths = {
        {"notes/todo.txt", "notes/todo.txt"},
        {"./notes//todo.txt", "notes/todo.txt"},
        {"new/dir/file.txt", "new/dir/file.txt"},
    };
    REQUIRE(!fs::exists(d.path() / "new"));
    for (const auto& entry : paths) {
        fs::path out;
        std::string reason;
        REQUIRE(ws.resolve(entry.first, &out, &reason));
        CHECK(out == ws.root() / entry.second);
    }
    CHECK(!fs::exists(d.path() / "new"));  // resolve creates nothing.
    CHECK(fs::is_empty(d.path()));
}

TEST(ws_normalizes_dot_and_parent_components) {
    TempDir d;
    Workspace ws(d.path());
    const std::vector<std::pair<std::string, fs::path>> paths = {
        {".", ws.root()},
        {"a/..", ws.root()},
        {"a/../b", ws.root() / "b"},
        {"my..notes.txt", ws.root() / "my..notes.txt"},
    };
    for (const auto& entry : paths) {
        fs::path out;
        std::string reason;
        REQUIRE(ws.resolve(entry.first, &out, &reason));
        CHECK(out == entry.second);
    }
}

TEST(ws_rejects_empty_and_embedded_nul) {
    TempDir d;
    Workspace ws(d.path());
    fs::path out;
    std::string reason;
    CHECK(!ws.resolve("", &out, &reason));
    CHECK(reason == "empty path");

    const std::string with_nul = std::string("notes") + '\0' + "/x";
    reason.clear();
    CHECK(!ws.resolve(std::string_view(with_nul.data(), with_nul.size()),
                      &out, &reason));
    CHECK(!reason.empty());
}

TEST(ws_rejects_absolute_paths) {
    TempDir d;
    Workspace ws(d.path());
    fs::path out;
    std::string reason;
    CHECK(!ws.resolve("/etc/passwd", &out, &reason));
    CHECK(!reason.empty());
}

TEST(ws_rejects_normalized_parent_escape) {
    TempDir d;
    fs::create_directory(d.path() / "ws");
    Workspace ws(d.path() / "ws");
    const std::vector<std::string> paths = {
        "..", "../etc/passwd", "a/../../b",
        "notes/../../ws/notes/todo.txt",
    };
    for (const std::string& path : paths) {
        fs::path out;
        std::string reason;
        CHECK(!ws.resolve(path, &out, &reason));
        CHECK(!reason.empty());
    }
}

TEST(ws_file_tools_valid_round_trip) {
    TempDir d;
    ToolContext ctx{Workspace(d.path())};
    WriteFileTool writer;
    ReadFileTool reader;
    ListDirTool listing;

    JsonValue write_args = JsonValue::object();
    write_args.set("path", JsonValue::string("./notes//new.txt"));
    write_args.set("content", JsonValue::string("hello workspace"));
    const ToolResult written = writer.invoke(write_args, ctx);
    REQUIRE(written.ok);
    CHECK(read_text(d.path() / "notes" / "new.txt") == "hello workspace");

    const ToolResult read = reader.invoke(
        args1("path", JsonValue::string("notes/new.txt")), ctx);
    REQUIRE(read.ok);
    CHECK(read.content == "hello workspace");

    const ToolResult listed = listing.invoke(
        args1("path", JsonValue::string("notes")), ctx);
    REQUIRE(listed.ok);
    CHECK(listed.content == "new.txt\t15");
}

TEST(ws_file_tools_reject_traversal) {
    TempDir d;
    ToolContext ctx{Workspace(d.path())};
    ReadFileTool reader;
    WriteFileTool writer;
    ListDirTool listing;

    const ToolResult read = reader.invoke(
        args1("path", JsonValue::string("../escape.txt")), ctx);
    CHECK(!read.ok);
    CHECK(!read.error.empty());

    JsonValue write_args = JsonValue::object();
    write_args.set("path", JsonValue::string("notes/../../escape.txt"));
    write_args.set("content", JsonValue::string("should not be written"));
    const ToolResult written = writer.invoke(write_args, ctx);
    CHECK(!written.ok);
    CHECK(!written.error.empty());

    const ToolResult listed = listing.invoke(
        args1("path", JsonValue::string("..")), ctx);
    CHECK(!listed.ok);
    CHECK(!listed.error.empty());
    CHECK(fs::is_empty(d.path()));
}

TEST(ws_provided_symlink_guard_blocks_escapes) {
    TempDir d;
    TempDir outside;
    write_text(outside.path() / "secret.txt", "test fixture");
    fs::create_directory_symlink(outside.path(), d.path() / "external");
    fs::create_symlink(outside.path() / "missing.txt", d.path() / "dangling");
    ToolContext ctx{Workspace(d.path())};

    for (const char* name : {"external/secret.txt", "dangling"}) {
        fs::path resolved;
        std::string reason;
        REQUIRE(ctx.workspace.resolve(name, &resolved, &reason));
        reason.clear();
        CHECK(!ctx.workspace.check_symlinks(resolved, &reason));
        CHECK(!reason.empty());
    }

    ReadFileTool reader;
    const ToolResult read = reader.invoke(
        args1("path", JsonValue::string("external/secret.txt")), ctx);
    CHECK(!read.ok);
    CHECK(!read.error.empty());

    WriteFileTool writer;
    JsonValue write_args = JsonValue::object();
    write_args.set("path", JsonValue::string("dangling"));
    write_args.set("content", JsonValue::string("not written"));
    const ToolResult written = writer.invoke(write_args, ctx);
    CHECK(!written.ok);
    CHECK(!written.error.empty());
    CHECK(!fs::exists(outside.path() / "missing.txt"));
}

// --------------------------------------------------------- calculator coverage

TEST(calc_left_to_right_associativity) {
    double value = 0;
    std::string error;
    REQUIRE(eval("10 - 4 - 3", &value, &error));
    CHECK(value == 3);
    REQUIRE(eval("100 / 10 / 5", &value, &error));
    CHECK(value == 2);
}

TEST(calc_parentheses_and_mixed_precedence) {
    double value = 0;
    std::string error;
    REQUIRE(eval("(2 + 3) * 4", &value, &error));
    CHECK(value == 20);
    REQUIRE(eval("2 * (3 + 4) - 5", &value, &error));
    CHECK(value == 9);
    REQUIRE(eval("18 / (2 + 1) - 4 * 0.5", &value, &error));
    CHECK(value == 4);  // Additional expression with grouped division.
}

TEST(calc_negative_numbers_and_fractions) {
    double value = 0;
    std::string error;
    REQUIRE(eval("-3 * -3", &value, &error));
    CHECK(value == 9);
    REQUIRE(eval("7 / 2", &value, &error));
    CHECK(value == 3.5);
    REQUIRE(eval("2 * -3 + 0.5", &value, &error));
    CHECK(value == -5.5);  // Additional expression with a signed literal.
}

TEST(calc_division_by_computed_zero) {
    double value = 0;
    std::string error;
    CHECK(!eval("1 / (2 - 2)", &value, &error));
    CHECK(error == "division by zero");
    error.clear();
    CHECK(!eval("1 / -0", &value, &error));
    CHECK(error == "division by zero");
}

TEST(calc_twenty_thousand_addition_terms) {
    std::string expression = "1";
    for (int i = 1; i < 20000; ++i) expression += "+1";
    double value = 0;
    std::string error;
    REQUIRE(eval(expression, &value, &error));
    CHECK(value == 20000);
}

TEST(calc_five_thousand_nested_parentheses) {
    const std::string expression =
        std::string(5000, '(') + "7" + std::string(5000, ')');
    double value = 0;
    std::string error;
    REQUIRE(eval(expression, &value, &error));
    CHECK(value == 7);
}

TEST(calc_malformed_stacks_return_errors) {
    using Kind = CalcToken::Kind;
    const std::vector<std::vector<CalcToken>> malformed = {
        {},
        {{Kind::Plus, 0.0}},
        {{Kind::Number, 1.0}, {Kind::Plus, 0.0}},
        {{Kind::RParen, 0.0}},
        {{Kind::LParen, 0.0}, {Kind::Number, 1.0}},
        {{Kind::Number, 1.0}, {Kind::Number, 2.0}},
        {{Kind::Number, 1.0}, {Kind::Plus, 0.0}, {Kind::RParen, 0.0}},
    };
    for (const auto& tokens : malformed) {
        double value = 0;
        std::string error;
        CHECK(!calc_evaluate(tokens, &value, &error));
        CHECK(!error.empty());
    }
    std::string error;
    const std::vector<CalcToken> valid = {{Kind::Number, 7.0}};
    CHECK(!calc_evaluate(valid, nullptr, &error));
    CHECK(!error.empty());
}

// -------------------------------------------------- final registry coverage

TEST(reg_empty_null_and_instrumentation) {
    ToolRegistry r;
    CHECK(r.size() == 0);
    CHECK(r.bucket_count() >= 1);
    CHECK(r.bucket_count() <= 64);
    CHECK(r.load_factor() == 0.0);
    CHECK(r.max_probe_length() == 0);
    CHECK(r.find("absent") == nullptr);
    CHECK(!r.add(nullptr));
    CHECK(r.size() == 0);
    CHECK(r.max_probe_length() == 0);

    int visits = 0;
    r.for_each([&](const Tool&) { ++visits; });
    CHECK(visits == 0);
}

TEST(reg_for_each_visits_each_and_owns_names) {
    ToolRegistry r;
    const std::vector<std::string> expected = {
        "first", "second", std::string("long_") + std::string(200, 'q')
    };
    for (const std::string& name : expected) {
        REQUIRE(r.add(std::make_unique<NamedTool>(name)));
    }
    CHECK(r.size() == expected.size());
    CHECK(r.load_factor() ==
          static_cast<double>(r.size()) / r.bucket_count());
    CHECK(r.max_probe_length() >= 1);

    std::vector<std::string> observed;
    r.for_each([&](const Tool& tool) { observed.push_back(tool.name()); });
    std::sort(observed.begin(), observed.end());
    auto sorted_expected = expected;
    std::sort(sorted_expected.begin(), sorted_expected.end());
    CHECK(observed == sorted_expected);  // Exactly once, in any iteration order.
    for (const auto& name : expected) CHECK(r.find(name) != nullptr);
}

TEST(reg_ownership_destroys_each_tool_once) {
    class CountedTool : public NamedTool {
    public:
        CountedTool(std::string n, int* destroyed)
            : NamedTool(std::move(n)), destroyed_(destroyed) {}
        ~CountedTool() override { ++*destroyed_; }
    private:
        int* destroyed_;
    };

    int destroyed = 0;
    {
        ToolRegistry r;
        REQUIRE(r.add(std::make_unique<CountedTool>("first", &destroyed)));
        Tool* first = r.find("first");
        REQUIRE(first != nullptr);
        CHECK(!r.add(std::make_unique<CountedTool>("first", &destroyed)));
        CHECK(destroyed == 1);  // Duplicate destroyed, original remains owned.
        CHECK(r.find("first") == first);
        CHECK(r.size() == 1);
        REQUIRE(r.add(std::make_unique<CountedTool>("second", &destroyed)));
        REQUIRE(r.add(std::make_unique<CountedTool>("third", &destroyed)));
        CHECK(r.size() == 3);
    }
    CHECK(destroyed == 4);  // Three stored tools and the rejected duplicate.
}

TEST(reg_explicit_collision_chaining) {
    ToolRegistry r;
    const std::size_t initial_buckets = r.bucket_count();
    const std::size_t target = ToolRegistry::hash("collision_seed") % initial_buckets;
    std::vector<std::string> keys;
    for (int i = 0; i < 100000 && keys.size() < 8; ++i) {
        std::string key = "collision_" + std::to_string(i);
        if (ToolRegistry::hash(key) % initial_buckets == target)
            keys.push_back(std::move(key));
    }
    REQUIRE(keys.size() == 8);
    for (const auto& key : keys) REQUIRE(r.add(std::make_unique<NamedTool>(key)));
    CHECK(r.bucket_count() == initial_buckets);  // No resize hides collision.
    CHECK(r.max_probe_length() == 8);
    for (const auto& key : keys) CHECK(r.find(key) != nullptr);
    CHECK(r.find("missing") == nullptr);
}

TEST(reg_10000_tools_and_weak_hash_comparison) {
    ToolRegistry r;
    std::vector<Tool*> original_pointers;
    std::vector<std::size_t> full_hashes;
    original_pointers.reserve(10000);
    full_hashes.reserve(10000);
    Tool* first = nullptr;

    for (int i = 0; i < 10000; ++i) {
        const std::string name = "tool_" + std::to_string(i);
        auto tool = std::make_unique<NamedTool>(name);
        Tool* raw = tool.get();
        if (i == 0) first = raw;
        REQUIRE(r.add(std::move(tool)));
        original_pointers.push_back(raw);
        full_hashes.push_back(ToolRegistry::hash(name));
        CHECK(r.load_factor() <= 1.0);  // Required after every insertion.
    }

    CHECK(r.size() == 10000);
    CHECK(r.find("tool_0") == first);
    for (int i = 0; i < 10000; ++i) {
        CHECK(r.find("tool_" + std::to_string(i)) == original_pointers[i]);
    }
    CHECK(r.max_probe_length() <= 64);
    CHECK(ToolRegistry::hash("ab") != ToolRegistry::hash("ba"));
    std::sort(full_hashes.begin(), full_hashes.end());
    full_hashes.erase(std::unique(full_hashes.begin(), full_hashes.end()),
                      full_hashes.end());
    CHECK(full_hashes.size() >= 9990);

    // Compare the specification's weak s[0] + s.size() using the SAME buckets.
    std::vector<std::size_t> weak_counts(r.bucket_count(), 0);
    std::size_t weak_longest = 0;
    for (int i = 0; i < 10000; ++i) {
        const std::string name = "tool_" + std::to_string(i);
        const std::size_t index =
            (static_cast<unsigned char>(name[0]) + name.size()) % r.bucket_count();
        weak_longest = std::max(weak_longest, ++weak_counts[index]);
    }
    CHECK(r.max_probe_length() < weak_longest);
    std::printf("registry 10000: buckets=%zu load=%.10f max_chain=%zu "
                "weak_max_chain=%zu distinct_hashes=%zu\n",
                r.bucket_count(), r.load_factor(), r.max_probe_length(),
                weak_longest, full_hashes.size());
}

int main(int argc, char** argv) { return mt::run_all(argc, argv); }
