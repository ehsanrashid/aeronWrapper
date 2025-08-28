# Aeron Wrapper

A modern C++17 wrapper for the [Aeron](https://github.com/aeron-io/aeron) high-performance messaging library. This wrapper provides a simplified, RAII-compliant interface with enhanced error handling, connection state management, and convenient publishing/subscribing methods.

## Features

- **Simplified API**: Easy-to-use classes for Publications and Subscriptions
- **Enhanced Error Handling**: Custom exception types and detailed error reporting
- **Connection State Management**: Automatic connection state tracking with optional callbacks
- **Background Polling**: Built-in background polling for subscriptions with thread management
- **Retry Logic**: Configurable retry mechanisms for reliable message publishing
- **Type Safety**: Template methods for type-safe data publishing
- **RAII Compliance**: Proper resource management with move semantics
- **Synchronous Operations**: Blocking publish methods with timeout support

## Building

### Prerequisites

- CMake 3.16 or higher
- C++17 compatible compiler
- Git (for fetching Aeron dependency)

### Build Instructions

```bash
#if build exists then run this command first:
rm -rf build
#else:
mkdir build && cd build
cmake -S .. -B . -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=<path_to_aeronWrapper/install>
cmake --build . -j$(nproc)
cmake --install .
```

The build system automatically fetches and builds Aeron from the official repository.

### CMake Integration

To use this wrapper in your own CMake project:

```cmake
find_package(aeronWrapper CONFIG REQUIRED)

target_link_libraries(${PROJECT_NAME} PRIVATE
    aeronWrapper::aeronWrapper
    ...
)
```

To compile your project

```run
cmake .. -DCMAKE_INSTALL_PREFIX=<path_to_aeronWrapper/install>
```


## Quick Start

### Basic Publisher

```cpp
#include "aeron_wrapper.h"

int main() {
    try {
        // Create Aeron client
        aeron_wrapper::Aeron aeron;
        
        // Create publication
        auto pub = aeron.create_publication("aeron:udp?endpoint=localhost:40123", 1001);
        
        // Publish a message
        std::string message = "Hello, Aeron!";
        auto result = pub->offer(message);
        
        if (result == aeron_wrapper::PublicationResult::SUCCESS) {
            std::cout << "Message published successfully" << std::endl;
        }
    } catch (const aeron_wrapper::AeronException& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    
    return 0;
}
```

### Basic Subscriber

```cpp
#include "aeron_wrapper.h"
#include <iostream>

int main() {
    try {
        // Create Aeron client
        aeron_wrapper::Aeron aeron;
        
        // Create subscription
        auto sub = aeron.create_subscription("aeron:udp?endpoint=localhost:40123", 1001);
        
        // Define message handler
        auto handler = [](const aeron_wrapper::FragmentData& fragment) {
            std::cout << "Received: " << fragment.as_string() << std::endl;
        };
        
        // Poll for messages
        while (true) {
            int fragments = sub->poll(handler, 10);
            if (fragments == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    } catch (const aeron_wrapper::AeronException& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    
    return 0;
}
```

## API Reference

### Core Classes

#### `aeron_wrapper::Aeron`

Main client class that manages the Aeron connection and creates publications/subscriptions.

**Constructor:**
```cpp
explicit Aeron(const std::string& aeronDir = "");
```

**Key Methods:**
- `create_publication(channel, streamId, connectionHandler)` - Creates a new publication
- `create_subscription(channel, streamId, connectionHandler)` - Creates a new subscription
- `is_running()` - Check if the client is active
- `close()` - Explicitly close the client

#### `aeron_wrapper::Publication`

Handles message publishing with enhanced error handling and retry logic.

**Key Methods:**

```cpp
// Basic publishing
PublicationResult offer(const std::uint8_t* buffer, std::size_t length);
PublicationResult offer(const std::string& message);
template<typename T> PublicationResult offer(const T& data);

// Publishing with retry logic
PublicationResult offer_with_retry(const std::uint8_t* buffer, std::size_t length, 
                                  int maxRetries = 3, 
                                  std::chrono::milliseconds retryDelay = std::chrono::milliseconds(1));

// Synchronous publishing (blocks until success or timeout)
bool publish_sync(const std::uint8_t* buffer, std::size_t length,
                 std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));
bool publish_sync(const std::string& message,
                 std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));

// Status methods
bool is_connected() const;
bool is_closed() const;
std::int64_t position() const;
std::int32_t session_id() const;
std::int32_t stream_id() const;
const std::string& channel() const;
```

#### `aeron_wrapper::Subscription`

Handles message subscription with polling and background processing capabilities.

**Key Methods:**

```cpp
// Polling methods
int poll(const FragmentHandler& fragmentHandler, int fragmentLimit = 10);
int block_poll(const FragmentHandler& fragmentHandler,
               std::chrono::milliseconds timeout = std::chrono::milliseconds(1000),
               int fragmentLimit = 10);

// Background polling
std::unique_ptr<BackgroundPoller> start_background_polling(const FragmentHandler& fragmentHandler);

// Status methods
bool is_connected() const;
bool is_closed() const;
bool has_images() const;
std::size_t image_count() const;
std::int32_t stream_id() const;
const std::string& channel() const;
```

### Helper Types

#### `PublicationResult`

Enum class representing the result of a publication attempt:

- `SUCCESS` - Message published successfully
- `NOT_CONNECTED` - Publication not connected
- `BACK_PRESSURED` - Back pressure from receiver
- `ADMIN_ACTION` - Administrative action in progress
- `CLOSED` - Publication is closed
- `MAX_POSITION_EXCEEDED` - Maximum position exceeded

#### `FragmentData`

Structure containing received message data and metadata:

```cpp
// Fragment handler with metadata
struct FragmentData final {
    aeron::concurrent::AtomicBuffer atomicBuffer;
    aeron::util::index_t length;
    aeron::util::index_t offset;
    aeron::Header header;

    // Helper to get data as string
    std::string as_string() const;

    // Helper to get data as specific type
    template <typename T>
    const T& as() const;
};
```

#### Function Types

```cpp
using FragmentHandler = std::function<void(const FragmentData& fragment)>;
using ConnectionHandler = std::function<void(bool connected)>;
```

## Advanced Usage

### Connection State Monitoring

```cpp
auto connectionHandler = [](bool connected) {
    if (connected) {
        std::cout << "Publication connected" << std::endl;
    } else {
        std::cout << "Publication disconnected" << std::endl;
    }
};

auto pub = aeron.create_publication("aeron:udp?endpoint=localhost:40123", 1001, connectionHandler);
```

### Background Polling

```cpp
auto sub = aeron.create_subscription("aeron:udp?endpoint=localhost:40123", 1001);

auto handler = [](const aeron_wrapper::FragmentData& fragment) {
    std::cout << "Background received: " << fragment.as_string() << std::endl;
};

// Start background polling
auto poller = sub->start_background_polling(handler);

// Do other work...
std::this_thread::sleep_for(std::chrono::seconds(10));

// Polling stops automatically when poller goes out of scope
```

### Type-Safe Publishing

```cpp
struct MyData {
    int id;
    double value;
    char name[32];
};

MyData data{42, 3.14, "example"};
auto result = pub->offer(data);
```

### Reliable Publishing with Retry

```cpp
std::string message = "Important message";

// Retry up to 5 times with 10ms delay between retries
auto result = pub->offer_with_retry(message, 5, std::chrono::milliseconds(10));

if (result != aeron_wrapper::PublicationResult::SUCCESS) {
    std::cerr << "Failed to publish after retries: " 
              << aeron_wrapper::pubresult_to_string(result) << std::endl;
}
```

### Synchronous Publishing

```cpp
std::string message = "Synchronous message";

// Block until published or 10 second timeout
bool success = pub->publish_sync(message, std::chrono::seconds(10));

if (success) {
    std::cout << "Message published synchronously" << std::endl;
} else {
    std::cout << "Failed to publish within timeout" << std::endl;
}
```

## Error Handling

The wrapper provides enhanced error handling through the `AeronException` class:

```cpp
try {
    aeron_wrapper::Aeron aeron("/invalid/path");
} catch (const aeron_wrapper::AeronException& e) {
    std::cerr << "Aeron error: " << e.what() << std::endl;
}
```

All wrapper methods that can fail throw `AeronException` with descriptive error messages.

## Performance Considerations

- Use `offer()` for best performance in high-throughput scenarios
- Use `offer_with_retry()` for improved reliability with minimal performance impact
- Use `publish_sync()` only when delivery confirmation is critical
- Background polling is efficient for continuous message processing
- Template `offer()` methods avoid unnecessary copying for POD types

## Thread Safety

- `Publication` and `Subscription` objects are **not** thread-safe
- The `Aeron` client can be used to create multiple publications/subscriptions safely
- Background polling creates its own thread and handles synchronization internally
- Connection handlers may be called from internal Aeron threads

## Examples Directory Structure

```
examples/
├── basic_pubsub/          # Simple publisher-subscriber example
├── reliable_messaging/    # Example with retry logic and error handling
├── background_polling/    # Background polling demonstration
├── type_safe_messaging/   # Template-based type-safe messaging
└── connection_monitoring/ # Connection state monitoring example
``` 
