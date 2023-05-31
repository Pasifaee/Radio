#include "utils.h"

namespace po = boost::program_options;

/** Command line options parsing **/

void get_options(bool sender, const int ac, char* av[], addr_t* address, std::string* name, port_t* ctrl_port, port_t* ui_port, size_t* bsize, port_t* data_port, size_t* psize) {
    std::string ctrl_port_str, ui_port_str, data_port_str;
    ssize_t bsize_test, psize_test;
    // Declare the supported options.
    po::options_description desc("Allowed options");
    desc.add_options()
            ("help", "produce help message")
            ("control-port,C", po::value<std::string>(&ctrl_port_str)->default_value(DFLT_CTRL_PORT),
             "specify port for control protocol");
    if (sender) {
        desc.add_options()
                ("multicast-addr,a", po::value<addr_t>(address), "specify multicast IPv4 address")
                ("audio-port,P", po::value<std::string>(&data_port_str)->default_value(DFLT_DATA_PORT),
                 "specify port for audio transfer")
                ("package-size,p", po::value<ssize_t>(&psize_test)->default_value(DFLT_PSIZE), "set package size")
                ("name,n", po::value<std::string>(name)->default_value(DFLT_NAME), "set the name");
    } else { // Receiver.
        desc.add_options()
                ("discover-addr,d", po::value<addr_t>(address)->default_value(DFLT_DISCOVER_ADDR),
                 "specify IPv4 address for looking up radio stations")
                ("ui-port,U", po::value<std::string>(&ui_port_str)->default_value(DFLT_UI_PORT),
                 "specify port for user interface")
                ("buffer-size,b", po::value<ssize_t>(&bsize_test)->default_value(DFLT_BSIZE), "set buffer size")
                ("name,n", po::value<std::string>(name)->default_value(DFLT_NAME), "set the name of desired sender");
    }

    po::variables_map vm;
    po::store(po::parse_command_line(ac, av, desc), vm);
    if (vm.count("help")) {
        std::cout << desc << "\n";
    }
    po::notify(vm);

    if (sender) {
        if (!vm.count("multicast-addr")) {
            fatal("Specifying the address with -a option is required\n");
        } else {
            in_addr tmp;
            if (inet_aton((*address).c_str(), &tmp) == 0) {
                fatal("Invalid multicast address\n");
            }
        }
    }

    get_udp_address((char*) (*address).data(), 12345); // Check if address is a valid IPv4 address.

    *ctrl_port = read_port(ctrl_port_str.c_str());
    if (!sender)
        *ui_port = read_port(ui_port_str.c_str());
    else
        *data_port = read_port(data_port_str.c_str());

    if (!sender) {
        if (bsize_test <= 0) {
            fatal("Buffer size must be positive.\n");
        } else {
            *bsize = (size_t) bsize_test;
        }
    }
    if (sender) {
        if (psize_test <= 0) {
            fatal("Package size must be positive.\n");
        } else {
            *psize = (size_t) psize_test;
        }
    }
}

/** Creating messages **/

std::string msg_lookup() {
    return LOOKUP_STR + '\n';
}

std::string msg_reply(const addr_t& mcast_addr, port_t data_port, const std::string& name) {
    return REPLY_STR + ' ' + mcast_addr + ' ' + std::to_string(data_port) + ' ' + name + '\n';
}

std::string msg_rexmit(std::vector<uint64_t> packages) {
    if (packages.empty()) {
        fatal("Won't send a re-exmit request for zero packages\n");
    }
    std::string msg = REXMIT_STR + ' ' + std::to_string(packages[0]);
    for (auto pkg : packages) {
        msg += ", " + std::to_string(pkg);
    }
    msg += '\n';

    return msg;
}

std::string get_message_str(message msg) {
    std::string fail_msg = "";
    switch (msg.msg_type) {
        case LOOKUP:
            return msg_lookup();
        case REPLY:
            return msg_reply(msg.mcast_addr, msg.data_port, msg.name);
        case REXMIT:
            return msg_rexmit(msg.packages);
        case INCORRECT:
            fatal("Won't' create a message with type INCORRECT\n");
    }
    return fail_msg;
}

/** Parsing messages **/

/**
 * Trims a string out of spaces at the beginning and at the end.
 * @return Trimmed string.
 */
std::string trim(const std::string& s)
{
    size_t first = s.find_first_not_of(' ');
    size_t last = s.find_last_not_of(' ');
    if (first == std::string::npos) {
        return "";
    }
    return s.substr(first, (last-first+1));
}

bool valid_chars(std::string s) {
    for (auto c : s) {
        if (c != '\n' && (c < 32 | c > 127))
            return false;
    }
    return true;
}

/**
 * Splits given string into parts using a delimiter.
 * Doesn't treat delimiter in the beginning of the string as delimiter.
 * If 2 delimiters are adjacent, treats first one as delimiter and second one as normal character.
 */
std::vector<std::string> split(std::string s, char delimiter) {
    auto res = std::vector<std::string>();
    std::string tmp;
    size_t i = 0;
    bool just_split = false;
    do {
        if (i > 0 && s[i] == delimiter && !just_split) {
            std::string new_str = tmp;
            res.push_back(new_str);
            tmp.clear();
            just_split = true;
        } else {
            tmp += s[i];
            just_split = false;
        }
        i++;
    } while (i < s.length());

    if (!tmp.empty())
        res.push_back(tmp);

    return res;
}

/**
 * Normalizes a string with list of packages.
 * @return String with removed spaces and newline if input is valid. Otherwise, an empty string.
 */
std::string normalize_packages(std::string list) {
    std::string list_normalized;
    std::string fail = "";

    bool last_was_comma = true; // True if we more recently encountered a comma than a digit.
    for (size_t i = 0; i < list.length(); i++) {
        if (i == list.length() - 1 && list[i] == '\n')
            return list_normalized;

        if (std::isdigit(list[i])) {
            list_normalized += list[i];
            last_was_comma = false;
        } else if (list[i] == ',') {
            if (last_was_comma)
                return fail;
            list_normalized += list[i];
            last_was_comma = true;
        } else if (list[i] == ' ') {
            if (i == 0 || i == list.length() - 1)
                continue;
            if (std::isdigit(list[i+1]) && !last_was_comma)
                return fail;
        } else { // We encountered an illegal character.
            return fail;
        }
    }

    if (last_was_comma)
        return fail;

    return list_normalized;
}

message parse_lookup(const std::string& msg_str) {
    message msg{};
    message fail_msg{};
    fail_msg.msg_type = INCORRECT;

    if (msg_str == LOOKUP_STR) {
        msg.msg_type = LOOKUP;
        return msg;
    }

    return fail_msg;
}

message parse_reply(const std::string& msg_str, std::vector<std::string> msg_parts) {
    message msg{};
    msg.msg_type = REPLY;
    message fail_msg{};
    fail_msg.msg_type = INCORRECT;

    if (msg_parts.size() < 4)
        return fail_msg;

    // Check if address argument is a valid multicast dotted address.
    in_addr *tmp;
    if (!inet_aton(msg_parts[1].c_str(), (in_addr*) tmp))
        return fail_msg;

    if (!valid_port(msg_parts[2].c_str()))
        return fail_msg;

    size_t first_args_len = msg_parts[0].length() + msg_parts[1].length() + msg_parts[2].length() + 3;
    std::string name = trim(msg_str.substr(first_args_len, msg_str.length() - first_args_len - 1));
    if (name.empty())
        return fail_msg;

    msg.mcast_addr = msg_parts[1];
    msg.data_port = read_port(msg_parts[2].c_str());
    msg.name = name;
    return msg;
}

message parse_rexmit(const std::string& msg_str) {
    message msg{};
    msg.msg_type = REXMIT;
    message fail_msg{};
    fail_msg.msg_type = INCORRECT;

    std::string packages_list = msg_str.substr((REXMIT_STR).length());
    packages_list = normalize_packages(packages_list);
    if (packages_list.empty())
        return fail_msg;
    auto packages_str = split(packages_list, ',');
    auto packages = std::vector<uint64_t>();

    for (auto pkg_str : packages_str) {
        errno = 0;
        packages.push_back(strtoull(pkg_str.c_str(), nullptr, 10));
        if (errno != 0)
            return fail_msg;
    }

    msg.packages = packages;
    return msg;
}

// TODO: check if message contains illegal characters
message parse_message(std::string msg_str) {
    message fail_msg{};
    fail_msg.msg_type = INCORRECT;

    if (msg_str.back() != '\n' || !valid_chars(msg_str))
        return fail_msg;

    auto msg_parts = split(msg_str, ' ');
    if (msg_parts[0] == LOOKUP_STR + '\n') {
        return parse_lookup(msg_str.substr(0, msg_str.length() - 1));
    } else if (msg_parts[0] == REPLY_STR) {
        return parse_reply(msg_str, msg_parts);
    } else if (msg_parts[0] == REXMIT_STR) {
        return parse_rexmit(msg_str);
    } else {
        return fail_msg;
    }
}
