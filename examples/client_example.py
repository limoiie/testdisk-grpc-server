#!/usr/bin/env python3
"""
PhotoRec gRPC Server Test Script

This script tests the PhotoRec gRPC server by connecting to it and
performing basic operations like initialization and disk discovery.

Usage:
    python3 test_server.py [server_address] [device_path]

Example:
    python3 test_server.py localhost:50051 /dev/sda
"""

import argparse
import sys
import time
from concurrent.futures import ThreadPoolExecutor

import grpc

# Import the generated protobuf modules
# Note: These need to be generated from the .proto file
try:
    import photorec_pb2
    import photorec_pb2_grpc
except ImportError:
    print("Error: Protobuf modules not found.")
    print("Please generate them using:")
    print(
        "  python3 -m grpc_tools.protoc --python_out=. --grpc_python_out=. -I. proto/photorec.proto"
    )
    sys.exit(1)


def test_server_connection(server_address):
    """Test basic server connectivity."""
    print(f"Testing connection to {server_address}...")

    try:
        # Create insecure channel
        channel = grpc.insecure_channel(server_address)

        # Test connection with a timeout
        try:
            grpc.channel_ready_future(channel).result(timeout=5)
            print("✓ Server connection successful")
            return True
        except grpc.FutureTimeoutError:
            print("✗ Server connection timeout")
            return False

    except Exception as e:
        print(f"✗ Connection error: {e}")
        return False


def test_initialization(stub, device_path, recovery_dir):
    """Test PhotoRec initialization."""
    print(f"\nTesting PhotoRec initialization...")
    print(f"Device: {device_path}")
    print(f"Recovery directory: {recovery_dir}")

    try:
        # Create initialization request
        request = photorec_pb2.InitializeRequest(
            device=device_path,
            recovery_dir=recovery_dir,
            log_mode=1,  # Info level logging
            log_file="",  # No log file
        )

        # Call the Initialize method
        response = stub.Initialize(request)

        if response.success:
            print(f"✓ Initialization successful")
            print(f"  Context ID: {response.context_id}")
            return response.context_id
        else:
            print(f"✗ Initialization failed: {response.error_message}")
            return None

    except Exception as e:
        print(f"✗ Initialization error: {e}")
        return None


def test_get_disks(stub, context_id):
    """Test getting available disks."""
    print(f"\nTesting disk discovery...")

    try:
        # Create request
        request = photorec_pb2.GetDisksRequest(context_id=context_id)

        # Call the GetDisks method
        response = stub.GetDisks(request)

        if response.success:
            print(f"✓ Disk discovery successful")
            print(f"  Found {len(response.disks)} disk(s):")

            for i, disk in enumerate(response.disks):
                print(f"    {i+1}. {disk.device}")
                print(f"       Description: {disk.description}")
                print(
                    f"       Size: {disk.size:,} bytes ({disk.size / (1024**3):.2f} GB)"
                )
                print(f"       Model: {disk.model}")
                print(f"       Serial: {disk.serial_no}")
                print()

            return response.disks
        else:
            print(f"✗ Disk discovery failed: {response.error_message}")
            return []

    except Exception as e:
        print(f"✗ Disk discovery error: {e}")
        return []


def test_get_partitions(stub, context_id, device_path):
    """Test getting partitions on a disk."""
    print(f"\nTesting partition discovery for {device_path}...")

    try:
        # Create request
        request = photorec_pb2.GetPartitionsRequest(
            context_id=context_id, device=device_path
        )

        # Call the GetPartitions method
        response = stub.GetPartitions(request)

        if response.success:
            print(f"✓ Partition discovery successful")
            print(f"  Found {len(response.partitions)} partition(s):")

            for partition in response.partitions:
                print(f"    Partition {partition.order}:")
                print(f"      Name: {partition.name}")
                print(f"      Filesystem: {partition.filesystem}")
                print(f"      Offset: {partition.offset:,} bytes")
                print(
                    f"      Size: {partition.size:,} bytes ({partition.size / (1024**3):.2f} GB)"
                )
                print(f"      Status: {partition.status}")
                print()

            return response.partitions
        else:
            print(f"✗ Partition discovery failed: {response.error_message}")
            return []

    except Exception as e:
        print(f"✗ Partition discovery error: {e}")
        return []


def test_cleanup(stub, context_id):
    """Test cleanup of PhotoRec context."""
    print(f"\nTesting cleanup...")

    try:
        # Create request
        request = photorec_pb2.CleanupRequest(context_id=context_id)

        # Call the Cleanup method
        response = stub.Cleanup(request)

        if response.success:
            print(f"✓ Cleanup successful")
            return True
        else:
            print(f"✗ Cleanup failed: {response.error_message}")
            return False

    except Exception as e:
        print(f"✗ Cleanup error: {e}")
        return False


def main():
    parser = argparse.ArgumentParser(description="Test PhotoRec gRPC Server")
    parser.add_argument(
        "server_address",
        nargs="?",
        default="localhost:50051",
        help="Server address (default: localhost:50051)",
    )
    parser.add_argument(
        "device_path",
        nargs="?",
        default="/dev/sda",
        help="Device path to test (default: /dev/sda)",
    )
    parser.add_argument(
        "--recovery-dir",
        default="/tmp/photorec_test",
        help="Recovery directory (default: /tmp/photorec_test)",
    )

    args = parser.parse_args()

    print("PhotoRec gRPC Server Test")
    print("=" * 40)
    print(f"Server: {args.server_address}")
    print(f"Device: {args.device_path}")
    print(f"Recovery dir: {args.recovery_dir}")
    print()

    # Test server connection
    if not test_server_connection(args.server_address):
        print("\n❌ Server connection failed. Please ensure the server is running.")
        sys.exit(1)

    # Create channel and stub
    channel = grpc.insecure_channel(args.server_address)
    stub = photorec_pb2_grpc.PhotoRecServiceStub(channel)

    context_id = None

    try:
        # Test initialization
        context_id = test_initialization(stub, args.device_path, args.recovery_dir)
        if not context_id:
            print("\n❌ Initialization failed. Cannot continue testing.")
            sys.exit(1)

        # Test disk discovery
        disks = test_get_disks(stub, context_id)
        if not disks:
            print("\n⚠️  No disks found. This might be normal depending on your system.")

        # Test partition discovery
        partitions = test_get_partitions(stub, context_id, args.device_path)
        if not partitions:
            print(
                "\n⚠️  No partitions found. This might be normal depending on your device."
            )

        print("\n✅ All tests completed successfully!")

    except KeyboardInterrupt:
        print("\n⚠️  Test interrupted by user")
    except Exception as e:
        print(f"\n❌ Test failed with error: {e}")
    finally:
        # Clean up
        if context_id:
            test_cleanup(stub, context_id)

        channel.close()


if __name__ == "__main__":
    main()
