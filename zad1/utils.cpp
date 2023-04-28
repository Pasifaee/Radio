#include "utils.h"

namespace po = boost::program_options;

void get_options(bool sender, const int ac, char* av[], addr_t* address, port_t* port, size_t* bsize, size_t* psize, std::string* name) {
    // Declare the supported options.
    po::options_description desc("Allowed options");
    desc.add_options()
            ("help", "produce help message")
            ("port,P", po::value<port_t>(port)->default_value(DFLT_PORT), "specify receiver's port for communication")
            ;
    if (sender) {
        desc.add_options()
            ("address,a", po::value<addr_t>(address), "specify receiver's IPv4 address")
            ("package-size,p", po::value<size_t>(psize)->default_value(DFLT_PSIZE), "set package size")
            ("name,n", po::value<std::string>(name)->default_value(DFLT_NAME), "set the name of the sender")
            ;
    } else { // Receiver.
        desc.add_options()
            ("address,a", po::value<addr_t>(address), "specify sender's IPv4 address")
            ("buffer-size,b", po::value<size_t>(bsize)->default_value(DFLT_BSIZE), "set buffer size")
            ;
    }

    po::variables_map vm;
    try {
        po::store(po::parse_command_line(ac, av, desc), vm);
        if (vm.count("help")) {
            std::cout << desc << "\n";
        }
        if (!vm.count("address"))
            throw std::runtime_error("Specifying the address with -a option is required");
    } catch (std::exception& e) { // TODO: if this code repeats, make a new function
        std::cerr << "Error: bad program call\n\t" << e.what() << "\n";
        exit(1);
    }
    po::notify(vm);
}
