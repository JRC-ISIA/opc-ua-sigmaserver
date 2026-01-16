# Certificate Management Guide

This document explains how to manage SSL/TLS certificates for the OPC UA Aggregation Server.

## Certificate Directory Structure

```
certs/
├── server/              # Server certificates
│   ├── server_cert.der  # Server public certificate
│   └── server_key.der   # Server private key
└── trusted/             # Trusted CA certificates
    └── *.der            # Trusted client certificates
```

### Certificate Formats

- **DER (Distinguished Encoding Rules)**: Binary format, used by open62541 library

## Generating Your Own Certificates

### 1. Generate Server Certificate and Key

```bash
# Navigate to the certs/server directory
cd certs/server

# Generate a 3072-bit RSA private key
openssl genrsa -out server_key.pem 3072

# Generate a self-signed certificate (valid for 365 days)
openssl req -new -x509 -key server_key.pem -out server_cert.pem -days 365 \
  -subj "/C=DE/ST=State/L=City/O=YourOrganization/OU=Department/CN=AggregationServer"

# Convert to DER format (required by open62541)
openssl x509 -in server_cert.pem -outform DER -out server_cert.der
openssl rsa -in server_key.pem -outform DER -out server_key.der
```

### 2. Add Client Certificates to Trusted Store

Add the client's public certificate to the server's trusted store:

```bash
# Copy client certificate to trusted directory
cp certs/client/client_cert.der certs/trusted/

# For connecting external clients (e.g., UAExpert), add their certificates here too
cp /path/to/uaexpert_cert.der certs/trusted/
```

## Using Certificates with Docker

When running in Docker, ensure certificates are mounted correctly:

```yaml
# docker-compose.yml
services:
  aggregation-server:
    image: your-image
    volumes:
      - ./certs:/app/certs:ro
```