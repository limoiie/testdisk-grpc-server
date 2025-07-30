#include "testdisk_grpc_server.h"
#include "logger.h"
#include <random>

namespace testdisk
{
    TestDiskGrpcServer::~TestDiskGrpcServer()
    {
        LOG_INFO("TestDisk gRPC Server destructor called");
        Stop();

        // Clean up all contexts
        std::lock_guard<std::mutex> lock(contexts_mutex_);
        LOG_DEBUG("Cleaning up " + std::to_string(contexts_.size()) + " contexts");
        // ReSharper disable once CppUseElementsView
        for (const auto& pair : contexts_)
        {
            if (pair.second)
            {
                LOG_DEBUG("Finishing TestDisk context: " + pair.first);
                finish_testdisk(pair.second);
            }
        }
        contexts_.clear();
        LOG_INFO("TestDisk gRPC Server cleanup completed");
    }

    std::string TestDiskGrpcServer::GenerateContextId()
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
        LOG_DEBUG("Generated context ID: " + id);
        return id;
    }

    std::string TestDiskGrpcServer::GenerateRecoveryId()
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
        LOG_DEBUG("Generated recovery ID: " + id);
        return id;
    }

    testdisk_cli_context_t* TestDiskGrpcServer::GetContext(const std::string& context_id)
    {
        std::lock_guard<std::mutex> lock(contexts_mutex_);
        auto it = contexts_.find(context_id);
        if (it != contexts_.end())
        {
            LOG_DEBUG("Found context: " + context_id);
            return it->second;
        }
        else
        {
            LOG_WARNING("Context not found: " + context_id);
            return nullptr;
        }
    }

    RecoverySession* TestDiskGrpcServer::GetRecoverySession(
        const std::string& recovery_id)
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto it = recovery_sessions_.find(recovery_id);
        if (it != recovery_sessions_.end())
        {
            LOG_DEBUG("Found recovery session: " + recovery_id);
            return it->second.get();
        }
        else
        {
            LOG_WARNING("Recovery session not found: " + recovery_id);
            return nullptr;
        }
    }

    grpc::Status TestDiskGrpcServer::Initialize(grpc::ServerContext* context,
                                                const InitializeRequest* request,
                                                InitializeResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        LOG_INFO("Initialize request received with " + std::to_string(request->args_size()) + " arguments");

        try
        {
            // Prepare command line arguments from the request
            std::vector<char*> argv_vector;
            std::vector<std::string> arg_strings;

            if (request->args_size() == 0)
            {
                // Default argument if none provided
                arg_strings.emplace_back("testdisk");
            }
            else
            {
                // Convert protobuf repeated string to char* array
                for (const auto& arg : request->args())
                {
                    arg_strings.emplace_back(arg);
                }
            }

            // Create char* array for C API
            for (auto& arg : arg_strings)
            {
                argv_vector.push_back(const_cast<char*>(arg.c_str()));
            }
            argv_vector.push_back(nullptr); // Null terminate

            LOG_DEBUG("Initializing TestDisk context with log mode: " +
                std::to_string(request->log_mode()) + ", argc: " + std::to_string(arg_strings.size()));

            testdisk_cli_context_t* ctx = init_testdisk(static_cast<int>(arg_strings.size()),
                                                  argv_vector.data(),
                                                  request->log_mode(),
                                                  request->log_file().empty()
                                                      ? nullptr
                                                      : request->log_file().c_str());

            if (!ctx)
            {
                LOG_ERROR("Failed to initialize TestDisk context");
                response->set_success(false);
                response->set_error_message("Failed to initialize TestDisk context");
                return grpc::Status::OK;
            }

            // Generate unique context ID
            std::string context_id = GenerateContextId();

            // Store context
            {
                std::lock_guard<std::mutex> lock(contexts_mutex_);
                contexts_[context_id] = ctx;
                LOG_INFO("TestDisk context initialized successfully: " + context_id);
            }

            response->set_success(true);
            response->set_context_id(context_id);

            return grpc::Status::OK;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Initialization error: " + std::string(e.what()));
            response->set_success(false);
            response->set_error_message(std::string("Initialization error: ") + e.what());
            return grpc::Status::OK;
        }
    }

    grpc::Status TestDiskGrpcServer::AddImage(grpc::ServerContext* context,
                                              const AddImageRequest* request,
                                              AddImageResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        LOG_INFO("AddImage request received for context: " + request->context_id() +
            ", Image file: " + request->image_file());

        try
        {
            testdisk_cli_context_t* ctx = GetContext(request->context_id());
            if (!ctx)
            {
                LOG_ERROR("Invalid context ID: " + request->context_id());
                response->set_success(false);
                response->set_error_message("Invalid context ID");
                return grpc::Status::OK;
            }

            // Add image file to context
            LOG_DEBUG("Adding image file: " + request->image_file());
            disk_t* disk = add_image(ctx, request->image_file().c_str());

            if (!disk)
            {
                LOG_ERROR("Failed to add image file: " + request->image_file());
                response->set_success(false);
                response->set_error_message("Failed to add image file: " + request->image_file());
                return grpc::Status::OK;
            }

            LOG_INFO("Image file added successfully: " + request->image_file());
            response->set_success(true);

            // Optionally populate disk info
            DiskInfo* disk_info = response->mutable_disk_info();
            ConvertDiskInfo(disk, disk_info);

            return grpc::Status::OK;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Add image error: " + std::string(e.what()));
            response->set_success(false);
            response->set_error_message(std::string("Add image error: ") + e.what());
            return grpc::Status::OK;
        }
    }

    grpc::Status TestDiskGrpcServer::GetDisks(grpc::ServerContext* context,
                                              const GetDisksRequest* request,
                                              GetDisksResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        LOG_INFO("GetDisks request received for context: " + request->context_id());

        try
        {
            testdisk_cli_context_t* ctx = GetContext(request->context_id());
            if (!ctx)
            {
                LOG_ERROR("Invalid context ID: " + request->context_id());
                response->set_success(false);
                response->set_error_message("Invalid context ID");
                return grpc::Status::OK;
            }

            // Iterate through available disks
            const list_disk_t* disk_list = ctx->list_disk;
            int disk_count = 0;
            while (disk_list)
            {
                if (const disk_t* disk = disk_list->disk)
                {
                    DiskInfo* proto_disk = response->add_disks();
                    ConvertDiskInfo(disk, proto_disk);
                    disk_count++;
                    LOG_DEBUG(
                        "Found disk: " + std::string(disk->device ? disk->device : "") +
                        " (" + std::to_string(disk->disk_size) + " bytes)");
                }
                disk_list = disk_list->next;
            }

            LOG_INFO("Found " + std::to_string(disk_count) + " disks");
            response->set_success(true);
            return grpc::Status::OK;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Get disks error: " + std::string(e.what()));
            response->set_success(false);
            response->set_error_message(std::string("Get disks error: ") + e.what());
            return grpc::Status::OK;
        }
    }

    grpc::Status TestDiskGrpcServer::GetPartitions(grpc::ServerContext* context,
                                                   const GetPartitionsRequest* request,
                                                   GetPartitionsResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        LOG_INFO("GetPartitions request received for device: " + request->device() +
            " (context: " + request->context_id() + ")");

        try
        {
            testdisk_cli_context_t* ctx = GetContext(request->context_id());
            if (!ctx)
            {
                LOG_ERROR("Invalid context ID: " + request->context_id());
                response->set_success(false);
                response->set_error_message("Invalid context ID");
                return grpc::Status::OK;
            }

            // Change to the specified disk
            LOG_DEBUG("Changing to disk: " + request->device());
            if (const disk_t* disk = change_disk(ctx, request->device().c_str()); !disk)
            {
                LOG_ERROR("Failed to access device: " + request->device());
                response->set_success(false);
                response->set_error_message(
                    "Failed to access device: " + request->device());
                return grpc::Status::OK;
            }

            // Get partitions
            int partition_count = 0;
            const list_part_t* part_list = ctx->list_part;
            while (part_list)
            {
                if (const partition_t* partition = part_list->part)
                {
                    PartitionInfo* proto_partition = response->add_partitions();
                    ConvertPartitionInfo(partition, proto_partition);
                    partition_count++;
                    LOG_DEBUG("Found partition: " + std::string(partition->partname) +
                        " (" + std::to_string(partition->part_size) + " bytes)");
                }
                part_list = part_list->next;
            }

            LOG_INFO("Found " + std::to_string(partition_count) + " partitions");
            response->set_success(true);
            return grpc::Status::OK;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Get partitions error: " + std::string(e.what()));
            response->set_success(false);
            response->set_error_message(std::string("Get partitions error: ") + e.what());
            return grpc::Status::OK;
        }
    }

    grpc::Status TestDiskGrpcServer::GetArchs(grpc::ServerContext* context,
                                              const GetArchsRequest* request,
                                              GetArchsResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        LOG_INFO("GetArchs request received for context: " + request->context_id());

        try
        {
            const testdisk_cli_context_t* ctx = GetContext(request->context_id());
            if (!ctx)
            {
                LOG_ERROR("Invalid context ID: " + request->context_id());
                response->set_success(false);
                response->set_error_message("Invalid context ID");
                return grpc::Status::OK;
            }

            // Iterate through available architectures
            if (ctx->list_arch)
            {
                for (int i = 0; ctx->list_arch[i] != nullptr; ++i)
                {
                    if (const arch_fnct_t* arch = ctx->list_arch[i]; arch)
                    {
                        ArchInfo* proto_arch = response->add_architectures();
                        proto_arch->set_name(arch->part_name_option ? arch->part_name_option : "");
                        proto_arch->set_description(arch->part_name ? arch->part_name : "");
                        proto_arch->set_type(arch->msg_part_type ? arch->msg_part_type : "");
                        proto_arch->set_is_available(true);
                        LOG_DEBUG("Found architecture: " + std::string(arch->part_name_option ? arch->part_name_option : ""));
                    }
                }
            }

            LOG_INFO("Found " + std::to_string(response->architectures_size()) + " architectures");
            response->set_success(true);
            return grpc::Status::OK;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Get architectures error: " + std::string(e.what()));
            response->set_success(false);
            response->set_error_message(std::string("Get architectures error: ") + e.what());
            return grpc::Status::OK;
        }
    }

    grpc::Status TestDiskGrpcServer::SetArchForCurrentDisk(grpc::ServerContext* context,
                                                           const SetArchForCurrentDiskRequest* request,
                                                           SetArchForCurrentDiskResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        LOG_INFO("SetArchForCurrentDisk request received - Arch: " + request->arch_name() +
            " (context: " + request->context_id() + ")");

        try
        {
            const testdisk_cli_context_t* ctx = GetContext(request->context_id());
            if (!ctx)
            {
                LOG_ERROR("Invalid context ID: " + request->context_id());
                response->set_success(false);
                response->set_error_message("Invalid context ID");
                return grpc::Status::OK;
            }

            // Set the architecture
            const char* arch_name = request->arch_name().empty() ? nullptr : request->arch_name().c_str();
            if (const arch_fnct_t* selected_arch = change_arch(ctx, const_cast<char*>(arch_name)))
            {
                response->set_success(true);
                response->set_selected_arch(selected_arch->part_name_option ? selected_arch->part_name_option : "");
                LOG_INFO("Architecture set successfully: " + response->selected_arch());
            }
            else
            {
                LOG_ERROR("Failed to set architecture: " + request->arch_name());
                response->set_success(false);
                response->set_error_message("Failed to set architecture: " + request->arch_name());
            }

            return grpc::Status::OK;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Set architecture error: " + std::string(e.what()));
            response->set_success(false);
            response->set_error_message(std::string("Set architecture error: ") + e.what());
            return grpc::Status::OK;
        }
    }

    grpc::Status TestDiskGrpcServer::GetFileOptions(grpc::ServerContext* context,
                                                    const GetFileOptionsRequest* request,
                                                    GetFileOptionsResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        LOG_INFO("GetFileOptions request received for context: " + request->context_id());

        try
        {
            testdisk_cli_context_t* ctx = GetContext(request->context_id());
            if (!ctx)
            {
                LOG_ERROR("Invalid context ID: " + request->context_id());
                response->set_success(false);
                response->set_error_message("Invalid context ID");
                return grpc::Status::OK;
            }

            // Iterate through file type options
            if (ctx->options.list_file_format)
            {
                for (int i = 0; array_file_enable[i].file_hint != nullptr; ++i)
                {
                    const file_enable_t* file_enable = &array_file_enable[i];
                    if (const file_hint_t* file_hint = file_enable->file_hint)
                    {
                        FileTypeOption* proto_option = response->add_file_types();
                        proto_option->set_extension(file_hint->extension ? file_hint->extension : "");
                        proto_option->set_description(file_hint->description ? file_hint->description : "");
                        proto_option->set_max_filesize(file_hint->max_filesize);
                        proto_option->set_is_enabled(file_enable->enable != 0);
                        proto_option->set_enabled_by_default(file_hint->enable_by_default != 0);
                        LOG_DEBUG("Found file type: " + std::string(file_hint->extension ? file_hint->extension : ""));
                    }
                }
            }

            LOG_INFO("Found " + std::to_string(response->file_types_size()) + " file types");
            response->set_success(true);
            return grpc::Status::OK;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Get file options error: " + std::string(e.what()));
            response->set_success(false);
            response->set_error_message(std::string("Get file options error: ") + e.what());
            return grpc::Status::OK;
        }
    }

    grpc::Status TestDiskGrpcServer::StartRecovery(grpc::ServerContext* context,
                                                   const StartRecoveryRequest* request,
                                                   StartRecoveryResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        LOG_INFO("StartRecovery request received for device: " + request->device() +
            " (context: " + request->context_id() + ")");

        try
        {
            testdisk_cli_context_t* ctx = GetContext(request->context_id());
            if (!ctx)
            {
                LOG_ERROR("Invalid context ID: " + request->context_id());
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

            LOG_DEBUG("Creating recovery session: " + recovery_id +
                " for partition order: " + std::to_string(request->partition_order()));

            // Store session
            {
                std::lock_guard<std::mutex> lock(sessions_mutex_);
                recovery_sessions_[recovery_id] = std::move(session);
                LOG_INFO("Recovery session created: " + recovery_id);
            }

            // Start recovery in background thread
            RecoverySession* session_ptr = recovery_sessions_[recovery_id].get();
            session_ptr->recovery_thread = std::thread(
                [session_ptr,
                    device = request->device(),
                    partition_order = request-> partition_order(),
                    recup_dir = request->recovery_dir(),
                    options = request->options()]()
                {
                    LOG_INFO(
                        "Starting recovery worker thread for session: " +
                        session_ptr->id);
                    RecoveryWorker(session_ptr, device, partition_order, recup_dir, &options);
                });

            response->set_success(true);
            response->set_recovery_id(recovery_id);
            LOG_INFO("Recovery started successfully: " + recovery_id);
            return grpc::Status::OK;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Start recovery error: " + std::string(e.what()));
            response->set_success(false);
            response->set_error_message(std::string("Start recovery error: ") + e.what());
            return grpc::Status::OK;
        }
    }

    grpc::Status TestDiskGrpcServer::GetRecoveryStatus(grpc::ServerContext* context,
                                                       const GetRecoveryStatusRequest*
                                                       request,
                                                       GetRecoveryStatusResponse*
                                                       response)
    {
        (void)context; // Suppress unused parameter warning
        LOG_DEBUG(
            "GetRecoveryStatus request received for session: " + request->recovery_id());

        try
        {
            RecoverySession* session = GetRecoverySession(request->recovery_id());
            if (!session)
            {
                LOG_ERROR("Invalid recovery ID: " + request->recovery_id());
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
            status->set_dir_num(session->context->params.dir_num);

            LOG_DEBUG("Recovery status for " + request->recovery_id() +
                ": " + session->status + " (" +
                std::to_string(session->files_recovered) + " files recovered)");

            response->set_success(true);
            return grpc::Status::OK;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Get status error: " + std::string(e.what()));
            response->set_success(false);
            response->set_error_message(std::string("Get status error: ") + e.what());
            return grpc::Status::OK;
        }
    }

    grpc::Status TestDiskGrpcServer::StopRecovery(grpc::ServerContext* context,
                                                  const StopRecoveryRequest* request,
                                                  StopRecoveryResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        LOG_INFO("StopRecovery request received for session: " + request->recovery_id());

        try
        {
            RecoverySession* session = GetRecoverySession(request->recovery_id());
            if (!session)
            {
                LOG_ERROR("Invalid recovery ID: " + request->recovery_id());
                response->set_success(false);
                response->set_error_message("Invalid recovery ID");
                return grpc::Status::OK;
            }

            // Stop the recovery
            LOG_DEBUG("Stopping recovery session: " + request->recovery_id());
            session->running = false;
            abort_testdisk(session->context);

            // Wait for thread to finish
            if (session->recovery_thread.joinable())
            {
                LOG_DEBUG("Waiting for recovery thread to finish");
                session->recovery_thread.join();
            }

            LOG_INFO("Recovery stopped successfully: " + request->recovery_id());
            response->set_success(true);
            return grpc::Status::OK;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Stop recovery error: " + std::string(e.what()));
            response->set_success(false);
            response->set_error_message(std::string("Stop recovery error: ") + e.what());
            return grpc::Status::OK;
        }
    }

    grpc::Status TestDiskGrpcServer::ConfigureOptions(grpc::ServerContext* context,
                                                      const ConfigureOptionsRequest*
                                                      request,
                                                      ConfigureOptionsResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        LOG_INFO(
            "ConfigureOptions request received for context: " + request->context_id());

        try
        {
            testdisk_cli_context_t* ctx = GetContext(request->context_id());
            if (!ctx)
            {
                LOG_ERROR("Invalid context ID: " + request->context_id());
                response->set_success(false);
                response->set_error_message("Invalid context ID");
                return grpc::Status::OK;
            }

            LOG_DEBUG("Applying recovery options");
            ApplyRecoveryOptions(ctx, request->options());

            response->set_success(true);
            LOG_INFO("Options configured successfully");
            return grpc::Status::OK;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Configure options error: " + std::string(e.what()));
            response->set_success(false);
            response->set_error_message(
                std::string("Configure options error: ") + e.what());
            return grpc::Status::OK;
        }
    }

    grpc::Status TestDiskGrpcServer::GetStatistics(grpc::ServerContext* context,
                                                   const GetStatisticsRequest* request,
                                                   GetStatisticsResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        LOG_INFO("GetStatistics request received for context: " + request->context_id());

        try
        {
            const testdisk_cli_context_t* ctx = GetContext(request->context_id());
            if (!ctx)
            {
                LOG_ERROR("Invalid context ID: " + request->context_id());
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

                    LOG_DEBUG(
                        "File type " + std::string(file_stats[i].file_hint->extension) +
                        ": " + std::to_string(file_stats[i].recovered) + " recovered, " +
                        std::to_string(file_stats[i].not_recovered) + " failed");
                }

                response->set_total_files_recovered(total_recovered);
                response->set_total_files_failed(total_failed);

                LOG_INFO(
                    "Statistics: " + std::to_string(total_recovered) +
                    " files recovered, " +
                    std::to_string(total_failed) + " files failed");
            }
            else
            {
                LOG_WARNING("No file statistics available");
            }

            response->set_success(true);
            return grpc::Status::OK;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Get statistics error: " + std::string(e.what()));
            response->set_success(false);
            response->set_error_message(std::string("Get statistics error: ") + e.what());
            return grpc::Status::OK;
        }
    }

    grpc::Status TestDiskGrpcServer::Cleanup(grpc::ServerContext* context,
                                             const CleanupRequest* request,
                                             CleanupResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        LOG_INFO("Cleanup request received for context: " + request->context_id());

        try
        {
            testdisk_cli_context_t* ctx = GetContext(request->context_id());
            if (!ctx)
            {
                LOG_ERROR("Invalid context ID: " + request->context_id());
                response->set_success(false);
                response->set_error_message("Invalid context ID");
                return grpc::Status::OK;
            }

            // Clean up context
            LOG_DEBUG("Finishing TestDisk context: " + request->context_id());
            finish_testdisk(ctx);

            // Remove from contexts map
            {
                std::lock_guard<std::mutex> lock(contexts_mutex_);
                contexts_.erase(request->context_id());
                LOG_INFO("Context cleaned up and removed: " + request->context_id());
            }

            response->set_success(true);
            return grpc::Status::OK;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Cleanup error: " + std::string(e.what()));
            response->set_success(false);
            response->set_error_message(std::string("Cleanup error: ") + e.what());
            return grpc::Status::OK;
        }
    }

    grpc::Status TestDiskGrpcServer::Shutdown(grpc::ServerContext* context,
                                              const ShutdownRequest* request,
                                              ShutdownResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        LOG_INFO("Shutdown request received - Force: " + 
                 std::to_string(request->force()) + 
                 ", Reason: " + request->reason());

        try
        {
            // Check if there are active recovery sessions
            int active_sessions = 0;
            {
                std::lock_guard<std::mutex> lock(sessions_mutex_);
                for (const auto& session_pair : recovery_sessions_)
                {
                    if (session_pair.second && session_pair.second->running)
                    {
                        active_sessions++;
                    }
                }
            }

            // If there are active sessions and force is not set, return an error
            if (active_sessions > 0 && !request->force())
            {
                LOG_WARNING("Shutdown request denied - " + 
                           std::to_string(active_sessions) + " active recovery sessions");
                response->set_success(false);
                response->set_error_message("Cannot shutdown: " + 
                                          std::to_string(active_sessions) + 
                                          " active recovery sessions. Use force=true to shutdown anyway.");
                response->set_message("Shutdown denied due to active recovery sessions");
                return grpc::Status::OK;
            }

            if (active_sessions > 0)
            {
                LOG_WARNING("Force shutting down with " + 
                           std::to_string(active_sessions) + " active recovery sessions");
                
                // Stop all active recovery sessions
                std::lock_guard<std::mutex> lock(sessions_mutex_);
                for (const auto& session_pair : recovery_sessions_)
                {
                    if (session_pair.second && session_pair.second->running)
                    {
                        RecoverySession* session = session_pair.second.get();
                        LOG_INFO("Stopping recovery session: " + session->id);
                        session->running = false;
                        abort_testdisk(session->context);
                        
                        // Wait for thread to finish
                        if (session->recovery_thread.joinable())
                        {
                            session->recovery_thread.join();
                        }
                    }
                }
            }

            // Log the shutdown reason if provided
            if (!request->reason().empty())
            {
                LOG_INFO("Shutdown reason: " + request->reason());
            }

            // Trigger server shutdown asynchronously
            LOG_INFO("Initiating server shutdown");
            std::thread shutdown_thread([this]() {
                // Give the response time to be sent
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                
                // Call shutdown callback if set
                if (shutdown_callback_) {
                    shutdown_callback_();
                }
                
                // Stop the server
                this->Stop();
            });
            shutdown_thread.detach();

            response->set_success(true);
            response->set_message("Server shutdown initiated");
            if (active_sessions > 0)
            {
                response->set_message("Server shutdown initiated (forced with " + 
                                    std::to_string(active_sessions) + " active sessions stopped)");
            }
            
            LOG_INFO("Shutdown response sent - Server will stop shortly");
            return grpc::Status::OK;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Shutdown error: " + std::string(e.what()));
            response->set_success(false);
            response->set_error_message(std::string("Shutdown error: ") + e.what());
            response->set_message("Shutdown failed");
            return grpc::Status::OK;
        }
    }

    grpc::Status TestDiskGrpcServer::Heartbeat(grpc::ServerContext* context,
                                              const HeartbeatRequest* request,
                                              HeartbeatResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        LOG_DEBUG("Heartbeat request received");

        try
        {
            // Basic liveness check
            response->set_success(true);
            response->set_server_version("TestDisk gRPC Wrapper v1.0.0");
            
            // Calculate uptime
            auto current_time = std::chrono::steady_clock::now();
            auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
                current_time - server_start_time_);
            response->set_uptime_seconds(uptime.count());
            
            // Count active contexts
            int active_contexts = 0;
            {
                std::lock_guard<std::mutex> lock(contexts_mutex_);
                active_contexts = static_cast<int>(contexts_.size());
            }
            response->set_active_contexts(active_contexts);
            
            // Count active recovery sessions
            int active_recoveries = 0;
            {
                std::lock_guard<std::mutex> lock(sessions_mutex_);
                for (const auto& session_pair : recovery_sessions_)
                {
                    if (session_pair.second && session_pair.second->running)
                    {
                        active_recoveries++;
                    }
                }
            }
            response->set_active_recoveries(active_recoveries);
            
            // Optional context validation if context_id is provided
            if (!request->context_id().empty())
            {
                testdisk_cli_context_t* ctx = GetContext(request->context_id());
                if (!ctx)
                {
                    LOG_WARNING("Heartbeat: Invalid context_id provided: " + request->context_id());
                    response->set_success(false);
                    response->set_error_message("Invalid context_id provided");
                    return grpc::Status::OK;
                }
                LOG_DEBUG("Heartbeat: Validated context_id: " + request->context_id());
            }
            
            LOG_DEBUG("Heartbeat response: uptime=" + std::to_string(uptime.count()) + 
                     "s, contexts=" + std::to_string(active_contexts) + 
                     ", recoveries=" + std::to_string(active_recoveries));
            
            return grpc::Status::OK;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Heartbeat error: " + std::string(e.what()));
            response->set_success(false);
            response->set_error_message(std::string("Heartbeat error: ") + e.what());
            return grpc::Status::OK;
        }
    }

    void TestDiskGrpcServer::RecoveryWorker(RecoverySession* session,
                                            const std::string& device,
                                            const int partition_order,
                                            const std::string& recup_dir,
                                            const RecoveryOptions* options)
    {
        LOG_INFO("Recovery worker started for session: " + session->id + " on device: " + device);

        try
        {
            testdisk_cli_context_t* ctx = session->context;

            // Apply recovery options
            LOG_DEBUG("Applying recovery options");
            ApplyRecoveryOptions(ctx, *options);

            // Change to target device
            LOG_DEBUG("Changing to target device: " + device);
            disk_t* disk = change_disk(ctx, device.c_str());
            if (!disk)
            {
                std::lock_guard<std::mutex> lock(session->status_mutex);
                session->error_message = "Failed to access device: " + device;
                session->completed = true;
                session->running = false;
                LOG_ERROR("Failed to access device: " + device);
                return;
            }

            // Set total size
            session->total_size = disk->disk_size;
            LOG_INFO("Disk size: " + std::to_string(disk->disk_size) + " bytes");

            // Change partition if specified
            if (partition_order >= 0)
            {
                LOG_DEBUG("Changing to partition: " + std::to_string(partition_order));
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
                    LOG_ERROR(
                        "Failed to access partition: " + std::to_string(partition_order));
                    return;
                }
                session->total_size = partition->part_size;
                LOG_INFO(
                    "Partition size: " + std::to_string(partition->part_size) + " bytes");
            }

            // Setup recovery dir
            LOG_DEBUG("Recovery to dir: " + recup_dir);
            change_recup_dir(ctx, recup_dir.c_str());

            // Start recovery
            LOG_INFO("Starting TestDisk recovery process");
            UpdateRecoveryStatus(session, STATUS_FIND_OFFSET, 0);

            LOG_INFO("Running TestDisk recovery in directory: " + std::string(ctx->params.recup_dir));
            const int result = run_testdisk(ctx);

            // Update final status
            {
                std::lock_guard<std::mutex> lock(session->status_mutex);
                if (result == 0)
                {
                    session->status = "Completed successfully";
                    LOG_INFO(
                        "Recovery completed successfully for session: " + session->id);
                }
                else
                {
                    session->status = "Completed with errors";
                    session->error_message = "Recovery process returned error code: " +
                        std::to_string(result);
                    LOG_WARNING(
                        "Recovery completed with errors for session: " + session->id +
                        " (error code: " + std::to_string(result) + ")");
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
            LOG_ERROR(
                "Recovery worker error for session " + session->id + ": " + e.what());
        }
    }

    void TestDiskGrpcServer::UpdateRecoveryStatus(RecoverySession* session,
                                                  testdisk_status_t status,
                                                  uint64_t offset)
    {
        std::lock_guard<std::mutex> lock(session->status_mutex);
        session->status = StatusToString(status);
        session->current_offset = offset;
        session->files_recovered = session->context->params.file_nbr;

        LOG_DEBUG("Recovery status update for session " + session->id +
            ": " + session->status + " at offset " + std::to_string(offset) +
            " (" + std::to_string(session->files_recovered) + " files recovered)");
    }

    std::string TestDiskGrpcServer::StatusToString(testdisk_status_t status)
    {
        switch (status)
        {
        case ::STATUS_FIND_OFFSET: return "Finding optimal block alignment";
        case ::STATUS_UNFORMAT: return "FAT unformat recovery";
        case ::STATUS_EXT2_ON: return "Main recovery with filesystem optimization";
        case ::STATUS_EXT2_ON_BF: return "Brute force with filesystem optimization";
        case ::STATUS_EXT2_OFF: return "Main recovery without filesystem optimization";
        case ::STATUS_EXT2_OFF_BF: return "Brute force without filesystem optimization";
        case ::STATUS_EXT2_ON_SAVE_EVERYTHING: return "Save everything mode with optimization";
        case ::STATUS_EXT2_OFF_SAVE_EVERYTHING: return "Save everything mode without optimization";
        case ::STATUS_QUIT: return "Recovery completed";
        default: return "Unknown status";
        }
    }

    void TestDiskGrpcServer::ConvertDiskInfo(const disk_t* disk, DiskInfo* proto_disk)
    {
        proto_disk->set_device(disk->device ? disk->device : "");
        proto_disk->set_description(disk->description_txt);
        proto_disk->set_size(disk->disk_size);
        proto_disk->set_model(disk->model ? disk->model : "");
        proto_disk->set_serial_no(disk->serial_no ? disk->serial_no : "");
        proto_disk->set_firmware_rev(disk->fw_rev ? disk->fw_rev : "");

        // Set architecture information
        if (disk->arch)
        {
            proto_disk->set_arch(disk->arch->part_name_option ? disk->arch->part_name_option : "");
        }
        if (disk->arch_autodetected)
        {
            proto_disk->set_autodetected_arch(disk->arch_autodetected->part_name_option ? disk->arch_autodetected->part_name_option : "");
        }
    }

    void TestDiskGrpcServer::ConvertPartitionInfo(const partition_t* partition,
                                                  PartitionInfo* proto_partition)
    {
        proto_partition->set_name(partition->partname);
        proto_partition->set_filesystem(partition->fsname);
        proto_partition->set_offset(partition->part_offset);
        proto_partition->set_size(partition->part_size);
        proto_partition->set_info(partition->info);
        proto_partition->set_order(static_cast<int32_t>(partition->order));

        // Convert status to enum
        switch (partition->status)
        {
        case ::STATUS_DELETED: proto_partition->set_status(PartitionStatus::STATUS_DELETED);
            break;
        case ::STATUS_PRIM: proto_partition->set_status(PartitionStatus::STATUS_PRIM);
            break;
        case ::STATUS_PRIM_BOOT: proto_partition->set_status(PartitionStatus::STATUS_PRIM_BOOT);
            break;
        case ::STATUS_LOG: proto_partition->set_status(PartitionStatus::STATUS_LOG);
            break;
        case ::STATUS_EXT: proto_partition->set_status(PartitionStatus::STATUS_EXT);
            break;
        case ::STATUS_EXT_IN_EXT: proto_partition->set_status(PartitionStatus::STATUS_EXT_IN_EXT);
            break;
        default: proto_partition->set_status(PartitionStatus::STATUS_DELETED);
            break;
        }

        // Set additional fields
        proto_partition->set_superblock_origin_offset(partition->sborg_offset);
        proto_partition->set_superblock_offset(partition->sb_offset);
        proto_partition->set_superblock_size(partition->sb_size);
        proto_partition->set_blocksize(partition->blocksize);

        // Convert EFI GUID fields
        if (partition->part_uuid.time_low != 0 || partition->part_uuid.time_mid != 0)
        {
            EfiGuid* uuid = proto_partition->mutable_partition_uuid();
            uuid->set_time_low(partition->part_uuid.time_low);
            uuid->set_time_mid(partition->part_uuid.time_mid);
            uuid->set_time_hi_and_version(partition->part_uuid.time_hi_and_version);
            uuid->set_clock_seq_hi_and_reserved(partition->part_uuid.clock_seq_hi_and_reserved);
            uuid->set_clock_seq_low(partition->part_uuid.clock_seq_low);
            uuid->set_node(std::string(reinterpret_cast<const char*>(partition->part_uuid.node), 6));
        }

        if (partition->part_type_gpt.time_low != 0 || partition->part_type_gpt.time_mid != 0)
        {
            EfiGuid* gpt = proto_partition->mutable_partition_type_gpt();
            gpt->set_time_low(partition->part_type_gpt.time_low);
            gpt->set_time_mid(partition->part_type_gpt.time_mid);
            gpt->set_time_hi_and_version(partition->part_type_gpt.time_hi_and_version);
            gpt->set_clock_seq_hi_and_reserved(partition->part_type_gpt.clock_seq_hi_and_reserved);
            gpt->set_clock_seq_low(partition->part_type_gpt.clock_seq_low);
            gpt->set_node(std::string(reinterpret_cast<const char*>(partition->part_type_gpt.node), 6));
        }

        // Set partition type fields
        proto_partition->set_partition_type_humax(partition->part_type_humax);
        proto_partition->set_partition_type_i386(partition->part_type_i386);
        proto_partition->set_partition_type_mac(partition->part_type_mac);
        proto_partition->set_partition_type_sun(partition->part_type_sun);
        proto_partition->set_partition_type_xbox(partition->part_type_xbox);

        // Convert unified partition type
        switch (partition->upart_type)
        {
        case ::UP_UNK: proto_partition->set_unified_type(UnifiedPartitionType::UP_UNK); break;
        case ::UP_APFS: proto_partition->set_unified_type(UnifiedPartitionType::UP_APFS); break;
        case ::UP_BEOS: proto_partition->set_unified_type(UnifiedPartitionType::UP_BEOS); break;
        case ::UP_BTRFS: proto_partition->set_unified_type(UnifiedPartitionType::UP_BTRFS); break;
        case ::UP_CRAMFS: proto_partition->set_unified_type(UnifiedPartitionType::UP_CRAMFS); break;
        case ::UP_EXFAT: proto_partition->set_unified_type(UnifiedPartitionType::UP_EXFAT); break;
        case ::UP_EXT2: proto_partition->set_unified_type(UnifiedPartitionType::UP_EXT2); break;
        case ::UP_EXT3: proto_partition->set_unified_type(UnifiedPartitionType::UP_EXT3); break;
        case ::UP_EXT4: proto_partition->set_unified_type(UnifiedPartitionType::UP_EXT4); break;
        case ::UP_EXTENDED: proto_partition->set_unified_type(UnifiedPartitionType::UP_EXTENDED); break;
        case ::UP_FAT12: proto_partition->set_unified_type(UnifiedPartitionType::UP_FAT12); break;
        case ::UP_FAT16: proto_partition->set_unified_type(UnifiedPartitionType::UP_FAT16); break;
        case ::UP_FAT32: proto_partition->set_unified_type(UnifiedPartitionType::UP_FAT32); break;
        case ::UP_FATX: proto_partition->set_unified_type(UnifiedPartitionType::UP_FATX); break;
        case ::UP_FREEBSD: proto_partition->set_unified_type(UnifiedPartitionType::UP_FREEBSD); break;
        case ::UP_F2FS: proto_partition->set_unified_type(UnifiedPartitionType::UP_F2FS); break;
        case ::UP_GFS2: proto_partition->set_unified_type(UnifiedPartitionType::UP_GFS2); break;
        case ::UP_HFS: proto_partition->set_unified_type(UnifiedPartitionType::UP_HFS); break;
        case ::UP_HFSP: proto_partition->set_unified_type(UnifiedPartitionType::UP_HFSP); break;
        case ::UP_HFSX: proto_partition->set_unified_type(UnifiedPartitionType::UP_HFSX); break;
        case ::UP_HPFS: proto_partition->set_unified_type(UnifiedPartitionType::UP_HPFS); break;
        case ::UP_ISO: proto_partition->set_unified_type(UnifiedPartitionType::UP_ISO); break;
        case ::UP_JFS: proto_partition->set_unified_type(UnifiedPartitionType::UP_JFS); break;
        case ::UP_LINSWAP: proto_partition->set_unified_type(UnifiedPartitionType::UP_LINSWAP); break;
        case ::UP_LINSWAP2: proto_partition->set_unified_type(UnifiedPartitionType::UP_LINSWAP2); break;
        case ::UP_LINSWAP_8K: proto_partition->set_unified_type(UnifiedPartitionType::UP_LINSWAP_8K); break;
        case ::UP_LINSWAP2_8K: proto_partition->set_unified_type(UnifiedPartitionType::UP_LINSWAP2_8K); break;
        case ::UP_LINSWAP2_8KBE: proto_partition->set_unified_type(UnifiedPartitionType::UP_LINSWAP2_8KBE); break;
        case ::UP_LUKS: proto_partition->set_unified_type(UnifiedPartitionType::UP_LUKS); break;
        case ::UP_LVM: proto_partition->set_unified_type(UnifiedPartitionType::UP_LVM); break;
        case ::UP_LVM2: proto_partition->set_unified_type(UnifiedPartitionType::UP_LVM2); break;
        case ::UP_MD: proto_partition->set_unified_type(UnifiedPartitionType::UP_MD); break;
        case ::UP_MD1: proto_partition->set_unified_type(UnifiedPartitionType::UP_MD1); break;
        case ::UP_NETWARE: proto_partition->set_unified_type(UnifiedPartitionType::UP_NETWARE); break;
        case ::UP_NTFS: proto_partition->set_unified_type(UnifiedPartitionType::UP_NTFS); break;
        case ::UP_OPENBSD: proto_partition->set_unified_type(UnifiedPartitionType::UP_OPENBSD); break;
        case ::UP_OS2MB: proto_partition->set_unified_type(UnifiedPartitionType::UP_OS2MB); break;
        case ::UP_ReFS: proto_partition->set_unified_type(UnifiedPartitionType::UP_ReFS); break;
        case ::UP_RFS: proto_partition->set_unified_type(UnifiedPartitionType::UP_RFS); break;
        case ::UP_RFS2: proto_partition->set_unified_type(UnifiedPartitionType::UP_RFS2); break;
        case ::UP_RFS3: proto_partition->set_unified_type(UnifiedPartitionType::UP_RFS3); break;
        case ::UP_RFS4: proto_partition->set_unified_type(UnifiedPartitionType::UP_RFS4); break;
        case ::UP_SUN: proto_partition->set_unified_type(UnifiedPartitionType::UP_SUN); break;
        case ::UP_SYSV4: proto_partition->set_unified_type(UnifiedPartitionType::UP_SYSV4); break;
        case ::UP_UFS: proto_partition->set_unified_type(UnifiedPartitionType::UP_UFS); break;
        case ::UP_UFS2: proto_partition->set_unified_type(UnifiedPartitionType::UP_UFS2); break;
        case ::UP_UFS_LE: proto_partition->set_unified_type(UnifiedPartitionType::UP_UFS_LE); break;
        case ::UP_UFS2_LE: proto_partition->set_unified_type(UnifiedPartitionType::UP_UFS2_LE); break;
        case ::UP_VMFS: proto_partition->set_unified_type(UnifiedPartitionType::UP_VMFS); break;
        case ::UP_WBFS: proto_partition->set_unified_type(UnifiedPartitionType::UP_WBFS); break;
        case ::UP_XFS: proto_partition->set_unified_type(UnifiedPartitionType::UP_XFS); break;
        case ::UP_XFS2: proto_partition->set_unified_type(UnifiedPartitionType::UP_XFS2); break;
        case ::UP_XFS3: proto_partition->set_unified_type(UnifiedPartitionType::UP_XFS3); break;
        case ::UP_XFS4: proto_partition->set_unified_type(UnifiedPartitionType::UP_XFS4); break;
        case ::UP_XFS5: proto_partition->set_unified_type(UnifiedPartitionType::UP_XFS5); break;
        case ::UP_ZFS: proto_partition->set_unified_type(UnifiedPartitionType::UP_ZFS); break;
        default: proto_partition->set_unified_type(UnifiedPartitionType::UP_UNK); break;
        }

        // Convert error code
        switch (partition->errcode)
        {
        case ::BAD_NOERR: proto_partition->set_error_code(ErrorCodeType::BAD_NOERR); break;
        case ::BAD_SS: proto_partition->set_error_code(ErrorCodeType::BAD_SS); break;
        case ::BAD_ES: proto_partition->set_error_code(ErrorCodeType::BAD_ES); break;
        case ::BAD_SH: proto_partition->set_error_code(ErrorCodeType::BAD_SH); break;
        case ::BAD_EH: proto_partition->set_error_code(ErrorCodeType::BAD_EH); break;
        case ::BAD_EBS: proto_partition->set_error_code(ErrorCodeType::BAD_EBS); break;
        case ::BAD_RS: proto_partition->set_error_code(ErrorCodeType::BAD_RS); break;
        case ::BAD_SC: proto_partition->set_error_code(ErrorCodeType::BAD_SC); break;
        case ::BAD_EC: proto_partition->set_error_code(ErrorCodeType::BAD_EC); break;
        case ::BAD_SCOUNT: proto_partition->set_error_code(ErrorCodeType::BAD_SCOUNT); break;
        default: proto_partition->set_error_code(ErrorCodeType::BAD_NOERR); break;
        }
    }

    void TestDiskGrpcServer::ApplyRecoveryOptions(testdisk_cli_context_t* ctx,
                                                  const RecoveryOptions& options)
    {
        LOG_DEBUG("Applying recovery options - Paranoid: " +
            std::to_string(options.paranoid_mode()) +
            ", Keep corrupted: " + std::to_string(options.keep_corrupted_files()) +
            ", Ext2 optimization: " + std::to_string(options.enable_ext2_optimization()) +
            ", Expert mode: " + std::to_string(options.expert_mode()) +
            ", Low memory: " + std::to_string(options.low_memory_mode()) +
            ", Verbose: " + std::to_string(options.verbose_output()));

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
            LOG_DEBUG("Applying file type filters - Enabled: " +
                std::to_string(options.enabled_file_types().size()) +
                ", Disabled: " + std::to_string(options.disabled_file_types().size()));

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

    bool TestDiskGrpcServer::Start(const std::string& address)
    {
        if (running_)
        {
            LOG_WARNING("Server is already running");
            return false;
        }

        server_address_ = address;
        LOG_INFO("Starting TestDisk gRPC Server on " + address);

        grpc::ServerBuilder builder;
        builder.AddListeningPort(address, grpc::InsecureServerCredentials());
        builder.RegisterService(this);

        server_ = builder.BuildAndStart();
        if (!server_)
        {
            LOG_ERROR("Failed to start gRPC server on " + address);
            return false;
        }

        running_ = true;
        server_start_time_ = std::chrono::steady_clock::now();
        LOG_INFO("TestDisk gRPC Server started successfully on " + address);
        return true;
    }

    void TestDiskGrpcServer::Stop()
    {
        if (running_ && server_)
        {
            LOG_INFO("Stopping TestDisk gRPC Server");
            server_->Shutdown();
            running_ = false;
            LOG_INFO("TestDisk gRPC Server stopped");
        }
    }

    void TestDiskGrpcServer::Wait() const
    {
        if (server_)
        {
            LOG_INFO("Waiting for server to finish");
            server_->Wait();
            LOG_INFO("Server finished");
        }
    }

    // ============================================================================
    // PARTITION RECOVERY OPERATIONS - Search and Recovery
    // ============================================================================

    grpc::Status TestDiskGrpcServer::SearchPartitions(grpc::ServerContext* context,
                                                      const SearchPartitionsRequest* request,
                                                      SearchPartitionsResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        LOG_INFO("SearchPartitions request received for context: " + request->context_id() +
            ", Fast mode: " + std::to_string(request->fast_mode()) +
            ", Dump index: " + std::to_string(request->dump_ind()));

        try
        {
            testdisk_cli_context_t* ctx = GetContext(request->context_id());
            if (!ctx)
            {
                LOG_ERROR("Invalid context ID: " + request->context_id());
                response->set_success(false);
                response->set_error_message("Invalid context ID");
                return grpc::Status::OK;
            }

            int result = search_partitions(ctx, request->fast_mode(), request->dump_ind());
            
            response->set_success(result == 0);
            response->set_result(result);
            if (result != 0)
            {
                response->set_error_message("Failed to search partitions");
            }

            LOG_INFO("SearchPartitions completed with result: " + std::to_string(result));
            return grpc::Status::OK;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("SearchPartitions error: " + std::string(e.what()));
            response->set_success(false);
            response->set_error_message(std::string("SearchPartitions error: ") + e.what());
            response->set_result(-1);
            return grpc::Status::OK;
        }
    }

    grpc::Status TestDiskGrpcServer::ValidateDiskGeometry(grpc::ServerContext* context,
                                                          const ValidateDiskGeometryRequest* request,
                                                          ValidateDiskGeometryResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        LOG_INFO("ValidateDiskGeometry request received for context: " + request->context_id());

        try
        {
            testdisk_cli_context_t* ctx = GetContext(request->context_id());
            if (!ctx)
            {
                LOG_ERROR("Invalid context ID: " + request->context_id());
                response->set_success(false);
                response->set_error_message("Invalid context ID");
                return grpc::Status::OK;
            }

            int result = validate_disk_geometry(ctx);
            
            response->set_success(result == 0);
            response->set_result(result);
            if (result != 0)
            {
                response->set_error_message("Disk geometry validation failed");
            }

            LOG_INFO("ValidateDiskGeometry completed with result: " + std::to_string(result));
            return grpc::Status::OK;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("ValidateDiskGeometry error: " + std::string(e.what()));
            response->set_success(false);
            response->set_error_message(std::string("ValidateDiskGeometry error: ") + e.what());
            response->set_result(-1);
            return grpc::Status::OK;
        }
    }

    grpc::Status TestDiskGrpcServer::WritePartitionTable(grpc::ServerContext* context,
                                                         const WritePartitionTableRequest* request,
                                                         WritePartitionTableResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        LOG_INFO("WritePartitionTable request received for context: " + request->context_id() +
            ", Simulate: " + std::to_string(request->simulate()) +
            ", No confirm: " + std::to_string(request->no_confirm()));

        try
        {
            testdisk_cli_context_t* ctx = GetContext(request->context_id());
            if (!ctx)
            {
                LOG_ERROR("Invalid context ID: " + request->context_id());
                response->set_success(false);
                response->set_error_message("Invalid context ID");
                return grpc::Status::OK;
            }

            int result = write_partition_table(ctx, request->simulate() ? 1 : 0, request->no_confirm() ? 1 : 0);
            
            response->set_success(result == 0);
            response->set_result(result);
            if (result != 0)
            {
                response->set_error_message("Failed to write partition table");
            }

            LOG_INFO("WritePartitionTable completed with result: " + std::to_string(result));
            return grpc::Status::OK;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("WritePartitionTable error: " + std::string(e.what()));
            response->set_success(false);
            response->set_error_message(std::string("WritePartitionTable error: ") + e.what());
            response->set_result(-1);
            return grpc::Status::OK;
        }
    }

    grpc::Status TestDiskGrpcServer::DeletePartitionTable(grpc::ServerContext* context,
                                                          const DeletePartitionTableRequest* request,
                                                          DeletePartitionTableResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        LOG_INFO("DeletePartitionTable request received for context: " + request->context_id() +
            ", Device: " + request->device());

        try
        {
            testdisk_cli_context_t* ctx = GetContext(request->context_id());
            if (!ctx)
            {
                LOG_ERROR("Invalid context ID: " + request->context_id());
                response->set_success(false);
                response->set_error_message("Invalid context ID");
                return grpc::Status::OK;
            }

            // This is a destructive operation
            LOG_WARNING("Deleting partition table for device: " + request->device());
            delete_partition_table(ctx);
            
            response->set_success(true);
            LOG_INFO("DeletePartitionTable completed successfully");
            return grpc::Status::OK;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("DeletePartitionTable error: " + std::string(e.what()));
            response->set_success(false);
            response->set_error_message(std::string("DeletePartitionTable error: ") + e.what());
            return grpc::Status::OK;
        }
    }

    // ============================================================================
    // PARTITION STRUCTURE OPERATIONS - Navigation and Management
    // ============================================================================

    grpc::Status TestDiskGrpcServer::TestPartitionStructure(grpc::ServerContext* context,
                                                            const TestPartitionStructureRequest* request,
                                                            TestPartitionStructureResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        LOG_INFO("TestPartitionStructure request received for context: " + request->context_id());

        try
        {
            testdisk_cli_context_t* ctx = GetContext(request->context_id());
            if (!ctx)
            {
                LOG_ERROR("Invalid context ID: " + request->context_id());
                response->set_success(false);
                response->set_error_message("Invalid context ID");
                return grpc::Status::OK;
            }

            int result = test_partition_structure(ctx);
            
            response->set_success(result == 0);
            response->set_result(result);
            if (result != 0)
            {
                response->set_error_message("Partition structure test failed");
            }

            LOG_INFO("TestPartitionStructure completed with result: " + std::to_string(result));
            return grpc::Status::OK;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("TestPartitionStructure error: " + std::string(e.what()));
            response->set_success(false);
            response->set_error_message(std::string("TestPartitionStructure error: ") + e.what());
            response->set_result(-1);
            return grpc::Status::OK;
        }
    }

    grpc::Status TestDiskGrpcServer::ChangePartitionStatusNext(grpc::ServerContext* context,
                                                               const ChangePartitionStatusNextRequest* request,
                                                               ChangePartitionStatusNextResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        LOG_INFO("ChangePartitionStatusNext request received for context: " + request->context_id() +
            ", Order: " + std::to_string(request->order()));

        try
        {
            testdisk_cli_context_t* ctx = GetContext(request->context_id());
            if (!ctx)
            {
                LOG_ERROR("Invalid context ID: " + request->context_id());
                response->set_success(false);
                response->set_error_message("Invalid context ID");
                return grpc::Status::OK;
            }

            int result = change_partition_status_next(ctx, request->order());
            
            response->set_success(result == 0);
            response->set_result(result);
            if (result != 0)
            {
                response->set_error_message("Failed to change partition status to next");
            }

            LOG_INFO("ChangePartitionStatusNext completed with result: " + std::to_string(result));
            return grpc::Status::OK;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("ChangePartitionStatusNext error: " + std::string(e.what()));
            response->set_success(false);
            response->set_error_message(std::string("ChangePartitionStatusNext error: ") + e.what());
            response->set_result(-1);
            return grpc::Status::OK;
        }
    }

    grpc::Status TestDiskGrpcServer::ChangePartitionStatusPrev(grpc::ServerContext* context,
                                                               const ChangePartitionStatusPrevRequest* request,
                                                               ChangePartitionStatusPrevResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        LOG_INFO("ChangePartitionStatusPrev request received for context: " + request->context_id() +
            ", Order: " + std::to_string(request->order()));

        try
        {
            testdisk_cli_context_t* ctx = GetContext(request->context_id());
            if (!ctx)
            {
                LOG_ERROR("Invalid context ID: " + request->context_id());
                response->set_success(false);
                response->set_error_message("Invalid context ID");
                return grpc::Status::OK;
            }

            int result = change_partition_status_prev(ctx, request->order());
            
            response->set_success(result == 0);
            response->set_result(result);
            if (result != 0)
            {
                response->set_error_message("Failed to change partition status to previous");
            }

            LOG_INFO("ChangePartitionStatusPrev completed with result: " + std::to_string(result));
            return grpc::Status::OK;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("ChangePartitionStatusPrev error: " + std::string(e.what()));
            response->set_success(false);
            response->set_error_message(std::string("ChangePartitionStatusPrev error: ") + e.what());
            response->set_result(-1);
            return grpc::Status::OK;
        }
    }

    grpc::Status TestDiskGrpcServer::ChangePartitionType(grpc::ServerContext* context,
                                                         const ChangePartitionTypeRequest* request,
                                                         ChangePartitionTypeResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        LOG_INFO("ChangePartitionType request received for context: " + request->context_id() +
            ", Order: " + std::to_string(request->order()) +
            ", Part type: " + std::to_string(request->part_type()));

        try
        {
            testdisk_cli_context_t* ctx = GetContext(request->context_id());
            if (!ctx)
            {
                LOG_ERROR("Invalid context ID: " + request->context_id());
                response->set_success(false);
                response->set_error_message("Invalid context ID");
                return grpc::Status::OK;
            }

            int result = change_partition_type(ctx, request->order(), request->part_type());
            
            response->set_success(result == 0);
            response->set_result(result);
            if (result != 0)
            {
                response->set_error_message("Failed to change partition type");
            }

            LOG_INFO("ChangePartitionType completed with result: " + std::to_string(result));
            return grpc::Status::OK;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("ChangePartitionType error: " + std::string(e.what()));
            response->set_success(false);
            response->set_error_message(std::string("ChangePartitionType error: ") + e.what());
            response->set_result(-1);
            return grpc::Status::OK;
        }
    }

    grpc::Status TestDiskGrpcServer::ListPartitionFiles(grpc::ServerContext* context,
                                                        const ListPartitionFilesRequest* request,
                                                        ListPartitionFilesResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        LOG_INFO("ListPartitionFiles request received for context: " + request->context_id() +
            ", Order: " + std::to_string(request->order()));

        try
        {
            testdisk_cli_context_t* ctx = GetContext(request->context_id());
            if (!ctx)
            {
                LOG_ERROR("Invalid context ID: " + request->context_id());
                response->set_success(false);
                response->set_error_message("Invalid context ID");
                return grpc::Status::OK;
            }

            int result = list_partition_files(ctx, request->order());
            
            response->set_success(result == 0);
            response->set_result(result);
            if (result != 0)
            {
                response->set_error_message("Failed to list partition files");
            }

            LOG_INFO("ListPartitionFiles completed with result: " + std::to_string(result));
            return grpc::Status::OK;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("ListPartitionFiles error: " + std::string(e.what()));
            response->set_success(false);
            response->set_error_message(std::string("ListPartitionFiles error: ") + e.what());
            response->set_result(-1);
            return grpc::Status::OK;
        }
    }

    grpc::Status TestDiskGrpcServer::SavePartitionBackup(grpc::ServerContext* context,
                                                         const SavePartitionBackupRequest* request,
                                                         SavePartitionBackupResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        LOG_INFO("SavePartitionBackup request received for context: " + request->context_id());

        try
        {
            testdisk_cli_context_t* ctx = GetContext(request->context_id());
            if (!ctx)
            {
                LOG_ERROR("Invalid context ID: " + request->context_id());
                response->set_success(false);
                response->set_error_message("Invalid context ID");
                return grpc::Status::OK;
            }

            int result = save_partition_backup(ctx);
            
            response->set_success(result == 0);
            response->set_result(result);
            if (result != 0)
            {
                response->set_error_message("Failed to save partition backup");
            }

            LOG_INFO("SavePartitionBackup completed with result: " + std::to_string(result));
            return grpc::Status::OK;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("SavePartitionBackup error: " + std::string(e.what()));
            response->set_success(false);
            response->set_error_message(std::string("SavePartitionBackup error: ") + e.what());
            response->set_result(-1);
            return grpc::Status::OK;
        }
    }

    grpc::Status TestDiskGrpcServer::LoadPartitionBackup(grpc::ServerContext* context,
                                                         const LoadPartitionBackupRequest* request,
                                                         LoadPartitionBackupResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        LOG_INFO("LoadPartitionBackup request received for context: " + request->context_id());

        try
        {
            testdisk_cli_context_t* ctx = GetContext(request->context_id());
            if (!ctx)
            {
                LOG_ERROR("Invalid context ID: " + request->context_id());
                response->set_success(false);
                response->set_error_message("Invalid context ID");
                return grpc::Status::OK;
            }

            int result = load_partition_backup(ctx);
            
            response->set_success(result == 0);
            response->set_result(result);
            if (result != 0)
            {
                response->set_error_message("Failed to load partition backup");
            }

            LOG_INFO("LoadPartitionBackup completed with result: " + std::to_string(result));
            return grpc::Status::OK;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("LoadPartitionBackup error: " + std::string(e.what()));
            response->set_success(false);
            response->set_error_message(std::string("LoadPartitionBackup error: ") + e.what());
            response->set_result(-1);
            return grpc::Status::OK;
        }
    }

    grpc::Status TestDiskGrpcServer::WriteMbrCode(grpc::ServerContext* context,
                                                  const WriteMbrCodeRequest* request,
                                                  WriteMbrCodeResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        LOG_INFO("WriteMbrCode request received for context: " + request->context_id() +
            ", Device: " + request->device());

        try
        {
            testdisk_cli_context_t* ctx = GetContext(request->context_id());
            if (!ctx)
            {
                LOG_ERROR("Invalid context ID: " + request->context_id());
                response->set_success(false);
                response->set_error_message("Invalid context ID");
                return grpc::Status::OK;
            }

            write_MBR_code(ctx);
            
            response->set_success(true);
            LOG_INFO("WriteMbrCode completed successfully");
            return grpc::Status::OK;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("WriteMbrCode error: " + std::string(e.what()));
            response->set_success(false);
            response->set_error_message(std::string("WriteMbrCode error: ") + e.what());
            return grpc::Status::OK;
        }
    }

    grpc::Status TestDiskGrpcServer::EnsureSingleBootablePartition(grpc::ServerContext* context,
                                                                   const EnsureSingleBootablePartitionRequest* request,
                                                                   EnsureSingleBootablePartitionResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        LOG_INFO("EnsureSingleBootablePartition request received for context: " + request->context_id());

        try
        {
            testdisk_cli_context_t* ctx = GetContext(request->context_id());
            if (!ctx)
            {
                LOG_ERROR("Invalid context ID: " + request->context_id());
                response->set_success(false);
                response->set_error_message("Invalid context ID");
                return grpc::Status::OK;
            }

            ensure_single_bootable_partition(ctx);
            
            response->set_success(true);
            LOG_INFO("EnsureSingleBootablePartition completed successfully");
            return grpc::Status::OK;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("EnsureSingleBootablePartition error: " + std::string(e.what()));
            response->set_success(false);
            response->set_error_message(std::string("EnsureSingleBootablePartition error: ") + e.what());
            return grpc::Status::OK;
        }
    }
} // namespace testdisk
