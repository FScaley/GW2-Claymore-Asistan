#include "chat/Markdown.h"
#include <cstdio>
#include <string>

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? "OK" : "FAIL", what);
    if (!ok) g_fail++;
}

static const char* TEST6_ANSWER = u8R"MD(**Roller Beetle** mount'ını açmak için **Living World Season 4 (Episode 3 - Long Live the Lich)** hikayesine sahip olmanız ve hikayenin **"Forearmed Is Forewarned"** bölümünü tamamlamış olmanız gerekir.

Hikayeyi ilerlettikten sonra Domain of Kourna'daki **Allied Encampment**'ta **Gorrik** ile konuşarak **High Roller** achievement'ını ve ardından mount koleksiyonlarını başlatabilirsiniz.

Mount'ı açmak için sırasıyla tamamlamanız gereken **3 koleksiyon** ve içindeki tüm eşyalar şunlardır:

### 1. Beetle Juice Koleksiyonu (10 Eşya)
Kodonur Crossing, Arkjok Farmlands ve Bitterfly Bayou bölgelerindeki gizli zula/sandıklardan toplandığı gibi son adımda Gorrik ile konuşularak tamamlanır:
- Unlabeled Bottle of Bubbling Liquid
- "Dragon" Protein
- Completely Legal Performance-Enhancing Serum
- Flask of Churning Liquid
- 210 Proof Rotgut
- Calcified Megalodon Fin
- Bottle of Dark Smoke
- Lightning in a Bottle
- Untested Speed Serum
- Full Recovery (Tüm malzemeleri topladıktan sonra Gorrik'e dönerek alınır)

### 2. Beetle Saddle Koleksiyonu (9 Eşya)
Semer (Saddle) yapımı için gerekli parçalar (ilk 7 tanesini bulduktan sonra son 2 tanesi otomatik açılır/görekle tamamlanır):
- Inquest Beetle Notes (Domain of Kourna'daki Awakened Inquest'lerden düşer)
- Inquest Power Schematics (Pogahn Bluffs laboratuvar terminali)
- Shadow Creator's Seal (Domain of Kourna bounty hedefleri)
- Steam Power Coupling (Lornar's Pass - Lake of Lamentation veya Metrica Province'deki Steam yaratıkları)
- Plagued Struts (Domain of Kourna'daki Inquest Golem'leri)
- Anomaly Spark (Ley-Line Anomaly world event'i)
- Mk II Power Inverter (Mount Maelstrom - Inquest Golem Mark II world boss)
- Completed Saddle (Malzemeleri Blish'e götürünce verilir)
- Saddle Up (Semeri Petey'e takarak tamamlanır)

### 3. Beetle Feed Koleksiyonu (8 Eşya)
Beetle'ı beslemek için gerekli malzemeler (ilk 7 tanesini topladıktan sonra son adım açılır):
- Live Plague Scarab (Domain of Kourna'daki "Trample scarabs" event'i)
- Plague Scarab Egg (Gandara, the Moon Fortress meta event'indeki Plague Experiments)
- Junundu Ichor (The Desolation'daki Junundu Wurm'ler)
- Hearty Beetle Slime (Far Silverwastes - Alpha Beetle)
- Desert Luciferin (Domain of Kourna Renown Heart vendor'ından 50 Inscribed Shards karşılığı alınır)
- Frigid Wurm Goo (Domain of Kourna - Ntouka Pond su altındaki Cave Wurm'ler)
- Toxic Spider Yolk (Kessex Hills - Toxic Spider Queen)
- Dinner's Ready (Bütün malzemeleri topladıktan sonra Petey'i besleyerek tamamlanır))MD";

static const char* TEST5_ANSWER = u8R"MD(Kessex Hills'de **Toxic Spider Queen** (Champion Toxic Spider Queen), ilgili eventler/etkinlikler sırasında şu bölgelerde bulunur:

* **Earthlord's Gap** (En yakın waypoint: **Gap Waypoint** `[&BLoDAAA=]`)
* **Viathan's Arm** (En yakın waypoint: **Viathan Waypoint** `[&BBAAAAA=]`)
* Ayrıca Togatl Grounds'un hemen kuzeyinde de görülebilir.

Bu boss yalnızca ilgili canlı dünya etkinlikleri veya eventler aktif olduğunda ortaya çıkar.)MD";

static const char* EDGE_CASES = u8R"MD(# Baslik 1
## Baslik 2 ##
Duz metin `inline kod` ve [&AgEJTQAA] kodu.
---
1. Birinci
2) Ikinci **kalin** madde
  - ic madde
    - daha ic madde
> alinti satiri
Stray ** yildiz ve tek ` backtick
***


son satir)MD";

int main() {
    using namespace Markdown;

    printf("=== TEST 6 answer (Roller Beetle) ===\n");
    {
        auto lines = Parse(TEST6_ANSWER);
        int headers = 0, headersL3 = 0, bullets = 0, plain = 0, blank = 0;
        for (auto& l : lines) {
            if (l.kind == LineKind::Header) { headers++; if (l.level == 3) headersL3++; }
            else if (l.kind == LineKind::Bullet) bullets++;
            else if (l.kind == LineKind::Plain) plain++;
            else if (l.kind == LineKind::Blank) blank++;
        }
        printf("lines=%zu headers=%d (L3=%d) bullets=%d plain=%d blank=%d\n",
               lines.size(), headers, headersL3, bullets, plain, blank);
        Check(headers == 3 && headersL3 == 3, "3 level-3 headers");
        Check(bullets == 27, "27 bullet lines");
        bool firstBold = !lines.empty() && !lines[0].tokens.empty() &&
                         lines[0].tokens[0].style == Style::Bold &&
                         lines[0].tokens[0].text == "Roller ";
        Check(firstBold, "first line starts with Bold token 'Roller '");
        bool anyMarkers = false;
        for (auto& l : lines) for (auto& t : l.tokens)
            if (t.text.find("**") != std::string::npos || t.text.find("###") != std::string::npos) anyMarkers = true;
        Check(!anyMarkers, "no raw ** or ### left in tokens");
        bool dragon = false;
        for (auto& l : lines) if (l.kind == LineKind::Bullet && !l.tokens.empty() && l.tokens[0].text == "\"Dragon\" ") dragon = true;
        Check(dragon, "bullet '\"Dragon\" Protein' preserved with quotes");
    }

    printf("\n=== TEST 5 answer (Toxic Spider Queen) ===\n");
    {
        auto lines = Parse(TEST5_ANSWER);
        int bullets = 0, chatlinks = 0; bool backtick = false; bool bulletBoldFirst = false;
        for (auto& l : lines) {
            if (l.kind == LineKind::Bullet) {
                bullets++;
                if (!l.tokens.empty() && l.tokens[0].style == Style::Bold) bulletBoldFirst = true;
            }
            for (auto& t : l.tokens) {
                if (t.style == Style::ChatLink) chatlinks++;
                if (t.text.find('`') != std::string::npos) backtick = true;
            }
        }
        printf("lines=%zu bullets=%d chatlinks=%d\n", lines.size(), bullets, chatlinks);
        Check(bullets == 3, "3 bullets from '* ' lines");
        Check(bulletBoldFirst, "'* **Earthlord's Gap**' -> Bullet with Bold first token");
        Check(chatlinks == 2, "two ChatLink tokens");
        Check(!backtick, "zero tokens contain a backtick");
        bool exact = false;
        for (auto& l : lines) for (auto& t : l.tokens)
            if (t.style == Style::ChatLink && t.text == "[&BLoDAAA=]") exact = true;
        Check(exact, "ChatLink token text is exactly [&BLoDAAA=]");
    }

    printf("\n=== Edge cases ===\n");
    {
        auto lines = Parse(EDGE_CASES);
        for (size_t i = 0; i < lines.size(); ++i) {
            const char* k = "?";
            switch (lines[i].kind) {
                case LineKind::Blank: k = "Blank"; break;
                case LineKind::Plain: k = "Plain"; break;
                case LineKind::Header: k = "Header"; break;
                case LineKind::Bullet: k = "Bullet"; break;
                case LineKind::Numbered: k = "Numbered"; break;
                case LineKind::Rule: k = "Rule"; break;
            }
            printf("  %2zu %-8s lvl=%d ind=%d mk=%-3s |", i, k, lines[i].level, lines[i].indent, lines[i].marker.c_str());
            for (auto& t : lines[i].tokens) {
                const char* s = t.style == Style::Bold ? "B" : t.style == Style::InlineCode ? "C" : t.style == Style::ChatLink ? "L" : "P";
                printf(" %s:[%s]", s, t.text.c_str());
            }
            printf("\n");
        }
        Check(lines.size() >= 11, "edge parse produced lines");
        Check(lines[0].kind == LineKind::Header && lines[0].level == 1, "# -> Header L1");
        Check(lines[1].kind == LineKind::Header && lines[1].level == 2 &&
              lines[1].tokens.size() == 2 && lines[1].tokens[1].text == "2", "## ... ## -> trailing hashes stripped");
        bool code = false, link = false;
        for (auto& t : lines[2].tokens) { if (t.style == Style::InlineCode && t.text == "inline kod") code = true; if (t.style == Style::ChatLink && t.text == "[&AgEJTQAA]") link = true; }
        Check(code && link, "inline code + bare chatlink on same line");
        Check(lines[3].kind == LineKind::Rule, "--- -> Rule");
        Check(lines[4].kind == LineKind::Numbered && lines[4].marker == "1.", "1. -> Numbered");
        Check(lines[5].kind == LineKind::Numbered && lines[5].marker == "2)" &&
              lines[5].tokens.size() >= 2 && lines[5].tokens[1].style == Style::Bold, "2) with bold -> Numbered + Bold");
        Check(lines[6].kind == LineKind::Bullet && lines[6].indent == 1, "  - -> Bullet indent 1");
        Check(lines[7].kind == LineKind::Bullet && lines[7].indent == 2, "    - -> Bullet indent 2");
        Check(lines[8].kind == LineKind::Plain && lines[8].indent == 1 && !lines[8].tokens.empty() &&
              lines[8].tokens[0].text == "alinti ", "> -> Plain indent 1, marker stripped");
        bool strayClean = true, noLoneSpace = true;
        for (auto& t : lines[9].tokens) {
            if (t.text.find('*') != std::string::npos || t.text.find('`') != std::string::npos) strayClean = false;
            if (t.text == " ") noLoneSpace = false;
        }
        Check(strayClean, "stray ** and ` dropped, text kept");
        Check(noLoneSpace, "no lone-space token left where stray markers were dropped");
        Check(lines[9].tokens.size() == 5, "stray line has exactly 5 word tokens");
        Check(lines[10].kind == LineKind::Rule, "*** -> Rule");
        Check(lines.back().kind == LineKind::Plain && lines.back().tokens.size() == 2, "trailing blanks trimmed, last line 'son satir'");
    }

    printf("\n%s (%d failures)\n", g_fail == 0 ? "=== TUMU GECTI ===" : "=== BASARISIZ ===", g_fail);
    return g_fail == 0 ? 0 : 1;
}
