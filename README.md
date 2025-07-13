# PhotoRec gRPC Wrapper

A gRPC server that wraps the PhotoRec file recovery library, providing remote access to PhotoRec's powerful file recovery capabilities through a network interface.

## Overview

This project wraps the PhotoRec API (`photorec_api.h`) into a gRPC server, allowing you to:

- Initialize PhotoRec contexts remotely
- Discover available disks and partitions
- Start file recovery processes
- Monitor recovery progress in real-time
- Configure recovery options
- Get recovery statistics

## Features

- **Remote File Recovery**: Access PhotoRec functionality over the network
- **Multi-Client Support**: Multiple clients can connect to the same server
- **Real-time Progress Monitoring**: Track recovery progress with detailed status updates
- **Flexible Configuration**: Configure recovery options per session
- **Thread-Safe**: Handles multiple concurrent recovery sessions
- **Remote Shutdown**: Gracefully shutdown the server via gRPC API
- **Cross-Platform**: Works on Linux, macOS, and Windows

## Prerequisites

### System Dependencies

- **CMake** (3.31 or higher)
- **C++20** compatible compiler (GCC 10+, Clang 12+, or MSVC 2019+)
- **gRPC** and **Protobuf** development libraries
- **ncurses** development library
- **zlib** development library

### Installing Dependencies

#### Ubuntu/Debian:
```bash
sudo apt update
sudo apt install cmake build-essential libgrpc++-dev libprotobuf-dev protobuf-compiler-grpc libncurses5-dev zlib1g-dev
```

#### CentOS/RHEL/Fedora:
```bash
sudo yum install cmake gcc-c++ grpc-devel protobuf-devel ncurses-devel zlib-devel
# or for newer versions:
sudo dnf install cmake gcc-c++ grpc-devel protobuf-devel ncurses-devel zlib-devel
```

#### macOS:
```bash
brew install cmake grpc protobuf ncurses zlib
```

#### Windows:
Install Visual Studio 2019 or later with C++ support, then use vcpkg:
```bash
vcpkg install grpc protobuf ncurses zlib
```

## Building

1. **Clone the repository**:
```bash
git clone <repository-url>
cd testdisk-grpc-wrapper
```

2. **Create build directory**:
```bash
mkdir build
cd build
```

3. **Configure and build**:
```bash
cmake ..
make -j$(nproc)
```

The build process will:
- Generate protobuf and gRPC code from `proto/photorec.proto`
- Compile the server and client examples
- Link against the PhotoRec library (`libphotorec.a`)

## Usage

### Starting the Server

```bash
# Start server on default port (50051)
./testdisk_grpc_wrapper

# Start server on custom address/port
./testdisk_grpc_wrapper --address 127.0.0.1:8080

# Start server on all interfaces
./testdisk_grpc_wrapper --address 0.0.0.0:50051
```

### Using the Client Example

```bash
# Build the client example
cd build
make client_example

# Run the client
./client_example localhost:50051 /dev/sda /tmp/recovery
```

### API Overview

The gRPC service provides the following methods:

#### Initialize
```cpp
rpc Initialize(InitializeRequest) returns (InitializeResponse)
```
Creates a new PhotoRec context and returns a unique context ID.

#### GetDisks
```cpp
rpc GetDisks(GetDisksRequest) returns (GetDisksResponse)
```
Lists all available disks on the system.

#### GetPartitions
```cpp
rpc GetPartitions(GetPartitionsRequest) returns (GetPartitionsResponse)
```
Lists partitions on a specific disk.

#### StartRecovery
```cpp
rpc StartRecovery(StartRecoveryRequest) returns (StartRecoveryResponse)
```
Starts a file recovery process in the background.

#### GetRecoveryStatus
```cpp
rpc GetRecoveryStatus(GetRecoveryStatusRequest) returns (GetRecoveryStatusResponse)
```
Gets the current status and progress of a recovery session.

#### StopRecovery
```cpp
rpc StopRecovery(StopRecoveryRequest) returns (StopRecoveryResponse)
```
Stops/aborts a running recovery process.

#### ConfigureOptions
```cpp
rpc ConfigureOptions(ConfigureOptionsRequest) returns (ConfigureOptionsResponse)
```
Configures recovery options for a context.

#### GetStatistics
```cpp
rpc GetStatistics(GetStatisticsRequest) returns (GetStatisticsResponse)
```
Gets recovery statistics by file type.

#### Cleanup
```cpp
rpc Cleanup(CleanupRequest) returns (CleanupResponse)
```
Cleans up resources for a context.

#### Shutdown
```cpp
rpc Shutdown(ShutdownRequest) returns (ShutdownResponse)
```
Gracefully shuts down the server. Can force shutdown even with active recoveries.

## Protocol Buffer Definition

The service interface is defined in `proto/photorec.proto`:

```protobuf
service PhotoRecService {
  rpc Initialize(InitializeRequest) returns (InitializeResponse);
  rpc GetDisks(GetDisksRequest) returns (GetDisksResponse);
  rpc GetPartitions(GetPartitionsRequest) returns (GetPartitionsResponse);
  rpc StartRecovery(StartRecoveryRequest) returns (StartRecoveryResponse);
  rpc GetRecoveryStatus(GetRecoveryStatusRequest) returns (GetRecoveryStatusResponse);
  rpc StopRecovery(StopRecoveryRequest) returns (StopRecoveryResponse);
  rpc ConfigureOptions(ConfigureOptionsRequest) returns (ConfigureOptionsResponse);
  rpc GetStatistics(GetStatisticsRequest) returns (GetStatisticsResponse);
  rpc Cleanup(CleanupRequest) returns (CleanupResponse);
  rpc Shutdown(ShutdownRequest) returns (ShutdownResponse);
}
```

For detailed information about the shutdown functionality, see [SHUTDOWN_GUIDE.md](SHUTDOWN_GUIDE.md).

## Client Libraries

### C++ Client
Use the generated gRPC C++ client:

```cpp
#include <grpcpp/grpcpp.h>
#include "proto/photorec.grpc.pb.h"

// Create channel
auto channel = grpc::CreateChannel("localhost:50051", 
                                  grpc::InsecureChannelCredentials());

// Create stub
auto stub = photorec::PhotoRecService::NewStub(channel);

// Make calls
photorec::InitializeRequest request;
request.set_device("/dev/sda");
request.set_recovery_dir("/tmp/recovery");

photorec::InitializeResponse response;
grpc::ClientContext context;
auto status = stub->Initialize(&context, request, &response);
```

### Python Client
```python
import grpc
import photorec_pb2
import photorec_pb2_grpc

# Create channel
channel = grpc.insecure_channel('localhost:50051')
stub = photorec_pb2_grpc.PhotoRecServiceStub(channel)

# Make calls
request = photorec_pb2.InitializeRequest(
    device="/dev/sda",
    recovery_dir="/tmp/recovery"
)
response = stub.Initialize(request)
```

### Go Client
```go
import (
    "google.golang.org/grpc"
    pb "path/to/generated/proto"
)

// Create connection
conn, err := grpc.Dial("localhost:50051", grpc.WithInsecure())
if err != nil {
    log.Fatal(err)
}
defer conn.Close()

// Create client
client := pb.NewPhotoRecServiceClient(conn)

// Make calls
request := &pb.InitializeRequest{
    Device:       "/dev/sda",
    RecoveryDir:  "/tmp/recovery",
}
response, err := client.Initialize(context.Background(), request)
```

## Configuration Options

### Recovery Options

- **Paranoid Mode** (0-2): Controls file validation strictness
- **Keep Corrupted Files**: Whether to keep partially recovered files
- **EXT2/3/4 Optimization**: Enable filesystem-specific optimizations
- **Expert Mode**: Enable advanced recovery features
- **Low Memory Mode**: Use memory-efficient algorithms
- **Verbose Output**: Enable detailed logging
- **Carve Free Space Only**: Only scan unallocated space
- **File Type Selection**: Enable/disable specific file types

### Server Configuration

- **Address**: Server listening address (default: 0.0.0.0:50051)
- **Log Level**: Control server logging verbosity
- **Max Concurrent Sessions**: Limit number of simultaneous recoveries

## Security Considerations

⚠️ **Important**: This server provides access to disk recovery functionality. Consider:

1. **Network Security**: Use TLS/SSL in production environments
2. **Access Control**: Implement authentication and authorization
3. **Device Access**: Ensure proper permissions for disk access
4. **Firewall Rules**: Restrict access to trusted networks
5. **Logging**: Monitor server access and operations

## Troubleshooting

### Common Issues

1. **Permission Denied**: Ensure the server has access to target devices
2. **gRPC Not Found**: Install gRPC development libraries
3. **Protobuf Errors**: Regenerate protobuf files with `make clean && make`
4. **Library Linking**: Ensure all required libraries are installed

### Debug Mode

Enable verbose logging:
```bash
./testdisk_grpc_wrapper --address 0.0.0.0:50051 --log-level debug
```

### Building with Debug Information

```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
```

## License

This project is licensed under the GNU General Public License v2 or later, same as PhotoRec.

Copyright (C) 1998-2024 Christophe GRENIER <grenier@cgsecurity.org>

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Add tests if applicable
5. Submit a pull request

## Support

For issues related to:
- **PhotoRec functionality**: See [PhotoRec documentation](https://www.cgsecurity.org/wiki/PhotoRec)
- **gRPC/Network issues**: Check gRPC documentation
- **Build problems**: Ensure all dependencies are installed correctly

## Acknowledgments

- **PhotoRec Team**: For the excellent file recovery library
- **gRPC Team**: For the high-performance RPC framework
- **Protobuf Team**: For the efficient serialization format 