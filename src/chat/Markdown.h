#pragma once
#include <string>
#include <vector>

namespace Markdown {

enum class Style { Plain, Bold, InlineCode, ChatLink };

struct Token {
    Style style;
    std::string text;
};

enum class LineKind { Blank, Plain, Header, Bullet, Numbered, Rule };

struct Line {
    LineKind kind = LineKind::Plain;
    int level = 0;
    int indent = 0;
    std::string marker;
    std::vector<Token> tokens;
};

std::vector<Line> Parse(const std::string& text);
std::vector<Token> TokenizeInline(const std::string& line);
bool IsChatLink(const std::string& s);

}
