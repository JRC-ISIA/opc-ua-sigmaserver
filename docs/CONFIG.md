# Configuration Guide

This document explains how to configure the OPC UA Aggregation Server.

## Quick Start

1. Copy the example configuration files:
   ```bash
   cp client_configuration.example.json client_configuration.json
   cp server_configuration.example.json server_configuration.json
   ```

2. Edit the configuration files to match your setup
3. Start the aggregation server

## Client Configuration (`client_configuration.json`)

This file configures the data sources (OPC UA and Modbus devices) that the aggregation server will connect to.

### OPC UA PLC Configuration

```json
{
  "plcs": [
    {
      "alias": "PLC1",
      "endpoint": "opc.tcp://localhost:4840",
      "endpointFallback": "opc.tcp://192.168.1.100:4840",
      "pollIntervalSec": 1,
      "latencyTestEnabled": false,
      "latencyCsvPath": "latency_data_PLC1.csv",
      "nodes": [
        { "namespaceIndex": 2, "nodeId": 1001 }
      ]
    }
  ]
}
```

**Parameters:**

- **`alias`** (string, required): Unique identifier for the legacy device.
- **`endpoint`** (string, required): Primary OPC UA endpoint URL. Format: `opc.tcp://hostname:port`
- **`endpointFallback`** (string, optional): Fallback endpoint URL if primary connection fails.
- **`pollIntervalSec`** (number, required): How often to poll data from this PLC (in seconds).
- **`latencyTestEnabled`** (boolean, optional): Enable internal software latency tracking for this connection. Default: `false`
- **`latencyCsvPath`** (string, optional): Path to CSV file for latency data. Required if `latencyTestEnabled` is `true`.
- **`nodes`** (array, required): List of OPC UA nodes to read from this PLC.
  - **`namespaceIndex`** (number): OPC UA namespace index
  - **`nodeId`** (number): OPC UA node identifier

### Modbus Configuration

```json
{
  "modbus": [
    {
      "alias": "ModbusDevice1",
      "ip": "192.168.1.200",
      "port": 502,
      "pollIntervalSec": 1,
      "registers": [
        { "name": "Temperature", "address": 0, "datatype": "float" }
      ]
    }
  ]
}
```

**Parameters:**

- **`alias`** (string, required): Unique identifier for this Modbus device.
- **`ip`** (string, required): IP address of the Modbus device.
- **`port`** (number, required): Modbus TCP port.
- **`pollIntervalSec`** (number, required): How often to poll data (in seconds).
- **`registers`** (array, required): List of Modbus registers to read.
  - **`name`** (string): Descriptive name for this register
  - **`address`** (number): Modbus register address
  - **`datatype`** (string): Data type. Supported: `float`, `uint16`, `int16`, `uint32`, `int32`

## Server Configuration (`server_configuration.json`)

This file configures the OPC UA server endpoints that the aggregation server will expose.

```json
{
  "servers": [
    {
      "port": 4841,
      "plcAlias": "PLC1",
      "mode": "CS",
      "latencyTestEnabled": false
    }
  ]
}
```

**Parameters:**

- **`port`** (number, required): Port number for this OPC UA server endpoint.
- **`plcAlias`** (string, required): Must match an `alias` from `client_configuration.json`. This determines which data source this server endpoint exposes.
- **`mode`** (string, required): Client-Server or Publish/Subscribe mode:
  - `"CS"` - Client-Server
  - `"PubSub"` - Publish/Subscribe
- **`latencyTestEnabled`** (boolean, optional): Enable internal software latency test for this connection. Default: `false`

## Example: Complete Configuration

### Multi-PLC Setup

If you have multiple legacy devices and want to expose each on a separate port:

**client_configuration.json:**
```json
{
  "plcs": [
    {
      "alias": "PLC_Line1",
      "endpoint": "opc.tcp://192.168.1.10:4840",
      "pollIntervalSec": 1,
      "nodes": [
        { "namespaceIndex": 2, "nodeId": 1001 },
        { "namespaceIndex": 2, "nodeId": 1002 }
      ]
    },
    {
      "alias": "PLC_Line2",
      "endpoint": "opc.tcp://192.168.1.20:4840",
      "pollIntervalSec": 2,
      "nodes": [
        { "namespaceIndex": 2, "nodeId": 2001 }
      ]
    }
  ],
  "modbus": []
}
```

**server_configuration.json:**
```json
{
  "servers": [
    {
      "port": 4841,
      "plcAlias": "PLC_Line1",
      "mode": "CS"
    },
    {
      "port": 4842,
      "plcAlias": "PLC_Line2",
      "mode": "CS"
    }
  ]
}
```

### Docker

When running in Docker, use `host.docker.internal` to connect to services on the host machine:

```json
{
  "plcs": [
    {
      "alias": "HostPLC",
      "endpoint": "opc.tcp://host.docker.internal:4840",
      "endpointFallback": "opc.tcp://192.168.1.100:4840",
      "pollIntervalSec": 1,
      "nodes": [...]
    }
  ]
}
```

## Internal Software Latency Testing

To enable the internal software latency test:

1. Set `"latencyTestEnabled": true` in client configuration
2. Specify a `"latencyCsvPath"` for CSV output
3. Enable in server configuration for end-to-end measurements