# Agent-C

An ultra-lightweight AI agent written in C that communicates with OpenRouter API and executes shell commands.

![Agent-C Preview](preview.webp)

## Features

- **Zero Dependencies**: No curl, no libssl — native TLS via macOS SecureTransport
- **Tool Calling**: OpenAI-compatible `tools` API for shell command execution
- **Streaming**: Tokens print as they arrive via SSE
- **~4KB binary**: Single file (`agent.c`), LZMA self-extracting archive
- **Conversation Memory**: Sliding window of 20 messages with tool call history
- **Cross-Platform**: macOS and Linux

## Quick Start

```bash
export OR_KEY=your_openrouter_api_key
make
./agent-c
```

## Build

```bash
make          # auto-detect platform
make macos    # macOS with LZMA compression
make linux    # Linux with UPX compression
make clean    # remove build artifacts
```

## How It Works

The agent connects to OpenRouter, sends messages with a `sh` tool definition, and streams the response. When the model returns a `tool_calls` response, the agent executes the command, sends the result back, and gets a follow-up response. Up to 5 tool calls per turn.

See [SPEC.md](SPEC.md) for full architecture and test cases.

## License

**CC0 — No Rights Reserved**
