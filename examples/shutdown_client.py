#!/usr/bin/env python3
"""
Example client to demonstrate server shutdown functionality.
"""
import argparse
import sys
from pathlib import Path

import grpc

# Add the proto directory to the path
sys.path.insert(0, str(Path(__file__).parent.parent / "proto"))

# Import the generated gRPC stubs
import photorec_pb2
import photorec_pb2_grpc


def shutdown_server(host="localhost", port=50051, force=False, reason=""):
    """
    Send a shutdown request to the PhotoRec gRPC server.

    Args:
        host: Server host address
        port: Server port
        force: Force shutdown even with active recoveries
        reason: Optional reason for shutdown
    """
    # Create gRPC channel
    channel = grpc.insecure_channel(f"{host}:{port}")

    # Create client stub
    stub = photorec_pb2_grpc.PhotoRecServiceStub(channel)

    try:
        # Create shutdown request
        request = photorec_pb2.ShutdownRequest(force=force, reason=reason)

        print(f"Sending shutdown request to {host}:{port}...")
        print(f"Force: {force}")
        if reason:
            print(f"Reason: {reason}")

        # Send shutdown request
        response = stub.Shutdown(request)

        if response.success:
            print("✓ Shutdown request successful")
            print(f"Message: {response.message}")
        else:
            print("✗ Shutdown request failed")
            print(f"Error: {response.error_message}")
            return False

    except grpc.RpcError as e:
        print(f"✗ gRPC error: {e.code()}: {e.details()}")
        return False
    except Exception as e:
        print(f"✗ Unexpected error: {e}")
        return False
    finally:
        channel.close()

    return True


def main():
    """Main function."""
    parser = argparse.ArgumentParser(
        description="Shutdown PhotoRec gRPC server remotely",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )

    parser.add_argument("--host", "-H", default="localhost", help="Server host address")

    parser.add_argument("--port", "-p", type=int, default=50051, help="Server port")

    parser.add_argument(
        "--force",
        "-f",
        action="store_true",
        help="Force shutdown even if there are active recovery sessions",
    )

    parser.add_argument(
        "--reason", "-r", default="", help="Optional reason for shutdown"
    )

    args = parser.parse_args()

    # Shutdown the server
    success = shutdown_server(
        host=args.host, port=args.port, force=args.force, reason=args.reason
    )

    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
