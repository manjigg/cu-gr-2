// 对旧实验的完整可用站点快照重放，精确核对已发现的位置质量反例。
#include "h3d/HbtLocationOptimizer.h"
#include "rsyn/util/Json.h"
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::invalid_argument("usage: h3d_location_replay cases.json result.json");
        std::ifstream file(argv[1]);
        if (!file) throw std::runtime_error("Cannot read replay cases");
        Rsyn::Json input; file >> input;
        Rsyn::Json rows = Rsyn::Json::array();
        for (const auto& item : input.at("cases")) {
            auto xs = item.at("hbt_x").get<std::vector<DBU>>();
            auto ys = item.at("hbt_y").get<std::vector<DBU>>();
            auto available = item.at("available_sites").get<std::vector<int>>();
            std::set<int> free(available.begin(), available.end());
            std::vector<int> blocked;
            for (std::size_t i = 0; i < xs.size() * ys.size(); i++) if (!free.count(i)) blocked.push_back(i);
            h3d::HbtGrid hbt(xs, ys, blocked);
            h3d::HbtGrid routing(item.at("routing_x").get<std::vector<DBU>>(), item.at("routing_y").get<std::vector<DBU>>());
            h3d::AssignedTopology topology;
            int pinIndex = 0;
            for (const auto& node : item.at("nodes")) {
                auto xy = node.at("position_dbu").get<std::vector<DBU>>();
                std::vector<h3d::PinReference> pins;
                for (const auto& die : node.at("pin_dies")) pins.push_back({static_cast<h3d::Die>(die.get<int>()), pinIndex++, 0});
                topology.tree.addNode({xy.at(0), xy.at(1)}, pins);
            }
            for (const auto& edge : item.at("edges")) {
                topology.tree.addEdge(edge.at(0).get<int>(), edge.at(1).get<int>());
                topology.edgeDies.push_back(h3d::Die::Bottom); // 纯位置回放不执行子网分解。
            }
            topology.hbtNodes = item.at("hbt_nodes").get<std::vector<int>>();
            if (topology.hbtNodes.size() != 1) throw std::invalid_argument("Replay oracle requires one HBT");
            int movable = topology.hbtNodes[0];
            for (std::size_t i = 0; i < topology.tree.getNodes().size(); i++) {
                if (static_cast<int>(i) != movable && topology.tree.getNodes()[i].pins.empty()) throw std::invalid_argument("Replay oracle requires all other nodes fixed");
            }
            DBU oracle = std::numeric_limits<DBU>::max();
            for (int site : available) {
                auto p = hbt.point(site);
                DBU value = 0;
                for (const auto& edge : topology.tree.getEdges()) {
                    auto u = edge.u == movable ? p : topology.tree.getNodes()[edge.u].position;
                    auto v = edge.v == movable ? p : topology.tree.getNodes()[edge.v].position;
                    value += utils::Dist(u, v);
                }
                oracle = std::min(oracle, value);
            }
            auto result = h3d::HbtLocationOptimizer().run(topology, hbt, routing);
            if (oracle != item.at("expected_cost_dbu").get<DBU>() || result.wirelength != oracle) throw std::runtime_error("Historical location gap is not repaired");
            for (std::size_t i = 0; i < topology.tree.getNodes().size(); i++) if (static_cast<int>(i) != movable &&
                result.positions[i] != topology.tree.getNodes()[i].position) throw std::runtime_error("Replay moved a fixed pin");
            rows.push_back({{"net", item.at("name")}, {"old_cost_dbu", item.at("old_cost_dbu")},
                            {"repaired_cost_dbu", result.wirelength}, {"all_site_oracle_dbu", oracle}, {"gap_dbu", 0}});
        }
        Rsyn::Json report = {{"status", "passed"}, {"case_count", rows.size()}, {"cases", rows}};
        std::ofstream output(argv[2]);
        if (!output) throw std::runtime_error("Cannot write replay report");
        output << report.dump(2) << '\n';
        std::cout << "Historical location cases repaired: " << rows.size() << ", all full-site gaps = 0\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
