#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>

// gRPC includes
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

// Generated protobuf includes
#include "photorec.grpc.pb.h"

// PhotoRec API includes
#include "photorec_api.h"

namespace photorec
{
    /**
     * @brief Recovery session information
     */
    struct RecoverySession
    {
        std::string id;
        ph_cli_context_t* context;
        std::thread recovery_thread;
        std::atomic<bool> running{false};
        std::atomic<bool> completed{false};
        std::atomic<uint32_t> files_recovered{0};
        std::atomic<uint32_t> directories_created{0};
        std::atomic<uint64_t> current_offset{0};
        std::atomic<uint64_t> total_size{0};
        std::string status;
        std::string error_message;
        std::mutex status_mutex;
    };

    /**
     * @brief PhotoRec gRPC Server implementation
     *
     * This class wraps the PhotoRec API into a gRPC service, providing
     * remote access to file recovery functionality.
     */
    class PhotoRecGrpcServer final : public PhotoRecService::Service
    {
    public:
        PhotoRecGrpcServer() = default;
        ~PhotoRecGrpcServer() override;

        // gRPC service methods
        grpc::Status Initialize(grpc::ServerContext* context,
                                const InitializeRequest* request,
                                InitializeResponse* response) override;

        grpc::Status GetDisks(grpc::ServerContext* context,
                              const GetDisksRequest* request,
                              GetDisksResponse* response) override;

        grpc::Status GetPartitions(grpc::ServerContext* context,
                                   const GetPartitionsRequest* request,
                                   GetPartitionsResponse* response) override;

        grpc::Status StartRecovery(grpc::ServerContext* context,
                                   const StartRecoveryRequest* request,
                                   StartRecoveryResponse* response) override;

        grpc::Status GetRecoveryStatus(grpc::ServerContext* context,
                                       const GetRecoveryStatusRequest* request,
                                       GetRecoveryStatusResponse* response) override;

        grpc::Status StopRecovery(grpc::ServerContext* context,
                                  const StopRecoveryRequest* request,
                                  StopRecoveryResponse* response) override;

        grpc::Status ConfigureOptions(grpc::ServerContext* context,
                                      const ConfigureOptionsRequest* request,
                                      ConfigureOptionsResponse* response) override;

        grpc::Status GetStatistics(grpc::ServerContext* context,
                                   const GetStatisticsRequest* request,
                                   GetStatisticsResponse* response) override;

        grpc::Status Cleanup(grpc::ServerContext* context,
                             const CleanupRequest* request,
                             CleanupResponse* response) override;

        /**
         * @brief Start the gRPC server
         * @param address Server address (e.g., "0.0.0.0:50051")
         * @return true on success, false on failure
         */
        bool Start(const std::string& address);

        /**
         * @brief Stop the gRPC server
         */
        void Stop();

        /**
         * @brief Wait for server to finish
         */
        void Wait() const;

    private:
        // Server management
        std::unique_ptr<grpc::Server> server_;
        std::string server_address_;
        std::atomic<bool> running_{false};

        // Context and session management
        std::unordered_map<std::string, ph_cli_context_t*> contexts_;
        std::unordered_map<std::string, std::unique_ptr<RecoverySession>>
        recovery_sessions_;
        std::mutex contexts_mutex_;
        std::mutex sessions_mutex_;

        // Utility methods
        static std::string GenerateContextId();
        static std::string GenerateRecoveryId();
        ph_cli_context_t* GetContext(const std::string& context_id);
        RecoverySession* GetRecoverySession(const std::string& recovery_id);

        // Recovery thread function
        static void RecoveryWorker(RecoverySession* session,
                                   const std::string& device,
                                   int partition_order,
                                   const RecoveryOptions* options);

        // Helper methods
        static void UpdateRecoveryStatus(RecoverySession* session,
                                         photorec_status_t status,
                                         uint64_t offset);
        static std::string StatusToString(photorec_status_t status);
        static void ConvertDiskInfo(const disk_t* disk, DiskInfo* proto_disk);
        static void ConvertPartitionInfo(const partition_t* partition,
                                         PartitionInfo* proto_partition);
        static void ApplyRecoveryOptions(ph_cli_context_t* ctx,
                                         const RecoveryOptions& options);
    };
} // namespace photorec
