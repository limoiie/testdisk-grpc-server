#include <iostream>
#include <csignal>
#include <grpcpp/grpcpp.h>
#include "photorec_grpc_server.h"

std::atomic<bool> g_shutdown_requested{false};

void SignalHandler(int signal)
{
    std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    g_shutdown_requested = true;
}

int main(int argc, char* argv[])
{
    // Set up signal handlers
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    std::cout << "PhotoRec gRPC Server Starting..." << std::endl;
    std::cout << "Copyright (C) 1998-2024 Christophe GRENIER <grenier@cgsecurity.org>" << std::endl;
    std::cout << "This software is free software; you can redistribute it and/or modify" << std::endl;
    std::cout << "it under the terms of the GNU General Public License as published by" << std::endl;
    std::cout << "the Free Software Foundation; either version 2 of the License, or" << std::endl;
    std::cout << "(at your option) any later version." << std::endl;
    std::cout << std::endl;

    // Default server address
    std::string server_address = "0.0.0.0:50051";

    // Parse command line arguments
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h")
        {
            std::cout << "Usage: " << argv[0] << " [OPTIONS]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  --address, -a ADDRESS    Server address (default: 0.0.0.0:50051)" << std::endl;
            std::cout << "  --help, -h               Show this help message" << std::endl;
            std::cout << std::endl;
            std::cout << "Examples:" << std::endl;
            std::cout << "  " << argv[0] << " --address 127.0.0.1:50051" << std::endl;
            std::cout << "  " << argv[0] << " -a 0.0.0.0:8080" << std::endl;
            return 0;
        }
        else if (arg == "--address" || arg == "-a")
        {
            if (i + 1 < argc)
            {
                server_address = argv[++i];
            }
            else
            {
                std::cerr << "Error: --address requires an argument" << std::endl;
                return 1;
            }
        }
        else
        {
            std::cerr << "Unknown option: " << arg << std::endl;
            std::cerr << "Use --help for usage information" << std::endl;
            return 1;
        }
    }

    try
    {
        // Create and start the gRPC server
        photorec::PhotoRecGrpcServer server;

        if (!server.Start(server_address))
        {
            std::cerr << "Failed to start server on " << server_address << std::endl;
            return 1;
        }

        std::cout << "Server started successfully on " << server_address << std::endl;
        std::cout << "Press Ctrl+C to stop the server" << std::endl;

        // Wait for shutdown signal
        while (!g_shutdown_requested)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        std::cout << "Shutting down server..." << std::endl;
        server.Stop();
        server.Wait();

        std::cout << "Server stopped successfully" << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
