#define SLIMR_TEST_MAIN
#include "test.hpp"
#include "app.hpp"

#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using namespace obsidian;

struct TestEnv {
    std::string name;
    std::optional<std::string> previous;

    TestEnv(const TestEnv&) = delete;
    TestEnv& operator=(const TestEnv&) = delete;

    TestEnv(std::string key, const char* value) : name(std::move(key)) {
        if (const char* old = std::getenv(name.c_str())) previous = old;
        if (value) setenv(name.c_str(), value, 1);
        else unsetenv(name.c_str());
    }

    ~TestEnv() {
        if (previous) setenv(name.c_str(), previous->c_str(), 1);
        else unsetenv(name.c_str());
    }
};

struct TestVault {
    std::filesystem::path root;
    std::optional<std::string> previous_env;

    TestVault(const TestVault&) = delete;
    TestVault& operator=(const TestVault&) = delete;

    explicit TestVault(const std::map<std::string, std::string>& files) {
        static uint64_t counter = 0;
        counter++;
        auto tmp_base = std::filesystem::temp_directory_path() / (
            "obsidian-vault-" + std::to_string(getpid()) + "_" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
            std::to_string(counter)
        );
        std::filesystem::create_directories(tmp_base);
        root = tmp_base;

        for (const auto& [rel, text] : files) {
            auto abs = root / rel;
            std::filesystem::create_directories(abs.parent_path());
            std::ofstream out(abs, std::ios::binary);
            out.write(text.data(), text.size());
        }

        const char* prev = std::getenv("OBSIDIAN_VAULT");
        if (prev) {
            previous_env = std::string(prev);
        } else {
            previous_env = std::nullopt;
        }
        setenv("OBSIDIAN_VAULT", root.c_str(), 1);
    }

    std::string read(const std::string& rel) const {
        auto abs = root / rel;
        std::ifstream in(abs, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    void remove() {
        if (previous_env.has_value()) {
            setenv("OBSIDIAN_VAULT", previous_env->c_str(), 1);
        } else {
            unsetenv("OBSIDIAN_VAULT");
        }
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }

    ~TestVault() {
        remove();
    }
};

// ============================================================================
// 1. Vault Path & Access Tests
// ============================================================================

TEST_SUITE("vaultPath") {
    TEST_CASE("refuses paths outside the vault") {
        TestVault vault({{"A.md", "a"}});
        CHECK_THROWS_WITH_AS(vault_path("../escape.md"), doctest::Contains("outside the vault"), std::runtime_error);
        CHECK_THROWS_WITH_AS(vault_path("People/../../escape.md"), doctest::Contains("outside the vault"), std::runtime_error);
        CHECK_THROWS_WITH_AS(vault_path("/etc/passwd"), doctest::Contains("absolute"), std::runtime_error);
        CHECK_THROWS_WITH_AS(vault_path("~/x.md"), doctest::Contains("absolute"), std::runtime_error);
        CHECK_THROWS_WITH_AS(vault_path(".obsidian/app.json"), doctest::Contains("hidden folder"), std::runtime_error);
    }

    TEST_CASE("refuses a symlink that leaves the vault") {
        TestVault vault({{"A.md", "a"}});
        auto outside = std::filesystem::temp_directory_path() / ("obsidian-outside-" + std::to_string(getpid()) + "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(outside);
        std::filesystem::create_directory_symlink(outside, vault.root / "Link");

        CHECK_THROWS_WITH_AS(vault_path("Link/x.md"), doctest::Contains("outside the vault"), std::runtime_error);
        std::error_code ec;
        std::filesystem::remove_all(outside, ec);
    }

    TEST_CASE("allows nested new paths inside the vault") {
        TestVault vault({{"A.md", "a"}});
        CHECK(vault_path("New/Deep/B.md") == (vault.root / "New/Deep/B.md").string());
    }
}

TEST_SUITE("notePaths") {
    TEST_CASE("lists markdown notes and skips dot folders and attachments") {
        TestVault vault({
            {"A.md", ""},
            {"People/B.md", ""},
            {".obsidian/C.md", ""},
            {".trash/D.md", ""},
            {"Places/photo.png", ""}
        });
        auto allNotes = note_paths();
        std::vector<std::string> expected = {"A.md", "People/B.md"};
        CHECK(allNotes == expected);

        auto nonRec = note_paths("", false);
        std::vector<std::string> expectedNonRec = {"A.md"};
        CHECK(nonRec == expectedNonRec);
    }
}

TEST_SUITE("noteResolve") {
    TEST_CASE("resolves by exact path, file name, then alias, ignoring case") {
        TestVault vault({
            {"People/Jane Doe.md", "---\naliases: [\"Nana\", \"JD\"]\n---\nbody"},
            {"Places/Cafe - Town.md", "---\naliases:\n  - Coffee Spot\n---\n"},
            {"Jane.md", ""}
        });

        CHECK(note_resolve("People/Jane Doe.md") == "People/Jane Doe.md");
        CHECK(note_resolve("people/jane doe") == "People/Jane Doe.md");
        CHECK(note_resolve("Jane Doe") == "People/Jane Doe.md");
        CHECK(note_resolve("[[Jane Doe]]") == "People/Jane Doe.md");
        CHECK(note_resolve("nana") == "People/Jane Doe.md");
        CHECK(note_resolve("Coffee Spot") == "Places/Cafe - Town.md");
        CHECK(note_resolve("Jane") == "Jane.md");
        CHECK_THROWS_WITH_AS(note_resolve("Nobody"), doctest::Contains("No note matches"), std::runtime_error);
    }

    TEST_CASE("reports ambiguous names") {
        TestVault vault({{"A/Same.md", ""}, {"B/Same.md", ""}});
        CHECK_THROWS_WITH_AS(note_resolve("Same"), doctest::Contains("matches 2 notes"), std::runtime_error);
        CHECK(note_resolve("B/Same") == "B/Same.md");
    }
}

TEST_SUITE("frontMatterParse") {
    TEST_CASE("parses front matter and finds the body line") {
        auto parsed = front_matter_parse("---\ntags: [person]\ncircle: family\n---\nline one\n");
        CHECK(parsed.frontMatter["circle"].get<std::string>() == "family");
        CHECK(parsed.frontMatter["tags"].get<Json::array_t>().size() == 1);
        CHECK(parsed.frontMatter["tags"].get<Json::array_t>()[0].get<std::string>() == "person");
        CHECK(parsed.body == "line one\n");
        CHECK(parsed.bodyLine == 5);
    }

    TEST_CASE("reports bad YAML without throwing") {
        auto parsed = front_matter_parse("---\nkey: [unclosed\n---\nbody");
        CHECK(parsed.frontMatter.empty());
        CHECK(parsed.frontMatterError.has_value());
    }
}

TEST_SUITE("vaultWrite / vaultPathMutable") {
    TEST_CASE("writes a new note, creating folders") {
        TestVault vault({{"A.md", "a"}});
        vault_write("New/Deep/B.md", "hello");
        CHECK(vault.read("New/Deep/B.md") == "hello");
        CHECK(vault_path_mutable("New/Deep/C.md") == (vault.root / "New/Deep/C.md").string());
    }

    TEST_CASE("refuses a symlinked note, even one pointing inside the vault") {
        TestVault vault({{"A.md", "a"}});
        auto outside = std::filesystem::temp_directory_path() / ("obsidian-outside-" + std::to_string(getpid()) + "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(outside);
        std::string targetFile = (outside / "target.md").string();
        {
            std::ofstream f(targetFile);
            f << "secret";
        }
        std::filesystem::create_symlink(targetFile, vault.root / "Out.md");
        std::filesystem::create_symlink(vault.root / "A.md", vault.root / "In.md");

        CHECK_THROWS_WITH_AS(vault_write("Out.md", "x"), doctest::Contains("symlink"), std::runtime_error);
        CHECK_THROWS_WITH_AS(vault_write("In.md", "x"), doctest::Contains("symlink"), std::runtime_error);

        std::ifstream in(targetFile);
        std::string s;
        in >> s;
        CHECK(s == "secret");
        CHECK(vault.read("A.md") == "a");

        std::error_code ec;
        std::filesystem::remove_all(outside, ec);
    }

    TEST_CASE("refuses a symlinked folder component inside the vault") {
        TestVault vault({{"Real/N.md", "n"}});
        std::filesystem::create_directory_symlink(vault.root / "Real", vault.root / "Alias");

        CHECK_THROWS_WITH_AS(vault_write("Alias/N.md", "x"), doctest::Contains("symlink"), std::runtime_error);
        CHECK_THROWS_WITH_AS(vault_write("Alias/New.md", "x"), doctest::Contains("symlink"), std::runtime_error);
        CHECK(vault.read("Real/N.md") == "n");
    }

    TEST_CASE("refuses paths that escape the vault") {
        TestVault vault({{"A.md", "a"}});
        auto outside = std::filesystem::temp_directory_path() / ("obsidian-outside-" + std::to_string(getpid()) + "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(outside / "d");
        std::filesystem::create_directory_symlink(outside, vault.root / "Link");

        CHECK_THROWS_WITH_AS(vault_write("../escape.md", "x"), doctest::Contains("outside the vault"), std::runtime_error);
        CHECK_THROWS_WITH_AS(vault_write("Link/d/x.md", "x"), doctest::Contains("symlink"), std::runtime_error);

        std::error_code ec;
        std::filesystem::remove_all(outside, ec);
    }
}

// ============================================================================
// 2. Commands Tests
// ============================================================================

TEST_SUITE("read command") {
    TEST_CASE("returns content and front matter for a note found by alias") {
        std::string text = "---\ntags: [person]\naliases: [\"Nana\"]\n---\nFacts\n";
        TestVault vault({{"People/Geralyn Saveri.md", text}});

        ReadOutput out = read_note("Nana");
        CHECK(out.content.get() == text);
        CHECK(out.path.get() == "People/Geralyn Saveri.md");
        CHECK(out.title.get() == "Geralyn Saveri");
        CHECK(out.frontMatter.get().contains("tags"));
        CHECK(out.frontMatter.get().contains("aliases"));
    }

    TEST_CASE("refuses paths outside the vault") {
        TestVault vault({});
        CHECK_THROWS_WITH_AS(read_note("../../etc/passwd"), doctest::Contains("outside the vault"), std::runtime_error);
    }
}

TEST_SUITE("write command") {
    TEST_CASE("creates notes and folders, adding .md") {
        TestVault vault({});
        WriteOutput out = write_note("Places/New Spot", "hi");
        CHECK(out.bytes.get() == 2);
        CHECK(out.created.get() == true);
        CHECK(out.path.get() == "Places/New Spot.md");
        CHECK(vault.read("Places/New Spot.md") == "hi");
    }

    TEST_CASE("refuses to overwrite unless overwrite is true") {
        TestVault vault({{"A.md", "old"}});
        CHECK_THROWS_WITH_AS(write_note("A.md", "new"), doctest::Contains("already exists"), std::runtime_error);
        CHECK_THROWS_WITH_AS(write_note("a", "new"), doctest::Contains("already exists"), std::runtime_error);
        CHECK(vault.read("A.md") == "old");

        WriteOutput out = write_note("a.md", "new", true);
        CHECK(out.bytes.get() == 3);
        CHECK(out.created.get() == false);
        CHECK(out.path.get() == "A.md");
        CHECK(vault.read("A.md") == "new");
    }

    TEST_CASE("refuses paths outside the vault or in hidden folders") {
        TestVault vault({});
        CHECK_THROWS_WITH_AS(write_note("../escape.md", "x"), doctest::Contains("outside the vault"), std::runtime_error);
        CHECK_THROWS_WITH_AS(write_note("/tmp/escape.md", "x"), doctest::Contains("absolute"), std::runtime_error);
        CHECK_THROWS_WITH_AS(write_note(".obsidian/x.md", "x"), doctest::Contains("hidden folder"), std::runtime_error);
        CHECK(!std::filesystem::exists(vault.root.parent_path() / "escape.md"));
    }
}

TEST_SUITE("append command") {
    const std::string NOTE = "---\naliases: [Spot]\n---\n# Spot\n\n## Contacts\n- Anna\n\n## Services\n- Power\n\n### Sub\n- x\n";

    TEST_CASE("appends to the end of a note found by alias") {
        TestVault vault({{"Places/Spot - Town.md", "line\n\n\n"}});
        AppendOutput out = append_note("spot - town", "- new");
        CHECK(out.line.get() == 2);
        CHECK(out.path.get() == "Places/Spot - Town.md");
        CHECK(vault.read("Places/Spot - Town.md") == "line\n- new\n");
    }

    TEST_CASE("appends at the end of a heading's section, before the next heading") {
        TestVault vault({{"Spot.md", NOTE}});
        AppendOutput out1 = append_note("Spot", "- Bob\n", "contacts");
        CHECK(out1.line.get() == 8);

        append_note("Spot", "- Water", "## Services");
        CHECK(vault.read("Spot.md") ==
            "---\naliases: [Spot]\n---\n# Spot\n\n## Contacts\n- Anna\n- Bob\n\n## Services\n- Power\n\n### Sub\n- x\n- Water\n"
        );
    }

    TEST_CASE("refuses a missing heading and lists the real ones") {
        TestVault vault({{"Spot.md", NOTE}});
        CHECK_THROWS_WITH_AS(append_note("Spot", "x", "Nope"), doctest::Contains("## Contacts | ## Services"), std::runtime_error);
    }
}

TEST_SUITE("list command") {
    TEST_CASE("lists a folder's notes, subfolders and front matter keys") {
        TestVault vault({
            {"People/A.md", "---\ntags: [person]\ncircle: family\n---\n"},
            {"People/Sub/B.md", ""},
            {"People/.hidden/C.md", ""}
        });

        ListOutput out = list_notes("People");
        CHECK(out.folders.get() == std::vector<std::string>{"People/Sub"});
        CHECK(out.notes.get().size() == 1);
        CHECK(out.notes.get()[0].path.get() == "People/A.md");
        CHECK(out.notes.get()[0].title.get() == "A");

        auto keys = out.notes.get()[0].frontMatterKeys.get();
        bool hasTags = (std::find(keys.begin(), keys.end(), "tags") != keys.end());
        bool hasCircle = (std::find(keys.begin(), keys.end(), "circle") != keys.end());
        CHECK(hasTags);
        CHECK(hasCircle);

        ListOutput recOut = list_notes("People/", true);
        std::vector<std::string> recPaths;
        for (const auto& n : recOut.notes.get()) recPaths.push_back(n.path.get());
        std::vector<std::string> expectedPaths = {"People/A.md", "People/Sub/B.md"};
        CHECK(recPaths == expectedPaths);

        CHECK_THROWS_WITH_AS(list_notes("Nope"), doctest::Contains("doesn't exist"), std::runtime_error);
    }
}

TEST_SUITE("search command") {
    TEST_CASE("search query, tags, front matter, snippets, and limit") {
        TestVault vault({
            {"People/Jane Doe.md",
             "---\ntags: [person]\ncircle: family\naliases: [\"Nana\"]\nkids: [\"[[Sam Doe]]\"]\n---\nLoves the open evening at Maynooth.\n"},
            {"People/Bob Smith.md",
             "---\ntags: [person]\ncircle: coworker\n---\nWorks at Maynooth office. #health/meds\n"},
            {"Places/Maynooth School.md",
             "---\ntags: [school]\n---\nOpen day in March.\n```\n#notatag\n```\n"},
            {"Log/2026.md",
             "- 2026.01.01 Party #social\n"},
            {".obsidian/hidden.md", "Maynooth"}
        });

        auto paths = [](const SearchOutput& out) {
            std::vector<std::string> res;
            for (const auto& r : out.results.get()) res.push_back(r.path.get());
            return res;
        };

        // 1. ANDs words case-insensitively and ranks title matches first
        {
            SearchOutput out = search_notes(SearchInput{.query = "MAYNOOTH"});
            CHECK(out.total.get() == 3);
            CHECK(out.results.get()[0].path.get() == "Places/Maynooth School.md");

            SearchOutput out2 = search_notes(SearchInput{.query = "maynooth open"});
            auto p2 = paths(out2);
            std::sort(p2.begin(), p2.end());
            std::vector<std::string> exp2 = {"People/Jane Doe.md", "Places/Maynooth School.md"};
            CHECK(p2 == exp2);
        }

        // 2. Matches quoted phrases
        {
            auto p1 = paths(search_notes(SearchInput{.query = "\"open evening\""}));
            CHECK(p1 == std::vector<std::string>{"People/Jane Doe.md"});
            auto p2 = paths(search_notes(SearchInput{.query = "\"evening open\""}));
            CHECK(p2.empty());
        }

        // 3. Matches aliases
        {
            auto p = paths(search_notes(SearchInput{.query = "nana"}));
            CHECK(p == std::vector<std::string>{"People/Jane Doe.md"});
        }

        // 4. Returns snippets with file line numbers
        {
            SearchOutput out = search_notes(SearchInput{.query = "open evening"});
            CHECK(out.results.get().size() >= 1);
            auto hit = out.results.get()[0];
            CHECK(hit.snippets.get().size() == 1);
            CHECK(hit.snippets.get()[0].line.get() == 7);
            CHECK(hit.snippets.get()[0].text.get() == "Loves the open evening at Maynooth.");
        }

        // 5. Filters by folder
        {
            SearchOutput out = search_notes(SearchInput{.folder = "People", .query = "maynooth"});
            auto p = paths(out);
            std::sort(p.begin(), p.end());
            std::vector<std::string> exp = {"People/Bob Smith.md", "People/Jane Doe.md"};
            CHECK(p == exp);
            CHECK_THROWS_WITH_AS(search_notes(SearchInput{.folder = "../x"}), doctest::Contains("outside the vault"), std::runtime_error);
        }

        // 6. Filters by front matter and inline tags, with nested tags
        {
            CHECK(paths(search_notes(SearchInput{.tag = "school"})) == std::vector<std::string>{"Places/Maynooth School.md"});
            CHECK(paths(search_notes(SearchInput{.tag = "#health"})) == std::vector<std::string>{"People/Bob Smith.md"});
            CHECK(paths(search_notes(SearchInput{.tag = "health/meds"})) == std::vector<std::string>{"People/Bob Smith.md"});
            CHECK(paths(search_notes(SearchInput{.tag = "social"})) == std::vector<std::string>{"Log/2026.md"});
            CHECK(paths(search_notes(SearchInput{.tag = "notatag"})).empty());
            CHECK(search_notes(SearchInput{.tag = "person"}).total.get() == 2);
        }

        // 7. Filters by front matter key=value, including list items and links
        {
            CHECK(paths(search_notes(SearchInput{.frontMatter = std::map<std::string, std::string>{{"circle", "Family"}}})) == std::vector<std::string>{"People/Jane Doe.md"});
            CHECK(paths(search_notes(SearchInput{.frontMatter = std::map<std::string, std::string>{{"kids", "Sam Doe"}}})) == std::vector<std::string>{"People/Jane Doe.md"});
            CHECK(paths(search_notes(SearchInput{.frontMatter = std::map<std::string, std::string>{{"circle", "family"}, {"tags", "person"}}, .query = "bob"})).empty());
        }

        // 8. Applies limit after counting
        {
            SearchOutput out = search_notes(SearchInput{.limit = 1, .query = "maynooth"});
            CHECK(out.results.get().size() == 1);
            CHECK(out.total.get() == 3);
        }
    }
}

TEST_SUITE("rename command") {
    const std::string LINKS =
        "Plain [[Old Name]]\n"
        "Label [[Old Name|the label]]\n"
        "Heading [[Old Name#Section]] and block [[Old Name#^abc|x]]\n"
        "Embed ![[Old Name]]\n"
        "Folder [[People/Old Name]] and ext [[Old Name.md]]\n"
        "Case [[old name]]\n"
        "Markdown [x](People/Old%20Name.md) and [y](<People/Old Name.md#Section>)\n"
        "Other [[Old Name Two]] [[Unrelated]] [z](https://example.com/Old%20Name.md)\n"
        "front: [\"[[Old Name]]\"]";

    TEST_CASE("rewrites every link form across the vault") {
        TestVault vault({
            {"People/Old Name.md", "---\ntags: [person]\n---\nSelf [[Old Name#Top]]\n"},
            {"Index.md", LINKS},
            {"People/Sibling.md", "See [x](Old%20Name.md) and [[Old Name]]"},
            {"People/Old Name Two.md", ""}
        });

        RenameOutput out = rename_note(RenameInput{.from = "Old Name", .to = "New Name"});
        CHECK(out.from.get() == "People/Old Name.md");
        CHECK(out.to.get() == "People/New Name.md");
        CHECK(!std::filesystem::exists(vault.root / "People/Old Name.md"));

        std::string expectedIndex =
            "Plain [[New Name]]\n"
            "Label [[New Name|the label]]\n"
            "Heading [[New Name#Section]] and block [[New Name#^abc|x]]\n"
            "Embed ![[New Name]]\n"
            "Folder [[People/New Name]] and ext [[New Name]]\n"
            "Case [[New Name]]\n"
            "Markdown [x](People/New%20Name.md) and [y](<People/New Name.md#Section>)\n"
            "Other [[Old Name Two]] [[Unrelated]] [z](https://example.com/Old%20Name.md)\n"
            "front: [\"[[New Name]]\"]";

        CHECK(vault.read("Index.md") == expectedIndex);
        CHECK(vault.read("People/Sibling.md") == "See [x](New%20Name.md) and [[New Name]]");
        CHECK(vault.read("People/New Name.md") == "---\ntags: [person]\n---\nSelf [[New Name#Top]]\n");
        CHECK(out.links.get() == 14);
    }

    TEST_CASE("dryRun reports without writing") {
        TestVault vault({{"Old.md", ""}, {"Index.md", "[[Old]]"}});
        RenameOutput out = rename_note(RenameInput{.dryRun = true, .from = "Old", .to = "New"});
        CHECK(out.dryRun.get() == true);
        CHECK(out.links.get() == 1);
        CHECK(out.notes.get().size() == 1);
        CHECK(out.notes.get()[0].links.get() == 1);
        CHECK(out.notes.get()[0].path.get() == "Index.md");
        CHECK(vault.read("Index.md") == "[[Old]]");
        CHECK(std::filesystem::exists(vault.root / "Old.md"));
    }

    TEST_CASE("moves to another folder and keeps links unique by path when needed") {
        TestVault vault({
            {"Inbox/Note.md", "[rel](../Places/Spot.md)"},
            {"Places/Spot.md", ""},
            {"Archive/Note.md", ""},
            {"Index.md", "[[Inbox/Note]] [m](Inbox/Note.md)"}
        });

        RenameOutput out = rename_note(RenameInput{.from = "Inbox/Note.md", .to = "People/Note"});
        CHECK(out.to.get() == "People/Note.md");
        CHECK(vault.read("Index.md") == "[[People/Note]] [m](People/Note.md)");
        CHECK(vault.read("People/Note.md") == "[rel](../Places/Spot.md)");
    }

    TEST_CASE("leaves links to a same-named note in another folder alone") {
        TestVault vault({
            {"A/Same.md", ""},
            {"B/Same.md", ""},
            {"A/Index.md", "[[Same]]"},
            {"B/Index.md", "[[Same]]"}
        });

        rename_note(RenameInput{.from = "A/Same.md", .to = "Renamed"});
        CHECK(vault.read("A/Index.md") == "[[Renamed]]");
        CHECK(vault.read("B/Index.md") == "[[Same]]");
    }

    TEST_CASE("adds aliases, skipping the old title and words already in the title") {
        TestVault vault({{"Ali.md", "---\naliases: [\"A\"]\ntags: [person]\n---\nbody"}});
        RenameOutput out = rename_note(RenameInput{
            .aliases = std::vector<std::string>{"Alice", "Ali", "Asmith", "A"},
            .from = "Ali",
            .to = "Alice Smith"
        });
        CHECK(out.aliasesAdded.get() == std::vector<std::string>{"Asmith"});
        CHECK(vault.read("Alice Smith.md") == "---\naliases: [\"A\", \"Asmith\"]\ntags: [person]\n---\nbody");
    }

    TEST_CASE("adds aliases to block lists and notes without front matter; keepOldTitle keeps the old title") {
        TestVault vault({
            {"A.md", "---\naliases:\n  - One\nx: 1\n---\n"},
            {"B.md", "plain"}
        });

        rename_note(RenameInput{.aliases = std::vector<std::string>{"Two"}, .from = "A", .to = "A2"});
        CHECK(vault.read("A2.md") == "---\naliases: [\"One\", \"Two\"]\nx: 1\n---\n");

        rename_note(RenameInput{.from = "B", .keepOldTitle = true, .to = "B2"});
        CHECK(vault.read("B2.md") == "---\naliases: [\"B\"]\n---\nplain");
    }

    TEST_CASE("refuses collisions, bad titles and escapes; allows case-only renames") {
        TestVault vault({{"A.md", ""}, {"B.md", ""}, {"Index.md", "[[a]]"}});

        CHECK_THROWS_WITH_AS(rename_note(RenameInput{.from = "A", .to = "B"}), doctest::Contains("already exists"), std::runtime_error);
        CHECK_THROWS_WITH_AS(rename_note(RenameInput{.from = "A", .to = "C: D"}), doctest::Contains("Bad title"), std::runtime_error);
        CHECK_THROWS_WITH_AS(rename_note(RenameInput{.from = "A", .to = "../C"}), doctest::Contains("outside the vault"), std::runtime_error);
        CHECK_THROWS_WITH_AS(rename_note(RenameInput{.from = "A", .to = ".obsidian/C"}), doctest::Contains("hidden folder"), std::runtime_error);

        rename_note(RenameInput{.from = "A", .to = "a"});
        CHECK(std::filesystem::exists(vault.root / "a.md"));
        CHECK(vault.read("Index.md") == "[[a]]");
    }

    TEST_CASE("refuses to rename a symlinked note") {
        TestVault vault({{"Real.md", "r"}});
        std::filesystem::create_symlink(vault.root / "Real.md", vault.root / "Link.md");

        CHECK_THROWS_WITH_AS(rename_note(RenameInput{.from = "Link", .to = "Other"}), doctest::Contains("symlink"), std::runtime_error);
        CHECK(std::filesystem::exists(vault.root / "Link.md"));
        CHECK(!std::filesystem::exists(vault.root / "Other.md"));
    }
}

// ============================================================================
// 3. MCP Server & App Configuration Tests
// ============================================================================

TEST_SUITE("app / mcp") {
    TEST_CASE("vault default is portable and resolved only when needed") {
        TestEnv vault("OBSIDIAN_VAULT", nullptr);
        TestEnv home("HOME", "/synthetic/home");
        CHECK(vault_root() == "/synthetic/home/Documents/Notes");
        TestEnv noHome("HOME", nullptr);
        CHECK_NOTHROW(make_obsidian_app());
        CHECK_THROWS_WITH_AS(vault_root(), doctest::Contains("HOME is required"), std::runtime_error);
        TestEnv explicitVault("OBSIDIAN_VAULT", "/synthetic/vault");
        CHECK(vault_root() == "/synthetic/vault");
    }

    TEST_CASE("conventions are lazy, private overrides are explicit, fallback creates nothing") {
        TestVault vault({{"custom.md", "# Custom conventions\n"}});
        TestEnv home("HOME", vault.root.c_str());
        TestEnv xdg("XDG_CONFIG_HOME", "");
        TestEnv configured("OBSIDIAN_CONVENTIONS_FILE", nullptr);
        CHECK(load_conventions() == CONVENTIONS_MD_EMBEDDED);
        CHECK(!std::filesystem::exists(vault.root / ".config"));

        const auto missing = (vault.root / "missing.md").string();
        TestEnv explicitMissing("OBSIDIAN_CONVENTIONS_FILE", missing.c_str());
        CHECK_NOTHROW(make_obsidian_app());
        CHECK_THROWS_AS(load_conventions(), std::runtime_error);
        const auto custom = (vault.root / "custom.md").string();
        TestEnv explicitFile("OBSIDIAN_CONVENTIONS_FILE", custom.c_str());
        CHECK(load_conventions() == "# Custom conventions\n");
        std::filesystem::permissions(custom, std::filesystem::perms::none);
        CHECK_THROWS_AS(load_conventions(), std::runtime_error);
        std::filesystem::permissions(custom, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
        TestEnv invalidFile("OBSIDIAN_CONVENTIONS_FILE", vault.root.c_str());
        CHECK_THROWS_AS(load_conventions(), std::runtime_error);
    }

    TEST_CASE("standard private conventions use XDG then HOME configuration") {
        TestVault vault({
            {".config/obsidian/conventions.md", "# Home conventions\n"},
            {"xdg/obsidian/conventions.md", "# XDG conventions\n"}
        });
        TestEnv home("HOME", vault.root.c_str());
        TestEnv configured("OBSIDIAN_CONVENTIONS_FILE", "");
        TestEnv xdg("XDG_CONFIG_HOME", "");
        CHECK(load_conventions() == "# Home conventions\n");
        const auto config = (vault.root / "xdg").string();
        TestEnv explicitXdg("XDG_CONFIG_HOME", config.c_str());
        CHECK(load_conventions() == "# XDG conventions\n");
    }

    TEST_CASE("serves the conventions guide and points the instructions at it") {
        TestEnv home("HOME", nullptr);
        TestEnv xdg("XDG_CONFIG_HOME", nullptr);
        TestEnv configured("OBSIDIAN_CONVENTIONS_FILE", nullptr);
        auto app = make_obsidian_app();
        CHECK(app.instructions.find("obsidian://conventions") != std::string::npos);

        bool foundResource = false;
        for (const auto& r : app.resources) {
            if (r.uri == "obsidian://conventions") {
                foundResource = true;
                std::string loaded = r.load ? r.load() : "";
                CHECK(loaded.find("# Obsidian note conventions") != std::string::npos);
            }
        }
        CHECK(foundResource);
    }

    TEST_CASE("mcp dispatching tools and resources") {
        TestEnv home("HOME", nullptr);
        TestEnv xdg("XDG_CONFIG_HOME", nullptr);
        TestEnv configured("OBSIDIAN_CONVENTIONS_FILE", nullptr);
        auto app = make_obsidian_app();

        // 1. initialize
        Json initReq = Json::object_t{};
        initReq["jsonrpc"] = "2.0";
        initReq["id"] = 1;
        initReq["method"] = "initialize";
        Json initResp = app.dispatchMcpJson(initReq);
        CHECK(initResp["result"]["serverInfo"]["name"].get<std::string>() == "obsidian-mcp");
        CHECK(initResp["result"]["serverInfo"]["version"].get<std::string>() == "2.0.0");

        // 2. tools/list
        Json listReq = Json::object_t{};
        listReq["jsonrpc"] = "2.0";
        listReq["id"] = 2;
        listReq["method"] = "tools/list";
        Json listResp = app.dispatchMcpJson(listReq);
        auto tools = listResp["result"]["tools"].get<Json::array_t>();
        std::vector<std::string> toolNames;
        for (const auto& t : tools) toolNames.push_back(t["name"].get<std::string>());
        CHECK(std::find(toolNames.begin(), toolNames.end(), "read") != toolNames.end());
        CHECK(std::find(toolNames.begin(), toolNames.end(), "write") != toolNames.end());
        CHECK(std::find(toolNames.begin(), toolNames.end(), "append") != toolNames.end());
        CHECK(std::find(toolNames.begin(), toolNames.end(), "rename") != toolNames.end());
        CHECK(std::find(toolNames.begin(), toolNames.end(), "list") != toolNames.end());
        CHECK(std::find(toolNames.begin(), toolNames.end(), "search") != toolNames.end());
        CHECK(std::find(toolNames.begin(), toolNames.end(), "status") != toolNames.end());
        CHECK(std::find(toolNames.begin(), toolNames.end(), "mcp_resource_read") != toolNames.end());

        // 3. resources/read
        Json resReq = Json::object_t{};
        resReq["jsonrpc"] = "2.0";
        resReq["id"] = 3;
        resReq["method"] = "resources/read";
        resReq["params"]["uri"] = "obsidian://conventions";
        Json resResp = app.dispatchMcpJson(resReq);
        auto contents = resResp["result"]["contents"].get<Json::array_t>();
        CHECK(contents.size() == 1);
        CHECK(contents[0]["text"].get<std::string>().find("# Obsidian note conventions") != std::string::npos);

        // 4. tools/call status
        Json callStatus = Json::object_t{};
        callStatus["jsonrpc"] = "2.0";
        callStatus["id"] = 4;
        callStatus["method"] = "tools/call";
        callStatus["params"]["name"] = "status";
        callStatus["params"]["arguments"] = Json::object_t{};
        Json statusResp = app.dispatchMcpJson(callStatus);
        CHECK(!statusResp["result"]["isError"].get<bool>());
        auto statusContent = statusResp["result"]["content"].get<Json::array_t>();
        CHECK(statusContent[0]["text"].get<std::string>().find("\"version\":\"2.0.0\"") != std::string::npos);
    }
}
