#include "agent-c.h"

extern Agent agent;
extern Config config;

void init_agent(void) {
    strcpy(agent.messages[0].role, "system");
    strcpy(agent.messages[0].content, "You are an AI assistant with Napoleon Dynamite's personality. Say things like 'Gosh!', 'Sweet!', 'Idiot!', and be awkwardly enthusiastic. For multi-step tasks, chain commands with && (e.g., 'echo content > file.py && python3 file.py'). Use execute_command for shell tasks. Answer questions in Napoleon's quirky style.");
    agent.msg_count = 1;
}

int execute_command(const char* response) {
    if (!response) return 0;
    
    char cmd[MAX_CONTENT] = {0};
    if (!extract_command(response, cmd, sizeof(cmd)) || !*cmd) return 0;
    
    printf("\033[31m$ %s\033[0m\n", cmd);
    
    char temp[] = "/tmp/ai_cmd_XXXXXX";
    int fd = mkstemp(temp);
    if (fd == -1) return 0;
    close(fd);
    
    static const char* exec_template = "(%s) > '%s' 2>&1";
    char full[MAX_BUFFER];
    snprintf(full, MAX_BUFFER, exec_template, cmd, temp);
    
    int code = system(full);
    FILE* f = fopen(temp, "r");
    if (f) {
        char result[MAX_CONTENT];
        size_t bytes = fread(result, 1, MAX_CONTENT - 1, f);
        result[bytes] = '\0';
        fclose(f);
        if (bytes > 0) {
            printf("%s", result);
            if (result[bytes-1] != '\n') printf("\n");
        }
        
        if (agent.msg_count < MAX_MESSAGES - 1) {
            strcpy(agent.messages[agent.msg_count].role, "tool");
            strncpy(agent.messages[agent.msg_count].content, result, MAX_CONTENT - 1);
            agent.messages[agent.msg_count].content[MAX_CONTENT - 1] = '\0';
            agent.msg_count++;
        }
    }
    unlink(temp);
    
    return code == 0 ? 1 : 0;
}

int process_agent(const char* task) {
    if (!task) return -1;
    
    if (agent.msg_count >= MAX_MESSAGES - 1) {
        for (int i = 1; i < agent.msg_count - 5; i++) agent.messages[i] = agent.messages[i + 5];
        agent.msg_count -= 5;
    }
    
    strcpy(agent.messages[agent.msg_count].role, "user");
    strncpy(agent.messages[agent.msg_count].content, task, MAX_CONTENT - 1);
    agent.messages[agent.msg_count].content[MAX_CONTENT - 1] = '\0';
    agent.msg_count++;
    
    char req[MAX_BUFFER], resp[MAX_BUFFER];
    json_request(&agent, &config, req, sizeof(req));
    
    if (http_request(req, resp, sizeof(resp))) return -1;
    
    if (has_tool_call(resp)) {
        execute_command(resp);
        json_request(&agent, &config, req, sizeof(req));
        http_request(req, resp, sizeof(resp));
    }
    
    char content[MAX_CONTENT];
    if (json_content(resp, content, sizeof(content))) {
        printf("\033[34m%s\033[0m\n", content);
        
        if (agent.msg_count < MAX_MESSAGES - 1) {
            strcpy(agent.messages[agent.msg_count].role, "assistant");
            strncpy(agent.messages[agent.msg_count].content, content, MAX_CONTENT - 1);
            agent.messages[agent.msg_count].content[MAX_CONTENT - 1] = '\0';
            agent.msg_count++;
        }
    }
    
    return 0;
}