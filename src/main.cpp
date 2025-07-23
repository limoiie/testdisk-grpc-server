#include <iostream>
#include <csignal>
#include <chrono>
#include <thread>
#include <grpcpp/grpcpp.h>
#include "testdisk_grpc_server.h"
#include "logger.h"

std::atomic<bool> g_shutdown_requested{false};

void SignalHandler(int signal)
{
    testdisk::LOG_INFO("Received signal " + std::to_string(signal) + ", shutting down...");
    g_shutdown_requested = true;
}

int main(int argc, char* argv[])
{
    // Set up signal handlers
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    std::cout << "TestDisk gRPC Server Starting..." << std::endl;
    std::cout << "Copyright (C) 1998-2024 Christophe GRENIER <grenier@cgsecurity.org>" << std::endl;
    std::cout << "This software is free software; you can redistribute it and/or modify" << std::endl;
    std::cout << "it under the terms of the GNU General Public License as published by" << std::endl;
    std::cout << "the Free Software Foundation; either version 2 of the License, or" << std::endl;
    std::cout << "(at your option) any later version." << std::endl;
    std::cout << std::endl;

    // Default server address
    std::string server_address = "0.0.0.0:50051";
    
    // Default log level
    testdisk::LogLevel log_level = testdisk::LogLevel::INFO;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h")
        {
            std::cout << "Usage: " << argv[0] << " [OPTIONS]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  --address, -a ADDRESS    Server address (default: 0.0.0.0:50051)" << std::endl;
            std::cout << "  --log-level, -l LEVEL    Log level: debug, info, warning, error (default: info)" << std::endl;
            std::cout << "  --verbose, -v            Enable verbose logging (same as --log-level debug)" << std::endl;
            std::cout << "  --quiet, -q              Enable quiet logging (same as --log-level error)" << std::endl;
            std::cout << "  --help, -h               Show this help message" << std::endl;
            std::cout << std::endl;
            std::cout << "Log Levels:" << std::endl;
            std::cout << "  debug     - Show all messages (most verbose)" << std::endl;
            std::cout << "  info      - Show info, warning, and error messages" << std::endl;
            std::cout << "  warning   - Show warning and error messages only" << std::endl;
            std::cout << "  error     - Show error messages only (least verbose)" << std::endl;
            std::cout << std::endl;
            std::cout << "Examples:" << std::endl;
            std::cout << "  " << argv[0] << " --address 127.0.0.1:50051" << std::endl;
            std::cout << "  " << argv[0] << " -a 0.0.0.0:8080 --log-level debug" << std::endl;
            std::cout << "  " << argv[0] << " --verbose" << std::endl;
            std::cout << "  " << argv[0] << " --quiet" << std::endl;
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
        else if (arg == "--log-level" || arg == "-l")
        {
            if (i + 1 < argc)
            {
                std::string level_str = argv[++i];
                if (level_str == "debug")
                {
                    log_level = testdisk::LogLevel::DEBUG;
                }
                else if (level_str == "info")
                {
                    log_level = testdisk::LogLevel::INFO;
                }
                else if (level_str == "warning")
                {
                    log_level = testdisk::LogLevel::WARNING;
                }
                else if (level_str == "error")
                {
                    log_level = testdisk::LogLevel::ERROR;
                }
                else
                {
                    std::cerr << "Error: Invalid log level '" << level_str << "'" << std::endl;
                    std::cerr << "Valid levels: debug, info, warning, error" << std::endl;
                    return 1;
                }
            }
            else
            {
                std::cerr << "Error: --log-level requires an argument" << std::endl;
                return 1;
            }
        }
        else if (arg == "--verbose" || arg == "-v")
        {
            log_level = testdisk::LogLevel::DEBUG;
        }
        else if (arg == "--quiet" || arg == "-q")
        {
            log_level = testdisk::LogLevel::ERROR;
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
        // Set the log level
        testdisk::Logger::Instance().SetLogLevel(log_level);
        
        // Log startup information
        std::string level_str = (log_level == testdisk::LogLevel::DEBUG ? "DEBUG" :
                                log_level == testdisk::LogLevel::INFO ? "INFO" :
                                log_level == testdisk::LogLevel::WARNING ? "WARNING" : "ERROR");
        testdisk::LOG_INFO("TestDisk gRPC Server starting with log level: " + level_str);
        
        // Create and start the gRPC server
        testdisk::TestDiskGrpcServer server;

        // Set up shutdown callback
        server.SetShutdownCallback([&]() {
            testdisk::LOG_INFO("Shutdown callback triggered");
            g_shutdown_requested = true;
        });

        if (!server.Start(server_address))
        {
            testdisk::LOG_ERROR("Failed to start server on " + server_address);
            return 1;
        }

        testdisk::LOG_INFO("Server started successfully on " + server_address);
        std::cout << "Press Ctrl+C to stop the server" << std::endl;

        // Wait for shutdown signal
        while (!g_shutdown_requested)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        testdisk::LOG_INFO("Shutting down server...");
        server.Stop();
        server.Wait();

        testdisk::LOG_INFO("Server stopped successfully");
    }
    catch (const std::exception& e)
    {
        testdisk::LOG_ERROR("Fatal error: " + std::string(e.what()));
        return 1;
    }

    return 0;
}
