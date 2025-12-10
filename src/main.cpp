#include "Server.hpp"

int main(int argc, char* argv[])
{
    if (argc > 2)
    {
        std::cerr << "Usage: " << argv[0] << " <config-file>\n";
        return 1;
    }
    if (argc == 1)
    {
        std::cout << "Usage: " << argv[0] << " <config-file>\n";
        std::cout << "Starting with default config...\n";
    }
    signal(SIGPIPE, SIG_IGN); 
    return webserv(argc, argv);
}