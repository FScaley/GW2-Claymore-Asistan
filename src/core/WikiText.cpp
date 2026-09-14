#include "WikiText.h"
#include <regex>
#include <cctype>
#include <algorithm>
#include <set>

namespace {

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

void AppendUtf8(std::string& out, unsigned long cp) {
    if (cp < 0x80) out += (char)cp;
    else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
    else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
}

std::string ExtractAttr(const std::string& attrs, const std::string& name) {
    std::string lower = ToLower(attrs);
    auto pos = lower.find(name + "=");
    if (pos == std::string::npos) return "";
    pos += name.size() + 1;
    if (pos >= attrs.size()) return "";
    char q = attrs[pos];
    if (q == '"' || q == '\'') {
        auto end = attrs.find(q, pos + 1);
        if (end == std::string::npos) return "";
        return attrs.substr(pos + 1, end - pos - 1);
    }
    auto end = attrs.find_first_of(" \t\n>", pos);
    return attrs.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
}

bool ClassHasAny(const std::string& cls, const std::vector<const char*>& needles) {
    std::string lower = ToLower(cls);
    for (auto n : needles)
        if (lower.find(n) != std::string::npos) return true;
    return false;
}

std::string CollapseWhitespace(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    std::string line;
    int blankRun = 0;

    auto flushLine = [&]() {
        std::string t = Trim(line);
        while (!t.empty() && (t.front() == '|')) t = Trim(t.substr(1));
        while (!t.empty() && (t.back() == '|')) t = Trim(t.substr(0, t.size() - 1));
        bool onlySep = true;
        for (char c : t) if (c != '|' && c != ' ' && c != '-') { onlySep = false; break; }
        if (t.empty() || onlySep) {
            if (blankRun < 1) { out += '\n'; }
            blankRun++;
        } else {
            std::string squashed;
            bool prevSpace = false;
            for (char c : t) {
                if (c == ' ' || c == '\t') { if (!prevSpace) squashed += ' '; prevSpace = true; }
                else { squashed += c; prevSpace = false; }
            }
            out += squashed;
            out += '\n';
            blankRun = 0;
        }
        line.clear();
    };

    for (char c : in) {
        if (c == '\n') flushLine();
        else if (c != '\r') line += c;
    }
    if (!line.empty()) flushLine();
    return Trim(out);
}

}

namespace WikiText {

std::string DecodeEntities(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    size_t i = 0, n = s.size();
    while (i < n) {
        if (s[i] != '&') { out += s[i++]; continue; }
        auto semi = s.find(';', i);
        if (semi == std::string::npos || semi - i > 10) { out += s[i++]; continue; }
        std::string ent = s.substr(i + 1, semi - i - 1);
        i = semi + 1;
        if (ent == "amp") out += '&';
        else if (ent == "lt") out += '<';
        else if (ent == "gt") out += '>';
        else if (ent == "quot") out += '"';
        else if (ent == "apos") out += '\'';
        else if (ent == "nbsp") out += ' ';
        else if (!ent.empty() && ent[0] == '#') {
            unsigned long cp = 0;
            try {
                if (ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X'))
                    cp = std::stoul(ent.substr(2), nullptr, 16);
                else
                    cp = std::stoul(ent.substr(1), nullptr, 10);
            } catch (...) { cp = 0; }
            if (cp == 160) out += ' ';
            else if (cp > 0) AppendUtf8(out, cp);
        } else {
            out += '&'; out += ent; out += ';';
        }
    }
    return out;
}

std::string HtmlToText(const std::string& html) {
    std::string out;
    out.reserve(html.size() / 4);

    size_t i = 0, n = html.size();
    std::string skipTag;
    int skipDepth = 0;
    int headerLevel = 0;
    std::string headerBuf;

    auto emit = [&](const std::string& s) {
        if (headerLevel > 0) headerBuf += s; else out += s;
    };
    auto emitChar = [&](char c) {
        if (headerLevel > 0) headerBuf += c; else out += c;
    };

    std::string lowerHtml = ToLower(html);

    while (i < n) {
        char c = html[i];
        if (c == '<') {
            if (html.compare(i, 4, "<!--") == 0) {
                auto end = html.find("-->", i);
                i = (end == std::string::npos) ? n : end + 3;
                continue;
            }
            auto end = html.find('>', i);
            if (end == std::string::npos) break;
            std::string tag = html.substr(i + 1, end - i - 1);
            i = end + 1;

            bool closing = !tag.empty() && tag[0] == '/';
            if (closing) tag = tag.substr(1);
            bool selfClosing = !tag.empty() && tag.back() == '/';
            auto sp = tag.find_first_of(" \t\n\r/");
            std::string name = ToLower(tag.substr(0, sp));
            std::string attrs = (sp == std::string::npos) ? "" : tag.substr(sp);

            if (skipDepth > 0) {
                if (name == skipTag) {
                    if (closing) skipDepth--;
                    else if (!selfClosing) skipDepth++;
                }
                continue;
            }

            if (!closing) {
                if (name == "script" || name == "style") {
                    auto close = lowerHtml.find("</" + name, i);
                    if (close == std::string::npos) { i = n; break; }
                    auto closeEnd = html.find('>', close);
                    i = (closeEnd == std::string::npos) ? n : closeEnd + 1;
                    continue;
                }
                std::string cls = ExtractAttr(attrs, "class");
                std::string id = ExtractAttr(attrs, "id");
                if (ClassHasAny(cls, {"toc", "navbox", "mw-editsection", "noprint",
                                      "infobox", "thumb", "mw-references-wrap",
                                      "catlinks", "printfooter", "mw-indicators",
                                      "build-header"})
                    || ToLower(id) == "toc") {
                    skipTag = name; skipDepth = 1; continue;
                }
                if (name == "sup" && ClassHasAny(cls, {"reference"})) {
                    skipTag = name; skipDepth = 1; continue;
                }
            }

            if (name == "tr") {
                if (closing) emitChar('\n');
            } else if (name == "td" || name == "th") {
                if (!closing) {
                    if (!out.empty() && out.back() != '\n') emit(" | ");
                }
            } else if (name == "li") {
                if (!closing) emit("\n- ");
            } else if (name == "br") {
                emitChar('\n');
            } else if (name == "p" || name == "div" || name == "table" ||
                       name == "ul" || name == "ol" || name == "dl" || name == "dd" ||
                       name == "dt" || name == "blockquote") {
                if (closing) emitChar('\n');
                else if (name == "dt") emitChar('\n');
            } else if (name.size() == 2 && name[0] == 'h' && std::isdigit((unsigned char)name[1])) {
                int lvl = name[1] - '0';
                if (!closing) { headerLevel = lvl; headerBuf.clear(); }
                else if (headerLevel > 0) {
                    std::string t = Trim(headerBuf);
                    headerLevel = 0;
                    if (!t.empty())
                        out += "\n" + std::string(lvl, '#') + " " + t + "\n";
                }
            }
            continue;
        }
        if (skipDepth > 0) { ++i; continue; }
        if (c == '&') {
            auto semi = html.find(';', i);
            if (semi != std::string::npos && semi - i <= 10) {
                emit(DecodeEntities(html.substr(i, semi - i + 1)));
                i = semi + 1;
                continue;
            }
        }
        if (c == '\n' || c == '\r' || c == '\t') { emitChar(' '); ++i; continue; }
        emitChar(c);
        ++i;
    }

    return CollapseWhitespace(out);
}

std::vector<WikiTableRow> ExtractTableRows(const std::string& html,
                                           const std::string& classSubstring) {
    std::vector<WikiTableRow> rows;
    std::string lower = ToLower(html);
    std::string needle = ToLower(classSubstring);

    size_t searchPos = 0;
    while (true) {
        auto tpos = lower.find("<table", searchPos);
        if (tpos == std::string::npos) break;
        auto tend = lower.find('>', tpos);
        if (tend == std::string::npos) break;
        std::string openTag = html.substr(tpos, tend - tpos + 1);
        std::string cls = ToLower(ExtractAttr(openTag, "class"));
        auto tclose = lower.find("</table>", tend);
        if (tclose == std::string::npos) break;
        searchPos = tclose + 8;

        if (cls.find(needle) == std::string::npos) continue;

        std::string tableHtml = html.substr(tend + 1, tclose - tend - 1);
        std::string tableLower = lower.substr(tend + 1, tclose - tend - 1);

        size_t rpos = 0;
        while (true) {
            auto trOpen = tableLower.find("<tr", rpos);
            if (trOpen == std::string::npos) break;
            auto trOpenEnd = tableLower.find('>', trOpen);
            if (trOpenEnd == std::string::npos) break;
            auto trClose = tableLower.find("</tr>", trOpenEnd);
            if (trClose == std::string::npos) break;
            rpos = trClose + 5;

            std::string rowHtml = tableHtml.substr(trOpenEnd + 1, trClose - trOpenEnd - 1);
            std::string rowLower = tableLower.substr(trOpenEnd + 1, trClose - trOpenEnd - 1);

            WikiTableRow row;
            bool isHeader = false;
            size_t cpos = 0;
            while (true) {
                auto tdPos = rowLower.find("<td", cpos);
                auto thPos = rowLower.find("<th", cpos);
                size_t cellPos;
                bool th = false;
                if (tdPos == std::string::npos && thPos == std::string::npos) break;
                if (thPos != std::string::npos && (tdPos == std::string::npos || thPos < tdPos)) {
                    cellPos = thPos; th = true;
                } else {
                    cellPos = tdPos;
                }
                auto cellOpenEnd = rowLower.find('>', cellPos);
                if (cellOpenEnd == std::string::npos) break;
                std::string closeTag = th ? "</th>" : "</td>";
                auto cellClose = rowLower.find(closeTag, cellOpenEnd);
                if (cellClose == std::string::npos) cellClose = rowLower.size();
                cpos = cellClose + closeTag.size();
                if (th) isHeader = true;

                std::string cellHtml = rowHtml.substr(cellOpenEnd + 1, cellClose - cellOpenEnd - 1);
                std::string text = HtmlToText(cellHtml);
                std::replace(text.begin(), text.end(), '\n', ' ');
                row.cells.push_back(Trim(text));
            }
            if (!row.cells.empty() && !isHeader)
                rows.push_back(std::move(row));
        }
    }
    return rows;
}

std::string SplitLead(const std::string& text, std::vector<WikiSection>& sections) {
    sections.clear();
    std::string lead;
    WikiSection* current = nullptr;
    size_t pos = 0;
    while (pos <= text.size()) {
        auto nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = (nl == std::string::npos) ? text.size() + 1 : nl + 1;

        if (line.size() > 3 && line.compare(0, 3, "## ") == 0) {
            sections.push_back({Trim(line.substr(3)), ""});
            current = &sections.back();
            continue;
        }
        if (current) { current->body += line; current->body += '\n'; }
        else { lead += line; lead += '\n'; }
    }
    for (auto& s : sections) s.body = Trim(s.body);
    return Trim(lead);
}

std::vector<std::string> ExtractSubCollectionNames(const std::string& wikitext,
                                                   size_t maxNames) {
    std::vector<std::string> names;
    std::set<std::string> seen;
    static const std::regex patterns[] = {
        std::regex("\\{\\{achievement icon\\|(?:page=)?([^|}]+)", std::regex::icase),
        std::regex("\\{\\{collection\\|([^|}]+)", std::regex::icase),
    };
    for (auto& re : patterns) {
        for (auto it = std::sregex_iterator(wikitext.begin(), wikitext.end(), re);
             it != std::sregex_iterator(); ++it) {
            std::string name = Trim((*it)[1].str());
            auto hash = name.find('#');
            if (hash != std::string::npos) name = Trim(name.substr(0, hash));
            if (name.empty()) continue;
            std::string key = ToLower(name);
            if (key.find("(achievements)") != std::string::npos) continue;
            if (seen.count(key)) continue;
            seen.insert(key);
            names.push_back(name);
            if (names.size() >= maxNames) return names;
        }
    }
    return names;
}

}
