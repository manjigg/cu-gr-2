// Allocation-only controlled experiments. No DEF/guide routing success is claimed.
#include "h3d/HbtLocationOptimizer.h"
#include "rsyn/util/Json.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <set>

int main(int argc, char** argv) {
    if (argc != 8) return 2;
    const std::string domain = argv[5], policy = argv[6], selection = argv[7];
    if ((domain != "paper" && domain != "full" && domain != "paired") ||
        (policy != "sequential" && policy != "reserve") || (selection != "all" && selection != "non_io") ||
        (policy == "reserve" && domain != "full")) return 2;
    std::ifstream input(argv[1]), gridFile(argv[2]), manifestFile(argv[3]);
    if (!input || !gridFile || !manifestFile) return 2;
    Rsyn::Json grid, manifest; gridFile >> grid; manifestFile >> manifest;
    std::set<std::string> ioNets;
    for (const auto& net : manifest.at("nets")) if (net.at("contains_external_io").get<bool>()) ioNets.insert(net.at("original_net_name").get<std::string>());
    std::vector<Rsyn::Json> records;
    int selectedCount = 0, crossTotal = 0;
    std::string line;
    while (std::getline(input, line)) {
        auto record = Rsyn::Json::parse(line);
        if (selection == "non_io" && ioNets.count(record.at("name").get<std::string>())) continue;
        selectedCount++; crossTotal += record.at("cross_die").get<bool>();
        records.push_back(std::move(record));
    }
    std::vector<DBU> xs, ys;
    std::vector<int> blocked;
    for (const auto& site : grid.at("sites")) {
        xs.push_back(site.at("position_dbu")[0].get<DBU>()); ys.push_back(site.at("position_dbu")[1].get<DBU>());
        if (site.at("initial_owner").get<int>() != -1) blocked.push_back(site.at("id").get<int>());
    }
    for (auto* axis : {&xs, &ys}) {
        std::sort(axis->begin(), axis->end()); axis->erase(std::unique(axis->begin(), axis->end()), axis->end());
    }
    h3d::HbtGrid hbt(xs, ys, blocked);
    // These probes intentionally use the audited AES 125 um / 2.1 um routing grid.
    std::vector<DBU> axis;
    for (DBU low = 0; low < 250000; low += 4200) axis.push_back((low + std::min(low + 4200, static_cast<DBU>(250000))) / 2);
    h3d::HbtGrid routing(axis, axis);
    Rsyn::Json result = {{"scope", "allocation-only controlled experiment, not a routed result"},
        {"domain", domain}, {"capacity_policy", policy}, {"selection", selection}, {"selected_original_nets", selectedCount},
        {"selected_cross_die_nets", crossTotal}, {"initial_sites", hbt.available()}, {"status", "completed"},
        {"nets", Rsyn::Json::array()}};
    result["argv"] = Rsyn::Json::array();
    for (int i = 0; i < argc; i++) result["argv"].push_back(argv[i]);
    int completed = 0, cross = 0, remaining = crossTotal, budgetFallbacks = 0;
    int shorterFull = 0, moreSitesPaper = 0, moreSitesFull = 0;
    const int initialSites = hbt.available();
    auto start = std::chrono::steady_clock::now();
    for (const auto& record : records) {
        if (!record.at("cross_die").get<bool>()) { completed++; continue; }
        const std::string name = record.at("name");
        try {
            h3d::H3DTree tree;
            int pinId = 0;
            for (const auto& node : record.at("nodes")) {
                std::vector<h3d::PinReference> pins;
                for (const auto& die : node.at("pin_dies")) pins.push_back({static_cast<h3d::Die>(die.get<int>()), pinId++, 0});
                tree.addNode({node.at("position_dbu")[0].get<DBU>(), node.at("position_dbu")[1].get<DBU>()}, pins);
            }
            for (const auto& edge : record.at("edges")) tree.addEdge(edge[0].get<int>(), edge[1].get<int>());
            auto assignment = h3d::DieAssignment().run(tree, record.at("die_assignment_root").get<int>());
            if (assignment.hbtCount != record.at("hbt_count").get<int>()) throw std::logic_error("Quantity DP differs from audited input");
            auto expanded = h3d::expandAssignment(tree, assignment);
            auto seed = record.at("location_seed").get<std::uint32_t>();
            auto siteCount = [](const h3d::LocationResult& value) { return static_cast<int>(std::set<int>(value.hbtSites.begin(), value.hbtSites.end()).size()); };
            h3d::LocationResult chosen;
            Rsyn::Json row = {{"name", name}, {"id", record.at("id")}, {"free_sites_before", hbt.available()}, {"topological_hbts", assignment.hbtCount}};
            if (domain == "paper" || domain == "paired") chosen = h3d::HbtLocationOptimizer().runPaper(expanded, hbt, routing, seed, false);
            else chosen = h3d::HbtLocationOptimizer().runPaperFullDomainAudit(expanded, hbt, routing, seed);
            if (domain == "paired") {
                auto other = h3d::HbtLocationOptimizer().runPaperFullDomainAudit(expanded, hbt, routing, seed);
                if (chosen.paperRoots != other.paperRoots || chosen.paperBfsOrder != other.paperBfsOrder) throw std::logic_error("Domain control changed the paper forest");
                if (other.wirelength > chosen.wirelength) throw std::logic_error("Full-domain control is worse than its subset");
                row["paper_wirelength_dbu"] = chosen.wirelength; row["full_wirelength_dbu"] = other.wirelength;
                row["paper_sites"] = chosen.hbtSites; row["full_sites"] = other.hbtSites;
                shorterFull += other.wirelength < chosen.wirelength;
                moreSitesPaper += siteCount(chosen) > siteCount(other); moreSitesFull += siteCount(other) > siteCount(chosen);
            }
            if (policy == "reserve") {
                int budget = hbt.available() - (remaining - 1);
                if (budget < 1) throw std::runtime_error("Capacity lower bound cannot be covered");
                if (siteCount(chosen) > budget) {
                    chosen = h3d::HbtLocationOptimizer().runSingleSite(expanded, hbt, routing); budgetFallbacks++;
                }
            }
            row["wirelength_dbu"] = chosen.wirelength; row["sites"] = chosen.hbtSites;
            for (int site : chosen.hbtSites) if (hbt.owner(site) != -1) throw std::logic_error("Probe reused another net's site");
            hbt.occupy(chosen.hbtSites, record.at("id").get<int>());
            completed++; cross++; remaining--;
            row["free_sites_after"] = hbt.available();
            if (hbt.available() < remaining && !result.count("first_overcommitment")) result["first_overcommitment"] = {
                {"net", name}, {"used_sites", initialSites - hbt.available()}, {"remaining_cross_die_nets", remaining},
                {"deficit_without_revisiting_previous_allocations", remaining - hbt.available()}};
            result["nets"].push_back(std::move(row));
        } catch (const std::exception& error) {
            result["status"] = "failed"; result["failed_net"] = name; result["error"] = error.what();
            result["free_sites_at_failure"] = hbt.available(); break;
        }
    }
    result["completed_original_nets"] = completed; result["completed_cross_die_nets"] = cross;
    result["unprocessed_original_nets"] = selectedCount - completed; result["remaining_cross_die_nets"] = remaining;
    result["used_sites"] = initialSites - hbt.available(); result["remaining_free_sites"] = hbt.available();
    result["full_domain_shorter_on_same_occupancy_nets"] = shorterFull;
    result["paper_uses_more_sites_on_same_occupancy_nets"] = moreSitesPaper;
    result["full_uses_more_sites_on_same_occupancy_nets"] = moreSitesFull;
    result["budget_fallbacks"] = budgetFallbacks;
    result["wall_seconds"] = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::ofstream output(argv[4]); output << result.dump(2) << '\n';
    result.erase("nets"); std::cout << result.dump(2) << std::endl;
    return 0;
}
