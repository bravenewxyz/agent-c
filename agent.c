#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <Security/SecureTransport.h>

#define N 20
#define B 8192
#define C 4096

typedef struct { char r; char id[40]; char c[C]; } Msg;
static Msg ms[N];
static int mc;
static char key[128];

static const char* rn(char r) {
    return r=='s'?"system":r=='u'?"user":"assistant";
}

static int jesc(char* o, int sz, const char* s) {
    int i = 0;
    while (*s && i < sz-2) {
        if (*s=='"'||*s=='\\') { o[i++]='\\'; o[i++]=*s; }
        else if (*s=='\n') { o[i++]='\\'; o[i++]='n'; }
        else if (*s=='\t') { o[i++]='\\'; o[i++]='t'; }
        else if ((unsigned char)*s < 0x20) { s++; continue; }
        else o[i++]=*s;
        s++;
    }
    o[i]=0;
    return i;
}

static void addm(char r, const char* id, const char* s) {
    if (mc >= N) { memmove(ms+1, ms+6, (mc-6)*sizeof(Msg)); mc -= 5; }
    ms[mc].r = r;
    if (id) { strncpy(ms[mc].id, id, 39); ms[mc].id[39]=0; } else ms[mc].id[0]=0;
    size_t l = strlen(s);
    if (l > C-1) l = C-1;
    memcpy(ms[mc].c, s, l);
    ms[mc++].c[l] = 0;
}

static int buildmsgs(char* out, int sz) {
    int o = 0;
    out[o++] = '[';
    for (int i = 0; i < mc; i++) {
        if (i) out[o++] = ',';
        if (ms[i].r == 'c') {
            char esc[C];
            jesc(esc, C, ms[i].c);
            o += snprintf(out+o, sz-o,
                "{\"role\":\"assistant\",\"content\":null,\"tool_calls\":"
                "[{\"id\":\"%s\",\"type\":\"function\",\"function\":"
                "{\"name\":\"sh\",\"arguments\":\"%s\"}}]}", ms[i].id, esc);
        } else if (ms[i].r == 't') {
            char esc[C];
            jesc(esc, C, ms[i].c);
            o += snprintf(out+o, sz-o,
                "{\"role\":\"tool\",\"tool_call_id\":\"%s\",\"content\":\"%s\"}",
                ms[i].id, esc);
        } else {
            o += snprintf(out+o, sz-o,
                "{\"role\":\"%s\",\"content\":\"%s\"}", rn(ms[i].r), ms[i].c);
        }
    }
    out[o++] = ']'; out[o] = 0;
    return o;
}

static OSStatus tls_read(SSLConnectionRef c, void* d, size_t* len) {
    int fd = *(int*)c;
    size_t req = *len;
    ssize_t r = read(fd, d, req);
    if (r <= 0) { *len = 0; return r == 0 ? errSSLClosedGraceful : errSSLClosedAbort; }
    *len = (size_t)r;
    return ((size_t)r < req) ? errSSLWouldBlock : noErr;
}

static OSStatus tls_write(SSLConnectionRef c, const void* d, size_t* len) {
    int fd = *(int*)c;
    size_t req = *len;
    ssize_t w = write(fd, d, req);
    if (w <= 0) { *len = 0; return errSSLClosedAbort; }
    *len = (size_t)w;
    return ((size_t)w < req) ? errSSLWouldBlock : noErr;
}

static int getdelta(const char* line, char* out, size_t sz) {
    const char* s = strstr(line, "\"content\":");
    if (!s) return 0;
    s += 10;
    while (*s == ' ') s++;
    if (*s == 'n') return 0;
    if (*s != '"') return 0;
    s++;
    size_t o = 0;
    while (*s && *s != '"' && o < sz-1) {
        if (*s == '\\' && s[1]) {
            s++;
            if (*s == 'n') out[o++] = '\n';
            else if (*s == 't') out[o++] = '\t';
            else out[o++] = *s;
            s++;
        } else out[o++] = *s++;
    }
    out[o] = 0;
    return o > 0;
}

static int get_tc_id(const char* line, char* out, size_t sz) {
    const char* tc = strstr(line, "\"tool_calls\"");
    if (!tc) return 0;
    const char* id = strstr(tc, "\"id\":\"");
    if (!id) return 0;
    id += 6;
    size_t o = 0;
    while (*id && *id != '"' && o < sz-1) out[o++] = *id++;
    out[o] = 0;
    return o > 0;
}

static int get_tc_args(const char* line, char* out, size_t sz) {
    const char* a = strstr(line, "\"arguments\":\"");
    if (!a) return 0;
    a += 13;
    size_t o = 0;
    while (*a && o < sz-1) {
        if (*a == '"') break;
        if (*a == '\\' && a[1]) {
            a++;
            if (*a == '"') out[o++] = '"';
            else if (*a == '\\') out[o++] = '\\';
            else if (*a == 'n') out[o++] = '\n';
            else if (*a == 't') out[o++] = '\t';
            else out[o++] = *a;
            a++;
        } else out[o++] = *a++;
    }
    out[o] = 0;
    return 1;
}

static int getcmd(const char* args, char* cmd, size_t sz) {
    const char* s = strstr(args, "\"c\":");
    if (!s) return 0;
    s += 4;
    while (*s == ' ') s++;
    if (*s != '"') return 0;
    s++;
    size_t o = 0;
    while (*s && *s != '"' && o < sz-1) {
        if (*s == '\\' && s[1]) {
            s++;
            if (*s == 'n') cmd[o++] = '\n';
            else if (*s == 't') cmd[o++] = '\t';
            else cmd[o++] = *s;
            s++;
        } else cmd[o++] = *s++;
    }
    cmd[o] = 0;
    return o > 0;
}

/* Returns: 0=text, 1=tool_call, -1=error */
static int apicall(char* ct, char* tc_id, char* tc_args) {
    char msgs[B*2];
    buildmsgs(msgs, B*2);
    char body[B*2];
    int blen = snprintf(body, B*2,
        "{\"model\":\"qwen/qwen3-coder\",\"messages\":%s,"
        "\"tools\":[{\"type\":\"function\",\"function\":{\"name\":\"sh\","
        "\"description\":\"Run shell command\","
        "\"parameters\":{\"type\":\"object\",\"properties\":{\"c\":{\"type\":\"string\",\"description\":\"command\"}},"
        "\"required\":[\"c\"]}}}],"
        "\"max_tokens\":1000,\"stream\":true}", msgs);

    struct addrinfo h = {0}, *ai;
    h.ai_family = AF_INET;
    h.ai_socktype = SOCK_STREAM;
    if (getaddrinfo("openrouter.ai", "443", &h, &ai)) return -1;

    int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0) { freeaddrinfo(ai); return -1; }
    if (connect(fd, ai->ai_addr, ai->ai_addrlen)) { freeaddrinfo(ai); close(fd); return -1; }
    freeaddrinfo(ai);

    SSLContextRef ctx = SSLCreateContext(NULL, kSSLClientSide, kSSLStreamType);
    if (!ctx) { close(fd); return -1; }
    SSLSetIOFuncs(ctx, tls_read, tls_write);
    SSLSetConnection(ctx, &fd);
    SSLSetPeerDomainName(ctx, "openrouter.ai", 13);
    OSStatus hs;
    do { hs = SSLHandshake(ctx); } while (hs == errSSLWouldBlock);
    if (hs != noErr) { CFRelease(ctx); close(fd); return -1; }

    char req[B*2];
    int rlen = snprintf(req, B*2,
        "POST /api/v1/chat/completions HTTP/1.1\r\n"
        "Host: openrouter.ai\r\n"
        "Content-Type: application/json\r\n"
        "Authorization: Bearer %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n%s", key, blen, body);

    size_t wt = rlen;
    SSLWrite(ctx, req, wt, &wt);

    char buf[B];
    size_t bpos = 0;
    int headers_done = 0;
    size_t clen = 0, talen = 0;
    int mode = 0, color_on = 0;
    ct[0] = 0;
    tc_id[0] = 0;
    tc_args[0] = 0;

    for (;;) {
        size_t rd = sizeof(buf) - 1 - bpos;
        if (rd == 0) { bpos = 0; rd = sizeof(buf) - 1; }
        OSStatus s = SSLRead(ctx, buf + bpos, rd, &rd);
        bpos += rd;
        buf[bpos] = 0;

        if (!headers_done) {
            char* hend = strstr(buf, "\r\n\r\n");
            if (hend) {
                hend += 4;
                size_t rem = bpos - (hend - buf);
                memmove(buf, hend, rem);
                bpos = rem;
                buf[bpos] = 0;
                headers_done = 1;
            } else {
                if (s == errSSLClosedGraceful || s == errSSLClosedAbort) break;
                if (s != noErr && s != errSSLWouldBlock) break;
                continue;
            }
        }

        char* p = buf;
        char* nl;
        while ((nl = strchr(p, '\n')) != NULL) {
            *nl = 0;
            if (nl > p && *(nl-1) == '\r') *(nl-1) = 0;

            if (strncmp(p, "data: [DONE]", 12) == 0) goto done;
            if (strncmp(p, "data: ", 6) == 0) {
                char* json = p + 6;

                if (strstr(json, "\"finish_reason\":\"tool_calls\"")) { mode = 1; goto done; }

                /* Content delta */
                char delta[C];
                if (getdelta(json, delta, C)) {
                    mode = 0;
                    size_t dl = strlen(delta);
                    if (!color_on) { write(1, "\033[38;5;102m", 11); color_on = 1; }
                    write(1, delta, dl);
                    if (clen + dl < C - 1) {
                        memcpy(ct + clen, delta, dl);
                        clen += dl;
                    }
                }

                /* Tool call delta */
                if (strstr(json, "\"tool_calls\"")) {
                    mode = 1;
                    char idbuf[40];
                    if (get_tc_id(json, idbuf, sizeof(idbuf)))
                        memcpy(tc_id, idbuf, strlen(idbuf)+1);
                    char afrag[C];
                    if (get_tc_args(json, afrag, sizeof(afrag))) {
                        size_t fl = strlen(afrag);
                        if (talen + fl < C - 1) {
                            memcpy(tc_args + talen, afrag, fl);
                            talen += fl;
                            tc_args[talen] = 0;
                        }
                    }
                }
            }
            p = nl + 1;
        }
        if (p < buf + bpos) {
            size_t rem = bpos - (p - buf);
            memmove(buf, p, rem);
            bpos = rem;
        } else bpos = 0;

        if (s == errSSLClosedGraceful || s == errSSLClosedAbort) break;
        if (s != noErr && s != errSSLWouldBlock) break;
    }
done:
    if (color_on) write(1, "\033[0m\n", 5);
    ct[clen] = 0;
    tc_args[talen] = 0;

    SSLClose(ctx);
    CFRelease(ctx);
    close(fd);

    if (mode == 1) return 1;
    return clen > 0 ? 0 : -1;
}

int main(void) {
    char* k = getenv("OR_KEY");
    if (!k) { write(2, "OR_KEY?\n", 8); return 1; }
    size_t kl = strlen(k);
    if (kl > 127) kl = 127;
    memcpy(key, k, kl);
    addm('s', 0, "Napoleon Dynamite AI. Gosh!/Sweet!/Idiot! Use the sh tool to run shell commands when needed.");
    char in[B];
    for (;;) {
        write(1, "\033[32mAgent> \033[0m", 16);
        if (!fgets(in, B, stdin)) break;
        size_t il = strlen(in);
        if (il && in[il-1] == '\n') in[il-1] = 0;
        char* s = in;
        while (*s == ' ') s++;
        if (!*s) continue;
        addm('u', 0, s);
        int turns = 0;
        while (turns++ < 5) {
            char ct[C], tid[40], targs[C];
            int r = apicall(ct, tid, targs);
            if (r < 0) break;
            if (r == 0) { addm('a', 0, ct); break; }
            /* tool call */
            char cmd[C];
            if (!getcmd(targs, cmd, C)) break;
            addm('c', tid, targs);
            write(1, "\033[32m$ ", 7);
            write(1, cmd, strlen(cmd));
            write(1, "\033[0m\n", 5);
            char full[B];
            snprintf(full, B, "(%s) 2>&1", cmd);
            FILE* fp = popen(full, "r");
            char res[C];
            res[0] = 0;
            if (fp) {
                size_t n = fread(res, 1, C-1, fp);
                res[n] = 0;
                pclose(fp);
                if (n) { write(1, res, n); if (res[n-1]!='\n') write(1,"\n",1); }
            }
            addm('t', tid, res);
        }
    }
}
