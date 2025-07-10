#include "photorec_grpc_server.h"
#include "logger.h"
#include <random>

namespace photorec
{
    PhotoRecGrpcServer::~PhotoRecGrpcServer()
    {
        LOG_INFO("PhotoRec gRPC Server destructor called");
        Stop();

        // Clean up all contexts
        std::lock_guard<std::mutex> lock(contexts_mutex_);
        LOG_DEBUG("Cleaning up " + std::to_string(contexts_.size()) + " contexts");
        // ReSharper disable once CppUseElementsView
        for (const auto& pair : contexts_)
        {
            if (pair.second)
            {
                LOG_DEBUG("Finishing PhotoRec context: " + pair.first);
                finish_photorec(pair.second);
            }
        }
        contexts_.clear();
        LOG_INFO("PhotoRec gRPC Server cleanup completed");
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
        LOG_DEBUG("Generated context ID: " + id);
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
        LOG_DEBUG("Generated recovery ID: " + id);
        return id;
    }

    ph_cli_context_t* PhotoRecGrpcServer::GetContext(const std::string& context_id)
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

    RecoverySession* PhotoRecGrpcServer::GetRecoverySession(
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

    grpc::Status PhotoRecGrpcServer::Initialize(grpc::ServerContext* context,
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
                arg_strings.emplace_back("photorec");
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

            LOG_DEBUG("Initializing PhotoRec context with log mode: " +
                std::to_string(request->log_mode()) + ", argc: " + std::to_string(arg_strings.size()));

            ph_cli_context_t* ctx = init_photorec(static_cast<int>(arg_strings.size()),
                                                  argv_vector.data(),
                                                  request->log_mode(),
                                                  request->log_file().empty()
                                                      ? nullptr
                                                      : request->log_file().c_str());

            if (!ctx)
            {
                LOG_ERROR("Failed to initialize PhotoRec context");
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
                LOG_INFO("PhotoRec context initialized successfully: " + context_id);
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

    grpc::Status PhotoRecGrpcServer::AddImage(grpc::ServerContext* context,
                                              const AddImageRequest* request,
                                              AddImageResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        LOG_INFO("AddImage request received for context: " + request->context_id() +
            ", Image file: " + request->image_file());

        try
        {
            ph_cli_context_t* ctx = GetContext(request->context_id());
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

    grpc::Status PhotoRecGrpcServer::GetDisks(grpc::ServerContext* context,
                                              const GetDisksRequest* request,
                                              GetDisksResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        LOG_INFO("GetDisks request received for context: " + request->context_id());

        try
        {
            ph_cli_context_t* ctx = GetContext(request->context_id());
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

    grpc::Status PhotoRecGrpcServer::GetPartitions(grpc::ServerContext* context,
                                                   const GetPartitionsRequest* request,
                                                   GetPartitionsResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        LOG_INFO("GetPartitions request received for device: " + request->device() +
            " (context: " + request->context_id() + ")");

        try
        {
            ph_cli_context_t* ctx = GetContext(request->context_id());
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

    grpc::Status PhotoRecGrpcServer::GetArchs(grpc::ServerContext* context,
                                              const GetArchsRequest* request,
                                              GetArchsResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        LOG_INFO("GetArchs request received for context: " + request->context_id());

        try
        {
            const ph_cli_context_t* ctx = GetContext(request->context_id());
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

    grpc::Status PhotoRecGrpcServer::SetArchForCurrentDisk(grpc::ServerContext* context,
                                                           const SetArchForCurrentDiskRequest* request,
                                                           SetArchForCurrentDiskResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        LOG_INFO("SetArchForCurrentDisk request received - Arch: " + request->arch_name() +
            " (context: " + request->context_id() + ")");

        try
        {
            const ph_cli_context_t* ctx = GetContext(request->context_id());
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

    grpc::Status PhotoRecGrpcServer::GetFileOptions(grpc::ServerContext* context,
                                                    const GetFileOptionsRequest* request,
                                                    GetFileOptionsResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        LOG_INFO("GetFileOptions request received for context: " + request->context_id());

        try
        {
            ph_cli_context_t* ctx = GetContext(request->context_id());
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

    grpc::Status PhotoRecGrpcServer::StartRecovery(grpc::ServerContext* context,
                                                   const StartRecoveryRequest* request,
                                                   StartRecoveryResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        LOG_INFO("StartRecovery request received for device: " + request->device() +
            " (context: " + request->context_id() + ")");

        try
        {
            ph_cli_context_t* ctx = GetContext(request->context_id());
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

    grpc::Status PhotoRecGrpcServer::GetRecoveryStatus(grpc::ServerContext* context,
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

    grpc::Status PhotoRecGrpcServer::StopRecovery(grpc::ServerContext* context,
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
            abort_photorec(session->context);

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

    grpc::Status PhotoRecGrpcServer::ConfigureOptions(grpc::ServerContext* context,
                                                      const ConfigureOptionsRequest*
                                                      request,
                                                      ConfigureOptionsResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        LOG_INFO(
            "ConfigureOptions request received for context: " + request->context_id());

        try
        {
            ph_cli_context_t* ctx = GetContext(request->context_id());
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

    grpc::Status PhotoRecGrpcServer::GetStatistics(grpc::ServerContext* context,
                                                   const GetStatisticsRequest* request,
                                                   GetStatisticsResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        LOG_INFO("GetStatistics request received for context: " + request->context_id());

        try
        {
            const ph_cli_context_t* ctx = GetContext(request->context_id());
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

    grpc::Status PhotoRecGrpcServer::Cleanup(grpc::ServerContext* context,
                                             const CleanupRequest* request,
                                             CleanupResponse* response)
    {
        (void)context; // Suppress unused parameter warning
        LOG_INFO("Cleanup request received for context: " + request->context_id());

        try
        {
            ph_cli_context_t* ctx = GetContext(request->context_id());
            if (!ctx)
            {
                LOG_ERROR("Invalid context ID: " + request->context_id());
                response->set_success(false);
                response->set_error_message("Invalid context ID");
                return grpc::Status::OK;
            }

            // Clean up context
            LOG_DEBUG("Finishing PhotoRec context: " + request->context_id());
            finish_photorec(ctx);

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

    void PhotoRecGrpcServer::RecoveryWorker(RecoverySession* session,
                                            const std::string& device,
                                            const int partition_order,
                                            const std::string& recup_dir,
                                            const RecoveryOptions* options)
    {
        LOG_INFO("Recovery worker started for session: " + session->id + " on device: " + device);

        try
        {
            ph_cli_context_t* ctx = session->context;

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
            LOG_INFO("Starting PhotoRec recovery process");
            UpdateRecoveryStatus(session, STATUS_FIND_OFFSET, 0);

            LOG_INFO("Running PhotoRec recovery in directory: " + std::string(ctx->params.recup_dir));
            const int result = run_photorec(ctx);

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

    void PhotoRecGrpcServer::UpdateRecoveryStatus(RecoverySession* session,
                                                  photorec_status_t status,
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
        case STATUS_EXT2_ON_SAVE_EVERYTHING: return "Save everything mode with optimization";
        case STATUS_EXT2_OFF_SAVE_EVERYTHING: return "Save everything mode without optimization";
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

    bool PhotoRecGrpcServer::Start(const std::string& address)
    {
        if (running_)
        {
            LOG_WARNING("Server is already running");
            return false;
        }

        server_address_ = address;
        LOG_INFO("Starting PhotoRec gRPC Server on " + address);

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
        LOG_INFO("PhotoRec gRPC Server started successfully on " + address);
        return true;
    }

    void PhotoRecGrpcServer::Stop()
    {
        if (running_ && server_)
        {
            LOG_INFO("Stopping PhotoRec gRPC Server");
            server_->Shutdown();
            running_ = false;
            LOG_INFO("PhotoRec gRPC Server stopped");
        }
    }

    void PhotoRecGrpcServer::Wait() const
    {
        if (server_)
        {
            LOG_INFO("Waiting for server to finish");
            server_->Wait();
            LOG_INFO("Server finished");
        }
    }
} // namespace photorec
