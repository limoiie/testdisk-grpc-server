#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <chrono>

// gRPC includes
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

// Generated protobuf includes
#include "testdisk.grpc.pb.h"

// TestDisk API includes
#include "testdisk_api.h"

// Logger includes
#include "logger.h"

namespace testdisk
{
    /**
     * @brief Recovery session information
     */
    struct RecoverySession
    {
        std::string id;
        testdisk_cli_context_t* context;
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
     * @brief TestDisk gRPC Server implementation
     *
     * This class wraps the TestDisk API into a gRPC service, providing
     * remote access to file recovery functionality.
     */
    class TestDiskGrpcServer final : public TestDiskService::Service
    {
    public:
        TestDiskGrpcServer() = default;
        ~TestDiskGrpcServer() override;

        // gRPC service methods
        grpc::Status Initialize(grpc::ServerContext* context,
                                const InitializeRequest* request,
                                InitializeResponse* response) override;

        grpc::Status AddImage(grpc::ServerContext* context,
                              const AddImageRequest* request,
                              AddImageResponse* response) override;

        grpc::Status GetDisks(grpc::ServerContext* context,
                              const GetDisksRequest* request,
                              GetDisksResponse* response) override;

        grpc::Status GetPartitions(grpc::ServerContext* context,
                                   const GetPartitionsRequest* request,
                                   GetPartitionsResponse* response) override;

        grpc::Status GetArchs(grpc::ServerContext* context,
                              const GetArchsRequest* request,
                              GetArchsResponse* response) override;

        grpc::Status SetArchForCurrentDisk(grpc::ServerContext* context,
                                           const SetArchForCurrentDiskRequest* request,
                                           SetArchForCurrentDiskResponse* response) override;

        grpc::Status GetFileOptions(grpc::ServerContext* context,
                                    const GetFileOptionsRequest* request,
                                    GetFileOptionsResponse* response) override;

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

        grpc::Status Shutdown(grpc::ServerContext* context,
                              const ShutdownRequest* request,
                              ShutdownResponse* response) override;

        grpc::Status Heartbeat(grpc::ServerContext* context,
                              const HeartbeatRequest* request,
                              HeartbeatResponse* response) override;

        // ============================================================================
        // PARTITION RECOVERY OPERATIONS - Search and Recovery
        // ============================================================================

        grpc::Status SearchPartitions(grpc::ServerContext* context,
                                      const SearchPartitionsRequest* request,
                                      SearchPartitionsResponse* response) override;

        grpc::Status ValidateDiskGeometry(grpc::ServerContext* context,
                                          const ValidateDiskGeometryRequest* request,
                                          ValidateDiskGeometryResponse* response) override;

        grpc::Status WritePartitionTable(grpc::ServerContext* context,
                                         const WritePartitionTableRequest* request,
                                         WritePartitionTableResponse* response) override;

        grpc::Status DeletePartitionTable(grpc::ServerContext* context,
                                          const DeletePartitionTableRequest* request,
                                          DeletePartitionTableResponse* response) override;

        // ============================================================================
        // PARTITION STRUCTURE OPERATIONS - Navigation and Management
        // ============================================================================

        grpc::Status TestPartitionStructure(grpc::ServerContext* context,
                                            const TestPartitionStructureRequest* request,
                                            TestPartitionStructureResponse* response) override;

        grpc::Status ChangePartitionStatusNext(grpc::ServerContext* context,
                                               const ChangePartitionStatusNextRequest* request,
                                               ChangePartitionStatusNextResponse* response) override;

        grpc::Status ChangePartitionStatusPrev(grpc::ServerContext* context,
                                               const ChangePartitionStatusPrevRequest* request,
                                               ChangePartitionStatusPrevResponse* response) override;

        grpc::Status ChangePartitionType(grpc::ServerContext* context,
                                         const ChangePartitionTypeRequest* request,
                                         ChangePartitionTypeResponse* response) override;

        grpc::Status ListPartitionFiles(grpc::ServerContext* context,
                                        const ListPartitionFilesRequest* request,
                                        ListPartitionFilesResponse* response) override;

        grpc::Status SavePartitionBackup(grpc::ServerContext* context,
                                         const SavePartitionBackupRequest* request,
                                         SavePartitionBackupResponse* response) override;

        grpc::Status LoadPartitionBackup(grpc::ServerContext* context,
                                         const LoadPartitionBackupRequest* request,
                                         LoadPartitionBackupResponse* response) override;

        grpc::Status WriteMbrCode(grpc::ServerContext* context,
                                  const WriteMbrCodeRequest* request,
                                  WriteMbrCodeResponse* response) override;

        grpc::Status EnsureSingleBootablePartition(grpc::ServerContext* context,
                                                   const EnsureSingleBootablePartitionRequest* request,
                                                   EnsureSingleBootablePartitionResponse* response) override;

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

        /**
         * @brief Set the logging level for the server
         * @param level The log level to set
         */
        static void SetLogLevel(const LogLevel level)
        {
            Logger::Instance().SetLogLevel(level);
        }

        /**
         * @brief Set a callback function to be called when shutdown is requested
         * @param callback Function to call when shutdown is requested
         */
        void SetShutdownCallback(std::function<void()> callback)
        {
            shutdown_callback_ = std::move(callback);
        }

    private:
        // Server management
        std::unique_ptr<grpc::Server> server_;
        std::string server_address_;
        std::atomic<bool> running_{false};
        std::function<void()> shutdown_callback_;
        std::chrono::steady_clock::time_point server_start_time_;

        // Context and session management
        std::unordered_map<std::string, testdisk_cli_context_t*> contexts_;
        std::unordered_map<std::string, std::unique_ptr<RecoverySession>>
        recovery_sessions_;
        std::mutex contexts_mutex_;
        std::mutex sessions_mutex_;

        // Utility methods
        static std::string GenerateContextId();
        static std::string GenerateRecoveryId();
        testdisk_cli_context_t* GetContext(const std::string& context_id);
        RecoverySession* GetRecoverySession(const std::string& recovery_id);

        // Recovery thread function
        static void RecoveryWorker(RecoverySession* session,
                                   const std::string& device,
                                   int partition_order,
                                   const std::string& recup_dir, const RecoveryOptions* options);

        // Helper methods
        static void UpdateRecoveryStatus(RecoverySession* session,
                                         testdisk_status_t status,
                                         uint64_t offset);
        static std::string StatusToString(testdisk_status_t status);
        static void ConvertDiskInfo(const disk_t* disk, DiskInfo* proto_disk);
        static void ConvertPartitionInfo(const partition_t* partition,
                                         PartitionInfo* proto_partition);
        static void ApplyRecoveryOptions(testdisk_cli_context_t* ctx,
                                         const RecoveryOptions& options);
    };
} // namespace testdisk
