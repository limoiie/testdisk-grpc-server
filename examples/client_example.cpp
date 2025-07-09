#include <iostream>
#include <memory>
#include <string>
#include <grpcpp/grpcpp.h>
#include "photorec.grpc.pb.h"
#include <thread>

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using photorec::PhotoRecService;

class PhotoRecClient
{
public:
    explicit PhotoRecClient(const std::shared_ptr<Channel>& channel)
        : stub_(PhotoRecService::NewStub(channel))
    {
    }

    // Initialize PhotoRec context
    bool Initialize(const std::string& device, const std::string& recovery_dir,
                    std::string& context_id) const
    {
        photorec::InitializeRequest request;
        request.set_device(device);
        request.set_recovery_dir(recovery_dir);
        request.set_log_mode(1); // Info level logging

        photorec::InitializeResponse response;
        ClientContext context;

        Status status = stub_->Initialize(&context, request, &response);

        if (status.ok() && response.success())
        {
            context_id = response.context_id();
            std::cout << "Initialized PhotoRec context: " << context_id << std::endl;
            return true;
        }
        else
        {
            std::cerr << "Failed to initialize: " << response.error_message() <<
                std::endl;
            return false;
        }
    }

    // Get available disks
    [[nodiscard]] bool GetDisks(const std::string& context_id) const
    {
        photorec::GetDisksRequest request;
        request.set_context_id(context_id);

        photorec::GetDisksResponse response;
        ClientContext context;

        Status status = stub_->GetDisks(&context, request, &response);

        if (status.ok() && response.success())
        {
            std::cout << "\nAvailable disks:" << std::endl;
            for (const auto& disk : response.disks())
            {
                std::cout << "  Device: " << disk.device() << std::endl;
                std::cout << "    Description: " << disk.description() << std::endl;
                std::cout << "    Size: " << disk.size() << " bytes" << std::endl;
                std::cout << "    Model: " << disk.model() << std::endl;
                std::cout << "    Serial: " << disk.serial_no() << std::endl;
                std::cout << std::endl;
            }
            return true;
        }
        else
        {
            std::cerr << "Failed to get disks: " << response.error_message() << std::endl;
            return false;
        }
    }

    // Get partitions on a disk
    [[nodiscard]] bool GetPartitions(const std::string& context_id,
                                     const std::string& device) const
    {
        photorec::GetPartitionsRequest request;
        request.set_context_id(context_id);
        request.set_device(device);

        photorec::GetPartitionsResponse response;
        ClientContext context;

        Status status = stub_->GetPartitions(&context, request, &response);

        if (status.ok() && response.success())
        {
            std::cout << "\nPartitions on " << device << ":" << std::endl;
            for (const auto& partition : response.partitions())
            {
                std::cout << "  Partition " << partition.order() << ":" << std::endl;
                std::cout << "    Name: " << partition.name() << std::endl;
                std::cout << "    Filesystem: " << partition.filesystem() << std::endl;
                std::cout << "    Offset: " << partition.offset() << std::endl;
                std::cout << "    Size: " << partition.size() << " bytes" << std::endl;
                std::cout << "    Status: " << partition.status() << std::endl;
                std::cout << std::endl;
            }
            return true;
        }
        else
        {
            std::cerr << "Failed to get partitions: " << response.error_message() <<
                std::endl;
            return false;
        }
    }

    // Start recovery process
    bool StartRecovery(const std::string& context_id, const std::string& device,
                       int partition_order, const std::string& recovery_dir,
                       std::string& recovery_id) const
    {
        photorec::StartRecoveryRequest request;
        request.set_context_id(context_id);
        request.set_device(device);
        request.set_partition_order(partition_order);
        request.set_recovery_dir(recovery_dir);

        // Configure recovery options
        auto* options = request.mutable_options();
        options->set_paranoid_mode(1);
        options->set_keep_corrupted_files(true);
        options->set_enable_ext2_optimization(true);
        options->set_verbose_output(true);

        photorec::StartRecoveryResponse response;
        ClientContext context;

        Status status = stub_->StartRecovery(&context, request, &response);

        if (status.ok() && response.success())
        {
            recovery_id = response.recovery_id();
            std::cout << "Started recovery process: " << recovery_id << std::endl;
            return true;
        }
        else
        {
            std::cerr << "Failed to start recovery: " << response.error_message() <<
                std::endl;
            return false;
        }
    }

    // Monitor recovery status
    [[nodiscard]] bool MonitorRecovery(const std::string& context_id,
                                       const std::string& recovery_id) const
    {
        while (true)
        {
            photorec::GetRecoveryStatusRequest request;
            request.set_context_id(context_id);
            request.set_recovery_id(recovery_id);

            photorec::GetRecoveryStatusResponse response;
            ClientContext context;

            Status status = stub_->GetRecoveryStatus(&context, request, &response);

            if (status.ok() && response.success())
            {
                const auto& recovery_status = response.status();

                std::cout << "\rStatus: " << recovery_status.status()
                    << " | Files: " << recovery_status.files_recovered()
                    << " | Progress: " << recovery_status.current_offset()
                    << "/" << recovery_status.total_size() << " bytes" << std::flush;

                if (recovery_status.is_complete())
                {
                    std::cout << std::endl;
                    if (!recovery_status.error_message().empty())
                    {
                        std::cout << "Recovery completed with error: "
                            << recovery_status.error_message() << std::endl;
                    }
                    else
                    {
                        std::cout << "Recovery completed successfully!" << std::endl;
                    }
                    break;
                }
            }
            else
            {
                std::cerr << "Failed to get recovery status: " << response.error_message()
                    << std::endl;
                return false;
            }

            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
        return true;
    }

    // Clean up
    [[nodiscard]] bool Cleanup(const std::string& context_id) const
    {
        photorec::CleanupRequest request;
        request.set_context_id(context_id);

        photorec::CleanupResponse response;
        ClientContext context;

        Status status = stub_->Cleanup(&context, request, &response);

        if (status.ok() && response.success())
        {
            std::cout << "Cleaned up context: " << context_id << std::endl;
            return true;
        }
        else
        {
            std::cerr << "Failed to cleanup: " << response.error_message() << std::endl;
            return false;
        }
    }

private:
    std::unique_ptr<PhotoRecService::Stub> stub_;
};

int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        std::cout << "Usage: " << argv[0] << " <server_address> <device_path> [recovery_dir]" << std::endl;
        std::cout << "Example: " << argv[0] << " localhost:50051 /dev/sda /tmp/recovery" << std::endl;
        return 1;
    }

    const std::string server_address = argv[1];
    const std::string device_path = argv[2];
    const std::string recovery_dir = (argc > 3) ? argv[3] : "/tmp/recovery";

    std::cout << "PhotoRec gRPC Client Example" << std::endl;
    std::cout << "Server: " << server_address << std::endl;
    std::cout << "Device: " << device_path << std::endl;
    std::cout << "Recovery dir: " << recovery_dir << std::endl;
    std::cout << std::endl;

    // Create client
    PhotoRecClient client(
        grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials())
    );

    try
    {
        std::string recovery_id;
        std::string context_id;
        // Initialize PhotoRec
        if (!client.Initialize(device_path, recovery_dir, context_id))
        {
            return 1;
        }

        // Get available disks
        if (!client.GetDisks(context_id))
        {
            return 1;
        }

        // Get partitions
        if (!client.GetPartitions(context_id, device_path))
        {
            return 1;
        }

        // Start recovery (entire disk, partition_order = -1)
        if (!client.StartRecovery(context_id, device_path, 255, recovery_dir, recovery_id))
        {
            return 1;
        }

        // Monitor recovery progress
        if (!client.MonitorRecovery(context_id, recovery_id))
        {
            return 1;
        }

        // Clean up
        if (!client.Cleanup(context_id))
        {
            return 1;
        }

        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
