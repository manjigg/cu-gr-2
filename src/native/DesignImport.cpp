#include "DefBlockages.h"
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/vector.hpp>
#include <ext/stdio_filebuf.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>

std::unique_ptr<Design> Design::importDie(const Parameters& params, const std::string& lef,
                                          const std::string& def) {
    if (!std::ifstream(lef) || !std::ifstream(def)) throw std::runtime_error("Cannot open " + lef + " or " + def);
    // 匿名文件避免 pipe 写满与父进程 wait 互锁，关闭后由内核回收。
    std::unique_ptr<FILE, decltype(&fclose)> file(tmpfile(), fclose);
    if (!file) throw std::runtime_error("Cannot create design snapshot");
    std::cout.flush();
    const pid_t pid = fork();
    if (pid < 0) throw std::runtime_error("Cannot fork Rsyn importer");
    if (pid == 0) {
        try {
            Design die(params, Empty{});
            die.read(lef, def);
            auto blocks=native3d::routingBlockages(die,def);
            die.obstacles.insert(die.obstacles.end(),blocks.begin(),blocks.end());
            __gnu_cxx::stdio_filebuf<char> buf(file.get(), std::ios::out);
            std::ostream out(&buf);
            { boost::archive::binary_oarchive archive(out); die.snapshot(archive); }
            out.flush();
            if (!out || fflush(file.get())) throw std::runtime_error("Cannot write design snapshot");
            std::cout.flush();
            _exit(0);
        } catch (const std::exception& e) {
            std::cerr << "Rsyn importer: " << e.what() << std::endl;
            _exit(1);
        }
    }
    int status;
    pid_t waited;
    do { waited = waitpid(pid, &status, 0); } while (waited < 0 && errno == EINTR);
    if (waited != pid || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
        throw std::runtime_error("Rsyn import failed for " + def);
    if (fseek(file.get(), 0, SEEK_SET)) throw std::runtime_error("Cannot rewind design snapshot");
    __gnu_cxx::stdio_filebuf<char> buf(file.get(), std::ios::in);
    std::istream in(&buf);
    boost::archive::binary_iarchive archive(in);
    std::unique_ptr<Design> die(new Design(params, Empty{}));
    die->snapshot(archive);
    die->setUnitCosts();
    return die;
}
