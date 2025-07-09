#include "photorec_grpc_server.h"
#include <iostream>
#include <random>

namespace photorec
{
    PhotoRecGrpcServer::~PhotoRecGrpcServer()
    {
        Stop();

        // Clean up all contexts
        std::lock_guard<std::mutex> lock(contexts_mutex_);
        // ReSharper disable once CppUseElementsView
        for (const auto& pair : contexts_)
        {
            if (pair.second)
            {
                finish_photorec(pair.second);
            }
        }
        contexts_.clear();
    }

    std::string PhotoRecGrpcServer::GenerateContextId()
    {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> dis(0, 15);
        static const char* hex_chars = "0123456789abcdef";

        std::string id = "ctx_";
        for (int i = 0; i < 16; ++i)
        {
            id += hex_chars[dis(gen)];
        }
        return id;
    }

    std::string PhotoRecGrpcServer::GenerateRecoveryId()
    {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> dis(0, 15);
        static const char* hex_chars = "0123456789abcdef";

        std::string id = "rec_";
        for (int i = 0; i < 16; ++i)
        {
            id += hex_chars[dis(gen)];
        }
        return id;
    }

    ph_cli_context_t* PhotoRecGrpcServer::GetContext(const std::string& context_id)
    {
        std::lock_guard<std::mutex> lock(contexts_mutex_);
        auto it = contexts_.find(context_id);
        return (it != contexts_.end()) ? it->second : nullptr;
    }

    RecoverySession* PhotoRecGrpcServer::GetRecoverySession(
        const std::string& recovery_id)
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto it = recovery_sessions_.find(recovery_id);
        return (it != recovery_sessions_.end()) ? it->second.get() : nullptr;
    }

    grpc::Status PhotoRecGrpcServer::Initialize(grpc::ServerContext* context,
                                                const InitializeRequest* request,
                                                InitializeResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        try
        {
            // Create PhotoRec context
            char* argv[] = {const_cast<char*>("photorec"), nullptr};
            ph_cli_context_t* ctx = init_photorec(1, argv,
                                                  const_cast<char*>(request->
                                                      recovery_dir().c_str()),
                                                  const_cast<char*>(request->device().
                                                      c_str()),
                                                  request->log_mode(),
                                                  request->log_file().empty()
                                                      ? nullptr
                                                      : request->log_file().c_str());

            if (!ctx)
            {
                response->set_success(false);
                response->set_error_message("Failed to initialize PhotoRec context");
                return grpc::Status::OK;
            }

            // Generate unique context ID
            std::string context_id = GenerateContextId();

            // Store context
            {
                std::lock_guard<std::mutex> lock(contexts_mutex_);
                contexts_[context_id] = ctx;
            }

            response->set_success(true);
            response->set_context_id(context_id);

            return grpc::Status::OK;
        }
        catch (const std::exception& e)
        {
            response->set_success(false);
            response->set_error_message(std::string("Initialization error: ") + e.what());
            return grpc::Status::OK;
        }
    }

    grpc::Status PhotoRecGrpcServer::GetDisks(grpc::ServerContext* context,
                                              const GetDisksRequest* request,
                                              GetDisksResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        try
        {
            ph_cli_context_t* ctx = GetContext(request->context_id());
            if (!ctx)
            {
                response->set_success(false);
                response->set_error_message("Invalid context ID");
                return grpc::Status::OK;
            }

            // Iterate through available disks
            list_disk_t* disk_list = ctx->list_disk;
            while (disk_list)
            {
                if (const disk_t* disk = disk_list->disk)
                {
                    DiskInfo* proto_disk = response->add_disks();
                    ConvertDiskInfo(disk, proto_disk);
                }
                disk_list = disk_list->next;
            }

            response->set_success(true);
            return grpc::Status::OK;
        }
        catch (const std::exception& e)
        {
            response->set_success(false);
            response->set_error_message(std::string("Get disks error: ") + e.what());
            return grpc::Status::OK;
        }
    }

    grpc::Status PhotoRecGrpcServer::GetPartitions(grpc::ServerContext* context,
                                                   const GetPartitionsRequest* request,
                                                   GetPartitionsResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        try
        {
            ph_cli_context_t* ctx = GetContext(request->context_id());
            if (!ctx)
            {
                response->set_success(false);
                response->set_error_message("Invalid context ID");
                return grpc::Status::OK;
            }

            // Change to the specified disk
            disk_t* disk = change_disk(ctx, request->device().c_str());
            if (!disk)
            {
                response->set_success(false);
                response->set_error_message(
                    "Failed to access device: " + request->device());
                return grpc::Status::OK;
            }

            // Get partitions
            list_part_t* part_list = ctx->list_part;
            while (part_list)
            {
                if (const partition_t* partition = part_list->part)
                {
                    PartitionInfo* proto_partition = response->add_partitions();
                    ConvertPartitionInfo(partition, proto_partition);
                }
                part_list = part_list->next;
            }

            response->set_success(true);
            return grpc::Status::OK;
        }
        catch (const std::exception& e)
        {
            response->set_success(false);
            response->set_error_message(std::string("Get partitions error: ") + e.what());
            return grpc::Status::OK;
        }
    }

    grpc::Status PhotoRecGrpcServer::StartRecovery(grpc::ServerContext* context,
                                                   const StartRecoveryRequest* request,
                                                   StartRecoveryResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        try
        {
            ph_cli_context_t* ctx = GetContext(request->context_id());
            if (!ctx)
            {
                response->set_success(false);
                response->set_error_message("Invalid context ID");
                return grpc::Status::OK;
            }

            // Generate recovery session ID
            std::string recovery_id = GenerateRecoveryId();

            // Create recovery session
            auto session = std::make_unique<RecoverySession>();
            session->id = recovery_id;
            session->context = ctx;
            session->running = true;

            // Store session
            {
                std::lock_guard<std::mutex> lock(sessions_mutex_);
                recovery_sessions_[recovery_id] = std::move(session);
            }

            // Start recovery in background thread
            RecoverySession* session_ptr = recovery_sessions_[recovery_id].get();
            session_ptr->recovery_thread = std::thread(
                [session_ptr, device = request->device(), partition_order = request->
                    partition_order(), options = request->options()]()
                {
                    RecoveryWorker(session_ptr, device, partition_order, &options);
                });

            response->set_success(true);
            response->set_recovery_id(recovery_id);
            return grpc::Status::OK;
        }
        catch (const std::exception& e)
        {
            response->set_success(false);
            response->set_error_message(std::string("Start recovery error: ") + e.what());
            return grpc::Status::OK;
        }
    }

    grpc::Status PhotoRecGrpcServer::GetRecoveryStatus(grpc::ServerContext* context,
                                                       const GetRecoveryStatusRequest*
                                                       request,
                                                       GetRecoveryStatusResponse*
                                                       response)
    {
        (void)context; // Suppress unused parameter warning
        try
        {
            RecoverySession* session = GetRecoverySession(request->recovery_id());
            if (!session)
            {
                response->set_success(false);
                response->set_error_message("Invalid recovery ID");
                return grpc::Status::OK;
            }

            RecoveryStatus* status = response->mutable_status();

            std::lock_guard<std::mutex> lock(session->status_mutex);
            status->set_status(session->status);
            status->set_current_offset(session->current_offset);
            status->set_total_size(session->total_size);
            status->set_files_recovered(session->files_recovered);
            status->set_directories_created(session->directories_created);
            status->set_is_complete(session->completed);
            status->set_error_message(session->error_message);

            response->set_success(true);
            return grpc::Status::OK;
        }
        catch (const std::exception& e)
        {
            response->set_success(false);
            response->set_error_message(std::string("Get status error: ") + e.what());
            return grpc::Status::OK;
        }
    }

    grpc::Status PhotoRecGrpcServer::StopRecovery(grpc::ServerContext* context,
                                                  const StopRecoveryRequest* request,
                                                  StopRecoveryResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        try
        {
            RecoverySession* session = GetRecoverySession(request->recovery_id());
            if (!session)
            {
                response->set_success(false);
                response->set_error_message("Invalid recovery ID");
                return grpc::Status::OK;
            }

            // Stop the recovery
            session->running = false;
            abort_photorec(session->context);

            // Wait for thread to finish
            if (session->recovery_thread.joinable())
            {
                session->recovery_thread.join();
            }

            response->set_success(true);
            return grpc::Status::OK;
        }
        catch (const std::exception& e)
        {
            response->set_success(false);
            response->set_error_message(std::string("Stop recovery error: ") + e.what());
            return grpc::Status::OK;
        }
    }

    grpc::Status PhotoRecGrpcServer::ConfigureOptions(grpc::ServerContext* context,
                                                      const ConfigureOptionsRequest*
                                                      request,
                                                      ConfigureOptionsResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        try
        {
            ph_cli_context_t* ctx = GetContext(request->context_id());
            if (!ctx)
            {
                response->set_success(false);
                response->set_error_message("Invalid context ID");
                return grpc::Status::OK;
            }

            ApplyRecoveryOptions(ctx, request->options());

            response->set_success(true);
            return grpc::Status::OK;
        }
        catch (const std::exception& e)
        {
            response->set_success(false);
            response->set_error_message(
                std::string("Configure options error: ") + e.what());
            return grpc::Status::OK;
        }
    }

    grpc::Status PhotoRecGrpcServer::GetStatistics(grpc::ServerContext* context,
                                                   const GetStatisticsRequest* request,
                                                   GetStatisticsResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        try
        {
            ph_cli_context_t* ctx = GetContext(request->context_id());
            if (!ctx)
            {
                response->set_success(false);
                response->set_error_message("Invalid context ID");
                return grpc::Status::OK;
            }

            // Get file statistics
            if (const file_stat_t* file_stats = ctx->params.file_stats)
            {
                uint32_t total_recovered = 0;
                uint32_t total_failed = 0;

                // Iterate through file statistics
                for (int i = 0; file_stats[i].file_hint != nullptr; ++i)
                {
                    FileTypeStatistics* stat = response->add_statistics();
                    stat->set_file_type(file_stats[i].file_hint->extension);
                    stat->set_recovered(file_stats[i].recovered);
                    stat->set_failed(file_stats[i].not_recovered);
                    stat->set_description(file_stats[i].file_hint->description);

                    total_recovered += file_stats[i].recovered;
                    total_failed += file_stats[i].not_recovered;
                }

                response->set_total_files_recovered(total_recovered);
                response->set_total_files_failed(total_failed);
            }

            response->set_success(true);
            return grpc::Status::OK;
        }
        catch (const std::exception& e)
        {
            response->set_success(false);
            response->set_error_message(std::string("Get statistics error: ") + e.what());
            return grpc::Status::OK;
        }
    }

    grpc::Status PhotoRecGrpcServer::Cleanup(grpc::ServerContext* context,
                                             const CleanupRequest* request,
                                             CleanupResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        try
        {
            ph_cli_context_t* ctx = GetContext(request->context_id());
            if (!ctx)
            {
                response->set_success(false);
                response->set_error_message("Invalid context ID");
                return grpc::Status::OK;
            }

            // Clean up context
            finish_photorec(ctx);

            // Remove from contexts map
            {
                std::lock_guard<std::mutex> lock(contexts_mutex_);
                contexts_.erase(request->context_id());
            }

            response->set_success(true);
            return grpc::Status::OK;
        }
        catch (const std::exception& e)
        {
            response->set_success(false);
            response->set_error_message(std::string("Cleanup error: ") + e.what());
            return grpc::Status::OK;
        }
    }

    void PhotoRecGrpcServer::RecoveryWorker(RecoverySession* session,
                                            const std::string& device,
                                            int partition_order,
                                            const RecoveryOptions* options)
    {
        try
        {
            ph_cli_context_t* ctx = session->context;

            // Apply recovery options
            ApplyRecoveryOptions(ctx, *options);

            // Change to target device
            disk_t* disk = change_disk(ctx, device.c_str());
            if (!disk)
            {
                std::lock_guard<std::mutex> lock(session->status_mutex);
                session->error_message = "Failed to access device: " + device;
                session->completed = true;
                session->running = false;
                return;
            }

            // Set total size
            session->total_size = disk->disk_size;

            // Change partition if specified
            if (partition_order >= 0)
            {
                partition_t* partition = change_part(ctx, partition_order,
                                                     options->enable_ext2_optimization(),
                                                     options->carve_free_space_only());
                if (!partition)
                {
                    std::lock_guard<std::mutex> lock(session->status_mutex);
                    session->error_message = "Failed to access partition: " +
                        std::to_string(partition_order);
                    session->completed = true;
                    session->running = false;
                    return;
                }
                session->total_size = partition->part_size;
            }

            // Start recovery
            UpdateRecoveryStatus(session, STATUS_FIND_OFFSET, 0);

            int result = run_photorec(ctx);

            // Update final status
            {
                std::lock_guard<std::mutex> lock(session->status_mutex);
                if (result == 0)
                {
                    session->status = "Completed successfully";
                }
                else
                {
                    session->status = "Completed with errors";
                    session->error_message = "Recovery process returned error code: " +
                        std::to_string(result);
                }
                session->completed = true;
                session->running = false;
            }
        }
        catch (const std::exception& e)
        {
            std::lock_guard<std::mutex> lock(session->status_mutex);
            session->error_message = std::string("Recovery worker error: ") + e.what();
            session->completed = true;
            session->running = false;
        }
    }

    void PhotoRecGrpcServer::UpdateRecoveryStatus(RecoverySession* session,
                                                  photorec_status_t status,
                                                  uint64_t offset)
    {
        std::lock_guard<std::mutex> lock(session->status_mutex);
        session->status = StatusToString(status);
        session->current_offset = offset;
        session->files_recovered = session->context->params.file_nbr;
    }

    std::string PhotoRecGrpcServer::StatusToString(photorec_status_t status)
    {
        switch (status)
        {
        case STATUS_FIND_OFFSET: return "Finding optimal block alignment";
        case STATUS_UNFORMAT: return "FAT unformat recovery";
        case STATUS_EXT2_ON: return "Main recovery with filesystem optimization";
        case STATUS_EXT2_ON_BF: return "Brute force with filesystem optimization";
        case STATUS_EXT2_OFF: return "Main recovery without filesystem optimization";
        case STATUS_EXT2_OFF_BF: return "Brute force without filesystem optimization";
        case STATUS_EXT2_ON_SAVE_EVERYTHING: return
                "Save everything mode with optimization";
        case STATUS_EXT2_OFF_SAVE_EVERYTHING: return
                "Save everything mode without optimization";
        case STATUS_QUIT: return "Recovery completed";
        default: return "Unknown status";
        }
    }

    void PhotoRecGrpcServer::ConvertDiskInfo(const disk_t* disk, DiskInfo* proto_disk)
    {
        proto_disk->set_device(disk->device ? disk->device : "");
        proto_disk->set_description(disk->description_txt);
        proto_disk->set_size(disk->disk_size);
        proto_disk->set_model(disk->model ? disk->model : "");
        proto_disk->set_serial_no(disk->serial_no ? disk->serial_no : "");
        proto_disk->set_firmware_rev(disk->fw_rev ? disk->fw_rev : "");
    }

    void PhotoRecGrpcServer::ConvertPartitionInfo(const partition_t* partition,
                                                  PartitionInfo* proto_partition)
    {
        proto_partition->set_name(partition->partname);
        proto_partition->set_filesystem(partition->fsname);
        proto_partition->set_offset(partition->part_offset);
        proto_partition->set_size(partition->part_size);
        proto_partition->set_info(partition->info);
        proto_partition->set_order(static_cast<int32_t>(partition->order));

        // Convert status to string
        switch (partition->status)
        {
        case STATUS_DELETED: proto_partition->set_status("Deleted");
            break;
        case STATUS_PRIM: proto_partition->set_status("Primary");
            break;
        case STATUS_PRIM_BOOT: proto_partition->set_status("Primary Boot");
            break;
        case STATUS_LOG: proto_partition->set_status("Logical");
            break;
        case STATUS_EXT: proto_partition->set_status("Extended");
            break;
        case STATUS_EXT_IN_EXT: proto_partition->set_status("Extended in Extended");
            break;
        default: proto_partition->set_status("Unknown");
            break;
        }
    }

    void PhotoRecGrpcServer::ApplyRecoveryOptions(ph_cli_context_t* ctx,
                                                  const RecoveryOptions& options)
    {
        // Apply basic options
        change_options(ctx, options.paranoid_mode(),
                       options.keep_corrupted_files(),
                       options.enable_ext2_optimization(),
                       options.expert_mode(),
                       options.low_memory_mode(),
                       options.verbose_output());

        // Apply file type options
        if (!options.enabled_file_types().empty() || !options.disabled_file_types().
            empty())
        {
            // Convert to arrays for the API
            std::vector<char*> enabled_exts;
            std::vector<char*> disabled_exts;

            for (const auto& ext : options.enabled_file_types())
            {
                enabled_exts.push_back(const_cast<char*>(ext.c_str()));
            }

            for (const auto& ext : options.disabled_file_types())
            {
                disabled_exts.push_back(const_cast<char*>(ext.c_str()));
            }

            change_fileopt(ctx,
                           enabled_exts.data(), static_cast<int>(enabled_exts.size()),
                           disabled_exts.data(), static_cast<int>(disabled_exts.size()));
        }
    }

    bool PhotoRecGrpcServer::Start(const std::string& address)
    {
        if (running_)
        {
            return false;
        }

        server_address_ = address;

        grpc::ServerBuilder builder;
        builder.AddListeningPort(address, grpc::InsecureServerCredentials());
        builder.RegisterService(this);

        server_ = builder.BuildAndStart();
        if (!server_)
        {
            return false;
        }

        running_ = true;
        std::cout << "PhotoRec gRPC Server listening on " << address << std::endl;
        return true;
    }

    void PhotoRecGrpcServer::Stop()
    {
        if (running_ && server_)
        {
            server_->Shutdown();
            running_ = false;
        }
    }

    void PhotoRecGrpcServer::Wait() const
    {
        if (server_)
        {
            server_->Wait();
        }
    }
} // namespace photorec
