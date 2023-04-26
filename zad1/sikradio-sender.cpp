#include <cstdint>
#include <string>
#include <iostream>
#include <boost/program_options.hpp>
#include "common.h"

// TODO:
//  - pamiętaj o zmienianiu kolejności bajtów
//  - jak czytać dane z stdin? std::cin?

// Types.
#define addr_t uint16_t
#define port_t uint16_t

// Default values.
#define DFLT_PORT 27924
#define DFLT_PSIZE 512
#define DFLT_NAME "Nienazwany nadajnik"

// Constants. TODO: wybrać dobre typy
addr_t DEST_ADDR; // IPv4 receiver's address.
port_t DATA_PORT;
size_t PSIZE; // Package size.
std::string NAME; // Sender's name.

namespace po = boost::program_options;

// Set program constants by parsing command line.
void get_options(const int ac, char* av[]) {
    // Declare the supported options.
    po::options_description desc("Allowed options");
    desc.add_options()
            ("help", "produce help message")
            ("receiver-address,a", po::value<addr_t>(&DEST_ADDR), "specify receiver address")
            ("port,P", po::value<port_t>(&DATA_PORT)->default_value(DFLT_PORT), "set receiver's port for communication") // TODO: receiver;s or sender's port?
            ("package-size,p", po::value<size_t>(&PSIZE)->default_value(DFLT_PSIZE), "set package size")
            ("name,n", po::value<std::string>(&NAME)->default_value(DFLT_NAME), "set the name of the sender")
            ;

    po::variables_map vm;
    po::store(po::parse_command_line(ac, av, desc), vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cout << desc << "\n";
        exit(0); // TODO: is this the right exit code? can i do my own quirks like this?
    }
}

// Reads data from stdin and sends them via UDP to DEST_ADDR on port DATA_PORT. It sends
// the data in packages of size PSIZE.
void send_data() {

}

int main(int argc, char* argv[]) {
    get_options(argc, argv);
    return 0;
}