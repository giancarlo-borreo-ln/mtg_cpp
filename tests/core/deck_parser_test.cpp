// M2.1 Arena deck-parser tests, mirroring backend/tests/test_deck_parser.py.

#include "core/deck_parser.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace mtgcpp::core {
namespace {

std::vector<std::string> namesOf(const std::vector<DeckEntry> &entries) {
  std::vector<std::string> names;
  names.reserve(entries.size());
  for (const DeckEntry &entry : entries) {
    names.push_back(entry.name);
  }
  return names;
}

TEST(ParseStandardArenaLines, ExtractsQuantityNameSetAndNumber) {
  const std::string text =
      "4 Forest (WAR) 263\n3 Llanowar Elves (M19) 314\n2 Ghalta, Primal Hunger (RIX) 130\n";

  const std::vector<DeckEntry> entries = parseArenaText(text);

  ASSERT_EQ(entries.size(), 3u);
  EXPECT_EQ(entries.at(0), (DeckEntry{"Forest", "WAR", "263", 4}));
  EXPECT_EQ(entries.at(1), (DeckEntry{"Llanowar Elves", "M19", "314", 3}));
  EXPECT_EQ(entries.at(2).name, "Ghalta, Primal Hunger");
}

TEST(ParseStandardArenaLines, ParsesDoubleFacedCardNames) {
  const std::vector<DeckEntry> entries =
      parseArenaText("2 Akoum Warrior // Akoum Teeth (ZNR) 134\n");

  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries.at(0), (DeckEntry{"Akoum Warrior // Akoum Teeth", "ZNR", "134", 2}));
}

TEST(ParseIgnoresCommentsBlankAndHashLines, SkipsAllOfThem) {
  const std::string text = "// sideboard\n\n# a note\n   \n2 Forest (WAR) 263\n";

  const std::vector<DeckEntry> entries = parseArenaText(text);

  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries.at(0).name, "Forest");
}

TEST(ParseLetterSuffixedCollectorNumber, KeepsTheSuffix) {
  const std::vector<DeckEntry> entries = parseArenaText("1 Akoum Warrior (ZNR) 222a\n");

  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries.at(0).number, "222a");
}

TEST(ParseSkipsInvalidLinesWithoutRaising, DropsTheBadLine) {
  const std::string text = "this is not a card\n2 Forest (WAR) 263\n";

  const std::vector<DeckEntry> entries = parseArenaText(text);

  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries.at(0).name, "Forest");
}

TEST(ParsePreservesCardNameCase, KeepsTheOriginalCase) {
  const std::vector<DeckEntry> entries = parseArenaText("1 lightning bolt (STA) 49\n");

  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries.at(0).name, "lightning bolt");
}

TEST(ParseEmptyTextRaises, ThrowsForBlankAndCommentOnlyText) {
  EXPECT_THROW(parseArenaText(""), DeckParseError);
  EXPECT_THROW(parseArenaText("   \n// nothing\n# here\n"), DeckParseError);
}

TEST(AggregateSumsQuantitiesAndDedupesByPrinting, SumsAndDedupes) {
  const std::string text = "4 Forest (WAR) 263\n1 Forest (WAR) 263\n2 Forest (M21) 260\n";

  const std::vector<DeckEntry> agg = aggregateEntries(parseArenaText(text));

  ASSERT_EQ(agg.size(), 2u);
  EXPECT_EQ(agg.at(0), (DeckEntry{"Forest", "WAR", "263", 5}));
  EXPECT_EQ(agg.at(1).quantity, 2);
  EXPECT_EQ(agg.at(1).set_code, "M21");
}

TEST(AggregateDedupeIsCaseInsensitive, MergesAcrossCase) {
  const std::string text = "2 Forest (WAR) 263\n1 Forest (war) 263\n";

  const std::vector<DeckEntry> agg = aggregateEntries(parseArenaText(text));

  ASSERT_EQ(agg.size(), 1u);
  EXPECT_EQ(agg.at(0).quantity, 3);
}

TEST(AggregatePreservesFirstSeenOrder, KeepsInsertionOrder) {
  const std::string text =
      "2 Ghalta, Primal Hunger (RIX) 130\n3 Forest (WAR) 263\n1 Ghalta, Primal Hunger (RIX) 130\n";

  const std::vector<DeckEntry> agg = aggregateEntries(parseArenaText(text));

  EXPECT_EQ(namesOf(agg), (std::vector<std::string>{"Ghalta, Primal Hunger", "Forest"}));
  EXPECT_EQ(agg.at(0).quantity, 3);
}

TEST(ParseSections, SplitsMainboardAndSideboard) {
  const std::string text = "Deck\n4 Forest (WAR) 263\n3 Llanowar Elves (M19) 314\nSideboard\n"
                           "2 Abrade (AKR) 136\n";

  const ParsedSections sections = parseArenaSections(text);

  EXPECT_EQ(namesOf(sections.at(ArenaSection::Mainboard)),
            (std::vector<std::string>{"Forest", "Llanowar Elves"}));
  EXPECT_EQ(sections.at(ArenaSection::Mainboard).at(0).quantity, 4);
  EXPECT_EQ(namesOf(sections.at(ArenaSection::Sideboard)), (std::vector<std::string>{"Abrade"}));
  EXPECT_EQ(sections.at(ArenaSection::Sideboard).at(0).quantity, 2);
  EXPECT_TRUE(sections.at(ArenaSection::Commander).empty());
}

TEST(ParseSections, CommanderSection) {
  const std::string text = "Deck\n1 Ghalta, Primal Hunger (RIX) 130\n99 Forest (WAR) 263\n"
                           "Commander\n1 Ghalta, Primal Hunger (RIX) 130\n";

  const ParsedSections sections = parseArenaSections(text);

  EXPECT_EQ(sections.at(ArenaSection::Mainboard).size(), 2u);
  ASSERT_EQ(sections.at(ArenaSection::Commander).size(), 1u);
  EXPECT_EQ(sections.at(ArenaSection::Commander).at(0).name, "Ghalta, Primal Hunger");
  EXPECT_EQ(sections.at(ArenaSection::Commander).at(0).quantity, 1);
}

TEST(ParseSections, AliasesDeckAndMainboardToMainboard) {
  const std::string text = "Mainboard\n1 Forest (WAR) 263\nDeck\n2 Mountain (UST) 216\n";

  const ParsedSections sections = parseArenaSections(text);

  EXPECT_EQ(namesOf(sections.at(ArenaSection::Mainboard)),
            (std::vector<std::string>{"Forest", "Mountain"}));
  EXPECT_TRUE(sections.at(ArenaSection::Sideboard).empty());
  EXPECT_TRUE(sections.at(ArenaSection::Commander).empty());
}

TEST(ParseSections, LinesBeforeAnyHeaderAreMainboard) {
  const std::string text = "2 Forest (WAR) 263\nSideboard\n1 Abrade (AKR) 136\n";

  const ParsedSections sections = parseArenaSections(text);

  ASSERT_EQ(sections.at(ArenaSection::Mainboard).size(), 1u);
  EXPECT_EQ(sections.at(ArenaSection::Mainboard).at(0).name, "Forest");
}

TEST(ParseSections, HeadersAreCaseInsensitive) {
  const std::string text = "deck\n1 Forest (WAR) 263\nSIDEBOARD\n1 Abrade (AKR) 136\nCommander\n"
                           "1 Ghalta, Primal Hunger (RIX) 130\n";

  const ParsedSections sections = parseArenaSections(text);

  EXPECT_EQ(sections.at(ArenaSection::Mainboard).size(), 1u);
  EXPECT_EQ(sections.at(ArenaSection::Sideboard).size(), 1u);
  EXPECT_EQ(sections.at(ArenaSection::Commander).size(), 1u);
}

TEST(ParseSections, SkipsCommentsBlankLinesAndDeckName) {
  const std::string text = "My Cool Deck\n// comment\n\n# note\nDeck\n2 Forest (WAR) 263\n"
                           "60 cards\n";

  const ParsedSections sections = parseArenaSections(text);

  ASSERT_EQ(sections.at(ArenaSection::Mainboard).size(), 1u);
  EXPECT_EQ(sections.at(ArenaSection::Mainboard).at(0).name, "Forest");
}

TEST(ParseSections, SkipsMalformedLinesWithinSections) {
  const std::string text =
      "Deck\nnot a card line\n4 Forest (WAR) 263\nForest\nSideboard\njunk\n1 Abrade (AKR) 136\n";

  const ParsedSections sections = parseArenaSections(text);

  EXPECT_EQ(namesOf(sections.at(ArenaSection::Mainboard)), (std::vector<std::string>{"Forest"}));
  EXPECT_EQ(namesOf(sections.at(ArenaSection::Sideboard)), (std::vector<std::string>{"Abrade"}));
}

TEST(ParseSections, ExtractsSetAndNumber) {
  const std::string text = "Deck\n1 Lightning Bolt (LEA) 223\nSideboard\n2 Forest (WAR) 263a\n";

  const ParsedSections sections = parseArenaSections(text);

  EXPECT_EQ(sections.at(ArenaSection::Mainboard).at(0).set_code, "LEA");
  EXPECT_EQ(sections.at(ArenaSection::Mainboard).at(0).number, "223");
  EXPECT_EQ(sections.at(ArenaSection::Sideboard).at(0).set_code, "WAR");
  EXPECT_EQ(sections.at(ArenaSection::Sideboard).at(0).number, "263a");
}

TEST(ParseSections, EmptyTextRaises) {
  EXPECT_THROW(parseArenaSections(""), DeckParseError);
  EXPECT_THROW(parseArenaSections("Deck\nSideboard\n// nothing\n"), DeckParseError);
}

TEST(ParseArenaText, FlattensSectionsInOrder) {
  const std::string text = "Deck\n2 Forest (WAR) 263\nSideboard\n1 Abrade (AKR) 136\n"
                           "Commander\n1 Ghalta, Primal Hunger (RIX) 130\n";

  const std::vector<DeckEntry> entries = parseArenaText(text);

  EXPECT_EQ(namesOf(entries),
            (std::vector<std::string>{"Forest", "Abrade", "Ghalta, Primal Hunger"}));
}

TEST(AggregateIsAppliedPerSection, DoesNotMergeAcrossSections) {
  const std::string text = "Deck\n2 Forest (WAR) 263\nSideboard\n1 Forest (WAR) 263\n";

  const ParsedSections sections = parseArenaSections(text);

  const std::vector<DeckEntry> main_agg = aggregateEntries(sections.at(ArenaSection::Mainboard));
  const std::vector<DeckEntry> side_agg = aggregateEntries(sections.at(ArenaSection::Sideboard));
  EXPECT_EQ(main_agg.at(0).quantity, 2);
  EXPECT_EQ(side_agg.at(0).quantity, 1);
}

TEST(ParseNameOnlyLines, ExtractsNameWithoutPrinting) {
  const std::string text = "4 Clockwork Percussionist\n2 Mountain\n";

  const std::vector<DeckEntry> entries = parseArenaText(text);

  ASSERT_EQ(entries.size(), 2u);
  EXPECT_EQ(entries.at(0), (DeckEntry{"Clockwork Percussionist", "", "", 4}));
  EXPECT_EQ(entries.at(1).name, "Mountain");
}

TEST(ParseNameOnlyLines, InsideSections) {
  const std::string text = "Deck\n4 Burst Lightning\nSideboard\n2 Negate\n";

  const ParsedSections sections = parseArenaSections(text);

  EXPECT_EQ(sections.at(ArenaSection::Mainboard).at(0).name, "Burst Lightning");
  EXPECT_EQ(sections.at(ArenaSection::Mainboard).at(0).set_code, "");
  EXPECT_EQ(sections.at(ArenaSection::Sideboard).at(0).name, "Negate");
  EXPECT_EQ(sections.at(ArenaSection::Sideboard).at(0).quantity, 2);
}

TEST(ParseSkipsArenaSummaryCardCountLines, DropsCardAndCards) {
  const std::string text = "Deck\n4 Forest (WAR) 263\n60 cards\nSideboard\n15 cards\n";

  const ParsedSections sections = parseArenaSections(text);

  EXPECT_EQ(namesOf(sections.at(ArenaSection::Mainboard)), (std::vector<std::string>{"Forest"}));
  EXPECT_TRUE(sections.at(ArenaSection::Sideboard).empty());
}

TEST(AggregateNameOnlyDedupesByName, MergesByName) {
  const std::string text = "2 Clockwork Percussionist\n1 Clockwork Percussionist\n3 Mountain\n";

  const std::vector<DeckEntry> agg = aggregateEntries(parseArenaText(text));

  ASSERT_EQ(agg.size(), 2u);
  EXPECT_EQ(agg.at(0).quantity, 3);
  EXPECT_EQ(agg.at(0).name, "Clockwork Percussionist");
  EXPECT_EQ(agg.at(1).name, "Mountain");
}

TEST(AggregateNameOnlyAndPrintingFormsStaySeparate, NeverMerge) {
  const std::string text = "2 Burst Lightning\n1 Burst Lightning (MKM) 132\n";

  const std::vector<DeckEntry> agg = aggregateEntries(parseArenaText(text));
  ASSERT_EQ(agg.size(), 2u);
  EXPECT_EQ(agg.at(0).set_code, "");
  EXPECT_EQ(agg.at(0).quantity, 2);
  EXPECT_EQ(agg.at(1).set_code, "MKM");
  EXPECT_EQ(agg.at(1).quantity, 1);
}

TEST(AggregateSaturatingSum, ClampsInsteadOfOverflowing) {
  // Regression: a fuzzer found signed integer overflow when two clamped
  // INT_MAX quantities aggregated. The sum must saturate, never wrap.
  const std::string text = "2147483647 Forest (WAR) 263\n2147483647 Forest (WAR) 263\n";

  const std::vector<DeckEntry> agg = aggregateEntries(parseArenaText(text));
  ASSERT_EQ(agg.size(), 1u);
  EXPECT_EQ(agg.at(0).quantity, 2147483647);
}

TEST(ParseArenaText, OversizedLinesAreSkippedWithoutCrashing) {
  // Regression: a fuzzer (under TSan) found that std::regex backtracks
  // recursively on pathologically long lines, overflowing the stack. Oversized
  // lines are malformed and must be skipped; the text below still parses the
  // one real card line.
  const std::string text = "1 Forest (WAR) 263\n" + std::string(1024 * 1024, 'a') + "\n";

  const std::vector<DeckEntry> agg = aggregateEntries(parseArenaText(text));
  ASSERT_EQ(agg.size(), 1u);
  EXPECT_EQ(agg.at(0).name, "Forest");
}

} // namespace
} // namespace mtgcpp::core
