# Agent-C Rules

Minimal AI agent in pure C. ~4KB binary, zero external dependencies.

## Architecture

Single file: `agent.c`. Compiles on macOS with `make macos`.

### Networking

Native HTTPS via BSD sockets + macOS SecureTransport (deprecated but functional). No curl, no libssl, no external libraries.

- DNS: `getaddrinfo("openrouter.ai", "443")`
- TCP: `socket()` + `connect()`
- TLS: `SSLCreateContext` + `SSLHandshake` with retry loop (`errSSLWouldBlock`)
- TLS callbacks (`tls_read`/`tls_write`): must save requested size before modifying `*len`, then compare against saved value to return `errSSLWouldBlock` correctly

### API

OpenRouter `/api/v1/chat/completions` with OpenAI-compatible format.

- Model: `qwen/qwen3-coder`
- Auth: `OR_KEY` env var (Bearer token)
- Streaming: `"stream": true` with SSE parsing

### Tool Calling

Standard OpenAI tool calling protocol. NOT markdown, NOT XML, NOT custom `CMD:` format.

**Request** includes `tools` array defining one tool:
```json
{
  "name": "sh",
  "description": "Run shell command",
  "parameters": { "type": "object", "properties": { "c": { "type": "string" } }, "required": ["c"] }
}
```

**Response** — model returns `tool_calls` in streaming deltas:
- First chunk: `"tool_calls":[{"id":"call_xxx","function":{"name":"sh","arguments":""}}]`
- Subsequent chunks: `"tool_calls":[{"index":0,"function":{"arguments":"{\"c\""}}]`
- End: `"finish_reason":"tool_calls"`

**Execution loop** (up to 5 turns per user message):
1. Send messages + tools
2. If text response: print streamed tokens, store in history, done
3. If tool_call: accumulate `id` and `arguments`, extract command from `{"c":"..."}`, execute via `popen()`, display output
4. Store assistant tool_call message (role `'c'`) and tool result (role `'t'`) in history
5. Send updated history back to API for follow-up response

### Message History

Fixed ring buffer of 20 messages (`Msg` struct). Role codes:
- `'s'` = system, `'u'` = user, `'a'` = assistant (text)
- `'c'` = assistant tool_call (stores tool_call_id + raw arguments JSON)
- `'t'` = tool result (stores tool_call_id + command output)

When full, drops 5 oldest messages (keeps system prompt at index 0).

### Output Format

```
Agent> user input here              ← green prompt
$ ls -la                            ← green command (from tool call)
file1.txt  file2.txt                ← default color (command output)
Here are your files: ...            ← muted gray (AI response, streamed)
Agent>                              ← green prompt (next turn)
```

Colors:
- `\033[32m` — green: prompt (`Agent> `) and tool commands (`$ ...`)
- `\033[38;5;102m` — muted gray: AI text responses
- `\033[0m` — reset after each colored section
- Command output: no color (default terminal)

### Build

```
make clean && make macos
```

Compiles with `-framework Security -framework CoreFoundation`. LZMA self-extracting archive. Target: ~4KB.

## Tests

All tests assume `OR_KEY` is set to a valid OpenRouter API key.

### 1. No key

```sh
unset OR_KEY && ./agent-c
```

Expected: prints `OR_KEY?` to stderr, exits with code 1.

### 2. Build succeeds

```sh
make clean && make macos
```

Expected: no errors, prints `Built: NNNN bytes` (should be under 5KB).

### 3. No external dependencies

```sh
# Build uncompressed binary first
gcc -Os -o agent-c.bin agent.c -framework Security -framework CoreFoundation
otool -L agent-c.bin
```

Expected: only system libraries (`libSystem`, `Security`, `CoreFoundation`). No libcurl, no libssl.

### 4. Text response (streaming)

```sh
echo "hello" | OR_KEY=<key> ./agent-c
```

Expected:
- Shows `Agent> ` prompt in green
- AI response streams in muted gray, token by token
- No `CMD:` text in output
- Clean exit after EOF

### 5. Tool call (shell execution)

```sh
echo "list files in the current directory" | OR_KEY=<key> ./agent-c
```

Expected:
- `Agent> ` prompt in green
- `$ ls` (or similar) in green — the tool call command
- File listing in default color — command output
- AI summary in muted gray — follow-up response
- `Agent> ` prompt again

### 6. Multi-turn tool loop

```sh
printf "create a file called /tmp/agent-c-test.txt with hello inside\nread it back\n" | OR_KEY=<key> ./agent-c
```

Expected: model uses `sh` tool multiple times across turns. Each tool call shows `$ command` in green, output in default, AI response in gray. Up to 5 tool calls per user message.

### 7. Prompt format

Verify the prompt reads `Agent> ` (with space), not `> `.

### 8. Graceful stdin EOF

```sh
echo "" | OR_KEY=<key> ./agent-c
```

Expected: prints prompt, exits cleanly (no crash, no hang).
