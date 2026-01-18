# Deploying a TCP Server to Fly.io: A Technical Report

## Overview

This document describes the process of creating and deploying a minimal
C-based TCP server to Fly.io. The server accepts connections on port 1848
and responds with "hello\n" before closing the connection. Running a service
on TCP can be more costly though, than relying on HTTP or HHTPS.

## Project Structure

```
fly-test/
├── main.c           # Server implementation
├── CMakeLists.txt   # Build configuration
├── Dockerfile       # Container image definition
└── fly.toml         # Fly.io deployment configuration
```

## Implementation

### Server Code

The server is implemented in C using standard POSIX socket APIs. The i
mplementation uses an IPv6 socket configured to accept both IPv4 and IPv6
connections.

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

int main(int argc, char *argv[])
{
   int server_fd, client_fd;
   struct sockaddr_in6 address;
   int opt = 1;
   int addrlen = sizeof(address);
   int port = 1848;

   if (argc > 1)
      port = atoi(argv[1]);

   // Create IPv6 socket
   if ((server_fd = socket(AF_INET6, SOCK_STREAM, 0)) == 0)
   {
      perror("socket failed");
      exit(EXIT_FAILURE);
   }

   // Enable address reuse
   if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)))
   {
      perror("setsockopt");
      exit(EXIT_FAILURE);
   }

   // Disable IPv6-only mode to accept IPv4 connections
   int no = 0;
   if (setsockopt(server_fd, IPPROTO_IPV6, IPV6_V6ONLY, &no, sizeof(no)))
   {
      perror("setsockopt IPV6_V6ONLY");
      exit(EXIT_FAILURE);
   }

   // Bind to all interfaces
   address.sin6_family = AF_INET6;
   address.sin6_addr = in6addr_any;
   address.sin6_port = htons(port);

   if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
   {
      perror("bind failed");
      exit(EXIT_FAILURE);
   }

   if (listen(server_fd, 3) < 0)
   {
      perror("listen");
      exit(EXIT_FAILURE);
   }

   printf("Server listening on port %d\n", port);

   // Accept connections and respond
   while (1)
   {
      if ((client_fd = accept(server_fd, (struct sockaddr *)&address, 
                              (socklen_t*)&addrlen)) < 0)
      {
         perror("accept");
         continue;
      }

      write(client_fd, "hello\n", 6);
      close(client_fd);
   }

   close(server_fd);
   return 0;
}
```

The critical aspect is the use of `AF_INET6` with `IPV6_V6ONLY` disabled.
This configuration allows the socket to accept connections on both IPv4 and
IPv6 addresses.

### Build Configuration

CMakeLists.txt provides a minimal build configuration:

```cmake
cmake_minimum_required(VERSION 3.10)
project(hello-server C)
add_executable(hello-server main.c)
```

### Container Image

The Dockerfile uses a multi-stage build to separate compilation from runtime:

```dockerfile
FROM --platform=linux/amd64 alpine:latest AS builder
RUN apk add --no-cache gcc musl-dev cmake make
WORKDIR /build
COPY main.c CMakeLists.txt ./
RUN cmake . && make

FROM --platform=linux/amd64 alpine:latest
COPY --from=builder /build/hello-server /app/hello-server
CMD ["/app/hello-server"]
```

The builder stage installs compilation tools and builds the binary. The runtime
stage contains only the Alpine base image and the compiled binary. This
approach keeps the source code out of the production image and reduces the
final image size to approximately 91 MB.

The platform is explicitly set to `linux/amd64` to match Fly.io's
infrastructure.

> #### Pain Point
>
> This could be a pain point in the future.
> Automatic discovery of the fly.io hardware would be preferable.


## Fly.io Configuration

### Service Definition

The fly.toml file defines a TCP service rather than an HTTP service:

```toml
app = "hello-server"
primary_region = "ams"

[build]

[[services]]
  internal_port = 1848
  protocol = "tcp"

  [[services.ports]]
    port = 1848
    handlers = []

[[vm]]
  cpu_kind = "shared"
  cpus = 1
  memory_mb = 256
```

The `[[services]]` section specifies:
- `protocol = "tcp"` for raw TCP connections
- `internal_port = 1848` where the application listens
- `port = 1848` for external access
- `handlers = []` to disable HTTP processing

The VM configuration allocates minimal resources: 1 shared CPU and 256 MB of memory.

### Deployment Process

```bash
# Create the application
flyctl apps create hello-server

# Deploy the application
flyctl deploy

# Verify deployment
flyctl status
```

Fly.io allocated two IP addresses:
- IPv6: `2a09:8280:1::c5:a273:0` (dedicated)
- IPv4: `66.241.125.63` (shared)

The deployment created two machines in the Amsterdam region for high
availability.

## Network Architecture

The server binds to `:::1848`, which represents the IPv6 wildcard address.
With `IPV6_V6ONLY` disabled, this binding accepts connections on both
IPv4-mapped addresses and native IPv6 addresses.

Verification on the deployed machine:

```bash
$ flyctl ssh console -C "netstat -tlnp | grep 1848"
tcp  0  0  :::1848  :::*  LISTEN  641/hello-server
```

The `:::1848` notation confirms IPv6 wildcard binding with IPv4 compatibility.

## Testing

### Local Testing

```bash
$ cd build && ./hello-server &
$ echo | nc localhost 1848
hello
$ echo | nc ::1 1848
hello
```

Both IPv4 (localhost) and IPv6 (::1) connections work correctly locally.

### Remote Testing

From a server with a routable IPv6 address:

```bash
$ telnet -6 hello-server.fly.dev 1848
Trying 2a09:8280:1::c5:a273:0...
Connected to hello-server.fly.dev.
Escape character is '^]'.
hello
Connection closed by foreign host.
```

IPv6 connections function reliably.

IPv4 connections show different behavior:

```bash
$ telnet -4 hello-server.fly.dev 1848
Trying 66.241.125.63...
Connected to hello-server.fly.dev.
Escape character is '^]'.
Connection closed by foreign host.
```

The connection establishes but the response is not received. This behavior
depends on the client's network configuration and IPv6 capabilities.

### Why it fails

Of course for many ipv6.

Check your setup with [test-ipv6.com](https://test-ipv6.com/).

It seems you will likely need to get an IPv4 address (at cost).

``` bash
fly ips allocate-v4
```

This will up the cost of the whole installation from nominally $2 when doing
nothing to $26. Ouch.


## Issues and Considerations

### IPv4-Only Socket Binding

Initial implementation used `AF_INET` with `INADDR_ANY`, resulting in binding
to `0.0.0.0:1848`. This configuration only accepts IPv4 connections. On
Fly.io, where IPv6 is the primary protocol, connections would establish but
immediately close without data transfer.

The solution requires using `AF_INET6` with `in6addr_any` and explicitly
disabling `IPV6_V6ONLY`. This creates a dual-stack socket that accepts both
protocol versions.

### Service Type Configuration

Using `[http_service]` in fly.toml causes Fly.io to expect HTTP protocol
traffic. The proxy attempts to parse HTTP requests and responses, which fails
with raw TCP data. The correct configuration uses `[[services]]` with
`protocol = "tcp"` and an empty `handlers` array.

### IPv4 Connectivity Limitations

Fly.io's IPv4 addresses are shared and routing depends on the client's network
configuration. Clients without proper IPv6 support or those behind certain NAT
configurations may experience issues with IPv4 connections. IPv6 connections
are more reliable on Fly.io's infrastructure.

Testing from virtual machines or Docker containers without routable IPv6
addresses may show inconsistent behavior. Production deployments should
account for this by ensuring clients can connect via IPv6 or by implementing
additional routing logic.

### Port Selection (Voodoo)

Standard ports like 80, 443, or 8080 may have special handling in Fly.io's
infrastructure. Using a non-standard port (1848 in this case) avoids potential
conflicts with HTTP-specific routing rules.

### Debugging Techniques

The `flyctl ssh console` command provides direct access to the running machine.
Useful commands for debugging:

```bash
# Check listening ports
netstat -tlnp | grep <port>

# View running processes
ps aux | grep <process>

# Test local connectivity
nc localhost <port>
```

The netstat output format differs between IPv4 and IPv6:
- `0.0.0.0:<port>` indicates IPv4-only binding
- `:::<port>` indicates IPv6 wildcard binding (may accept IPv4)

### Resource Allocation

The minimal configuration (256 MB RAM, 1 shared CPU) is sufficient for a simple
TCP server. Fly.io's auto-start and auto-stop features allow scaling to zero
when idle, reducing costs for low-traffic applications.


![Heartlessly Vibecoded](https://www.mulle-kybernetik.com/pix/heartlessly-vibecoded.png)

