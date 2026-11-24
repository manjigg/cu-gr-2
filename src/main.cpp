#include "global.h"
#include "obj/Design.h"
#include "gr/GlobalRouter.h"

#ifdef CUGR_NATIVE_3D
#include "native/Reachability.h"
#endif

int main(int argc, char* argv[]) try {
    logeol(2);
    log() << "GLOBAL ROUTER CUGR" << std::endl;
    logeol(2);
    // Parse parameters
    Parameters parameters(argc, argv);
    
    if (!parameters.native_reachability.empty()) {
#ifdef CUGR_NATIVE_3D
        if(parameters.out_file.empty() || std::ifstream(parameters.out_file))
            throw std::invalid_argument("Specify a new reachability report file with -output");
        native3d::MultiDieDesign model(parameters,parameters.native_reachability);
        native3d::RoutingGraph graph(model);
        auto result=native3d::checkStaticReachability(graph);
        std::ofstream out;
        out.exceptions(std::ios::failbit | std::ios::badbit);
        out.open(parameters.out_file);out<<result.dump(2)<<std::endl;out.close();
        return result.at("disconnected").empty()?0:2;
#else
        throw std::runtime_error("Native 3D build option is disabled");
#endif
    }
    if (!parameters.native_design.empty()) {
#ifdef CUGR_NATIVE_3D
        native3d::MultiDieDesign model(parameters,parameters.native_design);
        return GlobalRouter::route(model,parameters.out_file);
#else
        throw std::runtime_error("Native 3D build option is disabled");
#endif
    }
    if (!parameters.inspect_native.empty()) {
#ifdef CUGR_NATIVE_3D
        native3d::MultiDieDesign model(parameters,parameters.inspect_native);
        std::cout << "NATIVE_INPUT " << model.inventory().dump() << std::endl;
        return 0;
#else
        throw std::runtime_error("Native 3D build option is disabled");
#endif
    }
    // Read LEF/DEF
    Design design(parameters);
    
    // Global router
    GlobalRouter globalRouter(design, parameters);
    globalRouter.route();
    globalRouter.write();
    
    logeol();
    log() << "Terminated." << std::endl;
    loghline();
    logmem();
    logeol();
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << std::endl;
    return 1;
}