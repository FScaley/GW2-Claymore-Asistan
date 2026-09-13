#include "Markdown.h"
#include <algorithm>
#include <cctype>

namespace {

void PushWords(std::vector<Markdown::Token>& out, const std::string& text, Markdown::Style style) {
    size_t i = 0, n = text.size();
    while (i < n) {
        size_t sp = text.find(' ', i);
        std::string word;
        if (sp == std::string::npos) { word = text.substr(i); i = n; }
        else { word = text.substr(i, sp - i + 1); i = sp + 1; }
        if (!word.empty()) out.push_back({style, word});
    }
}

void PushRun(std::vector<Markdown::Token>& out, const std::string& text, Markdown::Style style) {
    std::string accum;
    auto flush = [&]() {
        if (!accum.empty()) { PushWords(out, accum, style); accum.clear(); }
    };
    size_t i = 0, n = text.size();
    while (i < n) {
        char c = text[i];
        if (c == '[' && i + 1 < n && text[i + 1] == '&') {
            auto end = text.find(']', i);
            if (end != std::string::npos && Markdown::IsChatLink(text.substr(i, end - i + 1))) {
                flush();
                out.push_back({Markdown::Style::ChatLink, text.substr(i, end - i + 1)});
                i = end + 1;
                continue;
            }
        }
        if (c == '`') {
            auto end = text.find('`', i + 1);
            if (end != std::string::npos && end > i + 1) {
                std::string inner = text.substr(i + 1, end - i - 1);
                flush();
                out.push_back({Markdown::IsChatLink(inner) ? Markdown::Style::ChatLink
                                                          : Markdown::Style::InlineCode, inner});
                i = end + 1;
                continue;
            }
            ++i;
            if (i < n && text[i] == ' ' && (accum.empty() || accum.back() == ' ')) ++i;
            continue;
        }
        accum += c;
        ++i;
    }
    flush();
}

}

namespace Markdown {

bool IsChatLink(const std::string& s) {
    if (s.size() < 4 || s[0] != '[' || s[1] != '&' || s.back() != ']') return false;
    for (size_t i = 2; i + 1 < s.size(); ++i) {
        unsigned char c = (unsigned char)s[i];
        if (!(std::isalnum(c) || c == '+' || c == '/' || c == '=')) return false;
    }
    return true;
}

std::vector<Token> TokenizeInline(const std::string& line) {
    std::vector<Token> tokens;
    std::string accum;
    auto flush = [&]() {
        if (!accum.empty()) { PushRun(tokens, accum, Style::Plain); accum.clear(); }
    };
    size_t i = 0, n = line.size();
    while (i < n) {
        if (line[i] == '*' && i + 1 < n && line[i + 1] == '*') {
            auto end = line.find("**", i + 2);
            if (end != std::string::npos) {
                flush();
                PushRun(tokens, line.substr(i + 2, end - i - 2), Style::Bold);
                i = end + 2;
                continue;
            }
            i += 2;
            if (i < n && line[i] == ' ' && (accum.empty() || accum.back() == ' ')) ++i;
            continue;
        }
        accum += line[i];
        ++i;
    }
    flush();
    return tokens;
}

std::vector<Line> Parse(const std::string& text) {
    std::vector<Line> lines;
    size_t pos = 0;
    while (pos <= text.size()) {
        auto nl = text.find('\n', pos);
        std::string raw = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = (nl == std::string::npos) ? text.size() + 1 : nl + 1;
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();

        Line ln;
        size_t lead = 0;
        while (lead < raw.size() && (raw[lead] == ' ' || raw[lead] == '\t')) ++lead;
        ln.indent = std::min(4, (int)(lead / 2));
        std::string s = raw.substr(lead);
        while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();

        if (s.empty()) {
            ln.kind = LineKind::Blank;
            lines.push_back(ln);
            continue;
        }

        if (s.size() >= 3) {
            char r = s[0];
            if ((r == '-' || r == '*' || r == '_') &&
                std::all_of(s.begin(), s.end(), [r](char c) { return c == r; })) {
                ln.kind = LineKind::Rule;
                lines.push_back(ln);
                continue;
            }
        }

        if (s[0] == '#') {
            size_t h = 0;
            while (h < s.size() && s[h] == '#' && h < 6) ++h;
            if (h < s.size() && s[h] == ' ') {
                ln.kind = LineKind::Header;
                ln.level = (int)h;
                std::string rest = s.substr(h + 1);
                while (!rest.empty() && (rest.back() == '#' || rest.back() == ' ')) rest.pop_back();
                ln.tokens = TokenizeInline(rest);
                lines.push_back(ln);
                continue;
            }
        }

        if ((s[0] == '-' || s[0] == '*' || s[0] == '+') && s.size() >= 2 && s[1] == ' ') {
            ln.kind = LineKind::Bullet;
            ln.tokens = TokenizeInline(s.substr(2));
            lines.push_back(ln);
            continue;
        }
        if (s.size() >= 4 && (unsigned char)s[0] == 0xE2 && (unsigned char)s[1] == 0x80 &&
            (unsigned char)s[2] == 0xA2 && s[3] == ' ') {
            ln.kind = LineKind::Bullet;
            ln.tokens = TokenizeInline(s.substr(4));
            lines.push_back(ln);
            continue;
        }

        {
            size_t d = 0;
            while (d < s.size() && d < 3 && std::isdigit((unsigned char)s[d])) ++d;
            if (d > 0 && d + 1 < s.size() && (s[d] == '.' || s[d] == ')') && s[d + 1] == ' ') {
                ln.kind = LineKind::Numbered;
                ln.marker = s.substr(0, d + 1);
                ln.tokens = TokenizeInline(s.substr(d + 2));
                lines.push_back(ln);
                continue;
            }
        }

        if (s[0] == '>') {
            size_t q = 1;
            while (q < s.size() && s[q] == ' ') ++q;
            s = s.substr(q);
            ln.indent = std::max(ln.indent, 1);
        }

        ln.kind = LineKind::Plain;
        ln.tokens = TokenizeInline(s);
        lines.push_back(ln);
    }
    while (!lines.empty() && lines.back().kind == LineKind::Blank) lines.pop_back();
    return lines;
}

}
