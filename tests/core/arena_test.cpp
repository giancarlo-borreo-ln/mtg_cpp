// M2.2 arena formatter tests, mirroring the webapp's core/arena.spec.ts.

#include "core/arena.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace mtgcpp::core {
namespace {

// Base card factory mirroring the spec's: a concrete printing with a lowercase
// set code (the formatter uppercases it on output).
Card card(std::string name = "Forest") {
  Card c;
  c.name = std::move(name);
  c.set_code = "war";
  c.set_name = "War of the Spark";
  c.collector_number = "263";
  c.quantity = 4;
  c.type_line = "Basic Land — Forest";
  return c;
}

// Writes `content` to a temp file and removes it on destruction. Non-copyable
// (and non-movable) so the destructor's cleanup can never run twice.
class TempFile {
public:
  explicit TempFile(std::string_view content) {
    std::ofstream out(path_);
    out << content;
  }
  ~TempFile() { std::filesystem::remove(path_); }

  TempFile(const TempFile &) = delete;
  TempFile &operator=(const TempFile &) = delete;
  TempFile(TempFile &&) = delete;
  TempFile &operator=(TempFile &&) = delete;

  const std::filesystem::path &path() const { return path_; }

private:
  std::filesystem::path path_ = std::filesystem::temp_directory_path() / "mtgcpp_arena_test.txt";
};

TEST(ToArenaText, RendersMainboardCardsUnderTheDeckHeader) {
  Card bolt = card("Bolt");
  bolt.quantity = 3;

  const std::string text = toArenaText({card("Forest"), bolt});

  EXPECT_EQ(text, "Deck\n4 Forest (WAR) 263\n3 Bolt (WAR) 263");
}

TEST(ToArenaText, GroupsSideboardAndCommanderCardsUnderTheirHeadersInOrder) {
  Card main = card("Main Forest");
  main.quantity = 2;
  Card side = card("Side Bolt");
  side.quantity = 1;
  side.section = ArenaSection::Sideboard;
  Card commander = card("Commander Ghalta");
  commander.quantity = 1;
  commander.section = ArenaSection::Commander;

  const std::string text = toArenaText({main, side, commander});

  EXPECT_EQ(text, "Deck\n2 Main Forest (WAR) 263\nSideboard\n1 Side Bolt (WAR) 263\n"
                  "Commander\n1 Commander Ghalta (WAR) 263");
}

TEST(ToArenaText, UppercasesSetCodes) {
  Card c = card();
  c.set_code = "lea";
  c.collector_number = "223";

  EXPECT_EQ(toArenaText({c}), "Deck\n4 Forest (LEA) 223");
}

TEST(ToArenaText, KeepsLetterSuffixedCollectorNumbersVerbatim) {
  Card c = card();
  c.collector_number = "222a";
  c.section = ArenaSection::Sideboard;

  EXPECT_EQ(toArenaText({c}), "Sideboard\n4 Forest (WAR) 222a");
}

TEST(ToArenaText, SkipsCardsWithoutAPrinting) {
  Card c = card();
  c.set_code.clear();
  c.collector_number.clear();

  EXPECT_EQ(toArenaText({c}), "");
}

TEST(ToArenaText, OmitsSectionHeadersForEmptySections) {
  Card c = card();
  c.quantity = 1;
  c.section = ArenaSection::Sideboard;

  EXPECT_EQ(toArenaText({c}), "Sideboard\n1 Forest (WAR) 263");
}

TEST(ToArenaText, ReturnsAnEmptyStringForNoCards) { EXPECT_EQ(toArenaText({}), ""); }

TEST(MissingCardKey, BuildsAStableSectionScopedKey) {
  const MissingCard missing{.name = "Mystery",
                            .set = "ZZZ",
                            .number = "1",
                            .quantity = 2,
                            .section = ArenaSection::Sideboard};

  EXPECT_EQ(missingCardKey(missing), "sideboard|ZZZ|1");
}

TEST(MissingCardKey, KeysNameOnlyFlaggedCardsByName) {
  const MissingCard missing{.name = "Clockwork Percussionist",
                            .set = "",
                            .number = "",
                            .quantity = 4,
                            .section = ArenaSection::Mainboard};

  EXPECT_EQ(missingCardKey(missing), "mainboard|name|Clockwork Percussionist");
}

struct TestItem {
  int id;
  ArenaSection section = ArenaSection::Mainboard;
};

std::vector<ArenaSection> sectionOrderOf(const std::vector<ArenaSectionGroup<TestItem>> &groups) {
  std::vector<ArenaSection> order;
  order.reserve(groups.size());
  for (const ArenaSectionGroup<TestItem> &group : groups) {
    order.push_back(group.section);
  }
  return order;
}

TEST(GroupBySection, GroupsItemsIntoCanonicalSectionOrder) {
  const std::vector<TestItem> items = {
      {1, ArenaSection::Sideboard},
      {2, ArenaSection::Commander},
      {3, ArenaSection::Mainboard},
      {4, ArenaSection::Sideboard},
  };

  const std::vector<ArenaSectionGroup<TestItem>> groups = groupBySection(items);

  EXPECT_EQ(sectionOrderOf(groups),
            (std::vector<ArenaSection>{ArenaSection::Mainboard, ArenaSection::Sideboard,
                                       ArenaSection::Commander}));
  ASSERT_EQ(groups.size(), 3u);
  EXPECT_EQ(groups.at(0).items.at(0).id, 3);
  EXPECT_EQ(groups.at(1).items.at(0).id, 1);
  EXPECT_EQ(groups.at(1).items.at(1).id, 4);
  EXPECT_EQ(groups.at(2).items.at(0).id, 2);
}

TEST(GroupBySection, TreatsAnUnspecifiedSectionAsMainboard) {
  const std::vector<TestItem> items = {{1, ArenaSection::Mainboard}, {2, ArenaSection::Mainboard}};

  const std::vector<ArenaSectionGroup<TestItem>> groups = groupBySection(items);

  EXPECT_EQ(sectionOrderOf(groups), (std::vector<ArenaSection>{ArenaSection::Mainboard}));
  ASSERT_EQ(groups.size(), 1u);
  EXPECT_EQ(groups.at(0).items.size(), 2u);
}

TEST(GroupBySection, OmitsEmptySections) {
  const std::vector<TestItem> items = {{1, ArenaSection::Commander}};

  const std::vector<ArenaSectionGroup<TestItem>> groups = groupBySection(items);

  EXPECT_EQ(sectionOrderOf(groups), (std::vector<ArenaSection>{ArenaSection::Commander}));
}

TEST(SectionLabel, LabelsTheArenaSections) {
  EXPECT_EQ(sectionLabel(ArenaSection::Mainboard), "Deck");
  EXPECT_EQ(sectionLabel(ArenaSection::Sideboard), "Sideboard");
  EXPECT_EQ(sectionLabel(ArenaSection::Commander), "Commander");
}

TEST(ReadFileText, ReadsTheFileContentAsText) {
  TempFile file("Deck\n1 Forest (WAR) 263\n");

  const std::optional<std::string> content = readFileText(file.path());
  if (content.has_value()) {
    EXPECT_EQ(content.value(), "Deck\n1 Forest (WAR) 263\n");
  } else {
    FAIL() << "expected the file to be readable";
  }
}

TEST(ReadFileText, ResolvesAnEmptyFileToAnEmptyString) {
  TempFile file("");

  const std::optional<std::string> content = readFileText(file.path());
  if (content.has_value()) {
    EXPECT_EQ(content.value(), "");
  } else {
    FAIL() << "expected the empty file to be readable";
  }
}

TEST(ReadFileText, ReturnsNulloptForAMissingFile) {
  const std::filesystem::path missing =
      std::filesystem::temp_directory_path() / "mtgcpp_definitely_missing.txt";
  std::filesystem::remove(missing);

  EXPECT_FALSE(readFileText(missing).has_value());
}

} // namespace
} // namespace mtgcpp::core
