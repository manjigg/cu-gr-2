#include "H3DWriter.h"
#include <regex>
#include <set>
#include <stdexcept>
#include <cctype>

namespace h3d {
namespace {
struct Token {
    std::string value;
    std::size_t begin = 0, end = 0;
};

Token nextToken(const std::string& text, std::size_t& cursor) {
    while (cursor < text.size()) {
        if (std::isspace(static_cast<unsigned char>(text[cursor]))) { cursor++; continue; }
        if (text[cursor] == '#') {
            while (cursor < text.size() && text[cursor] != '\n') cursor++;
            continue;
        }
        break;
    }
    Token token; token.begin = cursor;
    bool quoted = cursor < text.size() && text[cursor] == '"';
    if (quoted) {
        cursor++;
        while (cursor < text.size()) {
            if (text[cursor] == '\\' && cursor + 1 < text.size()) { cursor += 2; continue; }
            if (text[cursor++] == '"') break;
        }
    } else if (cursor < text.size() && (text[cursor] == ';' || text[cursor] == '(' || text[cursor] == ')')) cursor++;
    else while (cursor < text.size() && !std::isspace(static_cast<unsigned char>(text[cursor])) &&
                text[cursor] != ';' && text[cursor] != '(' && text[cursor] != ')') {
        cursor += text[cursor] == '\\' && cursor + 1 < text.size() ? 2 : 1;
    }
    token.end = cursor; token.value = text.substr(token.begin, token.end - token.begin);
    return token;
}

struct Section {
    bool found = false;
    std::size_t begin = 0, end = 0;
    std::string body;
};

// 流式 token 扫描避免对大 DEF 使用递归的整段 std::regex 匹配。
Section findSection(const std::string& text, const std::string& name) {
    std::size_t cursor = 0;
    while (cursor < text.size()) {
        Token token = nextToken(text, cursor);
        if (token.value != name) continue;
        std::size_t lookahead = cursor;
        auto count = nextToken(text, lookahead);
        auto semicolon = nextToken(text, lookahead);
        if (count.value.empty() || count.value.find_first_not_of("0123456789") != std::string::npos || semicolon.value != ";") continue;
        Section result; result.found = true; result.begin = token.begin;
        std::size_t bodyStart = lookahead;
        while (lookahead < text.size()) {
            Token end = nextToken(text, lookahead);
            if (end.value != "END") continue;
            auto afterEnd = lookahead;
            auto sectionName = nextToken(text, afterEnd);
            if (sectionName.value == name) {
                result.end = sectionName.end;
                result.body = text.substr(bodyStart, end.begin - bodyStart);
                return result;
            }
        }
        throw std::runtime_error("Unterminated DEF section " + name);
    }
    return {};
}

std::vector<std::string> records(const std::string& text) {
    std::vector<std::string> result;
    std::string record;
    bool comment = false, quoted = false, escaped = false;
    for (char ch : text) {
        if (comment) { if (ch == '\n') comment = false; else continue; }
        if (!quoted && ch == '#') { comment = true; continue; }
        record += ch;
        if (!escaped && ch == '"') quoted = !quoted;
        if (!quoted && ch == ';') { result.push_back(record); record.clear(); }
        if (ch == '\\' && !escaped) escaped = true; else escaped = false;
    }
    return result;
}

std::string recordName(const std::string& record) {
    // This runs once per original net. Compiling a regex for every record
    // dominated DEF output time on the author benchmarks.
    std::size_t cursor = 0;
    auto dash = nextToken(record, cursor), name = nextToken(record, cursor);
    if (dash.value != "-" || name.value.empty()) throw std::runtime_error("Malformed DEF record");
    return name.value;
}

std::string defInstanceName(const std::string& logical) {
    // DEFControlParser decodes component/connection instance identifiers.
    // Re-escape literal backslashes and delimiters when serializing NETS,
    // otherwise preserved COMPONENTS and new NETS disagree on DEF re-import.
    std::string result;
    for (unsigned char c : logical) {
        if (c == '\\' || c == '(' || c == ')' || c == ';' || c == '"' || std::isspace(c)) result += '\\';
        result += static_cast<char>(c);
    }
    return result;
}

void replaceSection(std::string& text, const std::string& name, const std::string& replacement) {
    auto section = findSection(text, name);
    if (section.found) text.replace(section.begin, section.end - section.begin, replacement);
    else {
        auto nets = findSection(text, "NETS");
        if (name == "PINS" && nets.found) {
            text.insert(nets.begin, replacement + "\n");
            return;
        }
        // 没有 PINS/NETS 时，在 END DESIGN 前创建该段。
        std::smatch end;
        if (!std::regex_search(text, end, std::regex("\\bEND\\s+DESIGN\\b"))) throw std::runtime_error("Missing END DESIGN");
        text.insert(end.position(), replacement + "\n");
    }
}
} // namespace

void writeDieDef(const std::string& input, const std::string& output, const H3DDesign& design,
                 int die, const std::vector<Subnet>& subnets) {
    std::ifstream source(input);
    if (!source) throw std::runtime_error("Cannot read original DEF: " + input);
    std::string text((std::istreambuf_iterator<char>(source)), std::istreambuf_iterator<char>());
    const auto& database = *design.dies[die].design;
    std::map<std::string, std::string> portNets;
    std::vector<const Subnet*> local;
    for (const auto& subnet : subnets) if (subnet.die == die) {
        local.push_back(&subnet);
        for (const auto& pin : subnet.pins) if (pin.pinIndex == -1) {
            std::string name = database.getPinNames(pin).first;
            if (!portNets.emplace(name, subnet.name).second) throw std::runtime_error("A DEF terminal belongs to two subnets");
        }
    }
    std::vector<std::string> oldPins, retainedNets;
    auto pinSection = findSection(text, "PINS");
    if (pinSection.found) oldPins = records(pinSection.body);
    auto netSection = findSection(text, "NETS");
    if (netSection.found) {
        std::set<std::string> signals;
        // 显式小网实验只替换实际处理的网络，其余源 NETS 原样保留。
        // 全量运行时集合覆盖全部原始信号网，行为与原实现一致。
        std::set<int> processed;
        for (const auto* subnet : local) processed.insert(subnet->original);
        for (const auto& net : design.nets) if (processed.count(net.id)) {
            for (const auto& name : net.localNetNames[die]) signals.insert(name);
        }
        for (const auto& record : records(netSection.body)) {
            if (!signals.count(recordName(record))) retainedNets.push_back(record);
        }
    }
    std::stringstream pins;
    pins << "PINS " << oldPins.size() + design.hbtPorts[die].size() << " ;\n";
    for (auto record : oldPins) {
        auto found = portNets.find(recordName(record));
        if (found != portNets.end()) {
            std::smatch net;
            static const std::regex netAttribute("\\+\\s*NET\\s+\\S+");
            if (!std::regex_search(record, net, netAttribute)) throw std::runtime_error("Missing port NET attribute");
            record.replace(net.position(), net.length(), "+ NET " + found->second);
        }
        pins << record << "\n";
    }
    utils::PointT<DBU> half(design.padSize.x / 2, design.padSize.y / 2);
    for (const auto& entry : design.hbtPorts[die]) {
        int site = entry.first;
        const auto& name = design.hbtPortNames[die].at(site);
        auto point = design.physicalHbtPoint(site);
        pins << "- " << name << " + NET " << portNets.at(name) << " + DIRECTION INOUT + USE SIGNAL\n"
             << "  + LAYER " << database.getLayer(design.landingLayers[die]).getName() << " ( "
             << -half.x << " " << -half.y << " ) ( " << design.padSize.x - half.x << " " << design.padSize.y - half.y << " )\n"
             << "  + FIXED ( " << point.x << " " << point.y << " ) N ;\n";
    }
    pins << "END PINS\n";
    std::stringstream nets;
    nets << "NETS " << local.size() + retainedNets.size() << " ;\n";
    for (const auto& record : retainedNets) nets << record << "\n";
    for (const auto* subnet : local) {
        nets << "- " << subnet->name;
        for (const auto& pin : subnet->pins) {
            auto names = database.getPinNames(pin);
            if (pin.pinIndex == -1) nets << " ( PIN " << names.first << " )";
            else nets << " ( " << defInstanceName(names.first) << " " << names.second << " )";
        }
        nets << " + USE " << subnet->use << " ;\n";
    }
    nets << "END NETS\n";
    replaceSection(text, "PINS", pins.str());
    replaceSection(text, "NETS", nets.str());
    std::ofstream target(output);
    if (!target) throw std::runtime_error("Cannot write DEF: " + output);
    target << text;
    if (!target) throw std::runtime_error("Failed to write DEF: " + output);
}

} // namespace h3d
