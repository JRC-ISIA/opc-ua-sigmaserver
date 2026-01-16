# SigmaServer

A secure TCP-level aggregating server bridging legacy OT devices (Modbus, OPC UA) into Industry 4.0. It provides unique secure endpoints for each device to prevent namespace pollution and ensure strict isolation between security zones.

## Main Features

- **OPC UA Server Implementation**: Provides secure data via the OPC UA protocol
- **OPC UA Client**: Connects to legacy devices (e.g. PLC)
- **Modbus Client**: Supports Modbus TCP connections
- **Multi-PLC Support**: Aggregates data from multiple PLCs simultaneously
- **Thread-safe Data Processing**: Secure management of data from various sources
- **Docker Support**: Deployable in container environments

## Directory Structure

```
AggregationServer/
├── src/                    # C++ source code
│   ├── server/            # OPC UA server implementation
│   ├── client/            # OPC UA/Modbus client implementation
│   ├── config/            # Configuration parser
│   ├── utils/             # Utility functions
│   └── thread/            # Thread utilities
├── config/                # Configuration files
│   ├── <alias>/           # List of aggregated nodes
├── certs/                 # SSL/TLS certificates
│   ├── server/           # Server certificates
│   ├── client/           # Client certificates
│   └── trusted/          # Trusted CA certificates
├── docs/                  # Documentation
│   ├── BUILD.md          # Linux/Docker build guide
│   ├── CONFIG.md         # Configuration documentation
│   └── CERTIFICATES.md   # Certificate management guide
└── docker/                # Docker configurations
    ├── Dockerfile.debian  # Debian-based image
    └── Dockerfile.ubuntu  # Ubuntu-based image
```

## Prerequisites

### Build Dependencies
- CMake 3.10+
- C++17-compatible compiler (GCC/Clang/MSVC)
- open62541 Library (OPC UA)
- OpenSSL (for TLS/SSL)
- libmodbus (Modbus TCP) - Linux only
- Python 3

## Build Instructions

### Quick Start

**Linux/Docker:**
```bash
# Clone with submodules
git clone --recursive https://github.com/JRC-ISIA/opc-ua-sigmaserver.git
cd AggregationServer

# Build with CMake
mkdir build && cd build
cmake ..
make -j$(nproc)

# Or use Docker
cd docker
docker-compose up --build
```

**Windows:**
```powershell
# Clone with submodules
git clone --recursive https://github.com/JRC-ISIA/opc-ua-sigmaserver.git
cd AggregationServer

# CMake build (builds open62541 first)
mkdir build-vs
cd build-vs
cmake -G "Visual Studio 17 2022" -A x64 ..
cmake --build . --config Release
```

### Detailed Build Instructions

For comprehensive platform-specific build instructions, see:
- **[BUILD.md](docs/BUILD.md)** - Windows/Linux/Docker build guide

## Configuration

Configuration is done through JSON files:

1. **Quick Start**: Copy the example configurations
   ```bash
   cp client_configuration.example.json client_configuration.json
   cp server_configuration.example.json server_configuration.json
   ```

2. **Edit the configuration files** to match your setup:
   - `client_configuration.json`: PLC and Modbus client connections
   - `server_configuration.json`: OPC UA server settings

3. **Certificates**: Generate your own certificates for production use
   ```bash
   # See docs/CERTIFICATES.md for detailed instructions
   cd certs/server && openssl genrsa -out server_key.pem 2048
   ```

**Detailed Documentation**:
- **[CONFIG.md](docs/CONFIG.md)** - Complete configuration guide with all parameters explained
- **[CERTIFICATES.md](docs/CERTIFICATES.md)** - Certificate generation