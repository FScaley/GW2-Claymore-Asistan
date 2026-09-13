#pragma once
#include <string>
#include <vector>

struct WikiSection {
    std::string title;
    std::string body;
};

struct WikiTableRow {
    std::vector<std::string> cells;
};

namespace WikiText {

std::string HtmlToText(const std::string& html);

std::vector<WikiTableRow> ExtractTableRows(const std::string& html,
                                           const std::string& classSubstring);

std::string SplitLead(const std::string& text, std::vector<WikiSection>& sections);

std::vector<std::string> ExtractSubCollectionNames(const std::string& wikitext,
                                                   size_t maxNames = 6);

std::string DecodeEntities(const std::string& s);

}
