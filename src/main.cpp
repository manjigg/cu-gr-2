#include "global.h"
#include "obj/Design.h"
#include "gr/GlobalRouter.h"
#include "gr/F2fRouter.h"

int main(int argc, char* argv[]) try {
    logeol(2);
    log() << "GLOBAL ROUTER CUGR" << std::endl;
    logeol(2);
    // Parse parameters
    Parameters parameters(argc, argv);
    if (!parameters.f2f_config.empty()) {
        runF2f(parameters);
        return 0;
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
} catch (const std::exception& error) {
    std::cerr << "Routing failed: " << error.what() << '\n';
    return 1;
}
