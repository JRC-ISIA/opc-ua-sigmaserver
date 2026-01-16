# Build Instructions for AggregationServer

This project **automatically builds open62541 v1.4.8** with all required features (Encryption, PubSub, etc.).

## Prerequisites

### Linux/Docker
- CMake 3.10+
- GCC/Clang with C++17 support
- OpenSSL Development Libraries (`libssl-dev`)
- libmodbus (`libmodbus-dev`)
- Python 3
- Git

### Windows
- Visual Studio 2022
- CMake 3.10+
- OpenSSL
- Python 3
- Git

## Linux/Docker Build

### Step 1: Clone Repository with Submodules

```bash
git clone --recursive https://github.com/Dalibor28/AggregationServer.git
cd AggregationServer

# If already cloned, initialize submodules:
git submodule update --init --recursive
```

### Step 2: Install Dependencies

```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    git \
    libssl-dev \
    libmodbus-dev \
    python3 \
    python3-pip \
    nlohmann-json3-dev
```

### Step 3: Build

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

The result is: `build/aggregation_server`

### Docker Build

```bash
cd docker
docker-compose up --build
```

## Windows Build (Visual Studio 2022)

### Step 1: Clone Repository with Submodules

```powershell
git clone --recursive https://github.com/Dalibor28/AggregationServer.git
cd AggregationServer

# If already cloned:
git submodule update --init --recursive
```
### Step 2: CMake Build for Visual Studio

```powershell
# In the AggregationServer directory
mkdir build-vs
cd build-vs

# Configure CMake (automatically uses the submodule)
cmake -G "Visual Studio 17 2022" -A x64 ..

# Build (or open in Visual Studio)
cmake --build . --config Release
```

### Step 3: Open Visual Studio Project

Now you can either:

**Option A:** Use the CMake-generated project:
```powershell
# Open the generated .sln file
start build-vs\aggregation_server.sln
```

**Option B:** Use the existing Visual Studio project:
1. Open `Sigma Serv.vcxproj` in Visual Studio 2022
2. The project file is already configured to reference `build-vs\`

## Verify Build

### Linux
```bash
./build/aggregation_server --version
```

### Windows
```powershell
.\build-vs\Release\aggregation_server.exe --version
```
## After the Build

### 1. Configure the Server

Copy the example configuration files and edit them for your setup:

```bash
# Copy templates
cp client_configuration.example.json client_configuration.json
cp server_configuration.example.json server_configuration.json

# Edit configurations to match your PLC setup
nano client_configuration.json
nano server_configuration.json
```

**See [CONFIG.md](CONFIG.md) for detailed configuration documentation**

### 2. Generate Certificates

```bash
# Quick example (see CERTIFICATES.md for details)
cd certs/server
openssl genrsa -out server_key.pem 2048
openssl req -x509 -new -key server_key.pem -out server_cert.pem -days 365
openssl x509 -outform der -in server_cert.pem -out server_cert.der
openssl rsa -outform der -in server_key.pem -out server_key.der
```

**See [CERTIFICATES.md](CERTIFICATES.md) for comprehensive certificate generation**

### 3. Install Python Test Dependencies (Optional)

```bash
pip install -r requirements.txt
```

### 4. Start the Server

```bash
# Linux
./build/aggregation_server

# Windows
.\build-vs\Release\aggregation_server.exe
```