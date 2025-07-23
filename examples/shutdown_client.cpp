#include <iostream>
#include <memory>
#include <string>
#include <grpcpp/grpcpp.h>
#include "testdisk.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using testdisk::TestDiskService;
using testdisk::ShutdownRequest;
using testdisk::ShutdownResponse;

class ShutdownClient {
public:
    explicit ShutdownClient(std::shared_ptr<Channel> channel)
        : stub_(TestDiskService::NewStub(channel)) {}

    bool ShutdownServer(bool force = false, const std::string& reason = "") {
        ShutdownRequest request;
        request.set_force(force);
        if (!reason.empty()) {
            request.set_reason(reason);
        }

        ShutdownResponse response;
        ClientContext context;

        std::cout << "Sending shutdown request..." << std::endl;
        std::cout << "Force: " << (force ? "true" : "false") << std::endl;
        if (!reason.empty()) {
            std::cout << "Reason: " << reason << std::endl;
        }

        Status status = stub_->Shutdown(&context, request, &response);

        if (status.ok()) {
            if (response.success()) {
                std::cout << "✓ Shutdown request successful" << std::endl;
                std::cout << "Message: " << response.message() << std::endl;
                return true;
            } else {
                std::cout << "✗ Shutdown request failed" << std::endl;
                std::cout << "Error: " << response.error_message() << std::endl;
                return false;
            }
        } else {
            std::cout << "✗ gRPC error: " << status.error_code() << ": " 
                      << status.error_message() << std::endl;
            return false;
        }
    }

private:
    std::unique_ptr<TestDiskService::Stub> stub_;
};

void PrintUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --host, -H HOST      Server host address (default: localhost)" << std::endl;
    std::cout << "  --port, -p PORT      Server port (default: 50051)" << std::endl;
    std::cout << "  --force, -f          Force shutdown even with active recoveries" << std::endl;
    std::cout << "  --reason, -r REASON  Optional reason for shutdown" << std::endl;
    std::cout << "  --help, -h           Show this help message" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << program_name << " --host localhost --port 50051" << std::endl;
    std::cout << "  " << program_name << " --force --reason \"Maintenance\"" << std::endl;
}

int main(int argc, char* argv[]) {
    std::string host = "localhost";
    int port = 50051;
    bool force = false;
    std::string reason = "";

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            return 0;
        } else if (arg == "--host" || arg == "-H") {
            if (i + 1 < argc) {
                host = argv[++i];
            } else {
                std::cerr << "Error: --host requires an argument" << std::endl;
                return 1;
            }
        } else if (arg == "--port" || arg == "-p") {
            if (i + 1 < argc) {
                port = std::stoi(argv[++i]);
            } else {
                std::cerr << "Error: --port requires an argument" << std::endl;
                return 1;
            }
        } else if (arg == "--force" || arg == "-f") {
            force = true;
        } else if (arg == "--reason" || arg == "-r") {
            if (i + 1 < argc) {
                reason = argv[++i];
            } else {
                std::cerr << "Error: --reason requires an argument" << std::endl;
                return 1;
            }
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            std::cerr << "Use --help for usage information" << std::endl;
            return 1;
        }
    }

    try {
        // Create channel
        std::string server_address = host + ":" + std::to_string(port);
        auto channel = grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials());

        // Create client
        ShutdownClient client(channel);

        std::cout << "Connecting to TestDisk gRPC server at " << server_address << std::endl;

        // Send shutdown request
        bool success = client.ShutdownServer(force, reason);

        return success ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}