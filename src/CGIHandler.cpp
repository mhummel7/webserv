/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   CGIHandler.cpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: leokubler <leokubler@student.42.fr>        +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/10/21 09:27:14 by mhummel           #+#    #+#             */
/*   Updated: 2025/12/14 23:49:34 by leokubler        ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../include/CGIHandler.hpp"
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <fcntl.h>
#include <signal.h>
#include <sstream>
#include <iostream>
#include <string.h>
#include <vector>
#include <memory>
#include <algorithm>
#include <poll.h>

/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   CGIHandler.cpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: leokubler <leokubler@student.42.fr>        +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/10/21 09:27:14 by mhummel           #+#    #+#             */
/*   Updated: 2025/12/14 23:39:01 by leokubler        ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../include/CGIHandler.hpp"
#include "../include/config.hpp"  // Für g_cfg
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <fcntl.h>
#include <signal.h>
#include <sstream>
#include <iostream>
#include <string.h>
#include <vector>
#include <memory>
#include <algorithm>
#include <poll.h>

static long long get_time_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

// Signalisierung für Timeout
static volatile sig_atomic_t timeout_occurred = 0;

static void timeout_handler(int sig)
{
    (void)sig;
    timeout_occurred = 1;
}

// Setze einen Alarm für Timeout
static void set_timeout_alarm(size_t timeout_ms)
{
    timeout_occurred = 0;
    
    // Setze Signal-Handler für SIGALRM
    struct sigaction sa;
    sa.sa_handler = timeout_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGALRM, &sa, NULL);
    
    // Konvertiere Millisekunden zu Sekunden für alarm()
    // alarm() nimmt ganze Sekunden, also aufrunden
    unsigned int seconds = (timeout_ms + 999) / 1000;
    alarm(seconds);
}

// Entferne den Alarm
static void clear_timeout_alarm(void)
{
    alarm(0);
    timeout_occurred = 0;
}

CGIHandler::CGIHandler() {}
CGIHandler::~CGIHandler() {}

Response CGIHandler::execute(const Request& req)
{
    Response res;
    size_t timeout_ms = g_cfg.keepalive_timeout_ms;  // Verwende cgi_timeout, nicht keepalive_timeout

    std::cout << "Executing CGI script: " << req.path 
              << " (timeout: " << timeout_ms << "ms)" << std::endl;

    std::map<std::string, std::string> env = buildEnv(req, req.path);
    std::string output = runCGI(req.path, env, req.body, timeout_ms);

    res.statusCode = 200;
    res.reasonPhrase = "OK";
    res.headers["Content-Type"] = "text/html";
    res.body = output;
    res.headers["Content-Length"] = std::to_string(res.body.size());

    return res;
}

std::map<std::string, std::string> CGIHandler::buildEnv(const Request& req, const std::string& scriptPath)
{
    std::map<std::string, std::string> env;
    env["GATEWAY_INTERFACE"] = "CGI/1.1";
    env["REQUEST_METHOD"] = req.method;
    env["SCRIPT_FILENAME"] = scriptPath;
    env["SCRIPT_NAME"] = req.path;
    env["QUERY_STRING"] = req.query;
    env["CONTENT_LENGTH"] = std::to_string(req.body.size());
    env["CONTENT_TYPE"] = "text/plain";
    env["SERVER_PROTOCOL"] = "HTTP/1.1";
    env["SERVER_SOFTWARE"] = "webserv/1.0";
    env["REDIRECT_STATUS"] = "200";
    return env;
}

static std::string getInterpreter(const std::string& scriptPath)
{
    if (scriptPath.find(".py") != std::string::npos)
        return "/usr/bin/python3";
    if (scriptPath.find(".php") != std::string::npos)
        return "/usr/local/bin/php-cgi";
    return "";
}

static bool chdir_to_script_dir(const std::string& scriptPath, std::string& outDir, std::string& outName)
{
    size_t lastSlash = scriptPath.find_last_of('/');
    if (lastSlash != std::string::npos) {
        outDir = scriptPath.substr(0, lastSlash);
        outName = scriptPath.substr(lastSlash + 1);
    } else {
        outDir = ".";
        outName = scriptPath;
    }
    if (chdir(outDir.c_str()) != 0) {
        perror("chdir to script directory");
        return false;
    }
    return true;
}

static std::vector<char*> build_envp_vec(const std::map<std::string, std::string>& env)
{
    std::vector<char*> envp_vec;
    envp_vec.reserve(env.size() + 1);
    for (std::map<std::string, std::string>::const_iterator it = env.begin(); it != env.end(); ++it)
    {
        std::string entry = it->first + "=" + it->second;
        envp_vec.push_back(strdup(entry.c_str()));
    }
    envp_vec.push_back(NULL);
    return envp_vec;
}

static void free_envp_vec(std::vector<char*>& envp_vec)
{
    for (size_t j = 0; j + 1 < envp_vec.size(); ++j) {
        if (envp_vec[j]) free(envp_vec[j]);
    }
}

std::string CGIHandler::runCGI(const std::string& scriptPath, 
                               const std::map<std::string, std::string>& env, 
                               const std::string& body,
                               size_t timeout_ms)
{
    int pipeIn[2];
    int pipeOut[2];

    if (pipe(pipeIn) < 0 || pipe(pipeOut) < 0)
    {
        perror("pipe");
        return "<h1>CGI pipe error</h1>";
    }

    // Setze Pipes auf non-blocking
    fcntl(pipeOut[0], F_SETFL, O_NONBLOCK);
    fcntl(pipeIn[1], F_SETFL, O_NONBLOCK);

    pid_t pid = fork();

    if (pid == 0)
    {
        // CHILD PROCESS - Kein Timeout-Handling hier
        dup2(pipeIn[0], STDIN_FILENO);
        dup2(pipeOut[1], STDOUT_FILENO);
        close(pipeIn[1]);
        close(pipeOut[0]);

        std::string scriptDir;
        std::string scriptName;
        if (!chdir_to_script_dir(scriptPath, scriptDir, scriptName))
            exit(1);

        std::vector<char*> envp_vec = build_envp_vec(env);

        std::string interpreter = getInterpreter(scriptPath);
        std::vector<char*> argv;
        argv.reserve(3);

        if (!interpreter.empty()) {
            argv.push_back(const_cast<char*>(interpreter.c_str()));
            argv.push_back(const_cast<char*>(scriptName.c_str()));
            argv.push_back(NULL);
            execve(interpreter.c_str(), argv.data(), envp_vec.data());
        } 
        else
        {
            argv.push_back(const_cast<char*>(scriptName.c_str()));
            argv.push_back(NULL);
            execve(scriptName.c_str(), argv.data(), envp_vec.data());
        }

        free_envp_vec(envp_vec);
        perror("execve");
        exit(127);
    }
    else if (pid > 0)
    {
        // PARENT PROCESS
        close(pipeIn[0]);
        close(pipeOut[1]);

        // Setze Alarm für Timeout
        set_timeout_alarm(timeout_ms);

        // Schreiben des Inputs (non-blocking)
        bool write_complete = true;
        if (!body.empty())
        {
            size_t written = 0;
            const char* data = body.c_str();
            size_t total_to_write = body.size();
            long long write_start = get_time_ms();

            while (written < total_to_write)
            {
                // Prüfe auf Timeout
                if (timeout_occurred || get_time_ms() - write_start > static_cast<long long>(timeout_ms))
                {
                    write_complete = false;
                    break;
                }

                ssize_t bytes = write(pipeIn[1], data + written, total_to_write - written);
                if (bytes > 0)
                {
                    written += bytes;
                }
                else if (bytes < 0)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        // Pipe ist voll, warte kurz
                        usleep(1000);
                        continue;
                    }
                    else
                    {
                        write_complete = false;
                        break;
                    }
                }
            }
        }
        close(pipeIn[1]);

        if (!write_complete)
        {
            clear_timeout_alarm();
            kill(pid, SIGKILL);
            close(pipeOut[0]);
            waitpid(pid, NULL, 0);
            return "<h1>CGI Timeout Error</h1><p>Timeout while writing to script</p>";
        }

        // Lesen der Ausgabe mit Timeout
        std::string output;
        bool process_done = false;
        bool timed_out = false;
        long long read_start = get_time_ms();

        while (!process_done && !timed_out)
        {
            // Prüfe auf Timeout
            if (timeout_occurred || get_time_ms() - read_start > static_cast<long long>(timeout_ms))
            {
                timed_out = true;
                break;
            }

            // Prüfe, ob Kindprozess beendet ist
            int status;
            pid_t ret = waitpid(pid, &status, WNOHANG);
            
            if (ret == pid)
            {
                process_done = true;
                // Lese restliche Daten
                char buffer[4096];
                ssize_t bytes;
                while ((bytes = read(pipeOut[0], buffer, sizeof(buffer))) > 0)
                {
                    output.append(buffer, bytes);
                }
                
                if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
                {
                    std::cerr << "CGI exited with code " << WEXITSTATUS(status) << std::endl;
                    clear_timeout_alarm();
                    close(pipeOut[0]);
                    return "<h1>CGI Error</h1><p>Script exited with error code " 
                           + std::to_string(WEXITSTATUS(status)) + "</p>";
                }
                if (WIFSIGNALED(status))
                {
                    std::cerr << "CGI killed by signal " << WTERMSIG(status) << std::endl;
                    clear_timeout_alarm();
                    close(pipeOut[0]);
                    return "<h1>CGI Crashed</h1><p>Script killed by signal " 
                           + std::to_string(WTERMSIG(status)) + "</p>";
                }
                break;
            }
            else if (ret < 0)
            {
                // Fehler bei waitpid
                perror("waitpid");
                break;
            }

            // Lese verfügbare Daten
            char buffer[4096];
            ssize_t bytes = read(pipeOut[0], buffer, sizeof(buffer));
            if (bytes > 0)
            {
                output.append(buffer, bytes);
                // Zurücksetzen des Timeout-Zählers bei neuen Daten
                read_start = get_time_ms();
            }
            else if (bytes < 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    // Keine Daten verfügbar, warte kurz
                    usleep(10000);  // 10ms
                }
                else
                {
                    // Lesefehler
                    perror("read from CGI");
                    break;
                }
            }
        }

        clear_timeout_alarm();
        close(pipeOut[0]);

        if (timed_out)
        {
            kill(pid, SIGKILL);
            waitpid(pid, NULL, 0);
            std::cerr << "CGI script timed out after " << timeout_ms << "ms" << std::endl;
            return "<h1>CGI Timeout</h1><p>Script exceeded maximum execution time of " 
                   + std::to_string(timeout_ms / 1000) + " seconds</p>";
        }

        return output;
    }
    else
    {
        perror("fork");
        return "<h1>CGI fork error</h1>";
    }
}

// Ähnliche Änderungen für executeWith
Response CGIHandler::executeWith(const Request& req, const std::string& execPath, const std::string& scriptFile)
{
    Response res;
    size_t timeout_ms = g_cfg.keepalive_timeout_ms;  // Verwende cgi_timeout

    std::cout << "Executing CGI executable: " << execPath
              << " (script file: " << scriptFile << ")"
              << " (timeout: " << timeout_ms << "ms)" << std::endl;

    std::map<std::string, std::string> env = buildEnv(req, scriptFile);

    int pipeIn[2];
    int pipeOut[2];

    if (pipe(pipeIn) < 0 || pipe(pipeOut) < 0)
    {
        perror("pipe");
        res.statusCode = 500;
        res.reasonPhrase = "Internal Server Error";
        res.body = "<h1>CGI pipe error</h1>";
        res.headers["Content-Length"] = std::to_string(res.body.size());
        res.headers["Content-Type"] = "text/html";
        return res;
    }

    // Setze Pipes auf non-blocking
    fcntl(pipeOut[0], F_SETFL, O_NONBLOCK);
    fcntl(pipeIn[1], F_SETFL, O_NONBLOCK);

    pid_t pid = fork();
    if (pid == 0)
    {
        // CHILD
        dup2(pipeIn[0], STDIN_FILENO);
        dup2(pipeOut[1], STDOUT_FILENO);
        close(pipeIn[1]);
        close(pipeOut[0]);

        std::string scriptDir;
        std::string scriptName;
        if (!chdir_to_script_dir(scriptFile, scriptDir, scriptName))
            exit(1);

        std::vector<char*> envp_vec = build_envp_vec(env);

        std::vector<char*> argv;
        argv.reserve(3);
        argv.push_back(const_cast<char*>(execPath.c_str()));
        if (!scriptName.empty())
            argv.push_back(const_cast<char*>(scriptName.c_str()));
        argv.push_back(NULL);

        execve(execPath.c_str(), argv.data(), envp_vec.data());

        free_envp_vec(envp_vec);
        perror("execve");
        exit(127);
    }
    else if (pid > 0)
    {
        // PARENT PROCESS
        close(pipeIn[0]);
        close(pipeOut[1]);

        // Setze Alarm für Timeout
        set_timeout_alarm(timeout_ms);

        // Schreiben mit Timeout
        bool write_complete = true;
        if (!req.body.empty())
        {
            size_t written = 0;
            const char* data = req.body.c_str();
            size_t total_to_write = req.body.size();
            long long write_start = get_time_ms();

            while (written < total_to_write)
            {
                if (timeout_occurred || get_time_ms() - write_start > static_cast<long long>(timeout_ms))
                {
                    write_complete = false;
                    break;
                }

                ssize_t bytes = write(pipeIn[1], data + written, total_to_write - written);
                if (bytes > 0)
                {
                    written += bytes;
                }
                else if (bytes < 0)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        usleep(1000);
                        continue;
                    }
                    else
                    {
                        write_complete = false;
                        break;
                    }
                }
            }
        }
        close(pipeIn[1]);

        if (!write_complete)
        {
            clear_timeout_alarm();
            kill(pid, SIGKILL);
            close(pipeOut[0]);
            waitpid(pid, NULL, 0);
            
            res.statusCode = 504;
            res.reasonPhrase = "Gateway Timeout";
            res.body = "<h1>504 Gateway Timeout</h1><p>Timeout while writing to CGI script</p>";
            res.headers["Content-Length"] = std::to_string(res.body.size());
            res.headers["Content-Type"] = "text/html";
            return res;
        }

        // Lesen mit Timeout
        std::string output;
        bool process_done = false;
        bool timed_out = false;
        long long read_start = get_time_ms();

        while (!process_done && !timed_out)
        {
            if (timeout_occurred || get_time_ms() - read_start > static_cast<long long>(timeout_ms))
            {
                timed_out = true;
                break;
            }

            int status;
            pid_t ret = waitpid(pid, &status, WNOHANG);
            
            if (ret == pid)
            {
                process_done = true;
                char buffer[4096];
                ssize_t bytes;
                while ((bytes = read(pipeOut[0], buffer, sizeof(buffer))) > 0)
                {
                    output.append(buffer, bytes);
                }
                
                if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
                {
                    std::cerr << "CGI exited with code " << WEXITSTATUS(status) << std::endl;
                    clear_timeout_alarm();
                    close(pipeOut[0]);
                    
                    res.statusCode = 500;
                    res.reasonPhrase = "Internal Server Error";
                    res.body = "<h1>CGI Error</h1><p>Script exited with error code " 
                               + std::to_string(WEXITSTATUS(status)) + "</p>";
                    res.headers["Content-Length"] = std::to_string(res.body.size());
                    res.headers["Content-Type"] = "text/html";
                    return res;
                }
                if (WIFSIGNALED(status))
                {
                    std::cerr << "CGI killed by signal " << WTERMSIG(status) << std::endl;
                    clear_timeout_alarm();
                    close(pipeOut[0]);
                    
                    res.statusCode = 500;
                    res.reasonPhrase = "Internal Server Error";
                    res.body = "<h1>CGI Crashed</h1><p>Script killed by signal " 
                               + std::to_string(WTERMSIG(status)) + "</p>";
                    res.headers["Content-Length"] = std::to_string(res.body.size());
                    res.headers["Content-Type"] = "text/html";
                    return res;
                }
                break;
            }
            else if (ret < 0)
            {
                perror("waitpid");
                break;
            }

            char buffer[4096];
            ssize_t bytes = read(pipeOut[0], buffer, sizeof(buffer));
            if (bytes > 0)
            {
                output.append(buffer, bytes);
                read_start = get_time_ms();
            }
            else if (bytes < 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    usleep(10000);
                }
                else
                {
                    perror("read from CGI");
                    break;
                }
            }
        }

        clear_timeout_alarm();
        close(pipeOut[0]);
        
        if (timed_out)
        {
            kill(pid, SIGKILL);
            waitpid(pid, NULL, 0);
            std::cerr << "CGI script timed out after " << timeout_ms << "ms" << std::endl;
            
            res.statusCode = 504;
            res.reasonPhrase = "Gateway Timeout";
            res.body = "<h1>504 Gateway Timeout</h1><p>CGI script exceeded maximum execution time of " 
                       + std::to_string(timeout_ms / 1000) + " seconds</p>";
            res.headers["Content-Length"] = std::to_string(res.body.size());
            res.headers["Content-Type"] = "text/html";
            return res;
        }

        res.statusCode = 200;
        res.reasonPhrase = "OK";
        res.headers["Content-Type"] = "text/html";
        res.body = output;
        res.headers["Content-Length"] = std::to_string(res.body.size());
        return res;
    }
    else
    {
        perror("fork");
        res.statusCode = 500;
        res.reasonPhrase = "Internal Server Error";
        res.body = "<h1>CGI fork error</h1>";
        res.headers["Content-Length"] = std::to_string(res.body.size());
        res.headers["Content-Type"] = "text/html";
        return res;
    }
}